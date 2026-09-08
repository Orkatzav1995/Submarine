/*
 * monitor.c
 *
 * See monitor.h for purpose, layer, and responsibilities.
 */

#include "monitor.h"
#include "dht.h"
#include "battery.h"
#include "light.h"
#include "configuration.h"
#include "log.h"
#include "event.h"
#include "tx_queue.h"
#include "rtc_sync.h"
#include "cmsis_os.h"

/* Last successfully-read DHT values - kept and reused whenever
 * DHT_Read() fails, instead of using a bogus 0.0 (see monitor.h). */
static float s_lastTemperature = 0.0f;
static float s_lastHumidity = 0.0f;

/* The mode from the previous Monitor_Sample() call, so a real change can
 * be detected. Starts at MODE_NORMAL - a safe, simple default; if the
 * very first real sample is already Error, that is correctly reported
 * as a genuine Normal->Error transition, not suppressed. */
static uint8_t s_previousMode = MODE_NORMAL;

/* The most recent sample Monitor_Sample() built, for Monitor_GetLastSample()
 * (Keep-Alive, Sec 2.8, needs "the latest measurement + mode" independently
 * of Monitor's own 5s cycle). Zero-initialized: timestamp=0, all floats
 * 0.0f, mode=0=MODE_NORMAL (SystemMode's enum ordering, tlv_common.h) -
 * the same safe default s_previousMode already uses. */
static MeasurementSample s_lastSample;

void Monitor_Init(void)
{
    s_lastTemperature = 0.0f;
    s_lastHumidity = 0.0f;
    s_previousMode = MODE_NORMAL;
    s_lastSample = (MeasurementSample){ 0 };
    DHT_Init();
}

/* Classifies a sensor that has both a lower AND upper bound per mode
 * (temperature only - Sec 2.5). The Normal band is nested inside the
 * wider Warning band (confirmed against Configuration's own default
 * values, see configuration.c's history in PROJECT_GUIDE.md), so
 * checking the tighter Normal band first and the wider Warning band
 * second is enough - anything outside both is Error. */
static SystemMode ClassifyRange(float value, float normalLow, float normalHigh, float warningLow, float warningHigh)
{
    if (value >= normalLow && value <= normalHigh)
    {
        return MODE_NORMAL;
    }
    if (value >= warningLow && value <= warningHigh)
    {
        return MODE_WARNING;
    }
    return MODE_ERROR;
}

/* Classifies a sensor with only a lower bound per mode (humidity, light,
 * battery - Sec 2.5, no upper bound exists for these). */
static SystemMode ClassifyLowerBound(float value, float normalLower, float warningLower)
{
    if (value >= normalLower)
    {
        return MODE_NORMAL;
    }
    if (value >= warningLower)
    {
        return MODE_WARNING;
    }
    return MODE_ERROR;
}

/* The overall mode is the worst (highest-severity) of the 4 sensors
 * (Sec 2.10: "one or more measurements in Warning/Error range"). This
 * relies on SystemMode's enum values already being ordered
 * MODE_NORMAL(0) < MODE_WARNING(1) < MODE_ERROR(2) in tlv_common.h. */
static SystemMode WorstOf4(SystemMode a, SystemMode b, SystemMode c, SystemMode d)
{
    SystemMode worst = a;
    if (b > worst) { worst = b; }
    if (c > worst) { worst = c; }
    if (d > worst) { worst = d; }
    return worst;
}

void Monitor_Sample(uint32_t timestamp, MeasurementSample *out_sample)
{
    DhtReading dhtReading;
    float tempNormalLow, tempNormalHigh, tempWarningLow, tempWarningHigh;
    SystemMode tempMode, humidityMode, lightMode, batteryMode, overallMode;
    osThreadId_t thisTask;
    osPriority_t originalPriority;

    /* DHT_Read()'s bit-banging is timing-critical (tens of microseconds
     * per step) and has no protection of its own against being
     * preempted mid-read - boosting this task's own priority for the
     * duration of the call is the same fix proven necessary on real
     * hardware during the DHT driver's own bring-up (see dht.c's history
     * in PROJECT_GUIDE.md). Done here, inside Monitor, so whichever task
     * calls Monitor_Sample() does not need to know about this DHT-
     * specific quirk. */
    thisTask = osThreadGetId();
    originalPriority = osThreadGetPriority(thisTask);
    osThreadSetPriority(thisTask, osPriorityAboveNormal);

    if (DHT_Read(&dhtReading) == DHT_OK)
    {
        s_lastTemperature = dhtReading.temperatureC;
        s_lastHumidity = dhtReading.humidityPercent;
    }
    /* else: DHT_Read() failed - keep the last successfully-read values,
     * do not replace them with 0.0 (see monitor.h). */

    osThreadSetPriority(thisTask, originalPriority);

    out_sample->timestamp = timestamp;
    out_sample->temperature = s_lastTemperature;
    out_sample->humidity = s_lastHumidity;
    out_sample->battery = Battery_ReadVoltage();
    out_sample->light = Light_ReadPercent();

    Config_GetTempNormalRange(&tempNormalLow, &tempNormalHigh);
    Config_GetTempWarningRange(&tempWarningLow, &tempWarningHigh);
    tempMode = ClassifyRange(out_sample->temperature, tempNormalLow, tempNormalHigh, tempWarningLow, tempWarningHigh);

    humidityMode = ClassifyLowerBound(out_sample->humidity, Config_GetHumidityNormalLower(), Config_GetHumidityWarningLower());
    lightMode = ClassifyLowerBound(out_sample->light, Config_GetLightNormalLower(), Config_GetLightWarningLower());
    batteryMode = ClassifyLowerBound(out_sample->battery, Config_GetBatteryNormalLower(), Config_GetBatteryWarningLower());

    overallMode = WorstOf4(tempMode, humidityMode, lightMode, batteryMode);
    out_sample->mode = (uint8_t)overallMode;

    /* Keep-Alive (Sec 2.8) reads this back via Monitor_GetLastSample() on
     * its own independent 6s cycle - captured here, now that every field
     * of *out_sample is final for this cycle. */
    s_lastSample = *out_sample;

    /* Sec 2.1: "sends measured data + resulting mode to Log" - every
     * cycle, unconditionally. */
    Log_Write(out_sample);

    /* Sec 2.5's "data" priority tier (TAG_DATA_REPORT) needs a producer -
     * Monitor's own periodic sample is exactly that, same "every cycle,
     * unconditionally" cadence as the Log_Write() call right above.
     * Enqueued at data priority (lowest, Sec 7) - CommTxTask sends it
     * when nothing higher-priority (keepalive/event) is waiting.
     *
     * Phase 1 (LNC Timestamp/RTC Hardening): the enqueue itself is gated
     * on RtcSync_IsSynchronized() - before the RTC has ever been set from
     * a real CC-supplied epoch, this frame's timestamp would be
     * misleading if it reached DataStore, so it is simply never sent
     * (this function's own sampling/classification/Log_Write() above are
     * all unaffected - only this transmission is withheld). */
    {
        uint8_t dataFrame[32];
        uint16_t dataFrameLength = Message_BuildDataReport(out_sample, dataFrame, sizeof(dataFrame));

        if (dataFrameLength > 0 && RtcSync_IsSynchronized())
        {
            TxQueue_EnqueueData(dataFrame, dataFrameLength);
        }
    }

    /* Sec 2.1: "on a mode change, sends measured values to Event" - only
     * when the mode actually differs from last cycle. */
    if (out_sample->mode != s_previousMode)
    {
        ModeTransitionMessage transition;
        uint8_t frame[64];
        uint16_t frameLength;

        transition.timestamp = timestamp;
        transition.oldMode = s_previousMode;
        transition.newMode = out_sample->mode;

        /* Event writes its own record, drives the LED/buzzer, and
         * returns the CC-bound frame - now enqueued at event priority
         * (Sec 7) so CommTxTask actually sends it. Phase 1 (LNC
         * Timestamp/RTC Hardening): the enqueue is gated on
         * RtcSync_IsSynchronized(), same reasoning as the data report
         * above - Event_OnModeTransition() itself (local record, LED,
         * buzzer) always runs regardless. */
        frameLength = Event_OnModeTransition(&transition, frame, sizeof(frame));

        if (frameLength > 0 && RtcSync_IsSynchronized())
        {
            TxQueue_EnqueueEvent(frame, frameLength);
        }

        s_previousMode = out_sample->mode;
    }
}

void Monitor_GetLastSample(MeasurementSample *out_sample)
{
    *out_sample = s_lastSample;
}
