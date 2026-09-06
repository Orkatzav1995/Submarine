/*
 * communication.c
 *
 * See communication.h for purpose, layer, and exactly which tags are
 * handled this pass vs. deliberately deferred.
 */

#include "communication.h"
#include "message.h"
#include "configuration.h"
#include "event.h"
#include "tx_queue.h"

static uint8_t s_lastDispatchedTag = 0;
static uint32_t s_setLimitCount = 0;
static uint32_t s_systemTimeRequestCount = 0;
static uint32_t s_deferredCount = 0;
static uint32_t s_unknownTagCount = 0;

/* Shared by all 8 "set limit" cases: reports the change to Event the way
 * Sec 2.6 requires ("Receives configuration changes from Communication"
 * -> Event writes it to the events file), then enqueues the frame Event
 * built at event priority (Sec 7) so CommTxTask sends it to the CC. */
static void ReportConfigurationChanged(uint32_t timestamp)
{
    TimestampMessage change;
    uint8_t frame[32];
    uint16_t frameLength;

    change.timestamp = timestamp;
    frameLength = Event_OnConfigurationChanged(&change, frame, sizeof(frame));

    if (frameLength > 0)
    {
        TxQueue_EnqueueEvent(frame, frameLength);
    }
}

/* Shared by the system-time request: builds the reply and enqueues it
 * at event priority (Sec 7) - not because it IS an event, but because
 * among the 3 frozen tiers it best matches this reply's traffic shape
 * (irregular, triggered by a CC request, not periodic like keepalive/
 * data) and that queue has the most headroom (depth 8). This also
 * ensures CommTxTask is the only task that ever calls
 * Transport_UART_Send() - CommRxTask no longer sends directly. */
static void SendSystemTimeResponse(uint32_t timestamp)
{
    TimestampMessage response;
    uint8_t frame[32];
    uint16_t frameLength;

    response.timestamp = timestamp;
    frameLength = Message_BuildSystemTimeResponse(&response, frame, sizeof(frame));

    if (frameLength > 0)
    {
        TxQueue_EnqueueEvent(frame, frameLength);
    }
}

void Communication_Dispatch(const ProtocolFrame *frame, uint32_t timestamp)
{
    s_lastDispatchedTag = frame->tag;

    switch ((MessageTag)frame->tag)
    {
        case TAG_SET_TEMP_NORMAL_RANGE:
        {
            TemperatureRangeMessage message;
            if (Message_ParseSetTempNormalRange(frame, &message))
            {
                Config_SetTempNormalRange(message.low, message.high);
                ReportConfigurationChanged(timestamp);
                s_setLimitCount++;
            }
            else
            {
                s_unknownTagCount++;
            }
            break;
        }

        case TAG_SET_TEMP_WARNING_RANGE:
        {
            TemperatureRangeMessage message;
            if (Message_ParseSetTempWarningRange(frame, &message))
            {
                Config_SetTempWarningRange(message.low, message.high);
                ReportConfigurationChanged(timestamp);
                s_setLimitCount++;
            }
            else
            {
                s_unknownTagCount++;
            }
            break;
        }

        case TAG_SET_HUMIDITY_NORMAL_LOWER:
        {
            SingleLimitMessage message;
            if (Message_ParseSetHumidityNormalLower(frame, &message))
            {
                Config_SetHumidityNormalLower(message.limitValue);
                ReportConfigurationChanged(timestamp);
                s_setLimitCount++;
            }
            else
            {
                s_unknownTagCount++;
            }
            break;
        }

        case TAG_SET_HUMIDITY_WARNING_LOWER:
        {
            SingleLimitMessage message;
            if (Message_ParseSetHumidityWarningLower(frame, &message))
            {
                Config_SetHumidityWarningLower(message.limitValue);
                ReportConfigurationChanged(timestamp);
                s_setLimitCount++;
            }
            else
            {
                s_unknownTagCount++;
            }
            break;
        }

        case TAG_SET_LIGHT_NORMAL_LOWER:
        {
            SingleLimitMessage message;
            if (Message_ParseSetLightNormalLower(frame, &message))
            {
                Config_SetLightNormalLower(message.limitValue);
                ReportConfigurationChanged(timestamp);
                s_setLimitCount++;
            }
            else
            {
                s_unknownTagCount++;
            }
            break;
        }

        case TAG_SET_LIGHT_WARNING_LOWER:
        {
            SingleLimitMessage message;
            if (Message_ParseSetLightWarningLower(frame, &message))
            {
                Config_SetLightWarningLower(message.limitValue);
                ReportConfigurationChanged(timestamp);
                s_setLimitCount++;
            }
            else
            {
                s_unknownTagCount++;
            }
            break;
        }

        case TAG_SET_BATTERY_NORMAL_LOWER:
        {
            SingleLimitMessage message;
            if (Message_ParseSetBatteryNormalLower(frame, &message))
            {
                Config_SetBatteryNormalLower(message.limitValue);
                ReportConfigurationChanged(timestamp);
                s_setLimitCount++;
            }
            else
            {
                s_unknownTagCount++;
            }
            break;
        }

        case TAG_SET_BATTERY_WARNING_LOWER:
        {
            SingleLimitMessage message;
            if (Message_ParseSetBatteryWarningLower(frame, &message))
            {
                Config_SetBatteryWarningLower(message.limitValue);
                ReportConfigurationChanged(timestamp);
                s_setLimitCount++;
            }
            else
            {
                s_unknownTagCount++;
            }
            break;
        }

        case TAG_GET_SYSTEM_TIME_REQUEST:
        {
            if (Message_ParseGetSystemTimeRequest(frame))
            {
                SendSystemTimeResponse(timestamp);
                s_systemTimeRequestCount++;
            }
            else
            {
                s_unknownTagCount++;
            }
            break;
        }

        /* Recognized tags, deliberately not acted on yet - see
         * communication.h's header comment for why each one is deferred. */
        case TAG_SET_RTC_DATETIME:
        case TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST:
        case TAG_GET_EVENTS_BY_RANGE_REQUEST:
            s_deferredCount++;
            break;

        /* Every other tag, including ones this side would never expect
         * to receive (e.g. LNC->CC-only tags) - matches the frozen
         * protocol's "unknown tags dropped silently" (Sec 6). */
        default:
            s_unknownTagCount++;
            break;
    }
}

uint8_t Communication_GetLastDispatchedTag(void)
{
    return s_lastDispatchedTag;
}

uint32_t Communication_GetSetLimitCount(void)
{
    return s_setLimitCount;
}

uint32_t Communication_GetSystemTimeRequestCount(void)
{
    return s_systemTimeRequestCount;
}

uint32_t Communication_GetDeferredCount(void)
{
    return s_deferredCount;
}

uint32_t Communication_GetUnknownTagCount(void)
{
    return s_unknownTagCount;
}
