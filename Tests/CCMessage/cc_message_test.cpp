/*
 * cc_message_test.cpp
 *
 * Purpose:
 *   Stand-alone PC test program for CentralComputer/message.h/.cpp (the
 *   Central Computer's Message layer, both directions). For each of the
 *   7 parsers (LNC->CC), builds a frame with the exact wire layout the
 *   LNC's message.c produces, decodes it with tlv::Decoder, then confirms
 *   the parser extracts the right fields - and rejects a wrong tag or a
 *   wrong-length payload. For each of the 12 builders (CC->LNC), builds a
 *   frame and manually inspects the decoded bytes (no CC-side parser
 *   exists for these yet, since CC never receives them).
 *
 * How to build and run (from this folder):
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" cc_message_test.cpp "../../CentralComputer/src/message.cpp" "../../Common/TLVCodec/tlv_codec.cpp" -o cc_message_test.exe
 *   ./cc_message_test.exe
 *
 * This file does NOT:
 *   - Touch sockets, serial ports, or any transport.
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
   through a fresh tlv::Decoder, one byte at a time. */
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

static uint32_t readUint32LE(const uint8_t *bytes)
{
    return static_cast<uint32_t>(bytes[0])
         | (static_cast<uint32_t>(bytes[1]) << 8)
         | (static_cast<uint32_t>(bytes[2]) << 16)
         | (static_cast<uint32_t>(bytes[3]) << 24);
}

static float readFloatLE(const uint8_t *bytes)
{
    float result;
    std::memcpy(&result, bytes, sizeof(float));
    return result;
}

static void testMeasurementSampleMessages()
{
    std::printf("\nTest: parseKeepAlive / parseDataReport\n");

    std::vector<uint8_t> value;
    appendUint32LE(value, 1700000000u);
    appendFloatLE(value, 23.5f);
    appendFloatLE(value, 45.0f);
    appendFloatLE(value, 800.0f);
    appendFloatLE(value, 3.7f);
    value.push_back(1); /* MODE_WARNING */

    tlv::Frame keepAliveFrame = decodeFrame(tlv::encodeFrame(TAG_KEEPALIVE, value));
    auto parsedKeepAlive = message::parseKeepAlive(keepAliveFrame);
    check(parsedKeepAlive.has_value(), "parseKeepAlive succeeds on a correct KEEPALIVE frame");
    if (parsedKeepAlive)
    {
        check(parsedKeepAlive->timestamp == 1700000000u, "timestamp matches");
        check(parsedKeepAlive->temperature == 23.5f, "temperature matches");
        check(parsedKeepAlive->humidity == 45.0f, "humidity matches");
        check(parsedKeepAlive->light == 800.0f, "light matches");
        check(parsedKeepAlive->battery == 3.7f, "battery matches");
        check(parsedKeepAlive->mode == 1, "mode matches");
    }

    tlv::Frame dataReportFrame = decodeFrame(tlv::encodeFrame(TAG_DATA_REPORT, value));
    auto parsedDataReport = message::parseDataReport(dataReportFrame);
    check(parsedDataReport.has_value(), "parseDataReport succeeds on a correct DATA_REPORT frame");

    check(!message::parseKeepAlive(dataReportFrame).has_value(),
          "parseKeepAlive rejects a DATA_REPORT frame (wrong tag)");
    check(!message::parseDataReport(keepAliveFrame).has_value(),
          "parseDataReport rejects a KEEPALIVE frame (wrong tag)");
}

static void testTimestampMessages()
{
    std::printf("\nTest: parseSystemTimeResponse / parseEventConfigChanged\n");

    std::vector<uint8_t> value;
    appendUint32LE(value, 1650000000u);

    tlv::Frame timeFrame = decodeFrame(tlv::encodeFrame(TAG_SYSTEM_TIME_RESPONSE, value));
    auto parsedTime = message::parseSystemTimeResponse(timeFrame);
    check(parsedTime.has_value(), "parseSystemTimeResponse succeeds");
    if (parsedTime) check(parsedTime->timestamp == 1650000000u, "timestamp matches");

    tlv::Frame configFrame = decodeFrame(tlv::encodeFrame(TAG_EVENT_CONFIG_CHANGED, value));
    auto parsedConfig = message::parseEventConfigChanged(configFrame);
    check(parsedConfig.has_value(), "parseEventConfigChanged succeeds");

    check(!message::parseSystemTimeResponse(configFrame).has_value(),
          "parseSystemTimeResponse rejects an EVENT_CONFIG_CHANGED frame (wrong tag)");
}

