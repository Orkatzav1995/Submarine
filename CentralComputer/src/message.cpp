/*
 * message.cpp
 *
 * Purpose:
 *   Implements the parsers and builders declared in message.h.
 *
 * Layer:
 *   Message
 *
 * This file does NOT:
 *   - Know about sockets, serial ports, or any transport.
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

void appendFloatLE(std::vector<uint8_t> &out, float v)
{
    uint8_t bytes[sizeof(float)];
    std::memcpy(bytes, &v, sizeof(float));
    for (uint8_t b : bytes)
    {
        out.push_back(b);
    }
}

/* Shared by buildMeasurementChunkResponse - the build-side counterpart of
   deserializeMeasurementSample below, same 21-byte wire layout. */
void serializeMeasurementSample(std::vector<uint8_t> &out, const MeasurementSample &sample)
{
    appendUint32LE(out, sample.timestamp);
    appendFloatLE(out, sample.temperature);
    appendFloatLE(out, sample.humidity);
    appendFloatLE(out, sample.light);
    appendFloatLE(out, sample.battery);
    out.push_back(sample.mode);
}

/* Shared by parseKeepAlive and parseDataReport - both carry identical fields. */
MeasurementSample deserializeMeasurementSample(const std::vector<uint8_t> &value)
{
    MeasurementSample sample;
    sample.timestamp = readUint32LE(&value[0]);
    sample.temperature = readFloatLE(&value[4]);
    sample.humidity = readFloatLE(&value[8]);
    sample.light = readFloatLE(&value[12]);
    sample.battery = readFloatLE(&value[16]);
    sample.mode = value[20];
    return sample;
}

}  // namespace

std::optional<MeasurementSample> parseKeepAlive(const tlv::Frame &frame)
{
    if (frame.tag != TAG_KEEPALIVE || frame.value.size() != 21)
    {
        return std::nullopt;
    }
    return deserializeMeasurementSample(frame.value);
}

std::optional<MeasurementSample> parseDataReport(const tlv::Frame &frame)
{
    if (frame.tag != TAG_DATA_REPORT || frame.value.size() != 21)
    {
        return std::nullopt;
    }
    return deserializeMeasurementSample(frame.value);
}

std::optional<TimestampMessage> parseSystemTimeResponse(const tlv::Frame &frame)
{
    if (frame.tag != TAG_SYSTEM_TIME_RESPONSE || frame.value.size() != 4)
    {
        return std::nullopt;
    }
    TimestampMessage message;
    message.timestamp = readUint32LE(frame.value.data());
    return message;
}

std::optional<TimestampMessage> parseEventConfigChanged(const tlv::Frame &frame)
{
    if (frame.tag != TAG_EVENT_CONFIG_CHANGED || frame.value.size() != 4)
    {
        return std::nullopt;
    }
    TimestampMessage message;
    message.timestamp = readUint32LE(frame.value.data());
    return message;
}

std::optional<TimestampWithFlagMessage> parseEventObjectDetection(const tlv::Frame &frame)
{
    if (frame.tag != TAG_EVENT_OBJECT_DETECTION || frame.value.size() != 5)
    {
        return std::nullopt;
    }
    TimestampWithFlagMessage message;
    message.timestamp = readUint32LE(frame.value.data());
    message.flag = frame.value[4];
    return message;
}

std::optional<TimestampWithFlagMessage> parseEventStartup(const tlv::Frame &frame)
{
    if (frame.tag != TAG_EVENT_STARTUP || frame.value.size() != 5)
    {
        return std::nullopt;
    }
    TimestampWithFlagMessage message;
    message.timestamp = readUint32LE(frame.value.data());
    message.flag = frame.value[4];
    return message;
}

std::optional<ModeTransitionMessage> parseEventModeTransition(const tlv::Frame &frame)
{
    if (frame.tag != TAG_EVENT_MODE_TRANSITION || frame.value.size() != 6)
    {
        return std::nullopt;
    }
    ModeTransitionMessage message;
    message.timestamp = readUint32LE(frame.value.data());
    message.oldMode = frame.value[4];
    message.newMode = frame.value[5];
    return message;
}

std::vector<uint8_t> buildSetRtcDateTime(const TimestampMessage &message)
{
    std::vector<uint8_t> value;
    appendUint32LE(value, message.timestamp);
    return tlv::encodeFrame(TAG_SET_RTC_DATETIME, value);
}

std::vector<uint8_t> buildGetSystemTimeRequest()
{
    return tlv::encodeFrame(TAG_GET_SYSTEM_TIME_REQUEST, {});
}

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

namespace {

constexpr size_t kChunkHeaderSize = 4; /* RequestId(1) + ChunkSeq(2) + MoreDataFlag(1) */

}  // namespace

