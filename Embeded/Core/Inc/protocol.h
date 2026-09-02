/*
 * protocol.h
 *
 * Purpose:
 *   This file defines the TLV (Tag, Length, Value) communication protocol
 *   used between the LNC and the Central Computer.
 *
 *   It defines how raw bytes are framed into a message and back:
 *
 *       SOF (1) | Tag (1) | Length (2, little-endian) | Value (Length) | CRC16 (2, little-endian)
 *
 *   The decoder in this file is a small state machine: the caller feeds it
 *   one byte at a time (exactly how bytes arrive from a polling UART read),
 *   and it reports back whether a full, valid frame is ready yet.
 *
 * Layer:
 *   Protocol (Transport -> Protocol -> Message -> Application)
 *
 * Responsibilities:
 *   - Build a TLV frame from a Tag + Value (encoding).
 *   - Accept incoming bytes one at a time and detect a complete, valid frame (decoding).
 *   - Calculate the CRC-16/CCITT-FALSE checksum used to validate frames.
 *   - Reject frames with a bad CRC or an invalid Length, and resynchronize
 *     on the next SOF byte, per the frozen protocol design.
 *
 * This file does NOT:
 *   - Know about UART, Ethernet, or any other transport.
 *   - Know what any Tag value *means* (that belongs to the Message layer).
 *   - Use FreeRTOS, interrupts, or any hardware access.
 *   - Use dynamic memory (malloc/free) - only fixed-size buffers.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Frame size limits (frozen protocol decision) ---- */

/* Maximum size of a whole frame: SOF + Tag + Length + Value + CRC16 */
#define PROTOCOL_MAX_FRAME_SIZE   256

/* Frame overhead in bytes: SOF(1) + Tag(1) + Length(2) + CRC16(2) */
#define PROTOCOL_FRAME_OVERHEAD   6

/* Maximum size of just the Value part of a frame */
#define PROTOCOL_MAX_VALUE_SIZE   (PROTOCOL_MAX_FRAME_SIZE - PROTOCOL_FRAME_OVERHEAD)

/* Start-of-frame marker (frozen protocol decision) */
#define PROTOCOL_SOF_BYTE         0xAA

/* ---- A fully decoded frame ---- */

typedef struct
{
    uint8_t  tag;                                /* what kind of message this is - meaning is defined by the Message layer, not here */
    uint16_t length;                             /* how many bytes are actually used in "value" */
    uint8_t  value[PROTOCOL_MAX_VALUE_SIZE];      /* the message payload */
} ProtocolFrame;

/* ---- Result of feeding one byte into the decoder ---- */

typedef enum
{
    PROTOCOL_DECODE_IN_PROGRESS = 0,  /* frame not complete yet, keep feeding bytes */
    PROTOCOL_DECODE_FRAME_READY,      /* a complete, CRC-valid frame is now in out_frame */
    PROTOCOL_DECODE_ERROR             /* bad CRC or bad length - frame rejected, decoder has resynced */
} ProtocolDecodeStatus;

/* ---- Internal decoder state machine steps ---- */

typedef enum
{
    PROTOCOL_STATE_WAIT_SOF = 0,
    PROTOCOL_STATE_READ_TAG,
    PROTOCOL_STATE_READ_LENGTH_LOW,
    PROTOCOL_STATE_READ_LENGTH_HIGH,
    PROTOCOL_STATE_READ_VALUE,
    PROTOCOL_STATE_READ_CRC_LOW,
    PROTOCOL_STATE_READ_CRC_HIGH
} ProtocolDecoderState;

/*
 * Decoder object: holds everything needed to decode one frame at a time,
 * one incoming byte at a time.
 *
 * Callers should not read or modify these fields directly - just call
 * Protocol_DecoderInit() once, then Protocol_FeedByte() for every incoming byte.
 */
typedef struct
{
    ProtocolDecoderState state;
    uint8_t  tag;
    uint16_t length;
    uint8_t  value[PROTOCOL_MAX_VALUE_SIZE];
    uint16_t value_bytes_received;
    uint16_t received_crc;
} ProtocolDecoder;

/* ---- Public functions ---- */

/* Resets a decoder so it starts looking for the next frame from the beginning. */
void Protocol_DecoderInit(ProtocolDecoder *decoder);

/*
 * Feeds one received byte into the decoder.
 *
 * Returns:
 *   PROTOCOL_DECODE_IN_PROGRESS - keep feeding bytes, no full frame yet.
 *   PROTOCOL_DECODE_FRAME_READY - out_frame now holds a complete, valid frame.
 *   PROTOCOL_DECODE_ERROR       - the frame was invalid (bad CRC/length) and was
 *                                 discarded; the decoder is already looking for
 *                                 the next SOF.
 */
ProtocolDecodeStatus Protocol_FeedByte(ProtocolDecoder *decoder,
                                       uint8_t byte,
                                       ProtocolFrame *out_frame);

/*
 * Builds a complete frame (SOF + Tag + Length + Value + CRC16) into out_buffer.
 *
 * Returns the number of bytes written into out_buffer, or 0 on failure
 * (value_length too large, or out_buffer_size too small to hold the frame).
 */
uint16_t Protocol_EncodeFrame(uint8_t tag,
                              const uint8_t *value,
                              uint16_t value_length,
                              uint8_t *out_buffer,
                              uint16_t out_buffer_size);

/*
 * Calculates the CRC-16/CCITT-FALSE checksum over "length" bytes of "data".
 * Exposed publicly so tests can check it against the standard test vector
 * (the ASCII text "123456789" must produce 0x29B1).
 */
uint16_t Protocol_CalculateCRC16(const uint8_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */
