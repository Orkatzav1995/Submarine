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

void Monitor_Init(void)
{
    s_lastTemperature = 0.0f;
    s_lastHumidity = 0.0f;
    s_previousMode = MODE_NORMAL;
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

    /* Sec 2.1: "sends measured data + resulting mode to Log" - every
     * cycle, unconditionally. */
    Log_Write(out_sample);

    /* Sec 2.1: "on a mode change, sends measured values to Event" - only
     * when the mode actually differs from last cycle. */
    if (out_sample->mode != s_previousMode)
    {
        ModeTransitionMessage transition;
        uint8_t frame[64];

        transition.timestamp = timestamp;
        transition.oldMode = s_previousMode;
        transition.newMode = out_sample->mode;

        /* Event writes its own record, drives the LED/buzzer, and
         * returns the CC-bound frame - discarded here, same "build but
         * don't send" boundary Event itself already has (no
         * Communication/TX-queue module exists yet to transmit it). */
        (void)Event_OnModeTransition(&transition, frame, sizeof(frame));

        s_previousMode = out_sample->mode;
    }
}
