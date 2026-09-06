/*
 * message.h
 *
 * Purpose:
 *   The Ground Station's own, independent Message layer (Sec 4 of the
 *   spec): GS only ever needs to do two things over the wire - ask the CC
 *   for measurement or event data in a date/time range, and make sense of
 *   the CC's chunked replies. This file covers exactly those two
 *   directions, nothing else.
 *
 *   This is a deliberately separate implementation from the CC's own
 *   CentralComputer/include/message.h - the same project precedent as the
 *   LNC (C) and CC (C++) never sharing a single message.h despite talking
 *   to each other constantly. The two sides share only the tag vocabulary
 *   (Common/Protocol/tlv_common.h) and the raw codec
 *   (Common/TLVCodec/tlv_codec.h/.cpp, already shared between CC and GS
 *   since both are C++) - never a single Application-facing struct
 *   definition. Struct names/shapes match the CC's own message.h where
 *   the same fields appear on both sides, for readability across the two
 *   codebases - same reasoning the CC/LNC side already documents.
 *
 * Layer:
 *   Message (Transport -> Protocol -> Message -> Application)
 *
 * Responsibilities:
 *   - Build the 2 "retrieve by range" requests GS sends to the CC
 *     (buildGetMeasurementsByRangeRequest / buildGetEventsByRangeRequest).
 *   - Parse the CC's 2 chunked response types
 *     (parseMeasurementChunkResponse / parseEventChunkResponse).
 *
 * This file does NOT:
 *   - Know about sockets or any transport.
 *   - Cover any of the LNC<->CC-only message types (set-limit commands,
 *     keepalive, events, system time, etc.) - GS never sends or receives
 *     those; Sec 4 gives GS exactly one job.
 */

#ifndef GROUND_STATION_MESSAGE_H
#define GROUND_STATION_MESSAGE_H

#include "tlv_codec.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace message {

/* ============================================================
 * GS -> CC: date/time ranges, the two "retrieve by range" requests
 * ============================================================ */

struct TimeRangeMessage
{
    uint8_t requestId = 0;  /* chosen by GS; echoed back in every chunk of the response */
    uint32_t startTime = 0; /* Unix epoch UTC */
    uint32_t endTime = 0;   /* Unix epoch UTC */
};

std::vector<uint8_t> buildGetMeasurementsByRangeRequest(const TimeRangeMessage &message);
std::vector<uint8_t> buildGetEventsByRangeRequest(const TimeRangeMessage &message);

/* ============================================================
 * CC -> GS: chunked bulk-transfer responses
 * See Common/Protocol/tlv_common.h for the sizing rationale
 * (MAX_MEASUREMENTS_PER_CHUNK, MAX_EVENTS_PER_CHUNK, EVENT_DESCRIPTION_SIZE).
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

/*
 * One stored event. "eventType" is one of the TAG_EVENT_* values from
 * tlv_common.h - reusing the same vocabulary used for live event
 * reporting, instead of a second enum. "description" is a normal
 * std::string (trimmed at the first null byte found within the wire
 * field's own bounds) - nicer for a C++ caller than the fixed-size wire
 * representation.
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

}  // namespace message

#endif  // GROUND_STATION_MESSAGE_H