static void testTimestampWithFlagMessages()
{
    std::printf("\nTest: parseEventObjectDetection / parseEventStartup\n");

    std::vector<uint8_t> value;
    appendUint32LE(value, 1690000000u);
    value.push_back(1);

    tlv::Frame detectionFrame = decodeFrame(tlv::encodeFrame(TAG_EVENT_OBJECT_DETECTION, value));
    auto parsedDetection = message::parseEventObjectDetection(detectionFrame);
    check(parsedDetection.has_value(), "parseEventObjectDetection succeeds");
    if (parsedDetection)
    {
        check(parsedDetection->timestamp == 1690000000u, "timestamp matches");
        check(parsedDetection->flag == 1, "detected flag matches");
    }

    tlv::Frame startupFrame = decodeFrame(tlv::encodeFrame(TAG_EVENT_STARTUP, value));
    auto parsedStartup = message::parseEventStartup(startupFrame);
    check(parsedStartup.has_value(), "parseEventStartup succeeds");

    check(!message::parseEventObjectDetection(startupFrame).has_value(),
          "parseEventObjectDetection rejects an EVENT_STARTUP frame (wrong tag)");
}

static void testModeTransitionMessage()
{
    std::printf("\nTest: parseEventModeTransition\n");

    std::vector<uint8_t> value;
    appendUint32LE(value, 1710000000u);
    value.push_back(0); /* MODE_NORMAL */
    value.push_back(1); /* MODE_WARNING */

    tlv::Frame frame = decodeFrame(tlv::encodeFrame(TAG_EVENT_MODE_TRANSITION, value));
    auto parsed = message::parseEventModeTransition(frame);
    check(parsed.has_value(), "parseEventModeTransition succeeds");
    if (parsed)
    {
        check(parsed->timestamp == 1710000000u, "timestamp matches");
        check(parsed->oldMode == 0, "oldMode matches");
        check(parsed->newMode == 1, "newMode matches");
    }
}

static void testSetRtcDateTimeAndGetSystemTimeRequest()
{
    std::printf("\nTest: buildSetRtcDateTime / buildGetSystemTimeRequest\n");

    message::TimestampMessage message;
    message.timestamp = 1650000000u;

    tlv::Frame frame = decodeFrame(message::buildSetRtcDateTime(message));
    check(frame.tag == TAG_SET_RTC_DATETIME, "correct tag");
    check(frame.value.size() == 4, "correct payload size");
    check(readUint32LE(frame.value.data()) == message.timestamp, "timestamp matches");

    std::vector<uint8_t> requestFrame = message::buildGetSystemTimeRequest();
    tlv::Frame decodedRequest = decodeFrame(requestFrame);
    check(decodedRequest.tag == TAG_GET_SYSTEM_TIME_REQUEST, "correct tag");
    check(decodedRequest.value.empty(), "payload is empty");
}

static void testTimeRangeBuilders()
{
    std::printf("\nTest: buildGetMeasurementsByRangeRequest / buildGetEventsByRangeRequest\n");

    message::TimeRangeMessage message;
    message.requestId = 7;
    message.startTime = 1600000000u;
    message.endTime = 1600086400u;

    tlv::Frame measurementsFrame = decodeFrame(message::buildGetMeasurementsByRangeRequest(message));
    check(measurementsFrame.tag == TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST, "correct tag");
    check(measurementsFrame.value[0] == message.requestId, "requestId matches");
    check(readUint32LE(&measurementsFrame.value[1]) == message.startTime, "startTime matches");
    check(readUint32LE(&measurementsFrame.value[5]) == message.endTime, "endTime matches");

    tlv::Frame eventsFrame = decodeFrame(message::buildGetEventsByRangeRequest(message));
    check(eventsFrame.tag == TAG_GET_EVENTS_BY_RANGE_REQUEST, "correct tag (distinct from measurements request)");
}