std::optional<MeasurementChunkResponse> parseMeasurementChunkResponse(const tlv::Frame &frame)
{
    if (frame.tag != TAG_MEASUREMENT_CHUNK_RESPONSE || frame.value.size() < kChunkHeaderSize)
    {
        return std::nullopt;
    }

    size_t recordsBytes = frame.value.size() - kChunkHeaderSize;
    if (recordsBytes % 21 != 0) /* 21 = MeasurementSample wire size */
    {
        return std::nullopt;
    }

    MeasurementChunkResponse response;
    response.requestId = frame.value[0];
    response.chunkSeq = static_cast<uint16_t>(frame.value[1]) | (static_cast<uint16_t>(frame.value[2]) << 8);
    response.moreDataFlag = (frame.value[3] != 0);

    size_t recordCount = recordsBytes / 21;
    response.records.reserve(recordCount);
    for (size_t i = 0; i < recordCount; i++)
    {
        std::vector<uint8_t> recordBytes(frame.value.begin() + kChunkHeaderSize + (i * 21),
                                          frame.value.begin() + kChunkHeaderSize + (i * 21) + 21);
        response.records.push_back(deserializeMeasurementSample(recordBytes));
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

namespace {

/* Build-side counterpart of parseEventChunkResponse's record-decoding loop:
   writes EVENT_RECORD_SIZE wire bytes (timestamp + eventType + a
   description field truncated at EVENT_DESCRIPTION_SIZE-1 characters and
   null-padded for the rest), matching the LNC's SerializeEventRecord()
   exactly - same truncate-then-pad approach, guaranteeing the field is
   always null-terminated within its own bounds. */
void serializeEventRecord(std::vector<uint8_t> &out, const EventRecord &record)
{
    appendUint32LE(out, record.timestamp);
    out.push_back(record.eventType);

    size_t descriptionLength = record.description.size();
    if (descriptionLength > EVENT_DESCRIPTION_SIZE - 1)
    {
        descriptionLength = EVENT_DESCRIPTION_SIZE - 1;
    }

    for (size_t i = 0; i < descriptionLength; i++)
    {
        out.push_back(static_cast<uint8_t>(record.description[i]));
    }
    for (size_t i = descriptionLength; i < EVENT_DESCRIPTION_SIZE; i++)
    {
        out.push_back(0);
    }
}

}  // namespace

std::vector<uint8_t> buildMeasurementChunkResponse(const MeasurementChunkResponse &response)
{
    if (response.records.size() > MAX_MEASUREMENTS_PER_CHUNK)
    {
        return {};  // empty vector signals failure - too many records for one chunk
    }

    std::vector<uint8_t> value;
    value.push_back(response.requestId);
    value.push_back(static_cast<uint8_t>(response.chunkSeq & 0xFF));
    value.push_back(static_cast<uint8_t>((response.chunkSeq >> 8) & 0xFF));
    value.push_back(response.moreDataFlag ? 1 : 0);

    for (const MeasurementSample &sample : response.records)
    {
        serializeMeasurementSample(value, sample);
    }

    return tlv::encodeFrame(TAG_MEASUREMENT_CHUNK_RESPONSE, value);
}

std::vector<uint8_t> buildEventChunkResponse(const EventChunkResponse &response)
{
    if (response.records.size() > MAX_EVENTS_PER_CHUNK)
    {
        return {};  // empty vector signals failure - too many records for one chunk
    }

    std::vector<uint8_t> value;
    value.push_back(response.requestId);
    value.push_back(static_cast<uint8_t>(response.chunkSeq & 0xFF));
    value.push_back(static_cast<uint8_t>((response.chunkSeq >> 8) & 0xFF));
    value.push_back(response.moreDataFlag ? 1 : 0);

    for (const EventRecord &record : response.records)
    {
        serializeEventRecord(value, record);
    }

    return tlv::encodeFrame(TAG_EVENT_CHUNK_RESPONSE, value);
}

std::vector<uint8_t> buildSetTempNormalRange(const TemperatureRangeMessage &message)
{
    std::vector<uint8_t> value;
    appendFloatLE(value, message.low);
    appendFloatLE(value, message.high);
    return tlv::encodeFrame(TAG_SET_TEMP_NORMAL_RANGE, value);
}

std::vector<uint8_t> buildSetTempWarningRange(const TemperatureRangeMessage &message)
{
    std::vector<uint8_t> value;
    appendFloatLE(value, message.low);
    appendFloatLE(value, message.high);
    return tlv::encodeFrame(TAG_SET_TEMP_WARNING_RANGE, value);
}

std::vector<uint8_t> buildSetHumidityNormalLower(const SingleLimitMessage &message)
{
    std::vector<uint8_t> value;
    appendFloatLE(value, message.limitValue);
    return tlv::encodeFrame(TAG_SET_HUMIDITY_NORMAL_LOWER, value);
}

std::vector<uint8_t> buildSetHumidityWarningLower(const SingleLimitMessage &message)
{
    std::vector<uint8_t> value;
    appendFloatLE(value, message.limitValue);
    return tlv::encodeFrame(TAG_SET_HUMIDITY_WARNING_LOWER, value);
}

std::vector<uint8_t> buildSetLightNormalLower(const SingleLimitMessage &message)
{
    std::vector<uint8_t> value;
    appendFloatLE(value, message.limitValue);
    return tlv::encodeFrame(TAG_SET_LIGHT_NORMAL_LOWER, value);
}

std::vector<uint8_t> buildSetLightWarningLower(const SingleLimitMessage &message)
{
    std::vector<uint8_t> value;
    appendFloatLE(value, message.limitValue);
    return tlv::encodeFrame(TAG_SET_LIGHT_WARNING_LOWER, value);
}

std::vector<uint8_t> buildSetBatteryNormalLower(const SingleLimitMessage &message)
{
    std::vector<uint8_t> value;
    appendFloatLE(value, message.limitValue);
    return tlv::encodeFrame(TAG_SET_BATTERY_NORMAL_LOWER, value);
}

std::vector<uint8_t> buildSetBatteryWarningLower(const SingleLimitMessage &message)
{
    std::vector<uint8_t> value;
    appendFloatLE(value, message.limitValue);
    return tlv::encodeFrame(TAG_SET_BATTERY_WARNING_LOWER, value);
}

}  // namespace message
