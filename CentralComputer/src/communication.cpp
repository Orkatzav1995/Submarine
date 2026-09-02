/*
 * communication.cpp
 *
 * Purpose:
 *   Implements the Communication class declared in communication.h.
 *
 * Layer:
 *   Communication (Transport -> Protocol -> Message -> Communication -> Application)
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, or CRC internals (that's tlv_codec.h).
 *   - Build outgoing command frames.
 */

#include "communication.h"
#include "tlv_common.h"

namespace communication {

bool Communication::open(const std::string &portName, uint32_t baudRate)
{
    return serialPort_.open(portName, baudRate);
}

void Communication::close()
{
    serialPort_.close();
}

bool Communication::isOpen() const
{
    return serialPort_.isOpen();
}

void Communication::poll()
{
    uint8_t receivedByte;
    if (serialPort_.receiveByte(receivedByte, 20))
    {
        feedByte(receivedByte);
    }
}

void Communication::feedByte(uint8_t byte)
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

void Communication::dispatch(const tlv::Frame &frame)
{
    switch (frame.tag)
    {
        case TAG_KEEPALIVE:
            if (auto parsed = message::parseKeepAlive(frame))
            {
                framesDispatched++;
                if (callbacks.onKeepAlive)
                {
                    callbacks.onKeepAlive(*parsed);
                }
            }
            break;

        case TAG_DATA_REPORT:
            if (auto parsed = message::parseDataReport(frame))
            {
                framesDispatched++;
                if (callbacks.onDataReport)
                {
                    callbacks.onDataReport(*parsed);
                }
            }
            break;

        case TAG_SYSTEM_TIME_RESPONSE:
            if (auto parsed = message::parseSystemTimeResponse(frame))
            {
                framesDispatched++;
                if (callbacks.onSystemTimeResponse)
                {
                    callbacks.onSystemTimeResponse(*parsed);
                }
            }
            break;

        case TAG_EVENT_CONFIG_CHANGED:
            if (auto parsed = message::parseEventConfigChanged(frame))
            {
                framesDispatched++;
                if (callbacks.onEventConfigChanged)
                {
                    callbacks.onEventConfigChanged(*parsed);
                }
            }
            break;

        case TAG_EVENT_OBJECT_DETECTION:
            if (auto parsed = message::parseEventObjectDetection(frame))
            {
                framesDispatched++;
                if (callbacks.onEventObjectDetection)
                {
                    callbacks.onEventObjectDetection(*parsed);
                }
            }
            break;

        case TAG_EVENT_STARTUP:
            if (auto parsed = message::parseEventStartup(frame))
            {
                framesDispatched++;
                if (callbacks.onEventStartup)
                {
                    callbacks.onEventStartup(*parsed);
                }
            }
            break;

        case TAG_EVENT_MODE_TRANSITION:
            if (auto parsed = message::parseEventModeTransition(frame))
            {
                framesDispatched++;
                if (callbacks.onEventModeTransition)
                {
                    callbacks.onEventModeTransition(*parsed);
                }
            }
            break;

        case TAG_MEASUREMENT_CHUNK_RESPONSE:
            if (auto parsed = message::parseMeasurementChunkResponse(frame))
            {
                framesDispatched++;
                if (callbacks.onMeasurementChunkResponse)
                {
                    callbacks.onMeasurementChunkResponse(*parsed);
                }
            }
            break;

        case TAG_EVENT_CHUNK_RESPONSE:
            if (auto parsed = message::parseEventChunkResponse(frame))
            {
                framesDispatched++;
                if (callbacks.onEventChunkResponse)
                {
                    callbacks.onEventChunkResponse(*parsed);
                }
            }
            break;

        default:
            /* Genuinely unrecognized tag - dropped silently, same policy
               as the frozen protocol design's handling of unknown tags. */
            break;
    }
}

bool Communication::sendFrame(const std::vector<uint8_t> &encodedFrame)
{
    lastSentFrame = encodedFrame;

    if (!serialPort_.isOpen())
    {
        return false;
    }
    return serialPort_.send(encodedFrame, 100);
}

}  // namespace communication
