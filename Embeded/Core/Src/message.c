/*
 * message.c
 *
 * Purpose:
 *   Implements the message builders and parsers declared in message.h.
 *
 * Layer:
 *   Message
 *
 * This file does NOT:
 *   - Know about UART, Ethernet, or any transport.
 *   - Use FreeRTOS.
 */

#include "message.h"
#include "protocol.h"
#include "tlv_common.h"
#include <string.h>

/* ============================================================
 * Data / Keep-alive
 * ============================================================ */

/* timestamp(4) + temperature(4) + humidity(4) + light(4) + battery(4) + mode(1) */
#define MEASUREMENT_SAMPLE_SIZE 21

/* Shared by Message_BuildKeepAlive and Message_BuildDataReport. */
static void SerializeMeasurementSample(const MeasurementSample *sample, uint8_t value[MEASUREMENT_SAMPLE_SIZE])
{
    value[0] = (uint8_t)(sample->timestamp & 0xFF);
    value[1] = (uint8_t)((sample->timestamp >> 8) & 0xFF);
    value[2] = (uint8_t)((sample->timestamp >> 16) & 0xFF);
    value[3] = (uint8_t)((sample->timestamp >> 24) & 0xFF);

    /* Floats are copied byte-for-byte: both the STM32 (Cortex-M4) and a
       Windows PC store IEEE754 floats in little-endian order, so no
       conversion is needed here. This would NOT be safe on a big-endian
       machine, but neither of our two real targets is one. */
    memcpy(&value[4],  &sample->temperature, sizeof(float));
    memcpy(&value[8],  &sample->humidity,    sizeof(float));
    memcpy(&value[12], &sample->light,       sizeof(float));
    memcpy(&value[16], &sample->battery,     sizeof(float));

    value[20] = sample->mode;
}

uint16_t Message_BuildKeepAlive(const MeasurementSample *sample, uint8_t *out_buffer, uint16_t out_buffer_size)
{
    uint8_t value[MEASUREMENT_SAMPLE_SIZE];
    SerializeMeasurementSample(sample, value);
    return Protocol_EncodeFrame((uint8_t)TAG_KEEPALIVE, value, sizeof(value), out_buffer, out_buffer_size);
}

uint16_t Message_BuildDataReport(const MeasurementSample *sample, uint8_t *out_buffer, uint16_t out_buffer_size)
{
    uint8_t value[MEASUREMENT_SAMPLE_SIZE];
    SerializeMeasurementSample(sample, value);
    return Protocol_EncodeFrame((uint8_t)TAG_DATA_REPORT, value, sizeof(value), out_buffer, out_buffer_size);
}

/* ============================================================
 * Single-timestamp messages
 * ============================================================ */

#define TIMESTAMP_SIZE 4

static void SerializeTimestamp(uint32_t timestamp, uint8_t value[TIMESTAMP_SIZE])
{
    value[0] = (uint8_t)(timestamp & 0xFF);
    value[1] = (uint8_t)((timestamp >> 8) & 0xFF);
    value[2] = (uint8_t)((timestamp >> 16) & 0xFF);
    value[3] = (uint8_t)((timestamp >> 24) & 0xFF);
}

/* Reverse of SerializeTimestamp - used by every parser below. */
static uint32_t DeserializeUint32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0]
         | ((uint32_t)bytes[1] << 8)
         | ((uint32_t)bytes[2] << 16)
         | ((uint32_t)bytes[3] << 24);
}

/* Reverse of the memcpy-based float serialization used elsewhere in this file. */
static float DeserializeFloat(const uint8_t *bytes)
{
    float result;
    memcpy(&result, bytes, sizeof(float));
    return result;
}

/* CC -> LNC: the LNC receives this, so it parses, not builds. */
int Message_ParseSetRtcDateTime(const ProtocolFrame *frame, TimestampMessage *out_message)
{
    if (frame->tag != TAG_SET_RTC_DATETIME || frame->length != TIMESTAMP_SIZE)
    {
        return 0;
    }
    out_message->timestamp = DeserializeUint32(frame->value);
    return 1;
}

