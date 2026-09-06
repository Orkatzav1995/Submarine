/*
 * gs_message_test.cpp
 *
 * Purpose:
 *   Stand-alone PC test program for GroundStation/message.h/.cpp (the
 *   Ground Station's own, independent Message layer). Covers both
 *   directions GS actually uses: builds the 2 "retrieve by range"
 *   requests and manually inspects the encoded bytes (no CC-side parser
 *   is exercised here - that's CC's own, separately-tested code), and
 *   parses the 2 chunked response types from hand-built frames with the
 *   exact wire layout the CC's message.cpp produces.
 *
 * How to build and run (from this folder):
 *   g++ -std=c++17 -I "../../GroundStation/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" gs_message_test.cpp "../../GroundStation/src/message.cpp" "../../Common/TLVCodec/tlv_codec.cpp" -o gs_message_test.exe
 *   ./gs_message_test.exe
 *
 * This file does NOT:
 *   - Touch sockets or any transport.
 */

#include "message.h"
#include "tlv_codec.h"
#include "tlv_common.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

static int g_testsRun = 0;
static int g_testsFailed = 0;

static void check(bool condition, const char *description)
{
    g_testsRun++;
    if (condition)
    {
        std::printf("  PASS: %s\n", description);
    }
    else
    {
        std::printf("  FAIL: %s\n", description);
        g_testsFailed++;
    }
}

/* Decodes a frame the same way real code would: feed the encoded bytes
   through a fresh tlv::Decoder, one byte at a time. This is the REAL
   codec - every test that round-trips through this genuinely exercises
   CRC-16/CCITT-FALSE validation, not a shortcut. */
static tlv::Frame decodeFrame(const std::vector<uint8_t> &encodedFrame)
{
    tlv::Decoder decoder;
    tlv::Frame decoded;

    for (uint8_t b : encodedFrame)
    {
        decoder.feedByte(b, decoded);
    }
    return decoded;
}

