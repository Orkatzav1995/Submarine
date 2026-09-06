/*
 * message.cpp
 *
 * Implements the parsers and builders declared in message.h.
 *
 * Layer:
 *   Message
 *
 * This file does NOT:
 *   - Know about sockets or any transport.
 */

#include "message.h"
#include "tlv_common.h"
#include <cstring>

namespace message {

namespace {

uint32_t readUint32LE(const uint8_t *bytes)
{
    return static_cast<uint32_t>(bytes[0])
         | (static_cast<uint32_t>(bytes[1]) << 8)
         | (static_cast<uint32_t>(bytes[2]) << 16)
         | (static_cast<uint32_t>(bytes[3]) << 24);
}

float readFloatLE(const uint8_t *bytes)
{
    float result;
    std::memcpy(&result, bytes, sizeof(float));
    return result;
}

void appendUint32LE(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

/* Reverse of the CC/LNC's own serializeMeasurementSample - same 21-byte
   wire layout (timestamp(4) + temperature(4) + humidity(4) + light(4) +
   battery(4) + mode(1)), independently re-derived here since GS never
   includes the CC's own message.h. */
MeasurementSample deserializeMeasurementSample(const uint8_t *bytes)
{
    MeasurementSample sample;
    sample.timestamp = readUint32LE(&bytes[0]);
    sample.temperature = readFloatLE(&bytes[4]);
    sample.humidity = readFloatLE(&bytes[8]);
    sample.light = readFloatLE(&bytes[12]);
    sample.battery = readFloatLE(&bytes[16]);
    sample.mode = bytes[20];
    return sample;
}

constexpr size_t kChunkHeaderSize = 4; /* RequestId(1) + ChunkSeq(2) + MoreDataFlag(1) */
constexpr size_t kMeasurementSampleSize = 21;

}  // namespace

std::vector<uint8_t> buildGetMeasurementsByRangeRequest(const TimeRangeMessage &message)
{
    std::vector<uint8_t> value;
    value.push_back(message.requestId);
    appendUint32LE(value, message.startTime);
    appendUint32LE(value, message.endTime);
    return tlv::encodeFrame(TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST, value);
}

std::vector<uint8_t> buildGetEventsByRangeRequest(const TimeRangeMessage &message)
{
    std::vector<uint8_t> value;
    value.push_back(message.requestId);
    appendUint32LE(value, message.startTime);
    appendUint32LE(value, message.endTime);
    return tlv::encodeFrame(TAG_GET_EVENTS_BY_RANGE_REQUEST, value);
}

std::optional<MeasurementChunkResponse> parseMeasurementChunkResponse(const tlv::Frame &frame)
{
    if (frame.tag != TAG_MEASUREMENT_CHUNK_RESPONSE || frame.value.size() < kChunkHeaderSize)
    {
        return std::nullopt;
    }

    size_t recordsBytes = frame.value.size() - kChunkHeaderSize;
    if (recordsBytes % kMeasurementSampleSize != 0)
    {
        return std::nullopt;
    }

    MeasurementChunkResponse response;
    response.requestId = frame.value[0];
    response.chunkSeq = static_cast<uint16_t>(frame.value[1]) | (static_cast<uint16_t>(frame.value[2]) << 8);
    response.moreDataFlag = (frame.value[3] != 0);

    size_t recordCount = recordsBytes / kMeasurementSampleSize;
    response.records.reserve(recordCount);
    for (size_t i = 0; i < recordCount; i++)
    {
        const uint8_t *record = &frame.value[kChunkHeaderSize + (i * kMeasurementSampleSize)];
        response.records.push_back(deserializeMeasurementSample(record));
    }

    return response;
}

std::optional<EventChunkResponse> parseEventChunkResponse(const tlv::Frame &frame)
{
    constexpr size_t kEventRecordSize = EVENT_RECORD_SIZE; /* timestamp(4) + eventType(1) + description(EVENT_DESCRIPTION_SIZE) */

    if (frame.tag != TAG_EVENT_CHUNK_RESPONSE || frame.value.size() < kChunkHeaderSize)
    {
        return std::nullopt;
    }

    size_t recordsBytes = frame.value.size() - kChunkHeaderSize;
    if (recordsBytes % kEventRecordSize != 0)
    {
        return std::nullopt;
    }

    EventChunkResponse response;
    response.requestId = frame.value[0];
    response.chunkSeq = static_cast<uint16_t>(frame.value[1]) | (static_cast<uint16_t>(frame.value[2]) << 8);
    response.moreDataFlag = (frame.value[3] != 0);

    size_t recordCount = recordsBytes / kEventRecordSize;
    response.records.reserve(recordCount);
    for (size_t i = 0; i < recordCount; i++)
    {
        const uint8_t *record = &frame.value[kChunkHeaderSize + (i * kEventRecordSize)];

        EventRecord eventRecord;
        eventRecord.timestamp = readUint32LE(record);
        eventRecord.eventType = record[4];

        /* description is a fixed EVENT_DESCRIPTION_SIZE-byte field that
           SHOULD be null-terminated within it (see the field's contract in
           tlv_common.h), but this is untrusted wire data - a well-formed
           sender guarantees a null byte inside the field, a corrupted or
           malicious one might not, so the search is explicitly bounded to
           the field's own size rather than trusting a null shows up. */
        const uint8_t *descriptionBytes = &record[5];
        size_t descriptionLength = 0;
        while (descriptionLength < EVENT_DESCRIPTION_SIZE && descriptionBytes[descriptionLength] != 0)
        {
            descriptionLength++;
        }
        eventRecord.description.assign(reinterpret_cast<const char *>(descriptionBytes), descriptionLength);

        response.records.push_back(std::move(eventRecord));
    }

    return response;
}

}  // namespace message