uint16_t Message_BuildSystemTimeResponse(const TimestampMessage *message, uint8_t *out_buffer, uint16_t out_buffer_size)
{
    uint8_t value[TIMESTAMP_SIZE];
    SerializeTimestamp(message->timestamp, value);
    return Protocol_EncodeFrame((uint8_t)TAG_SYSTEM_TIME_RESPONSE, value, sizeof(value), out_buffer, out_buffer_size);
}

uint16_t Message_BuildEventConfigChanged(const TimestampMessage *message, uint8_t *out_buffer, uint16_t out_buffer_size)
{
    uint8_t value[TIMESTAMP_SIZE];
    SerializeTimestamp(message->timestamp, value);
    return Protocol_EncodeFrame((uint8_t)TAG_EVENT_CONFIG_CHANGED, value, sizeof(value), out_buffer, out_buffer_size);
}

int Message_ParseGetSystemTimeRequest(const ProtocolFrame *frame)
{
    return (frame->tag == TAG_GET_SYSTEM_TIME_REQUEST && frame->length == 0) ? 1 : 0;
}

/* ============================================================
 * Timestamp + one flag byte
 * ============================================================ */

#define TIMESTAMP_WITH_FLAG_SIZE 5

static void SerializeTimestampWithFlag(uint32_t timestamp, uint8_t flag, uint8_t value[TIMESTAMP_WITH_FLAG_SIZE])
{
    SerializeTimestamp(timestamp, value);
    value[4] = flag;
}

uint16_t Message_BuildEventObjectDetection(const TimestampWithFlagMessage *message, uint8_t *out_buffer, uint16_t out_buffer_size)
{
    uint8_t value[TIMESTAMP_WITH_FLAG_SIZE];
    SerializeTimestampWithFlag(message->timestamp, message->flag, value);
    return Protocol_EncodeFrame((uint8_t)TAG_EVENT_OBJECT_DETECTION, value, sizeof(value), out_buffer, out_buffer_size);
}

uint16_t Message_BuildEventStartup(const TimestampWithFlagMessage *message, uint8_t *out_buffer, uint16_t out_buffer_size)
{
    uint8_t value[TIMESTAMP_WITH_FLAG_SIZE];
    SerializeTimestampWithFlag(message->timestamp, message->flag, value);
    return Protocol_EncodeFrame((uint8_t)TAG_EVENT_STARTUP, value, sizeof(value), out_buffer, out_buffer_size);
}

/* ============================================================
 * Date/time ranges
 * ============================================================ */

/* requestId(1) + startTime(4) + endTime(4) */
#define TIME_RANGE_SIZE 9

int Message_ParseGetMeasurementsByRangeRequest(const ProtocolFrame *frame, TimeRangeMessage *out_message)
{
    if (frame->tag != TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST || frame->length != TIME_RANGE_SIZE)
    {
        return 0;
    }
    out_message->requestId = frame->value[0];
    out_message->startTime = DeserializeUint32(&frame->value[1]);
    out_message->endTime = DeserializeUint32(&frame->value[5]);
    return 1;
}

int Message_ParseGetEventsByRangeRequest(const ProtocolFrame *frame, TimeRangeMessage *out_message)
{
    if (frame->tag != TAG_GET_EVENTS_BY_RANGE_REQUEST || frame->length != TIME_RANGE_SIZE)
    {
        return 0;
    }
    out_message->requestId = frame->value[0];
    out_message->startTime = DeserializeUint32(&frame->value[1]);
    out_message->endTime = DeserializeUint32(&frame->value[5]);
    return 1;
}

/* ============================================================
 * Configuration "set limit" commands
 * ============================================================ */

#define TEMPERATURE_RANGE_SIZE 8