static void testParseGetRangeRequests()
{
    std::printf("\nTest: parseGetMeasurementsByRangeRequest / parseGetEventsByRangeRequest\n");

    /* Step 4b: CC never needed to parse these two before (it only built
       them, to ask the LNC for a backfill) - now the Ground Station sends
       them TO CC, so this is the "missing other half," same pattern as
       Step 1's buildMeasurementChunkResponse/buildEventChunkResponse. */
    message::TimeRangeMessage original;
    original.requestId = 42;
    original.startTime = 1700000000u;
    original.endTime = 1700086400u;

    /* Round trip through the real build+decode+parse path - same wire
       bytes a real Ground Station would send. */
    tlv::Frame measurementsFrame = decodeFrame(message::buildGetMeasurementsByRangeRequest(original));
    std::optional<message::TimeRangeMessage> parsedMeasurements =
        message::parseGetMeasurementsByRangeRequest(measurementsFrame);
    check(parsedMeasurements.has_value(), "parseGetMeasurementsByRangeRequest accepts a well-formed frame");
    check(parsedMeasurements->requestId == original.requestId, "requestId round-trips correctly (measurements)");
    check(parsedMeasurements->startTime == original.startTime, "startTime round-trips correctly (measurements)");
    check(parsedMeasurements->endTime == original.endTime, "endTime round-trips correctly (measurements)");

    tlv::Frame eventsFrame = decodeFrame(message::buildGetEventsByRangeRequest(original));
    std::optional<message::TimeRangeMessage> parsedEvents = message::parseGetEventsByRangeRequest(eventsFrame);
    check(parsedEvents.has_value(), "parseGetEventsByRangeRequest accepts a well-formed frame");
    check(parsedEvents->requestId == original.requestId, "requestId round-trips correctly (events)");
    check(parsedEvents->startTime == original.startTime, "startTime round-trips correctly (events)");
    check(parsedEvents->endTime == original.endTime, "endTime round-trips correctly (events)");

    /* Wrong tag: each parser must reject the other request's frame. */
    check(!message::parseGetMeasurementsByRangeRequest(eventsFrame).has_value(),
          "parseGetMeasurementsByRangeRequest rejects a GET_EVENTS_BY_RANGE_REQUEST frame (wrong tag)");
    check(!message::parseGetEventsByRangeRequest(measurementsFrame).has_value(),
          "parseGetEventsByRangeRequest rejects a GET_MEASUREMENTS_BY_RANGE_REQUEST frame (wrong tag)");

    /* Wrong length: valid tag, but a payload that isn't exactly 9 bytes
       (RequestId(1) + StartTime(4) + EndTime(4)). */
    std::vector<uint8_t> tooShortValue = {0x01, 0x02, 0x03};
    tlv::Frame tooShortMeasurements = decodeFrame(tlv::encodeFrame(TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST, tooShortValue));
    tlv::Frame tooShortEvents = decodeFrame(tlv::encodeFrame(TAG_GET_EVENTS_BY_RANGE_REQUEST, tooShortValue));
    check(!message::parseGetMeasurementsByRangeRequest(tooShortMeasurements).has_value(),
          "parseGetMeasurementsByRangeRequest rejects a too-short payload");
    check(!message::parseGetEventsByRangeRequest(tooShortEvents).has_value(),
          "parseGetEventsByRangeRequest rejects a too-short payload");
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

static void testMeasurementChunkResponse()
{
    std::printf("\nTest: parseMeasurementChunkResponse\n");

    /* Build a chunk "as the LNC would send it": header + 2 records. */
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
        check(parsed->records[0].timestamp == 1000 && parsed->records[0].mode == MODE_NORMAL, "record 0 correct");
        check(parsed->records[1].timestamp == 2000 && parsed->records[1].mode == MODE_WARNING, "record 1 correct");
    }

    /* A payload length that isn't header + an exact multiple of 21 is rejected. */
    std::vector<uint8_t> badValue = value;
    badValue.push_back(0xFF); /* one stray extra byte */
    tlv::Frame badFrame = decodeFrame(tlv::encodeFrame(TAG_MEASUREMENT_CHUNK_RESPONSE, badValue));
    check(!message::parseMeasurementChunkResponse(badFrame).has_value(),
          "rejects a payload whose record area isn't a multiple of 21 bytes");
}

