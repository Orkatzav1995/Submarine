/*
 * cc_communication.cpp
 *
 * Implements the class declared in cc_communication.h.
 *
 * Layer:
 *   CcCommunication (Transport -> Protocol -> Message -> CcCommunication -> Application)
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, or CRC internals (that's tlv_codec.h).
 *   - Persist anything - GS has no database.
 */

#include "cc_communication.h"
#include "tlv_common.h"

namespace cc_communication {

bool CcCommunication::connect(const std::string &host, uint16_t port)
{
    return socket_.connect(host, port);
}

void CcCommunication::close()
{
    socket_.close();
}

bool CcCommunication::isConnected() const
{
    return socket_.isConnected();
}

void CcCommunication::poll()
{
    uint8_t receivedByte;
    if (socket_.receiveByte(receivedByte, 20))
    {
        feedByte(receivedByte);
    }
}

void CcCommunication::feedByte(uint8_t byte)
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

void CcCommunication::dispatch(const tlv::Frame &frame)
{
    switch (frame.tag)
    {
        case TAG_MEASUREMENT_CHUNK_RESPONSE:
            if (auto parsed = message::parseMeasurementChunkResponse(frame))
            {
                framesDispatched++;
                handleMeasurementChunkResponse(*parsed);
            }
            break;

        case TAG_EVENT_CHUNK_RESPONSE:
            if (auto parsed = message::parseEventChunkResponse(frame))
            {
                framesDispatched++;
                handleEventChunkResponse(*parsed);
            }
            break;

        default:
            /* Genuinely unrecognized tag - dropped silently, same policy
               as the frozen protocol design's handling of unknown tags
               and Communication/GsCommunication's own default case. GS
               never receives anything else from CC. */
            break;
    }
}

bool CcCommunication::requestMeasurements(uint32_t startTime, uint32_t endTime)
{
    if (pendingMeasurementRequestId_.has_value())
    {
        return false;
    }

    uint8_t requestId = nextRequestId_++;

    message::TimeRangeMessage request;
    request.requestId = requestId;
    request.startTime = startTime;
    request.endTime = endTime;

    /* socket_.send()'s own true/false result is not checked here: there is
       no retry/timeout mechanism in this design, so a low-level send
       failure and "sent fine but CC never replies" are already
       indistinguishable, equally unrecoverable failure modes - same
       reasoning DataCollection's own backfill requests already document. */
    socket_.send(message::buildGetMeasurementsByRangeRequest(request), 1000);
    pendingMeasurementRequestId_ = requestId;
    accumulatedMeasurements_.clear();
    return true;
}

bool CcCommunication::requestEvents(uint32_t startTime, uint32_t endTime)
{
    if (pendingEventRequestId_.has_value())
    {
        return false;
    }

    uint8_t requestId = nextRequestId_++;

    message::TimeRangeMessage request;
    request.requestId = requestId;
    request.startTime = startTime;
    request.endTime = endTime;

    socket_.send(message::buildGetEventsByRangeRequest(request), 1000);
    pendingEventRequestId_ = requestId;
    accumulatedEvents_.clear();
    return true;
}

bool CcCommunication::isMeasurementRequestInProgress() const
{
    return pendingMeasurementRequestId_.has_value();
}

bool CcCommunication::isEventRequestInProgress() const
{
    return pendingEventRequestId_.has_value();
}

void CcCommunication::handleMeasurementChunkResponse(const message::MeasurementChunkResponse &response)
{
    if (!pendingMeasurementRequestId_.has_value() || response.requestId != *pendingMeasurementRequestId_)
    {
        return; /* stale or unrelated - not the request we're waiting for */
    }

    accumulatedMeasurements_.insert(accumulatedMeasurements_.end(), response.records.begin(),
                                     response.records.end());

    if (!response.moreDataFlag)
    {
        pendingMeasurementRequestId_.reset();
        if (onMeasurementsReceived)
        {
            onMeasurementsReceived(accumulatedMeasurements_);
        }
        accumulatedMeasurements_.clear();
    }
}

void CcCommunication::handleEventChunkResponse(const message::EventChunkResponse &response)
{
    if (!pendingEventRequestId_.has_value() || response.requestId != *pendingEventRequestId_)
    {
        return;
    }

    accumulatedEvents_.insert(accumulatedEvents_.end(), response.records.begin(), response.records.end());

    if (!response.moreDataFlag)
    {
        pendingEventRequestId_.reset();
        if (onEventsReceived)
        {
            onEventsReceived(accumulatedEvents_);
        }
        accumulatedEvents_.clear();
    }
}

}  // namespace cc_communication
