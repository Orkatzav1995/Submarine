/*
 * tlv_codec.cpp
 *
 * Purpose:
 *   Implements the TLV protocol declared in tlv_codec.h: building frames
 *   (encoding), recognizing frames one byte at a time (decoding), and the
 *   CRC-16/CCITT-FALSE checksum used to validate them.
 *
 *   This is a separate, independent implementation of the exact same wire
 *   format as the LNC's protocol.c (deliberately not shared code - see
 *   PROJECT_GUIDE.md - so both are tested against the same CRC standard
 *   test vector to catch any divergence between them).
 *
 * Layer:
 *   Protocol (Transport -> Protocol -> Message -> Application)
 *
 * This file does NOT:
 *   - Touch serial ports, sockets, or any hardware/OS transport.
 *   - Know what any Tag value means.
 *   - Touch a database.
 */

#include "tlv_codec.h"

namespace tlv {

/*
 * Calculates CRC-16/CCITT-FALSE over "length" bytes starting at "data".
 * Same bit-by-bit algorithm as the LNC's Protocol_CalculateCRC16(), so the
 * two independent implementations can be compared directly.
 */
uint16_t calculateCrc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= static_cast<uint16_t>(static_cast<uint16_t>(data[i]) << 8);

        for (int bit = 0; bit < 8; bit++)
        {
            if ((crc & 0x8000) != 0)
            {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            }
            else
            {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }

    return crc;
}

std::vector<uint8_t> encodeFrame(uint8_t tag, const std::vector<uint8_t> &value)
{
    if (value.size() > static_cast<size_t>(kMaxValueSize))
    {
        return {};  // empty vector signals failure - value too large for one frame
    }

    uint16_t valueLength = static_cast<uint16_t>(value.size());

    std::vector<uint8_t> frame;
    frame.reserve(static_cast<size_t>(kFrameOverhead) + valueLength);

    frame.push_back(kSofByte);
    frame.push_back(tag);
    frame.push_back(static_cast<uint8_t>(valueLength & 0xFF));         /* length, low byte first (little-endian) */
    frame.push_back(static_cast<uint8_t>((valueLength >> 8) & 0xFF));  /* length, high byte */

    for (uint8_t b : value)
    {
        frame.push_back(b);
    }

    /* CRC covers Tag + Length + Value - that is everything after SOF. */
    uint16_t crc = calculateCrc16(&frame[1], frame.size() - 1);

    frame.push_back(static_cast<uint8_t>(crc & 0xFF));         /* crc, low byte first */
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));  /* crc, high byte */

    return frame;
}

Decoder::Decoder()
{
    reset();
}

void Decoder::reset()
{
    state_ = State::WaitSof;
    tag_ = 0;
    length_ = 0;
    value_.clear();
    receivedCrc_ = 0;
}

DecodeStatus Decoder::feedByte(uint8_t byte, Frame &outFrame)
{
    switch (state_)
    {
        case State::WaitSof:
            if (byte == kSofByte)
            {
                state_ = State::ReadTag;
            }
            /* anything else is just noise before the frame - ignore it and keep waiting */
            return DecodeStatus::InProgress;

        case State::ReadTag:
            tag_ = byte;
            state_ = State::ReadLengthLow;
            return DecodeStatus::InProgress;

        case State::ReadLengthLow:
            length_ = byte;
            state_ = State::ReadLengthHigh;
            return DecodeStatus::InProgress;

        case State::ReadLengthHigh:
            length_ = static_cast<uint16_t>(length_ | (static_cast<uint16_t>(byte) << 8));

            if (length_ > static_cast<uint16_t>(kMaxValueSize))
            {
                /* This length would make the frame bigger than the 256-byte
                   cap. Reject now instead of trying to read that many
                   value bytes, and go straight back to hunting for SOF. */
                reset();
                return DecodeStatus::Error;
            }

            value_.clear();

            if (length_ == 0)
            {
                /* No Value bytes at all - go straight to reading the CRC. */
                state_ = State::ReadCrcLow;
            }
            else
            {
                value_.reserve(length_);
                state_ = State::ReadValue;
            }
            return DecodeStatus::InProgress;

        case State::ReadValue:
            value_.push_back(byte);

            if (value_.size() >= length_)
            {
                state_ = State::ReadCrcLow;
            }
            return DecodeStatus::InProgress;

        case State::ReadCrcLow:
            receivedCrc_ = byte;
            state_ = State::ReadCrcHigh;
            return DecodeStatus::InProgress;

        case State::ReadCrcHigh:
        {
            receivedCrc_ = static_cast<uint16_t>(receivedCrc_ | (static_cast<uint16_t>(byte) << 8));

            /* Rebuild the same byte sequence the sender calculated its CRC over. */
            std::vector<uint8_t> crcInput;
            crcInput.reserve(3 + value_.size());
            crcInput.push_back(tag_);
            crcInput.push_back(static_cast<uint8_t>(length_ & 0xFF));
            crcInput.push_back(static_cast<uint8_t>((length_ >> 8) & 0xFF));

            for (uint8_t b : value_)
            {
                crcInput.push_back(b);
            }

            uint16_t calculatedCrc = calculateCrc16(crcInput.data(), crcInput.size());

            if (calculatedCrc == receivedCrc_)
            {
                outFrame.tag = tag_;
                outFrame.value = value_;

                reset();
                return DecodeStatus::FrameReady;
            }
            else
            {
                reset();
                return DecodeStatus::Error;
            }
        }

        default:
            reset();
            return DecodeStatus::Error;
    }
}

}  // namespace tlv