static void appendUint32LE(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static void appendFloatLE(std::vector<uint8_t> &out, float v)
{
    uint8_t bytes[sizeof(float)];
    std::memcpy(bytes, &v, sizeof(float));
    for (uint8_t b : bytes)
    {
        out.push_back(b);
    }
}

static std::vector<uint8_t> buildMeasurementSampleValueBytes(uint32_t timestamp, float temperature, float humidity,
                                                               float light, float battery, uint8_t mode)
{
    std::vector<uint8_t> value;
    appendUint32LE(value, timestamp);
    appendFloatLE(value, temperature);
    appendFloatLE(value, humidity);
    appendFloatLE(value, light);
    appendFloatLE(value, battery);
    value.push_back(mode);
    return value;
}

static std::vector<uint8_t> buildEventRecordValueBytes(uint32_t timestamp, uint8_t eventType, const std::string &description)
{
    std::vector<uint8_t> bytes;
    appendUint32LE(bytes, timestamp);
    bytes.push_back(eventType);
    std::vector<uint8_t> descriptionField(EVENT_DESCRIPTION_SIZE, 0);
    size_t copyLength = std::min(description.size(), static_cast<size_t>(EVENT_DESCRIPTION_SIZE - 1));
    std::memcpy(descriptionField.data(), description.data(), copyLength);
    bytes.insert(bytes.end(), descriptionField.begin(), descriptionField.end());
    return bytes;
}

static void testTimeRangeBuilders()
{
    std::printf("\nTest: buildGetMeasurementsByRangeRequest / buildGetEventsByRangeRequest\n");

    message::TimeRangeMessage message;
    message.requestId = 42;
    message.startTime = 1700000000u;
    message.endTime = 1700086400u;

    tlv::Frame measurementsFrame = decodeFrame(message::buildGetMeasurementsByRangeRequest(message));
    check(measurementsFrame.tag == TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST, "correct tag");
    check(measurementsFrame.value.size() == 9, "correct payload size (requestId(1)+startTime(4)+endTime(4))");
    check(measurementsFrame.value[0] == 42, "requestId matches");

    std::vector<uint8_t> expected;
    expected.push_back(42);
    appendUint32LE(expected, 1700000000u);
    appendUint32LE(expected, 1700086400u);
    check(measurementsFrame.value == expected, "payload bytes match exactly, byte-for-byte");

    tlv::Frame eventsFrame = decodeFrame(message::buildGetEventsByRangeRequest(message));
    check(eventsFrame.tag == TAG_GET_EVENTS_BY_RANGE_REQUEST, "correct tag (distinct from measurements request)");
    check(eventsFrame.value == expected, "events request has the identical payload layout");
}

static void testMeasurementChunkResponse()
{
    std::printf("\nTest: parseMeasurementChunkResponse\n");

    /* Build a chunk "as the CC would send it": header + 2 records. */
    std::vector<uint8_t> value;
    value.push_back(9);              /* requestId */
    value.push_back(0x34);           /* chunkSeq low byte */
    value.push_back(0x12);           /* chunkSeq high byte -> 0x1234 */
    value.push_back(1);              /* moreDataFlag = true */
    std::vector<uint8_t> record1 = buildMeasurementSampleValueBytes(1000, 20.0f, 50.0f, 300.0f, 3.7f, MODE_NORMAL);
    std::vector<uint8_t> record2 = buildMeasurementSampleValueBytes(2000, 21.0f, 51.0f, 310.0f, 3.6f, MODE_WARNING);
    value.insert(value.end(), record1.begin(), record1.end());
    value.insert(value.end(), record2.begin(), record2.end());

    tlv::Frame frame = decodeFrame(tlv::encodeFrame(TAG_MEASUREMENT_CHUNK_RESPONSE, value));
    std::optional<message::MeasurementChunkResponse> parsed = message::parseMeasurementChunkResponse(frame);

    check(parsed.has_value(), "parses successfully");
    if (parsed)
    {
        check(parsed->requestId == 9, "requestId matches");
        check(parsed->chunkSeq == 0x1234, "chunkSeq matches (little-endian)");
        check(parsed->moreDataFlag == true, "moreDataFlag matches");
        check(parsed->records.size() == 2, "correct record count");
        check(parsed->records[0].timestamp == 1000 && parsed->records[0].temperature == 20.0f
                  && parsed->records[0].mode == MODE_NORMAL,
              "record 0 correct");
        check(parsed->records[1].timestamp == 2000 && parsed->records[1].temperature == 21.0f
                  && parsed->records[1].mode == MODE_WARNING,
              "record 1 correct");
    }

    /* Wrong tag is rejected. */
    tlv::Frame wrongTagFrame = decodeFrame(tlv::encodeFrame(TAG_EVENT_CHUNK_RESPONSE, value));
    check(!message::parseMeasurementChunkResponse(wrongTagFrame).has_value(), "rejects a frame with the wrong tag");

    /* A payload length that isn't header + an exact multiple of 21 is rejected. */
    std::vector<uint8_t> badValue = value;
    badValue.push_back(0xFF); /* one stray extra byte */
    tlv::Frame badFrame = decodeFrame(tlv::encodeFrame(TAG_MEASUREMENT_CHUNK_RESPONSE, badValue));
    check(!message::parseMeasurementChunkResponse(badFrame).has_value(),
          "rejects a payload whose record area isn't a multiple of 21 bytes");

    /* Zero records (a valid, empty final chunk) parses correctly. */
    std::vector<uint8_t> emptyValue = {1, 0, 0, 0}; /* requestId=1, chunkSeq=0, moreDataFlag=false */
    tlv::Frame emptyFrame = decodeFrame(tlv::encodeFrame(TAG_MEASUREMENT_CHUNK_RESPONSE, emptyValue));
    std::optional<message::MeasurementChunkResponse> emptyParsed = message::parseMeasurementChunkResponse(emptyFrame);
    check(emptyParsed.has_value() && emptyParsed->records.empty(), "a zero-record (header-only) chunk parses correctly");

    /* The maximum allowed record count (MAX_MEASUREMENTS_PER_CHUNK = 11) parses correctly. */
    std::vector<uint8_t> maxValue = {5, 0, 0, 1};
    for (int i = 0; i < MAX_MEASUREMENTS_PER_CHUNK; i++)
    {
        std::vector<uint8_t> record = buildMeasurementSampleValueBytes(
            static_cast<uint32_t>(1000 + i), 20.0f, 50.0f, 300.0f, 3.7f, MODE_NORMAL);
        maxValue.insert(maxValue.end(), record.begin(), record.end());
    }
    tlv::Frame maxFrame = decodeFrame(tlv::encodeFrame(TAG_MEASUREMENT_CHUNK_RESPONSE, maxValue));
    std::optional<message::MeasurementChunkResponse> maxParsed = message::parseMeasurementChunkResponse(maxFrame);
    check(maxParsed.has_value() && maxParsed->records.size() == MAX_MEASUREMENTS_PER_CHUNK,
          "a full MAX_MEASUREMENTS_PER_CHUNK-record chunk parses correctly");
}

static void testEventChunkResponse()
{
    std::printf("\nTest: parseEventChunkResponse\n");

    std::vector<uint8_t> value;
    value.push_back(3);    /* requestId */
    value.push_back(0x05); /* chunkSeq low */
    value.push_back(0x00); /* chunkSeq high -> 5 */
    value.push_back(0);    /* moreDataFlag = false (last chunk) */
    std::vector<uint8_t> record1 = buildEventRecordValueBytes(111, TAG_EVENT_STARTUP, "Startup after watchdog reset");
    std::vector<uint8_t> record2 = buildEventRecordValueBytes(222, TAG_EVENT_OBJECT_DETECTION, "Object detected");
    value.insert(value.end(), record1.begin(), record1.end());
    value.insert(value.end(), record2.begin(), record2.end());

    tlv::Frame frame = decodeFrame(tlv::encodeFrame(TAG_EVENT_CHUNK_RESPONSE, value));
    std::optional<message::EventChunkResponse> parsed = message::parseEventChunkResponse(frame);

    check(parsed.has_value(), "parses successfully");
    if (parsed)
    {
        check(parsed->requestId == 3, "requestId matches");
        check(parsed->chunkSeq == 5, "chunkSeq matches");
        check(parsed->moreDataFlag == false, "moreDataFlag matches (last chunk)");
        check(parsed->records.size() == 2, "correct record count");
        check(parsed->records[0].timestamp == 111 && parsed->records[0].eventType == TAG_EVENT_STARTUP
                  && parsed->records[0].description == "Startup after watchdog reset",
              "record 0 correct, including description string");
        check(parsed->records[1].timestamp == 222 && parsed->records[1].eventType == TAG_EVENT_OBJECT_DETECTION
                  && parsed->records[1].description == "Object detected",
              "record 1 correct, including description string");
    }

    /* Wrong tag is rejected. */
    tlv::Frame wrongTagFrame = decodeFrame(tlv::encodeFrame(TAG_MEASUREMENT_CHUNK_RESPONSE, value));
    check(!message::parseEventChunkResponse(wrongTagFrame).has_value(), "rejects a frame with the wrong tag");

    /* A payload length that isn't header + an exact multiple of EVENT_RECORD_SIZE is rejected. */
    std::vector<uint8_t> badValue = value;
    badValue.push_back(0xFF);
    tlv::Frame badFrame = decodeFrame(tlv::encodeFrame(TAG_EVENT_CHUNK_RESPONSE, badValue));
    check(!message::parseEventChunkResponse(badFrame).has_value(),
          "rejects a payload whose record area isn't a multiple of EVENT_RECORD_SIZE bytes");

    /* A description that fills the field with no null byte at all (a
       corrupted/malicious frame) is handled safely - reading stops at the
       field's own bound, never past it. */
    std::vector<uint8_t> noNullValue = {1, 0, 0, 0};
    appendUint32LE(noNullValue, 999);
    noNullValue.push_back(TAG_EVENT_CONFIG_CHANGED);
    for (int i = 0; i < EVENT_DESCRIPTION_SIZE; i++)
    {
        noNullValue.push_back('X'); /* fills the entire field, no null terminator anywhere */
    }
    tlv::Frame noNullFrame = decodeFrame(tlv::encodeFrame(TAG_EVENT_CHUNK_RESPONSE, noNullValue));
    std::optional<message::EventChunkResponse> noNullParsed = message::parseEventChunkResponse(noNullFrame);
    check(noNullParsed.has_value() && noNullParsed->records[0].description.size() == EVENT_DESCRIPTION_SIZE,
          "a description field with no null terminator is bounded to exactly EVENT_DESCRIPTION_SIZE characters, not read past it");

    /* Zero records (a valid, empty final chunk) parses correctly. */
    std::vector<uint8_t> emptyValue = {7, 0, 0, 1};
    tlv::Frame emptyFrame = decodeFrame(tlv::encodeFrame(TAG_EVENT_CHUNK_RESPONSE, emptyValue));
    std::optional<message::EventChunkResponse> emptyParsed = message::parseEventChunkResponse(emptyFrame);
    check(emptyParsed.has_value() && emptyParsed->records.empty(), "a zero-record (header-only) chunk parses correctly");

    /* The maximum allowed record count (MAX_EVENTS_PER_CHUNK = 6) parses correctly. */
    std::vector<uint8_t> maxValue = {2, 0, 0, 1};
    for (int i = 0; i < MAX_EVENTS_PER_CHUNK; i++)
    {
        std::vector<uint8_t> record = buildEventRecordValueBytes(static_cast<uint32_t>(1 + i), TAG_EVENT_STARTUP, "e");
        maxValue.insert(maxValue.end(), record.begin(), record.end());
    }
    tlv::Frame maxFrame = decodeFrame(tlv::encodeFrame(TAG_EVENT_CHUNK_RESPONSE, maxValue));
    std::optional<message::EventChunkResponse> maxParsed = message::parseEventChunkResponse(maxFrame);
    check(maxParsed.has_value() && maxParsed->records.size() == MAX_EVENTS_PER_CHUNK,
          "a full MAX_EVENTS_PER_CHUNK-record chunk parses correctly");
}

static void testCrcValidityThroughRealDecoder()
{
    std::printf("\nTest: CRC/frame validity through the real tlv::Decoder\n");

    message::TimeRangeMessage message;
    message.requestId = 1;
    message.startTime = 100;
    message.endTime = 200;
    std::vector<uint8_t> goodFrame = message::buildGetMeasurementsByRangeRequest(message);

    /* A byte-for-byte correct frame decodes cleanly through the real decoder. */
    {
        tlv::Decoder decoder;
        tlv::Frame outFrame;
        tlv::DecodeStatus lastStatus = tlv::DecodeStatus::InProgress;
        for (uint8_t b : goodFrame)
        {
            lastStatus = decoder.feedByte(b, outFrame);
        }
        check(lastStatus == tlv::DecodeStatus::FrameReady, "a correctly-built frame reports FrameReady from the real decoder");
    }

    /* Corrupting one CRC byte must be caught as a real decode error, not
       silently accepted - proves this test suite exercises genuine CRC
       validation, not a shortcut. */
    {
        std::vector<uint8_t> corrupted = goodFrame;
        corrupted[corrupted.size() - 1] ^= 0xFF; /* flip the last CRC byte */

        tlv::Decoder decoder;
        tlv::Frame outFrame;
        tlv::DecodeStatus lastStatus = tlv::DecodeStatus::InProgress;
        for (uint8_t b : corrupted)
        {
            lastStatus = decoder.feedByte(b, outFrame);
        }
        check(lastStatus == tlv::DecodeStatus::Error, "a corrupted CRC byte is reported as Error by the real decoder");
    }

    /* The standard CRC-16/CCITT-FALSE test vector, same one used to
       cross-validate the LNC's independent C implementation. */
    uint16_t crc = tlv::calculateCrc16(reinterpret_cast<const uint8_t *>("123456789"), 9);
    check(crc == 0x29B1, "CRC-16/CCITT-FALSE standard test vector matches (0x29B1)");
}

int main()
{
    std::printf("=== GroundStation message.cpp test suite ===\n");

    testTimeRangeBuilders();
    testMeasurementChunkResponse();
    testEventChunkResponse();
    testCrcValidityThroughRealDecoder();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
