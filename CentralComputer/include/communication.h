/*
 * communication.h
 *
 * Purpose:
 *   The Central Computer's Communication module (Sec 3 of the spec) - the
 *   LNC-facing "listener" that owns the actual serial link end-to-end.
 *   Reads raw bytes off the port, feeds them through the TLV decoder, and
 *   once a full frame arrives, parses it into the correct typed message
 *   (via message.h) and hands it to whichever callback was registered for
 *   that message type. This is the CC-side counterpart to the LNC's
 *   CommRxTask/CommTxTask - same idea, no RTOS underneath it, so a simple
 *   poll() takes the place of a FreeRTOS task.
 *
 * Layer:
 *   Sits above Transport/Protocol/Message, below Application.
 *   (Transport -> Protocol -> Message -> Communication -> Application)
 *
 * Responsibilities:
 *   - Own one transport::SerialPort and one tlv::Decoder.
 *   - poll(): try to receive one byte (short timeout) and feed it in.
 *   - feedByte(): the actual decode+dispatch logic, exposed publicly so
 *     it can be unit-tested with hand-built frames, no real COM port
 *     needed - the same seam tlv::Decoder::feedByte itself is tested with.
 *   - On a complete, CRC-valid frame: parse it via message.h's parse*
 *     functions and invoke the matching callback in "callbacks", if one
 *     was registered. An unregistered callback, or a genuinely
 *     unrecognized tag, is silently ignored - same spirit as the frozen
 *     protocol's "unknown tags dropped silently".
 *   - sendFrame(): forward an already-built frame straight to the serial
 *     port. Communication does not build outgoing command frames itself -
 *     that's Management Command's job, using message.h's buildXxx
 *     functions. The one exception (Phase 1, LNC Timestamp/RTC Hardening):
 *     TAG_GET_SYSTEM_TIME_REQUEST, now actively sent by the LNC at boot,
 *     is answered immediately and synchronously inline (this PC's own
 *     real time via std::time(nullptr)) - a mechanical auto-reply, not a
 *     "command," so no callback or Management Command involvement.
 *   - Track framesDispatched / decodeErrors counters, mirroring the LNC's
 *     g_frames_decoded_ok / g_frames_decode_error, for observability and
 *     for tests to check against without intercepting every callback.
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, or CRC internals (Protocol's job).
 *   - Implement the LNC's 3-tier TX priority queue - that is an LNC-only
 *     requirement (Sec 2.5); the CC sends one frame at a time.
 *   - Build outgoing command frames - except the one auto-reply above.
 *   - Touch a database, the Ground Station / Ethernet side, or any UI.
 */

#ifndef CENTRAL_COMPUTER_COMMUNICATION_H
#define CENTRAL_COMPUTER_COMMUNICATION_H

#include "message.h"
#include "serial_transport.h"
#include "tlv_codec.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace communication {

/*
 * One callback per LNC -> CC message type. Left unset (default), an
 * arriving message of that type is simply not delivered anywhere -
 * useful before the modules that will eventually consume them
 * (Management Command, Data Collection & Analysis) exist yet.
 */
struct Callbacks
{
    std::function<void(const message::MeasurementSample &)> onKeepAlive;
    std::function<void(const message::MeasurementSample &)> onDataReport;
    std::function<void(const message::TimestampMessage &)> onSystemTimeResponse;
    std::function<void(const message::TimestampMessage &)> onEventConfigChanged;
    std::function<void(const message::TimestampWithFlagMessage &)> onEventObjectDetection;
    std::function<void(const message::TimestampWithFlagMessage &)> onEventStartup;
    std::function<void(const message::ModeTransitionMessage &)> onEventModeTransition;
    std::function<void(const message::MeasurementChunkResponse &)> onMeasurementChunkResponse;
    std::function<void(const message::EventChunkResponse &)> onEventChunkResponse;
};

class Communication
{
public:
    Communication() = default;

    /* Fill this in (any subset of fields) before calling poll(). */
    Callbacks callbacks;

    /* Observability counters - mirrors the LNC's g_frames_decoded_ok /
       g_frames_decode_error. Public, read-only in spirit (tests and
       callers may read them; only this class writes them). */
    uint32_t framesDispatched = 0;
    uint32_t decodeErrors = 0;

    /* Set at the top of every sendFrame() call, regardless of whether the
       port is actually open - lets tests (e.g. management_command_test.cpp)
       verify the exact bytes a caller attempted to send without needing a
       real, open serial port. Not meant for production logic - just
       observability, same spirit as the two counters above. */
    std::vector<uint8_t> lastSentFrame;

    /*
     * Opens "portName" (e.g. "COM10") at "baudRate". Returns true on
     * success. Closes any previously open port first.
     */
    bool open(const std::string &portName, uint32_t baudRate);

    void close();

    bool isOpen() const;

    /*
     * Tries to receive one byte from the port (short timeout) and, if one
     * arrived, feeds it into the decoder via feedByte(). Call this
     * repeatedly from the CC's main loop - one call makes at most one
     * byte of progress, same model as the LNC's CommRxTask.
     */
    void poll();

    /*
     * Feeds one byte into the TLV decoder. On a complete, valid frame:
     * parses it and invokes the matching callback (if registered),
     * incrementing framesDispatched. On a decode error (bad CRC/length):
     * increments decodeErrors. Public so tests can drive it directly with
     * hand-built frames, without opening a real serial port.
     */
    void feedByte(uint8_t byte);

    /*
     * Sends an already-built frame (e.g. from message::buildSetXxx)
     * straight to the serial port. Returns false if the port isn't open
     * or the send fails.
     */
    bool sendFrame(const std::vector<uint8_t> &encodedFrame);

private:
    void dispatch(const tlv::Frame &frame);

    transport::SerialPort serialPort_;
    tlv::Decoder decoder_;
};

}  // namespace communication

#endif  // CENTRAL_COMPUTER_COMMUNICATION_H
