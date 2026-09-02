/*
 * protocol.c
 *
 * Purpose:
 *   Implements the TLV protocol declared in protocol.h: building frames
 *   (encoding), recognizing frames one byte at a time (decoding), and the
 *   CRC-16/CCITT-FALSE checksum used to validate them.
 *
 * Layer:
 *   Protocol (Transport -> Protocol -> Message -> Application)
 *
 * Responsibilities:
 *   - Build a TLV frame from a Tag + Value.
 *   - Feed one incoming byte at a time through a small state machine and
 *     detect a complete, CRC-valid frame.
 *   - Calculate CRC-16/CCITT-FALSE.
 *
 * This file does NOT:
 *   - Touch UART, Ethernet, or any hardware.
 *   - Know what any Tag value means.
 *   - Use FreeRTOS or dynamic memory.
 */

#include "protocol.h"
#include <stddef.h>  /* for NULL */

static uint16_t Protocol_UpdateCRC16(uint16_t crc, uint8_t byte)
{
    uint8_t bit;

    crc ^= (uint16_t)((uint16_t)byte << 8);

    for (bit = 0; bit < 8; bit++)
    {
        if ((crc & 0x8000) != 0)
        {
            crc = (uint16_t)((crc << 1) ^ 0x1021);
        }
        else
        {
            crc = (uint16_t)(crc << 1);
        }
    }

    return crc;
}

/*
 * Calculates CRC-16/CCITT-FALSE over "length" bytes starting at "data".
 *
 * Algorithm (bit-by-bit, easy to follow):
 *   - Start with crc = 0xFFFF (the standard's required initial value).
 *   - For each byte: XOR it into the top 8 bits of crc.
 *   - Then, 8 times: if the top bit of crc is 1, shift left and XOR with
 *     0x1021 (the standard's polynomial); otherwise just shift left.
 */
uint16_t Protocol_CalculateCRC16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;

    for (i = 0; i < length; i++)
    {
        crc = Protocol_UpdateCRC16(crc, data[i]);
    }

    return crc;
}

uint16_t Protocol_EncodeFrame(uint8_t tag,
                              const uint8_t *value,
                              uint16_t value_length,
                              uint8_t *out_buffer,
                              uint16_t out_buffer_size)
{
    uint16_t frame_size;
    uint16_t crc;
    uint16_t i;

    if (value_length > PROTOCOL_MAX_VALUE_SIZE)
    {
        return 0; /* value would not fit in one frame */
    }

    if ((value_length > 0) && (value == NULL))
    {
        return 0; /* caller said there is data, but gave no buffer for it */
    }

    frame_size = (uint16_t)(PROTOCOL_FRAME_OVERHEAD + value_length);

    if (out_buffer_size < frame_size)
    {
        return 0; /* caller's output buffer is too small */
    }

    out_buffer[0] = PROTOCOL_SOF_BYTE;
    out_buffer[1] = tag;
    out_buffer[2] = (uint8_t)(value_length & 0xFF);         /* length, low byte first (little-endian) */
    out_buffer[3] = (uint8_t)((value_length >> 8) & 0xFF);  /* length, high byte */

    for (i = 0; i < value_length; i++)
    {
        out_buffer[4 + i] = value[i];
    }

    /* CRC covers Tag + Length + Value - that is out_buffer[1] onward,
       NOT the SOF byte. */
    crc = Protocol_CalculateCRC16(&out_buffer[1], (uint16_t)(1 + 2 + value_length));

    out_buffer[4 + value_length]     = (uint8_t)(crc & 0xFF);        /* crc, low byte first */
    out_buffer[4 + value_length + 1] = (uint8_t)((crc >> 8) & 0xFF); /* crc, high byte */

    return frame_size;
}

void Protocol_DecoderInit(ProtocolDecoder *decoder)
{
    decoder->state = PROTOCOL_STATE_WAIT_SOF;
    decoder->tag = 0;
    decoder->length = 0;
    decoder->value_bytes_received = 0;
    decoder->received_crc = 0;
}