static void testEventChunkResponse()
{
    std::printf("\nTest: parseEventChunkResponse\n");

    auto buildEventRecordBytes = [](uint32_t timestamp, uint8_t eventType, const std::string &description)
    {
        std::vector<uint8_t> bytes;
        appendUint32LE(bytes, timestamp);
        bytes.push_back(eventType);
        std::vector<uint8_t> descriptionField(EVENT_DESCRIPTION_SIZE, 0);
        size_t copyLength = std::min(description.size(), static_cast<size_t>(EVENT_DESCRIPTION_SIZE - 1));
        std::memcpy(descriptionField.data(), description.data(), copyLength);
        bytes.insert(bytes.end(), descriptionField.begin(), descriptionField.end());
        return bytes;
    };

    std::vector<uint8_t> value;
    value.push_back(3);    /* requestId */
    value.push_back(0x05); /* chunkSeq low */
    value.push_back(0x00); /* chunkSeq high -> 5 */
    value.push_back(0);    /* moreDataFlag = false (last chunk) */
    std::vector<uint8_t> record1 = buildEventRecordBytes(111, TAG_EVENT_STARTUP, "Startup after watchdog reset");
    std::vector<uint8_t> record2 = buildEventRecordBytes(222, TAG_EVENT_OBJECT_DETECTION, "Object detected");
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

    /* A description longer than the field truncates cleanly, still null-terminated. */
    std::string longDescription(EVENT_DESCRIPTION_SIZE + 20, 'X');
    std::vector<uint8_t> truncatedValue;
    truncatedValue.push_back(1);
    truncatedValue.push_back(0);
    truncatedValue.push_back(0);
    truncatedValue.push_back(0);
    std::vector<uint8_t> truncatedRecord = buildEventRecordBytes(1, TAG_EVENT_CONFIG_CHANGED, longDescription);
    truncatedValue.insert(truncatedValue.end(), truncatedRecord.begin(), truncatedRecord.end());
    tlv::Frame truncatedFrame = decodeFrame(tlv::encodeFrame(TAG_EVENT_CHUNK_RESPONSE, truncatedValue));
    std::optional<message::EventChunkResponse> truncatedParsed = message::parseEventChunkResponse(truncatedFrame);
    check(truncatedParsed.has_value() && truncatedParsed->records[0].description.size() == EVENT_DESCRIPTION_SIZE - 1,
          "an over-long description is truncated to EVENT_DESCRIPTION_SIZE-1 characters");
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

static void testBuildMeasurementChunkResponse()
{
    std::printf("\nTest: buildMeasurementChunkResponse\n");

    message::MeasurementChunkResponse response;
    response.requestId = 9;
    response.chunkSeq = 0x1234;
    response.moreDataFlag = true;
    message::MeasurementSample sample1;
    sample1.timestamp = 1000;
    sample1.temperature = 20.0f;
    sample1.humidity = 50.0f;
    sample1.light = 300.0f;
    sample1.battery = 3.7f;
    sample1.mode = MODE_NORMAL;
    message::MeasurementSample sample2;
    sample2.timestamp = 2000;
    sample2.temperature = 21.0f;
    sample2.humidity = 51.0f;
    sample2.light = 310.0f;
    sample2.battery = 3.6f;
    sample2.mode = MODE_WARNING;
    response.records = {sample1, sample2};

    std::vector<uint8_t> built = message::buildMeasurementChunkResponse(response);
    check(!built.empty(), "build succeeds (non-empty result)");

    tlv::Frame frame = decodeFrame(built);
    check(frame.tag == TAG_MEASUREMENT_CHUNK_RESPONSE, "correct tag");

    /* Exact byte-for-byte payload check against hand-built bytes - not just
       "it parses back", the actual wire bytes must match. */
    std::vector<uint8_t> expectedValue;
    expectedValue.push_back(9);
    expectedValue.push_back(0x34);
    expectedValue.push_back(0x12);
    expectedValue.push_back(1);
    std::vector<uint8_t> record1 = buildMeasurementSampleValueBytes(1000, 20.0f, 50.0f, 300.0f, 3.7f, MODE_NORMAL);
    std::vector<uint8_t> record2 = buildMeasurementSampleValueBytes(2000, 21.0f, 51.0f, 310.0f, 3.6f, MODE_WARNING);
    expectedValue.insert(expectedValue.end(), record1.begin(), record1.end());
    expectedValue.insert(expectedValue.end(), record2.begin(), record2.end());
    check(frame.value == expectedValue, "payload bytes match exactly, byte-for-byte");

    /* Round trip through the already-tested parser must reproduce the input. */
    std::optional<message::MeasurementChunkResponse> roundTrip = message::parseMeasurementChunkResponse(frame);
    check(roundTrip.has_value(), "round-trips through parseMeasurementChunkResponse");
    if (roundTrip)
    {
        check(roundTrip->requestId == response.requestId && roundTrip->chunkSeq == response.chunkSeq
                  && roundTrip->moreDataFlag == response.moreDataFlag && roundTrip->records.size() == 2,
              "round-tripped header matches original");
        check(roundTrip->records[0].timestamp == 1000 && roundTrip->records[0].mode == MODE_NORMAL
                  && roundTrip->records[1].timestamp == 2000 && roundTrip->records[1].mode == MODE_WARNING,
              "round-tripped records match original");
    }

    /* More records than MAX_MEASUREMENTS_PER_CHUNK is rejected (empty vector). */
    message::MeasurementChunkResponse tooMany;
    tooMany.records.assign(MAX_MEASUREMENTS_PER_CHUNK + 1, sample1);
    check(message::buildMeasurementChunkResponse(tooMany).empty(),
          "rejects more than MAX_MEASUREMENTS_PER_CHUNK records");

    /* Zero records (a valid, empty final chunk) still builds a correct 4-byte-payload frame. */
    message::MeasurementChunkResponse empty;
    empty.requestId = 1;
    empty.chunkSeq = 0;
    empty.moreDataFlag = false;
    std::vector<uint8_t> emptyBuilt = message::buildMeasurementChunkResponse(empty);
    tlv::Frame emptyFrame = decodeFrame(emptyBuilt);
    check(emptyFrame.value.size() == 4, "zero records still builds a valid 4-byte (header-only) payload");
    std::optional<message::MeasurementChunkResponse> emptyParsed = message::parseMeasurementChunkResponse(emptyFrame);
    check(emptyParsed.has_value() && emptyParsed->records.empty(), "zero-record chunk round-trips correctly");
}

static void testBuildEventChunkResponse()
{
    std::printf("\nTest: buildEventChunkResponse\n");

    message::EventChunkResponse response;
    response.requestId = 3;
    response.chunkSeq = 5;
    response.moreDataFlag = false;
    message::EventRecord record1;
    record1.timestamp = 111;
    record1.eventType = TAG_EVENT_STARTUP;
    record1.description = "Startup after watchdog reset";
    message::EventRecord record2;
    record2.timestamp = 222;
    record2.eventType = TAG_EVENT_OBJECT_DETECTION;
    record2.description = "Object detected";
    response.records = {record1, record2};

    std::vector<uint8_t> built = message::buildEventChunkResponse(response);
    check(!built.empty(), "build succeeds (non-empty result)");

    tlv::Frame frame = decodeFrame(built);
    check(frame.tag == TAG_EVENT_CHUNK_RESPONSE, "correct tag");

    std::vector<uint8_t> expectedValue;
    expectedValue.push_back(3);
    expectedValue.push_back(0x05);
    expectedValue.push_back(0x00);
    expectedValue.push_back(0);
    std::vector<uint8_t> bytes1 = buildEventRecordValueBytes(111, TAG_EVENT_STARTUP, "Startup after watchdog reset");
    std::vector<uint8_t> bytes2 = buildEventRecordValueBytes(222, TAG_EVENT_OBJECT_DETECTION, "Object detected");
    expectedValue.insert(expectedValue.end(), bytes1.begin(), bytes1.end());
    expectedValue.insert(expectedValue.end(), bytes2.begin(), bytes2.end());
    check(frame.value == expectedValue, "payload bytes match exactly, byte-for-byte");

    std::optional<message::EventChunkResponse> roundTrip = message::parseEventChunkResponse(frame);
    check(roundTrip.has_value(), "round-trips through parseEventChunkResponse");
    if (roundTrip)
    {
        check(roundTrip->requestId == 3 && roundTrip->chunkSeq == 5 && roundTrip->moreDataFlag == false
                  && roundTrip->records.size() == 2,
              "round-tripped header matches original");
        check(roundTrip->records[0].description == "Startup after watchdog reset"
                  && roundTrip->records[1].description == "Object detected",
              "round-tripped descriptions match original");
    }

    /* More records than MAX_EVENTS_PER_CHUNK is rejected (empty vector). */
    message::EventChunkResponse tooMany;
    tooMany.records.assign(MAX_EVENTS_PER_CHUNK + 1, record1);
    check(message::buildEventChunkResponse(tooMany).empty(), "rejects more than MAX_EVENTS_PER_CHUNK records");

    /* An over-long description is truncated on the BUILD side too - the
       wire bytes themselves must be truncated+null-padded, not just the
       already-tested parse-side behavior. */
    message::EventChunkResponse longDesc;
    longDesc.requestId = 1;
    message::EventRecord longRecord;
    longRecord.timestamp = 1;
    longRecord.eventType = TAG_EVENT_CONFIG_CHANGED;
    longRecord.description = std::string(EVENT_DESCRIPTION_SIZE + 20, 'X');
    longDesc.records = {longRecord};
    tlv::Frame longFrame = decodeFrame(message::buildEventChunkResponse(longDesc));
    std::optional<message::EventChunkResponse> longParsed = message::parseEventChunkResponse(longFrame);
    check(longParsed.has_value() && longParsed->records[0].description.size() == EVENT_DESCRIPTION_SIZE - 1,
          "an over-long description is truncated to EVENT_DESCRIPTION_SIZE-1 characters when building");

    /* Zero records still builds a correct 4-byte-payload frame. */
    message::EventChunkResponse empty;
    empty.requestId = 7;
    empty.moreDataFlag = true;
    tlv::Frame emptyFrame = decodeFrame(message::buildEventChunkResponse(empty));
    check(emptyFrame.value.size() == 4, "zero records still builds a valid 4-byte (header-only) payload");
    std::optional<message::EventChunkResponse> emptyParsed = message::parseEventChunkResponse(emptyFrame);
    check(emptyParsed.has_value() && emptyParsed->records.empty(), "zero-record chunk round-trips correctly");
}

static void testTemperatureRangeBuilders()
{
    std::printf("\nTest: buildSetTempNormalRange / buildSetTempWarningRange\n");

    message::TemperatureRangeMessage message;
    message.low = 10.0f;
    message.high = 30.0f;

    tlv::Frame normalFrame = decodeFrame(message::buildSetTempNormalRange(message));
    check(normalFrame.tag == TAG_SET_TEMP_NORMAL_RANGE, "correct tag");
    check(readFloatLE(&normalFrame.value[0]) == message.low, "low matches");
    check(readFloatLE(&normalFrame.value[4]) == message.high, "high matches");

    tlv::Frame warningFrame = decodeFrame(message::buildSetTempWarningRange(message));
    check(warningFrame.tag == TAG_SET_TEMP_WARNING_RANGE, "correct tag (distinct from normal range)");
}

static void testSingleLimitBuilders()
{
    std::printf("\nTest: the 6 single-limit set commands (humidity/light/battery x normal/warning)\n");

    message::SingleLimitMessage message;
    message.limitValue = 42.5f;

    tlv::Frame f;

    f = decodeFrame(message::buildSetHumidityNormalLower(message));
    check(f.tag == TAG_SET_HUMIDITY_NORMAL_LOWER, "SetHumidityNormalLower correct tag");
    check(readFloatLE(f.value.data()) == message.limitValue, "SetHumidityNormalLower value matches");

    f = decodeFrame(message::buildSetHumidityWarningLower(message));
    check(f.tag == TAG_SET_HUMIDITY_WARNING_LOWER, "SetHumidityWarningLower correct tag");

    f = decodeFrame(message::buildSetLightNormalLower(message));
    check(f.tag == TAG_SET_LIGHT_NORMAL_LOWER, "SetLightNormalLower correct tag");

    f = decodeFrame(message::buildSetLightWarningLower(message));
    check(f.tag == TAG_SET_LIGHT_WARNING_LOWER, "SetLightWarningLower correct tag");

    f = decodeFrame(message::buildSetBatteryNormalLower(message));
    check(f.tag == TAG_SET_BATTERY_NORMAL_LOWER, "SetBatteryNormalLower correct tag");

    f = decodeFrame(message::buildSetBatteryWarningLower(message));
    check(f.tag == TAG_SET_BATTERY_WARNING_LOWER, "SetBatteryWarningLower correct tag");
}

static void testWrongLengthIsRejected()
{
    std::printf("\nTest: a right tag but wrong-length payload is rejected\n");

    std::vector<uint8_t> tooShortValue = {0x01, 0x02, 0x03}; /* KEEPALIVE needs 21 bytes, not 3 */
    tlv::Frame badFrame = decodeFrame(tlv::encodeFrame(TAG_KEEPALIVE, tooShortValue));

    check(!message::parseKeepAlive(badFrame).has_value(),
          "parseKeepAlive rejects a KEEPALIVE frame with the wrong payload length");
}

int main()
{
    std::printf("=== CentralComputer message.cpp test suite ===\n");

    testMeasurementSampleMessages();
    testTimestampMessages();
    testTimestampWithFlagMessages();
    testModeTransitionMessage();
    testSetRtcDateTimeAndGetSystemTimeRequest();
    testTimeRangeBuilders();
    testParseGetRangeRequests();
    testMeasurementChunkResponse();
    testEventChunkResponse();
    testBuildMeasurementChunkResponse();
    testBuildEventChunkResponse();
    testTemperatureRangeBuilders();
    testSingleLimitBuilders();
    testWrongLengthIsRejected();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
