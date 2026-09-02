/*
 * message_test.c
 *
 * Purpose:
 *   Stand-alone PC test program for message.c/message.h (the Message
 *   layer). For the 7 tags the LNC sends (KeepAlive, DataReport, the 4
 *   event types, SystemTimeResponse), builds a frame and confirms it
 *   decodes correctly. For the 12 tags the LNC receives (the "set limit"
 *   commands and "get" requests), manually constructs the raw frame bytes
 *   the Central Computer would send, decodes them, and confirms the
 *   parser extracts the right fields - and rejects a wrong tag.
 *
 * How to build and run (from this folder):
 *   gcc -I "../../Embeded/Core/Inc" -I "../../Common/Protocol" message_test.c "../../Embeded/Core/Src/message.c" "../../Embeded/Core/Src/protocol.c" -o message_test.exe
 *   ./message_test.exe
 *
 * This file does NOT:
 *   - Touch UART, Ethernet, FreeRTOS, or any hardware.
 *   - Test the Transport or Application layers.
 */

#include "message.h"
#include "protocol.h"
#include "tlv_common.h"
#include <stdio.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;

static void check(int condition, const char *description)
{
    g_tests_run++;
    if (condition)
    {
        printf("  PASS: %s\n", description);
    }
    else
    {
        printf("  FAIL: %s\n", description);
        g_tests_failed++;
    }
}

/* Feeds "frame_buffer" through a fresh decoder. Shared by every test below. */
static int DecodeFrame(const uint8_t *frame_buffer, uint16_t frame_size, ProtocolFrame *decoded_frame)
{
    ProtocolDecoder decoder;
    ProtocolDecodeStatus status;
    uint16_t i;
    int frame_was_ready = 0;

    Protocol_DecoderInit(&decoder);
    for (i = 0; i < frame_size; i++)
    {
        status = Protocol_FeedByte(&decoder, frame_buffer[i], decoded_frame);
        if (status == PROTOCOL_DECODE_FRAME_READY)
        {
            frame_was_ready = 1;
        }
    }
    return frame_was_ready;
}

static uint32_t ReadUint32LE(const uint8_t *value)
{
    return (uint32_t)value[0]
         | ((uint32_t)value[1] << 8)
         | ((uint32_t)value[2] << 16)
         | ((uint32_t)value[3] << 24);
}

static float ReadFloatLE(const uint8_t *value)
{
    float result;
    memcpy(&result, value, sizeof(float));
    return result;
}