ProtocolDecodeStatus Protocol_FeedByte(ProtocolDecoder *decoder,
                                       uint8_t byte,
                                       ProtocolFrame *out_frame)
{
    switch (decoder->state)
    {
        case PROTOCOL_STATE_WAIT_SOF:
            if (byte == PROTOCOL_SOF_BYTE)
            {
                decoder->state = PROTOCOL_STATE_READ_TAG;
            }
            /* anything else is just noise before the frame - ignore it and keep waiting */
            return PROTOCOL_DECODE_IN_PROGRESS;

        case PROTOCOL_STATE_READ_TAG:
            decoder->tag = byte;
            decoder->state = PROTOCOL_STATE_READ_LENGTH_LOW;
            return PROTOCOL_DECODE_IN_PROGRESS;

        case PROTOCOL_STATE_READ_LENGTH_LOW:
            decoder->length = byte;
            decoder->state = PROTOCOL_STATE_READ_LENGTH_HIGH;
            return PROTOCOL_DECODE_IN_PROGRESS;

        case PROTOCOL_STATE_READ_LENGTH_HIGH:
            decoder->length = (uint16_t)(decoder->length | ((uint16_t)byte << 8));

            if (decoder->length > PROTOCOL_MAX_VALUE_SIZE)
            {
                /* This length would make the frame bigger than the 256-byte
                   cap. Reject now instead of trying to read that many
                   value bytes, and go straight back to hunting for SOF. */
                Protocol_DecoderInit(decoder);
                return PROTOCOL_DECODE_ERROR;
            }

            decoder->value_bytes_received = 0;

            if (decoder->length == 0)
            {
                /* No Value bytes at all - go straight to reading the CRC. */
                decoder->state = PROTOCOL_STATE_READ_CRC_LOW;
            }
            else
            {
                decoder->state = PROTOCOL_STATE_READ_VALUE;
            }
            return PROTOCOL_DECODE_IN_PROGRESS;

        case PROTOCOL_STATE_READ_VALUE:
            decoder->value[decoder->value_bytes_received] = byte;
            decoder->value_bytes_received++;

            if (decoder->value_bytes_received >= decoder->length)
            {
                decoder->state = PROTOCOL_STATE_READ_CRC_LOW;
            }
            return PROTOCOL_DECODE_IN_PROGRESS;

        case PROTOCOL_STATE_READ_CRC_LOW:
            decoder->received_crc = byte;
            decoder->state = PROTOCOL_STATE_READ_CRC_HIGH;
            return PROTOCOL_DECODE_IN_PROGRESS;

        case PROTOCOL_STATE_READ_CRC_HIGH:
        {
            uint16_t calculated_crc;
            uint16_t i;

            decoder->received_crc = (uint16_t)(decoder->received_crc | ((uint16_t)byte << 8));

            calculated_crc = 0xFFFF;
            calculated_crc = Protocol_UpdateCRC16(calculated_crc, decoder->tag);
            calculated_crc = Protocol_UpdateCRC16(calculated_crc, (uint8_t)(decoder->length & 0xFF));
            calculated_crc = Protocol_UpdateCRC16(calculated_crc, (uint8_t)((decoder->length >> 8) & 0xFF));
            for (i = 0; i < decoder->length; i++)
            {
                calculated_crc = Protocol_UpdateCRC16(calculated_crc, decoder->value[i]);
            }

            if (calculated_crc == decoder->received_crc)
            {
                out_frame->tag = decoder->tag;
                out_frame->length = decoder->length;

                for (i = 0; i < decoder->length; i++)
                {
                    out_frame->value[i] = decoder->value[i];
                }

                Protocol_DecoderInit(decoder);
                return PROTOCOL_DECODE_FRAME_READY;
            }
            else
            {
                Protocol_DecoderInit(decoder);
                return PROTOCOL_DECODE_ERROR;
            }
        }

        default:
            /* Should never happen - reset just in case. */
            Protocol_DecoderInit(decoder);
            return PROTOCOL_DECODE_ERROR;
    }
}
