/*
 * tlv_codec.h
 *
 * Purpose:
 *   Defines the TLV (Tag, Length, Value) communication protocol used
 *   between the Central Computer and the Ground Station (and, in wire
 *   format only, with the LNC - the LNC has its own separate C
 *   implementation of the same format in protocol.h/protocol.c).
 *
 *   Frame layout (identical to the LNC's protocol.h):
 *
 *       SOF (1) | Tag (1) | Length (2, little-endian) | Value (Length) | CRC16 (2, little-endian)
 *
 *   The Decoder class is fed one byte at a time and reports back whether
 *   a full, valid frame is ready yet - the same idea as the LNC's
 *   Protocol_FeedByte(), just expressed as a C++ class since the decoder
 *   genuinely holds state between calls.
 *
 * Layer:
 *   Protocol (Transport -> Protocol -> Message -> Application)
 *
 * Responsibilities:
 *   - Build a TLV frame from a Tag + Value (encodeFrame).
 *   - Accept incoming bytes one at a time and detect a complete, valid
 *     frame (Decoder::feedByte).
 *   - Calculate the CRC-16/CCITT-FALSE checksum used to validate frames.
 *   - Reject frames with a bad CRC or an invalid Length, and resynchronize
 *     on the next SOF byte, per the frozen protocol design.
 *
 * This file does NOT:
 *   - Know about serial ports, sockets, or any other transport.
 *   - Know what any Tag value *means* (that belongs to the Message layer).
 *   - Touch a database, a socket, or the Windows API.
 */

#ifndef TLV_CODEC_H
#define TLV_CODEC_H

#include <cstdint>
#include <vector>

namespace tlv {

/* ---- Frame size limits (frozen protocol decision - same as the LNC side) ---- */

constexpr int kMaxFrameSize  = 256;                        // SOF+Tag+Length+Value+CRC16
constexpr int kFrameOverhead = 6;                           // SOF(1)+Tag(1)+Length(2)+CRC16(2)
constexpr int kMaxValueSize  = kMaxFrameSize - kFrameOverhead;
constexpr uint8_t kSofByte   = 0xAA;

/* ---- A fully decoded frame ---- */

struct Frame
{
    uint8_t tag = 0;                 // what kind of message this is - meaning is defined by the Message layer, not here
    std::vector<uint8_t> value;      // the message payload
};

/* ---- Result of feeding one byte into the decoder ---- */

enum class DecodeStatus
{
    InProgress,   // frame not complete yet, keep feeding bytes
    FrameReady,   // a complete, CRC-valid frame is now in outFrame
    Error         // bad CRC or bad length - frame rejected, decoder has resynced
};

/*
 * Decoder: fed one incoming byte at a time (feedByte), same idea as the
 * LNC's Protocol_FeedByte() state machine, expressed as a class here
 * because the decoder's progress genuinely needs to persist between calls.
 */
class Decoder
{
public:
    Decoder();

    DecodeStatus feedByte(uint8_t byte, Frame &outFrame);

private:
    enum class State
    {
        WaitSof,
        ReadTag,
        ReadLengthLow,
        ReadLengthHigh,
        ReadValue,
        ReadCrcLow,
        ReadCrcHigh
    };

    void reset();

    State state_;
    uint8_t tag_;
    uint16_t length_;
    std::vector<uint8_t> value_;
    uint16_t receivedCrc_;
};

/*
 * Builds a complete frame (SOF + Tag + Length + Value + CRC16).
 * Returns an empty vector on failure (value too large for one frame).
 */
std::vector<uint8_t> encodeFrame(uint8_t tag, const std::vector<uint8_t> &value);

/*
 * Calculates the CRC-16/CCITT-FALSE checksum over "length" bytes of "data".
 * Exposed publicly so tests can check it against the standard test vector
 * (the ASCII text "123456789" must produce 0x29B1) - the same vector used
 * to validate the LNC's C implementation, to catch any divergence between
 * the two independent implementations early.
 */
uint16_t calculateCrc16(const uint8_t *data, size_t length);

}  // namespace tlv

#endif  // TLV_CODEC_H