int Message_ParseSetTempNormalRange(const ProtocolFrame *frame, TemperatureRangeMessage *out_message)
{
    if (frame->tag != TAG_SET_TEMP_NORMAL_RANGE || frame->length != TEMPERATURE_RANGE_SIZE)
    {
        return 0;
    }
    out_message->low = DeserializeFloat(&frame->value[0]);
    out_message->high = DeserializeFloat(&frame->value[4]);
    return 1;
}

int Message_ParseSetTempWarningRange(const ProtocolFrame *frame, TemperatureRangeMessage *out_message)
{
    if (frame->tag != TAG_SET_TEMP_WARNING_RANGE || frame->length != TEMPERATURE_RANGE_SIZE)
    {
        return 0;
    }
    out_message->low = DeserializeFloat(&frame->value[0]);
    out_message->high = DeserializeFloat(&frame->value[4]);
    return 1;
}

#define SINGLE_LIMIT_SIZE 4

int Message_ParseSetHumidityNormalLower(const ProtocolFrame *frame, SingleLimitMessage *out_message)
{
    if (frame->tag != TAG_SET_HUMIDITY_NORMAL_LOWER || frame->length != SINGLE_LIMIT_SIZE)
    {
        return 0;
    }
    out_message->limitValue = DeserializeFloat(frame->value);
    return 1;
}

int Message_ParseSetHumidityWarningLower(const ProtocolFrame *frame, SingleLimitMessage *out_message)
{
    if (frame->tag != TAG_SET_HUMIDITY_WARNING_LOWER || frame->length != SINGLE_LIMIT_SIZE)
    {
        return 0;
    }
    out_message->limitValue = DeserializeFloat(frame->value);
    return 1;
}

int Message_ParseSetLightNormalLower(const ProtocolFrame *frame, SingleLimitMessage *out_message)
{
    if (frame->tag != TAG_SET_LIGHT_NORMAL_LOWER || frame->length != SINGLE_LIMIT_SIZE)
    {
        return 0;
    }
    out_message->limitValue = DeserializeFloat(frame->value);
    return 1;
}

int Message_ParseSetLightWarningLower(const ProtocolFrame *frame, SingleLimitMessage *out_message)
{
    if (frame->tag != TAG_SET_LIGHT_WARNING_LOWER || frame->length != SINGLE_LIMIT_SIZE)
    {
        return 0;
    }
    out_message->limitValue = DeserializeFloat(frame->value);
    return 1;
}

int Message_ParseSetBatteryNormalLower(const ProtocolFrame *frame, SingleLimitMessage *out_message)
{
    if (frame->tag != TAG_SET_BATTERY_NORMAL_LOWER || frame->length != SINGLE_LIMIT_SIZE)
    {
        return 0;
    }
    out_message->limitValue = DeserializeFloat(frame->value);
    return 1;
}

int Message_ParseSetBatteryWarningLower(const ProtocolFrame *frame, SingleLimitMessage *out_message)
{
    if (frame->tag != TAG_SET_BATTERY_WARNING_LOWER || frame->length != SINGLE_LIMIT_SIZE)
    {
        return 0;
    }
    out_message->limitValue = DeserializeFloat(frame->value);
    return 1;
}

/* ============================================================
 * Mode transition event
 * ============================================================ */

#define MODE_TRANSITION_SIZE 6

uint16_t Message_BuildEventModeTransition(const ModeTransitionMessage *message, uint8_t *out_buffer, uint16_t out_buffer_size)
{
    uint8_t value[MODE_TRANSITION_SIZE];

    SerializeTimestamp(message->timestamp, value);
    value[4] = message->oldMode;
    value[5] = message->newMode;

    return Protocol_EncodeFrame((uint8_t)TAG_EVENT_MODE_TRANSITION, value, sizeof(value), out_buffer, out_buffer_size);
}

/* ============================================================
 * Chunked bulk-transfer responses
 * ============================================================ */

