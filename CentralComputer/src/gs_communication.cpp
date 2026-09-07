/*
 * gs_communication.cpp
 *
 * Implements the class declared in gs_communication.h.
 *
 * Layer:
 *   GsCommunication (Transport -> Protocol -> Message -> GsCommunication -> Application)
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, or CRC internals (that's tlv_codec.h).
 *   - Write to the database - read-only (getMeasurementsInRange/
 *     getEventsInRange), same relationship report_generator already has.
 */

#include "gs_communication.h"
#include "tlv_common.h"
#include <algorithm>

namespace gs_communication {

GsCommunication::GsCommunication(data_store::DataStore &dataStore)
    : dataStore_(dataStore)
{
}

bool GsCommunication::startListening(uint16_t port)
{
    return serverTransport_.startListening(port);
}

void GsCommunication::close()
{
    serverTransport_.close();
}

bool GsCommunication::isListening() const
{
    return serverTransport_.isListening();
}

bool GsCommunication::isClientConnected() const
{
    return serverTransport_.isClientConnected();
}

void GsCommunication::poll()
{
    serverTransport_.tryAcceptClient();

    if (!serverTransport_.isClientConnected())
    {
        return;
    }

    uint8_t receivedByte;
    if (serverTransport_.receiveByte(receivedByte, 20))
    {
        feedByte(receivedByte);
    }
}

void GsCommunication::feedByte(uint8_t byte)
{
    tlv::Frame frame;
    tlv::DecodeStatus status = decoder_.feedByte(byte, frame);

    if (status == tlv::DecodeStatus::FrameReady)
    {
        dispatch(frame);
    }
    else if (status == tlv::DecodeStatus::Error)
    {
        decodeErrors++;
    }
}

void GsCommunication::dispatch(const tlv::Frame &frame)
{
    switch (frame.tag)
    {
        case TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST:
            if (auto parsed = message::parseGetMeasurementsByRangeRequest(frame))
            {
                framesDispatched++;
                sendMeasurementsForRange(*parsed);
            }
            break;

        case TAG_GET_EVENTS_BY_RANGE_REQUEST:
            if (auto parsed = message::parseGetEventsByRangeRequest(frame))
            {
                framesDispatched++;
                sendEventsForRange(*parsed);
            }
            break;

        default:
            /* Genuinely unrecognized tag - dropped silently, same policy
               as the frozen protocol design's handling of unknown tags
               and communication::Communication::dispatch()'s default case. */
            break;
    }
}

void GsCommunication::sendMeasurementsForRange(const message::TimeRangeMessage &request)
{
    std::vector<message::MeasurementSample> records =
        dataStore_.getMeasurementsInRange(request.startTime, request.endTime);

    size_t totalRecords = records.size();
    size_t offset = 0;
    uint16_t chunkSeq = 0;

    /* A do/while, not a for-loop over chunks: a zero-record result must
       still send exactly one (empty) chunk, so GS learns the request
       completed with no matches rather than waiting forever. */
    do
    {
        size_t chunkCount = std::min<size_t>(MAX_MEASUREMENTS_PER_CHUNK, totalRecords - offset);

        message::MeasurementChunkResponse response;
        response.requestId = request.requestId;
        response.chunkSeq = chunkSeq;
        response.records.assign(records.begin() + static_cast<long>(offset),
                                 records.begin() + static_cast<long>(offset + chunkCount));

        offset += chunkCount;
        response.moreDataFlag = (offset < totalRecords);

        std::vector<uint8_t> frameBytes = message::buildMeasurementChunkResponse(response);
        serverTransport_.send(frameBytes, 1000);

        chunkSeq++;
    } while (offset < totalRecords);
}

void GsCommunication::sendEventsForRange(const message::TimeRangeMessage &request)
{
    std::vector<message::EventRecord> records = dataStore_.getEventsInRange(request.startTime, request.endTime);

    size_t totalRecords = records.size();
    size_t offset = 0;
    uint16_t chunkSeq = 0;

    do
    {
        size_t chunkCount = std::min<size_t>(MAX_EVENTS_PER_CHUNK, totalRecords - offset);

        message::EventChunkResponse response;
        response.requestId = request.requestId;
        response.chunkSeq = chunkSeq;
        response.records.assign(records.begin() + static_cast<long>(offset),
                                 records.begin() + static_cast<long>(offset + chunkCount));

        offset += chunkCount;
        response.moreDataFlag = (offset < totalRecords);

        std::vector<uint8_t> frameBytes = message::buildEventChunkResponse(response);
        serverTransport_.send(frameBytes, 1000);

        chunkSeq++;
    } while (offset < totalRecords);
}

}  // namespace gs_communication
