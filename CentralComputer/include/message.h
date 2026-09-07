/*
 * message.h
 *
 * Purpose:
 *   The Central Computer's Message layer, covering both directions:
 *     - Parses decoded TLV frames (tlv::Frame) coming FROM the LNC into
 *       typed structs (the 7 tags that flow LNC -> CC).
 *     - Builds outgoing TLV frames TO the LNC from typed structs (the 12
 *       "set limit" commands and "get" requests that flow CC -> LNC).
 *   Struct names/shapes match the LNC's own message.h where the same
 *   fields appear on both sides, for readability across the two codebases.
 *
 *   TAG_MEASUREMENT_CHUNK_RESPONSE / TAG_EVENT_CHUNK_RESPONSE: the CC only
 *   ever needed to parse these (replies arriving from the LNC) until now.
 *   The build side (buildMeasurementChunkResponse/buildEventChunkResponse
 *   below) exists for the Ground Station work - the CC answers GS's own
 *   range requests using data it already has locally, so it needs to
 *   build these same two frame types itself. See PROJECT_GUIDE.md.
 *
 * Layer:
 *   Message (Transport -> Protocol -> Message -> Application)
 *
 * Responsibilities:
 *   - Define typed structs for each application message.
 *   - Parse a tlv::Frame into one of those structs, rejecting a frame
 *     with the wrong tag or an unexpected payload length.
 *   - Build a complete, ready-to-send frame from one of those structs.
 *
 * This file does NOT:
 *   - Know about sockets, serial ports, or any transport.
 */

#ifndef CENTRAL_COMPUTER_MESSAGE_H
#define CENTRAL_COMPUTER_MESSAGE_H

#include "tlv_codec.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace message {

/* ============================================================
 * Data / Keep-alive - shared by TAG_KEEPALIVE and TAG_DATA_REPORT
 * ============================================================ */

struct MeasurementSample
{
    uint32_t timestamp = 0; /* Unix epoch UTC */
    float temperature = 0.0f;
    float humidity = 0.0f;
    float light = 0.0f;
    float battery = 0.0f;
    uint8_t mode = 0; /* a SystemMode value from tlv_common.h */
};

std::optional<MeasurementSample> parseKeepAlive(const tlv::Frame &frame);
std::optional<MeasurementSample> parseDataReport(const tlv::Frame &frame);

/* ============================================================
 * Single-timestamp messages
 * ============================================================ */

struct TimestampMessage
{
    uint32_t timestamp = 0;
};

std::optional<TimestampMessage> parseSystemTimeResponse(const tlv::Frame &frame);
std::optional<TimestampMessage> parseEventConfigChanged(const tlv::Frame &frame);

/* Same struct, CC -> LNC direction: the new time the LNC should set its clock to. */
std::vector<uint8_t> buildSetRtcDateTime(const TimestampMessage &message);

/* A request with no payload at all. */
std::vector<uint8_t> buildGetSystemTimeRequest();

/* ============================================================
 * Timestamp + one flag byte
 * ============================================================ */

struct TimestampWithFlagMessage
{
    uint32_t timestamp = 0;
    uint8_t flag = 0; /* meaning depends on which parse function produced it - see each one below */
};

/* flag: 1 = object detected, 0 = cleared */
std::optional<TimestampWithFlagMessage> parseEventObjectDetection(const tlv::Frame &frame);

/* flag: 1 = startup followed a watchdog reset, 0 = did not */
std::optional<TimestampWithFlagMessage> parseEventStartup(const tlv::Frame &frame);

/* ============================================================
 * Mode transition event
 * ============================================================ */

struct ModeTransitionMessage
{
    uint32_t timestamp = 0;
    uint8_t oldMode = 0; /* a SystemMode value from tlv_common.h */
    uint8_t newMode = 0; /* a SystemMode value from tlv_common.h */
};

std::optional<ModeTransitionMessage> parseEventModeTransition(const tlv::Frame &frame);

/* ============================================================
 * CC -> LNC: date/time ranges, the two "retrieve by range" requests
 * ============================================================ */

struct TimeRangeMessage
{
    uint8_t requestId = 0;  /* chosen by the CC; echoed back in every chunk of the response */
    uint32_t startTime = 0; /* Unix epoch UTC */
    uint32_t endTime = 0;   /* Unix epoch UTC */
};

std::vector<uint8_t> buildGetMeasurementsByRangeRequest(const TimeRangeMessage &message);
std::vector<uint8_t> buildGetEventsByRangeRequest(const TimeRangeMessage &message);