/* RequestId(1) + ChunkSeq(2) + MoreDataFlag(1) */
#define CHUNK_HEADER_SIZE 4

static void SerializeChunkHeader(uint8_t requestId, uint16_t chunkSeq, uint8_t moreDataFlag,
                                  uint8_t value[CHUNK_HEADER_SIZE])
{
    value[0] = requestId;
    value[1] = (uint8_t)(chunkSeq & 0xFF);
    value[2] = (uint8_t)((chunkSeq >> 8) & 0xFF);
    value[3] = moreDataFlag;
}

uint16_t Message_BuildMeasurementChunkResponse(uint8_t requestId, uint16_t chunkSeq, uint8_t moreDataFlag,
                                                const MeasurementSample *samples, uint8_t sampleCount,
                                                uint8_t *out_buffer, uint16_t out_buffer_size)
{
    uint8_t value[CHUNK_HEADER_SIZE + (MAX_MEASUREMENTS_PER_CHUNK * MEASUREMENT_SAMPLE_SIZE)];
    uint8_t i;

    if (sampleCount > MAX_MEASUREMENTS_PER_CHUNK)
    {
        return 0;
    }

    SerializeChunkHeader(requestId, chunkSeq, moreDataFlag, value);

    for (i = 0; i < sampleCount; i++)
    {
        SerializeMeasurementSample(&samples[i], &value[CHUNK_HEADER_SIZE + (i * MEASUREMENT_SAMPLE_SIZE)]);
    }

    return Protocol_EncodeFrame((uint8_t)TAG_MEASUREMENT_CHUNK_RESPONSE, value,
                                 (uint16_t)(CHUNK_HEADER_SIZE + (sampleCount * MEASUREMENT_SAMPLE_SIZE)),
                                 out_buffer, out_buffer_size);
}

/* Copies "record" into "out" as EVENT_RECORD_SIZE wire bytes: timestamp +
   eventType + a description field that is always null-terminated within
   its EVENT_DESCRIPTION_SIZE bytes, truncating a longer source string. */
static void SerializeEventRecord(const EventRecord *record, uint8_t out[EVENT_RECORD_SIZE])
{
    size_t descriptionLength = strlen(record->description);
    if (descriptionLength > EVENT_DESCRIPTION_SIZE - 1)
    {
        descriptionLength = EVENT_DESCRIPTION_SIZE - 1;
    }

    SerializeTimestamp(record->timestamp, out);
    out[4] = record->eventType;
    memset(&out[5], 0, EVENT_DESCRIPTION_SIZE);
    memcpy(&out[5], record->description, descriptionLength);
    /* out[5 + descriptionLength] through the end of the field are already
       0 from the memset above, guaranteeing null-termination. */
}

uint16_t Message_BuildEventChunkResponse(uint8_t requestId, uint16_t chunkSeq, uint8_t moreDataFlag,
                                          const EventRecord *events, uint8_t eventCount,
                                          uint8_t *out_buffer, uint16_t out_buffer_size)
{
    uint8_t value[CHUNK_HEADER_SIZE + (MAX_EVENTS_PER_CHUNK * EVENT_RECORD_SIZE)];
    uint8_t i;

    if (eventCount > MAX_EVENTS_PER_CHUNK)
    {
        return 0;
    }

    SerializeChunkHeader(requestId, chunkSeq, moreDataFlag, value);

    for (i = 0; i < eventCount; i++)
    {
        SerializeEventRecord(&events[i], &value[CHUNK_HEADER_SIZE + (i * EVENT_RECORD_SIZE)]);
    }

    return Protocol_EncodeFrame((uint8_t)TAG_EVENT_CHUNK_RESPONSE, value,
                                 (uint16_t)(CHUNK_HEADER_SIZE + (eventCount * EVENT_RECORD_SIZE)),
                                 out_buffer, out_buffer_size);
}