static void WriteUint32LE(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
    out[2] = (uint8_t)((v >> 16) & 0xFF);
    out[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void WriteFloatLE(uint8_t *out, float v)
{
    memcpy(out, &v, sizeof(float));
}

/* Builds a raw frame the way the Central Computer would - used to test the
   12 parser functions, since the LNC itself never builds these tags. */
static ProtocolFrame DecodeRawFrame(uint8_t tag, const uint8_t *value, uint16_t valueLength)
{
    uint8_t frame_buffer[64];
    uint16_t frame_size;
    ProtocolFrame decoded;

    frame_size = Protocol_EncodeFrame(tag, value, valueLength, frame_buffer, sizeof(frame_buffer));
    DecodeFrame(frame_buffer, frame_size, &decoded);
    return decoded;
}

/* ============================================================ */

static void test_measurement_sample_messages(void)
{
    MeasurementSample sample;
    uint8_t frame_buffer[64];
    uint16_t frame_size;
    ProtocolFrame decoded;

    sample.timestamp = 1700000000u;
    sample.temperature = 23.5f;
    sample.humidity = 45.0f;
    sample.light = 800.0f;
    sample.battery = 3.7f;
    sample.mode = 0;

    printf("\nTest: KeepAlive\n");
    frame_size = Message_BuildKeepAlive(&sample, frame_buffer, sizeof(frame_buffer));
    check(frame_size == PROTOCOL_FRAME_OVERHEAD + 21, "correct frame size");
    check(DecodeFrame(frame_buffer, frame_size, &decoded), "decodes");
    check(decoded.tag == TAG_KEEPALIVE, "correct tag");
    check(ReadUint32LE(&decoded.value[0]) == sample.timestamp, "timestamp matches");
    check(ReadFloatLE(&decoded.value[4]) == sample.temperature, "temperature matches");
    check(ReadFloatLE(&decoded.value[8]) == sample.humidity, "humidity matches");
    check(ReadFloatLE(&decoded.value[12]) == sample.light, "light matches");
    check(ReadFloatLE(&decoded.value[16]) == sample.battery, "battery matches");
    check(decoded.value[20] == sample.mode, "mode matches");

    printf("\nTest: DataReport\n");
    frame_size = Message_BuildDataReport(&sample, frame_buffer, sizeof(frame_buffer));
    check(frame_size == PROTOCOL_FRAME_OVERHEAD + 21, "correct frame size");
    check(DecodeFrame(frame_buffer, frame_size, &decoded), "decodes");
    check(decoded.tag == TAG_DATA_REPORT, "correct tag (distinct from TAG_KEEPALIVE)");

    printf("\nTest: too-small buffer refused (both builders)\n");
    check(Message_BuildKeepAlive(&sample, frame_buffer, 3) == 0, "KeepAlive refuses too-small buffer");
    check(Message_BuildDataReport(&sample, frame_buffer, 3) == 0, "DataReport refuses too-small buffer");
}

static void test_timestamp_messages(void)
{
    TimestampMessage message;
    uint8_t frame_buffer[64];
    uint16_t frame_size;
    ProtocolFrame decoded;
    uint8_t rawValue[4];
    TimestampMessage parsed;

    message.timestamp = 1650000000u;

    printf("\nTest: SystemTimeResponse (LNC builds this - LNC -> CC)\n");
    frame_size = Message_BuildSystemTimeResponse(&message, frame_buffer, sizeof(frame_buffer));
    check(DecodeFrame(frame_buffer, frame_size, &decoded), "decodes");
    check(decoded.tag == TAG_SYSTEM_TIME_RESPONSE, "correct tag");
    check(ReadUint32LE(decoded.value) == message.timestamp, "timestamp matches");

    printf("\nTest: EventConfigChanged (LNC builds this - LNC -> CC)\n");
    frame_size = Message_BuildEventConfigChanged(&message, frame_buffer, sizeof(frame_buffer));
    check(DecodeFrame(frame_buffer, frame_size, &decoded), "decodes");
    check(decoded.tag == TAG_EVENT_CONFIG_CHANGED, "correct tag");
    check(ReadUint32LE(decoded.value) == message.timestamp, "timestamp matches");

    printf("\nTest: SetRtcDateTime (LNC parses this - CC -> LNC)\n");
    WriteUint32LE(rawValue, 1660000000u);
    decoded = DecodeRawFrame((uint8_t)TAG_SET_RTC_DATETIME, rawValue, sizeof(rawValue));
    check(Message_ParseSetRtcDateTime(&decoded, &parsed) == 1, "parses a correct SET_RTC_DATETIME frame");
    check(parsed.timestamp == 1660000000u, "timestamp matches");

    printf("\nTest: SetRtcDateTime rejects the wrong tag\n");
    decoded = DecodeRawFrame((uint8_t)TAG_SYSTEM_TIME_RESPONSE, rawValue, sizeof(rawValue));
    check(Message_ParseSetRtcDateTime(&decoded, &parsed) == 0, "rejects a frame with the wrong tag");

    printf("\nTest: GetSystemTimeRequest (empty payload, LNC parses this - CC -> LNC)\n");
    decoded = DecodeRawFrame((uint8_t)TAG_GET_SYSTEM_TIME_REQUEST, NULL, 0);
    check(decoded.length == 0, "decoded value length is 0");
    check(Message_ParseGetSystemTimeRequest(&decoded) == 1, "parses a correct GET_SYSTEM_TIME_REQUEST frame");

    decoded = DecodeRawFrame((uint8_t)TAG_SET_RTC_DATETIME, rawValue, sizeof(rawValue));
    check(Message_ParseGetSystemTimeRequest(&decoded) == 0, "rejects a frame with the wrong tag");
}

static void test_timestamp_with_flag_messages(void)
{
    TimestampWithFlagMessage message;
    uint8_t frame_buffer[64];
    uint16_t frame_size;
    ProtocolFrame decoded;

    message.timestamp = 1690000000u;
    message.flag = 1;

    printf("\nTest: EventObjectDetection\n");
    frame_size = Message_BuildEventObjectDetection(&message, frame_buffer, sizeof(frame_buffer));
    check(frame_size == PROTOCOL_FRAME_OVERHEAD + 5, "correct frame size");
    check(DecodeFrame(frame_buffer, frame_size, &decoded), "decodes");
    check(decoded.tag == TAG_EVENT_OBJECT_DETECTION, "correct tag");
    check(ReadUint32LE(decoded.value) == message.timestamp, "timestamp matches");
    check(decoded.value[4] == 1, "detected flag matches");

    printf("\nTest: EventStartup\n");
    message.flag = 0;
    frame_size = Message_BuildEventStartup(&message, frame_buffer, sizeof(frame_buffer));
    check(DecodeFrame(frame_buffer, frame_size, &decoded), "decodes");
    check(decoded.tag == TAG_EVENT_STARTUP, "correct tag (distinct from TAG_EVENT_OBJECT_DETECTION)");
    check(decoded.value[4] == 0, "was_watchdog_reset flag matches");
}

static void test_time_range_messages(void)
{
    uint8_t rawValue[9];
    ProtocolFrame decoded;
    TimeRangeMessage parsed;

    rawValue[0] = 42; /* requestId */
    WriteUint32LE(&rawValue[1], 1600000000u);
    WriteUint32LE(&rawValue[5], 1600086400u);

    printf("\nTest: GetMeasurementsByRangeRequest (LNC parses this - CC -> LNC)\n");
    decoded = DecodeRawFrame((uint8_t)TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST, rawValue, sizeof(rawValue));
    check(Message_ParseGetMeasurementsByRangeRequest(&decoded, &parsed) == 1, "parses a correct frame");
    check(parsed.requestId == 42, "requestId matches");
    check(parsed.startTime == 1600000000u, "startTime matches");
    check(parsed.endTime == 1600086400u, "endTime matches");

    printf("\nTest: GetEventsByRangeRequest (LNC parses this - CC -> LNC)\n");
    decoded = DecodeRawFrame((uint8_t)TAG_GET_EVENTS_BY_RANGE_REQUEST, rawValue, sizeof(rawValue));
    check(Message_ParseGetEventsByRangeRequest(&decoded, &parsed) == 1, "parses a correct frame");

    check(Message_ParseGetMeasurementsByRangeRequest(&decoded, &parsed) == 0,
          "GetMeasurementsByRangeRequest parser rejects a GetEventsByRangeRequest frame (wrong tag)");
}

static void test_chunk_response_messages(void)
{
    MeasurementSample samples[MAX_MEASUREMENTS_PER_CHUNK];
    EventRecord events[MAX_EVENTS_PER_CHUNK];
    uint8_t frame_buffer[300];
    uint16_t frame_size;
    ProtocolFrame decoded;
    uint8_t i;

    printf("\nTest: Message_BuildMeasurementChunkResponse\n");

    for (i = 0; i < MAX_MEASUREMENTS_PER_CHUNK; i++)
    {
        samples[i].timestamp = 1000u + i;
        samples[i].temperature = 20.0f + (float)i;
        samples[i].humidity = 50.0f;
        samples[i].light = 300.0f;
        samples[i].battery = 3.7f;
        samples[i].mode = MODE_NORMAL;
    }

    frame_size = Message_BuildMeasurementChunkResponse(7, 0x1234, 1, samples, MAX_MEASUREMENTS_PER_CHUNK,
                                                        frame_buffer, sizeof(frame_buffer));
    check(frame_size == PROTOCOL_FRAME_OVERHEAD + 4 + (MAX_MEASUREMENTS_PER_CHUNK * 21),
          "correct frame size at the max chunk record count");
    check(frame_size <= PROTOCOL_MAX_FRAME_SIZE, "fits within the 256-byte frame cap");
    check(DecodeFrame(frame_buffer, frame_size, &decoded), "decodes");
    check(decoded.tag == TAG_MEASUREMENT_CHUNK_RESPONSE, "correct tag");
    check(decoded.value[0] == 7, "requestId matches");
    check(decoded.value[1] == 0x34 && decoded.value[2] == 0x12, "chunkSeq matches (little-endian)");
    check(decoded.value[3] == 1, "moreDataFlag matches");
    check(ReadUint32LE(&decoded.value[4]) == samples[0].timestamp, "first record's timestamp matches");

    check(Message_BuildMeasurementChunkResponse(7, 0, 0, samples, MAX_MEASUREMENTS_PER_CHUNK + 1,
                                                 frame_buffer, sizeof(frame_buffer)) == 0,
          "rejects sampleCount > MAX_MEASUREMENTS_PER_CHUNK");

    printf("\nTest: Message_BuildEventChunkResponse\n");

    events[0].timestamp = 2000u;
    events[0].eventType = TAG_EVENT_STARTUP;
    strncpy(events[0].description, "Startup after watchdog reset", EVENT_DESCRIPTION_SIZE - 1);
    events[0].description[EVENT_DESCRIPTION_SIZE - 1] = '\0';

    events[1].timestamp = 2001u;
    events[1].eventType = TAG_EVENT_OBJECT_DETECTION;
    strncpy(events[1].description, "Object detected", EVENT_DESCRIPTION_SIZE - 1);
    events[1].description[EVENT_DESCRIPTION_SIZE - 1] = '\0';

    frame_size = Message_BuildEventChunkResponse(3, 5, 0, events, 2, frame_buffer, sizeof(frame_buffer));
    check(frame_size == PROTOCOL_FRAME_OVERHEAD + 4 + (2 * EVENT_RECORD_SIZE), "correct frame size for 2 records");
    check(DecodeFrame(frame_buffer, frame_size, &decoded), "decodes");
    check(decoded.tag == TAG_EVENT_CHUNK_RESPONSE, "correct tag");
    check(decoded.value[0] == 3, "requestId matches");
    check(decoded.value[3] == 0, "moreDataFlag matches");
    check(ReadUint32LE(&decoded.value[4]) == events[0].timestamp, "record 0 timestamp matches");
    check(decoded.value[8] == TAG_EVENT_STARTUP, "record 0 eventType matches");
    check(strcmp((const char *)&decoded.value[9], "Startup after watchdog reset") == 0,
          "record 0 description matches");
    check(ReadUint32LE(&decoded.value[4 + EVENT_RECORD_SIZE]) == events[1].timestamp,
          "record 1 timestamp matches");

    /* An over-long description is truncated but always stays null-terminated. */
    {
        char longDescription[EVENT_DESCRIPTION_SIZE + 20];
        memset(longDescription, 'X', sizeof(longDescription) - 1);
        longDescription[sizeof(longDescription) - 1] = '\0';

        events[0].timestamp = 3000u;
        events[0].eventType = TAG_EVENT_CONFIG_CHANGED;
        strncpy(events[0].description, longDescription, EVENT_DESCRIPTION_SIZE - 1);
        events[0].description[EVENT_DESCRIPTION_SIZE - 1] = '\0';

        frame_size = Message_BuildEventChunkResponse(1, 0, 0, events, 1, frame_buffer, sizeof(frame_buffer));
        check(DecodeFrame(frame_buffer, frame_size, &decoded), "decodes (truncated description)");
        check(decoded.value[9 + EVENT_DESCRIPTION_SIZE - 1] == 0,
              "description field's last byte is always null, even for a truncated source string");
    }

    check(Message_BuildEventChunkResponse(1, 0, 0, events, MAX_EVENTS_PER_CHUNK + 1,
                                           frame_buffer, sizeof(frame_buffer)) == 0,
          "rejects eventCount > MAX_EVENTS_PER_CHUNK");
}

static void test_temperature_range_messages(void)
{
    uint8_t rawValue[8];
    ProtocolFrame decoded;
    TemperatureRangeMessage parsed;

    WriteFloatLE(&rawValue[0], 10.0f);
    WriteFloatLE(&rawValue[4], 30.0f);

    printf("\nTest: SetTempNormalRange / SetTempWarningRange (LNC parses these - CC -> LNC)\n");
    decoded = DecodeRawFrame((uint8_t)TAG_SET_TEMP_NORMAL_RANGE, rawValue, sizeof(rawValue));
    check(Message_ParseSetTempNormalRange(&decoded, &parsed) == 1, "parses a correct SetTempNormalRange frame");
    check(parsed.low == 10.0f, "low matches");
    check(parsed.high == 30.0f, "high matches");

    decoded = DecodeRawFrame((uint8_t)TAG_SET_TEMP_WARNING_RANGE, rawValue, sizeof(rawValue));
    check(Message_ParseSetTempWarningRange(&decoded, &parsed) == 1, "parses a correct SetTempWarningRange frame");

    check(Message_ParseSetTempNormalRange(&decoded, &parsed) == 0,
          "SetTempNormalRange parser rejects a SetTempWarningRange frame (wrong tag)");
}

static void test_single_limit_messages(void)
{
    uint8_t rawValue[4];
    ProtocolFrame decoded;
    SingleLimitMessage parsed;

    WriteFloatLE(rawValue, 42.5f);

    printf("\nTest: the 6 single-limit set commands (LNC parses these - CC -> LNC)\n");

    decoded = DecodeRawFrame((uint8_t)TAG_SET_HUMIDITY_NORMAL_LOWER, rawValue, sizeof(rawValue));
    check(Message_ParseSetHumidityNormalLower(&decoded, &parsed) == 1, "SetHumidityNormalLower parses");
    check(parsed.limitValue == 42.5f, "SetHumidityNormalLower value matches");

    decoded = DecodeRawFrame((uint8_t)TAG_SET_HUMIDITY_WARNING_LOWER, rawValue, sizeof(rawValue));
    check(Message_ParseSetHumidityWarningLower(&decoded, &parsed) == 1, "SetHumidityWarningLower parses");

    decoded = DecodeRawFrame((uint8_t)TAG_SET_LIGHT_NORMAL_LOWER, rawValue, sizeof(rawValue));
    check(Message_ParseSetLightNormalLower(&decoded, &parsed) == 1, "SetLightNormalLower parses");

    decoded = DecodeRawFrame((uint8_t)TAG_SET_LIGHT_WARNING_LOWER, rawValue, sizeof(rawValue));
    check(Message_ParseSetLightWarningLower(&decoded, &parsed) == 1, "SetLightWarningLower parses");

    decoded = DecodeRawFrame((uint8_t)TAG_SET_BATTERY_NORMAL_LOWER, rawValue, sizeof(rawValue));
    check(Message_ParseSetBatteryNormalLower(&decoded, &parsed) == 1, "SetBatteryNormalLower parses");

    decoded = DecodeRawFrame((uint8_t)TAG_SET_BATTERY_WARNING_LOWER, rawValue, sizeof(rawValue));
    check(Message_ParseSetBatteryWarningLower(&decoded, &parsed) == 1, "SetBatteryWarningLower parses");

    printf("\nTest: a wrong-length payload is rejected\n");
    decoded = DecodeRawFrame((uint8_t)TAG_SET_HUMIDITY_NORMAL_LOWER, rawValue, 3); /* needs 4 bytes, not 3 */
    check(Message_ParseSetHumidityNormalLower(&decoded, &parsed) == 0, "rejects a wrong-length payload");
}

static void test_mode_transition_message(void)
{
    ModeTransitionMessage message;
    uint8_t frame_buffer[64];
    uint16_t frame_size;
    ProtocolFrame decoded;

    message.timestamp = 1710000000u;
    message.oldMode = 0; /* MODE_NORMAL */
    message.newMode = 1; /* MODE_WARNING */

    printf("\nTest: EventModeTransition\n");
    frame_size = Message_BuildEventModeTransition(&message, frame_buffer, sizeof(frame_buffer));
    check(frame_size == PROTOCOL_FRAME_OVERHEAD + 6, "correct frame size");
    check(DecodeFrame(frame_buffer, frame_size, &decoded), "decodes");
    check(decoded.tag == TAG_EVENT_MODE_TRANSITION, "correct tag");
    check(ReadUint32LE(&decoded.value[0]) == message.timestamp, "timestamp matches");
    check(decoded.value[4] == message.oldMode, "oldMode matches");
    check(decoded.value[5] == message.newMode, "newMode matches");
}

int main(void)
{
    printf("=== message.c test suite ===\n");

    test_measurement_sample_messages();
    test_timestamp_messages();
    test_timestamp_with_flag_messages();
    test_time_range_messages();
    test_chunk_response_messages();
    test_temperature_range_messages();
    test_single_limit_messages();
    test_mode_transition_message();

    printf("\n=== Summary: %d checks run, %d failed ===\n", g_tests_run, g_tests_failed);

    return (g_tests_failed == 0) ? 0 : 1;
}