/*
 * GS -> CC direction (new): CC never needed to *parse* these two requests
 * before - it only ever built them itself, to ask the LNC for a backfill.
 * Now the Ground Station sends them *to* CC, so CC needs the missing other
 * half. Same struct, same wire layout, same tags - both reject a frame
 * with the wrong tag or a payload that isn't exactly 9 bytes
 * (RequestId(1) + StartTime(4) + EndTime(4)).
 */
std::optional<TimeRangeMessage> parseGetMeasurementsByRangeRequest(const tlv::Frame &frame);
std::optional<TimeRangeMessage> parseGetEventsByRangeRequest(const tlv::Frame &frame);

/* ============================================================
 * Chunked bulk-transfer responses (LNC -> CC)
 * See Common/Protocol/tlv_common.h for the sizing rationale.
 * ============================================================ */

/*
 * One stored event, as retrieved in reply to TAG_GET_EVENTS_BY_RANGE_REQUEST.
 * "eventType" is one of the TAG_EVENT_* values from tlv_common.h - reusing
 * the same vocabulary used for live event reporting, instead of a second
 * enum. Unlike the LNC's fixed-size wire field, "description" is a normal
 * std::string here (trimmed at the first null byte) - nicer for C++ callers,
 * with no reason to expose the fixed-size wire representation past parsing.
 */
struct EventRecord
{
    uint32_t timestamp = 0;
    uint8_t eventType = 0;
    std::string description;
};

struct MeasurementChunkResponse
{
    uint8_t requestId = 0;
    uint16_t chunkSeq = 0;
    bool moreDataFlag = false;
    std::vector<MeasurementSample> records;
};

struct EventChunkResponse
{
    uint8_t requestId = 0;
    uint16_t chunkSeq = 0;
    bool moreDataFlag = false;
    std::vector<EventRecord> records;
};

/*
 * Both reject a frame with the wrong tag, a payload too short to even hold
 * the 4-byte chunk header, or a payload whose length (after the header)
 * isn't an exact multiple of one record's size.
 */
std::optional<MeasurementChunkResponse> parseMeasurementChunkResponse(const tlv::Frame &frame);
std::optional<EventChunkResponse> parseEventChunkResponse(const tlv::Frame &frame);

/*
 * CC -> GS direction (new): the CC already has all the requested data
 * locally (its own DataStore), so it builds these same two frame types
 * itself when answering a Ground Station range request. Both return an
 * empty vector if "response.records" holds more than the max allowed per
 * chunk (MAX_MEASUREMENTS_PER_CHUNK / MAX_EVENTS_PER_CHUNK, tlv_common.h) -
 * same failure convention as tlv::encodeFrame's other failure cases.
 * Wire format is identical to what parseMeasurementChunkResponse/
 * parseEventChunkResponse already expect - this is the LNC's existing
 * Message_BuildMeasurementChunkResponse/Message_BuildEventChunkResponse
 * layout, ported to this struct-based C++ API.
 */
std::vector<uint8_t> buildMeasurementChunkResponse(const MeasurementChunkResponse &response);
std::vector<uint8_t> buildEventChunkResponse(const EventChunkResponse &response);

/* ============================================================
 * CC -> LNC: configuration "set limit" commands
 * ============================================================ */

/*
 * Temperature is the only sensor with both a low AND a high bound per mode
 * (Sec 2.5) - the other three sensors only get a single lower bound.
 */
struct TemperatureRangeMessage
{
    float low = 0.0f;
    float high = 0.0f;
};

std::vector<uint8_t> buildSetTempNormalRange(const TemperatureRangeMessage &message);
std::vector<uint8_t> buildSetTempWarningRange(const TemperatureRangeMessage &message);

/*
 * A single limit value. Shared by the 6 humidity/light/battery "set lower
 * bound" commands - each sensor gets one Normal and one Warning bound.
 */
struct SingleLimitMessage
{
    float limitValue = 0.0f;
};

std::vector<uint8_t> buildSetHumidityNormalLower(const SingleLimitMessage &message);
std::vector<uint8_t> buildSetHumidityWarningLower(const SingleLimitMessage &message);
std::vector<uint8_t> buildSetLightNormalLower(const SingleLimitMessage &message);
std::vector<uint8_t> buildSetLightWarningLower(const SingleLimitMessage &message);
std::vector<uint8_t> buildSetBatteryNormalLower(const SingleLimitMessage &message);
std::vector<uint8_t> buildSetBatteryWarningLower(const SingleLimitMessage &message);

}  // namespace message

#endif  // CENTRAL_COMPUTER_MESSAGE_H
