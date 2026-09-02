/*
 * protocol_test.c
 *
 * Purpose:
 *   Stand-alone PC test program for protocol.c/protocol.h (the Protocol
 *   layer). Builds and checks TLV frames without any STM32 hardware,
 *   FreeRTOS, or transport code involved.
 *
 * Layer:
 *   Test harness for the Protocol layer only.
 *
 * How to build and run (from this folder):
 *   gcc -I "../../Embeded/Core/Inc" protocol_test.c "../../Embeded/Core/Src/protocol.c" -o protocol_test.exe
 *   ./protocol_test.exe
 *
 *   (Any normal C compiler works - MinGW gcc, or "cl" with the equivalent
 *   /I include path. No special toolchain or library is required.)
 *
 * This file does NOT:
 *   - Touch UART, Ethernet, FreeRTOS, or any hardware.
 *   - Test the Transport, Message, or Application layers.
 */

#include "protocol.h"
#include <stdio.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;

static void check(int condition, const char *description)
{
    g_tests_run++;
    if (condition)
    {
        printf("  PASS: %s\n", description);
    }
    else
    {
        printf("  FAIL: %s\n", description);
        g_tests_failed++;
    }
}

/* CRC-16/CCITT-FALSE has a well-known standard test vector:
   the ASCII text "123456789" must produce 0x29B1. If our CRC function
   doesn't match this, nothing built on top of it can be trusted. */
static void test_crc_standard_vector(void)
{
    const uint8_t data[] = "123456789";
    uint16_t crc = Protocol_CalculateCRC16(data, (uint16_t)(sizeof(data) - 1));

    printf("\nTest: CRC-16/CCITT-FALSE standard test vector\n");
    check(crc == 0x29B1, "CRC of \"123456789\" equals 0x29B1");
}

static void test_encode_decode_round_trip(void)
{
    uint8_t value[] = { 0x11, 0x22, 0x33 };
    uint8_t frame_buffer[32];
    uint16_t frame_size;
    ProtocolDecoder decoder;
    ProtocolFrame decoded_frame;
    ProtocolDecodeStatus status;
    uint16_t i;
    int frame_was_ready;

    printf("\nTest: Encode then decode a simple frame\n");

    frame_size = Protocol_EncodeFrame(0x05, value, (uint16_t)sizeof(value), frame_buffer, (uint16_t)sizeof(frame_buffer));
    check(frame_size == (PROTOCOL_FRAME_OVERHEAD + sizeof(value)), "encoded frame has the expected size");
    check(frame_buffer[0] == PROTOCOL_SOF_BYTE, "encoded frame starts with SOF");

    Protocol_DecoderInit(&decoder);
    frame_was_ready = 0;
    for (i = 0; i < frame_size; i++)
    {
        status = Protocol_FeedByte(&decoder, frame_buffer[i], &decoded_frame);
        if (status == PROTOCOL_DECODE_FRAME_READY)
        {
            frame_was_ready = 1;
        }
    }

    check(frame_was_ready == 1, "decoder reports frame ready after the last byte");
    check(decoded_frame.tag == 0x05, "decoded tag matches original tag");
    check(decoded_frame.length == sizeof(value), "decoded length matches original length");
    check((decoded_frame.value[0] == 0x11) && (decoded_frame.value[1] == 0x22) && (decoded_frame.value[2] == 0x33),
          "decoded value bytes match original value bytes");
}

static void test_wrong_crc(void)
{
    uint8_t value[] = { 0xAB };
    uint8_t frame_buffer[32];
    uint16_t frame_size;
    ProtocolDecoder decoder;
    ProtocolFrame decoded_frame;
    ProtocolDecodeStatus status = PROTOCOL_DECODE_IN_PROGRESS;
    uint16_t i;

    printf("\nTest: Wrong CRC is rejected\n");

    frame_size = Protocol_EncodeFrame(0x01, value, (uint16_t)sizeof(value), frame_buffer, (uint16_t)sizeof(frame_buffer));

    /* Corrupt the last CRC byte on purpose. */
    frame_buffer[frame_size - 1] ^= 0xFF;

    Protocol_DecoderInit(&decoder);
    for (i = 0; i < frame_size; i++)
    {
        status = Protocol_FeedByte(&decoder, frame_buffer[i], &decoded_frame);
    }

    check(status == PROTOCOL_DECODE_ERROR, "decoder reports an error for a corrupted CRC");
}

static void test_invalid_length_is_rejected(void)
{
    ProtocolDecoder decoder;
    ProtocolFrame decoded_frame;
    ProtocolDecodeStatus status;

    printf("\nTest: A Length beyond the 256-byte cap is rejected\n");

    Protocol_DecoderInit(&decoder);
    Protocol_FeedByte(&decoder, PROTOCOL_SOF_BYTE, &decoded_frame); /* SOF */
    Protocol_FeedByte(&decoder, 0x01, &decoded_frame);              /* Tag */
    Protocol_FeedByte(&decoder, 0xFF, &decoded_frame);              /* Length low byte */
    status = Protocol_FeedByte(&decoder, 0xFF, &decoded_frame);     /* Length high byte -> 0xFFFF, far too big */

    check(status == PROTOCOL_DECODE_ERROR, "an oversized Length is rejected immediately");
}

static void test_garbage_before_sof_is_ignored(void)
{
    uint8_t value[] = { 0x7A };
    uint8_t frame_buffer[32];
    uint16_t frame_size;
    uint8_t garbage[] = { 0x00, 0x11, 0x22, 0x99 }; /* none of these bytes is SOF (0xAA) */
    ProtocolDecoder decoder;
    ProtocolFrame decoded_frame;
    ProtocolDecodeStatus status;
    uint16_t i;
    int frame_was_ready;
    int garbage_was_clean;

    printf("\nTest: Garbage bytes before SOF are ignored\n");

    frame_size = Protocol_EncodeFrame(0x02, value, (uint16_t)sizeof(value), frame_buffer, (uint16_t)sizeof(frame_buffer));

    Protocol_DecoderInit(&decoder);

    garbage_was_clean = 1;
    for (i = 0; i < sizeof(garbage); i++)
    {
        status = Protocol_FeedByte(&decoder, garbage[i], &decoded_frame);
        if (status != PROTOCOL_DECODE_IN_PROGRESS)
        {
            garbage_was_clean = 0;
        }
    }
    check(garbage_was_clean == 1, "garbage bytes before SOF never produce an error or a frame");

    frame_was_ready = 0;
    for (i = 0; i < frame_size; i++)
    {
        status = Protocol_FeedByte(&decoder, frame_buffer[i], &decoded_frame);
        if (status == PROTOCOL_DECODE_FRAME_READY)
        {
            frame_was_ready = 1;
        }
    }
    check(frame_was_ready == 1, "the real frame after the garbage still decodes correctly");
}

static void test_resync_after_error(void)
{
    uint8_t value[] = { 0x5A, 0x5B };
    uint8_t bad_frame[32];
    uint8_t good_frame[32];
    uint16_t bad_size;
    uint16_t good_size;
    ProtocolDecoder decoder;
    ProtocolFrame decoded_frame;
    ProtocolDecodeStatus status;
    uint16_t i;
    int saw_error;
    int frame_was_ready;

    printf("\nTest: Decoder resynchronizes after a bad frame\n");

    bad_size = Protocol_EncodeFrame(0x03, value, (uint16_t)sizeof(value), bad_frame, (uint16_t)sizeof(bad_frame));
    bad_frame[bad_size - 1] ^= 0xFF; /* corrupt its CRC */

    good_size = Protocol_EncodeFrame(0x04, value, (uint16_t)sizeof(value), good_frame, (uint16_t)sizeof(good_frame));

    Protocol_DecoderInit(&decoder);

    saw_error = 0;
    for (i = 0; i < bad_size; i++)
    {
        status = Protocol_FeedByte(&decoder, bad_frame[i], &decoded_frame);
        if (status == PROTOCOL_DECODE_ERROR)
        {
            saw_error = 1;
        }
    }
    check(saw_error == 1, "the corrupted frame is reported as an error");

    frame_was_ready = 0;
    for (i = 0; i < good_size; i++)
    {
        status = Protocol_FeedByte(&decoder, good_frame[i], &decoded_frame);
        if (status == PROTOCOL_DECODE_FRAME_READY)
        {
            frame_was_ready = 1;
        }
    }
    check(frame_was_ready == 1, "the following good frame still decodes correctly");
    check(decoded_frame.tag == 0x04, "the decoded frame is the good one, not leftover data from the bad one");
}

static void test_unknown_tag_is_not_rejected(void)
{
    uint8_t value[] = { 0x01 };
    uint8_t frame_buffer[32];
    uint16_t frame_size;
    ProtocolDecoder decoder;
    ProtocolFrame decoded_frame;
    ProtocolDecodeStatus status;
    uint16_t i;
    int frame_was_ready;

    printf("\nTest: An unrecognized Tag value is still framed/decoded correctly\n");

    /* The Protocol layer does not know what any tag means - 0xFF here is
       just "some tag nothing has defined a meaning for yet". */
    frame_size = Protocol_EncodeFrame(0xFF, value, (uint16_t)sizeof(value), frame_buffer, (uint16_t)sizeof(frame_buffer));

    Protocol_DecoderInit(&decoder);
    frame_was_ready = 0;
    for (i = 0; i < frame_size; i++)
    {
        status = Protocol_FeedByte(&decoder, frame_buffer[i], &decoded_frame);
        if (status == PROTOCOL_DECODE_FRAME_READY)
        {
            frame_was_ready = 1;
        }
    }

    check(frame_was_ready == 1, "frame with an unrecognized tag still decodes");
    check(decoded_frame.tag == 0xFF, "the unrecognized tag value is preserved");
}

static void test_maximum_frame_size(void)
{
    uint8_t value[PROTOCOL_MAX_VALUE_SIZE];
    uint8_t frame_buffer[PROTOCOL_MAX_FRAME_SIZE];
    uint8_t too_big_buffer[PROTOCOL_MAX_FRAME_SIZE];
    uint16_t frame_size;
    uint16_t too_big_frame_size;
    uint16_t i;

    printf("\nTest: Maximum frame size\n");

    for (i = 0; i < sizeof(value); i++)
    {
        value[i] = (uint8_t)i;
    }

    frame_size = Protocol_EncodeFrame(0x06, value, (uint16_t)sizeof(value), frame_buffer, (uint16_t)sizeof(frame_buffer));
    check(frame_size == PROTOCOL_MAX_FRAME_SIZE, "a value filling the max payload produces exactly a 256-byte frame");

    /* One byte more than the maximum payload must be refused - the encoder
       checks the length before it ever reads from "value", so this is safe
       even though "value" itself isn't that large. */
    too_big_frame_size = Protocol_EncodeFrame(0x06, value, (uint16_t)(PROTOCOL_MAX_VALUE_SIZE + 1),
                                               too_big_buffer, (uint16_t)sizeof(too_big_buffer));
    check(too_big_frame_size == 0, "a value one byte over the maximum is refused by the encoder");
}

static void test_little_endian_length_and_crc(void)
{
    uint8_t tag_length_value[5]; /* tag(1) + length(2) + a 2-byte value */
    uint8_t value_bytes[2] = { 0x10, 0x20 };
    uint16_t expected_crc;
    ProtocolDecoder decoder;
    ProtocolFrame decoded_frame;
    ProtocolDecodeStatus status;

    printf("\nTest: Length and CRC fields are read low-byte-first (little-endian)\n");

    tag_length_value[0] = 0x09;         /* tag */
    tag_length_value[1] = 0x02;         /* length low byte  = 2 */
    tag_length_value[2] = 0x00;         /* length high byte = 0 */
    tag_length_value[3] = value_bytes[0];
    tag_length_value[4] = value_bytes[1];

    expected_crc = Protocol_CalculateCRC16(tag_length_value, (uint16_t)sizeof(tag_length_value));

    Protocol_DecoderInit(&decoder);
    Protocol_FeedByte(&decoder, PROTOCOL_SOF_BYTE, &decoded_frame);
    Protocol_FeedByte(&decoder, tag_length_value[0], &decoded_frame); /* tag */
    Protocol_FeedByte(&decoder, tag_length_value[1], &decoded_frame); /* length low byte */
    Protocol_FeedByte(&decoder, tag_length_value[2], &decoded_frame); /* length high byte */
    Protocol_FeedByte(&decoder, tag_length_value[3], &decoded_frame); /* value byte 0 */
    Protocol_FeedByte(&decoder, tag_length_value[4], &decoded_frame); /* value byte 1 */
    Protocol_FeedByte(&decoder, (uint8_t)(expected_crc & 0xFF), &decoded_frame);          /* crc low byte first */
    status = Protocol_FeedByte(&decoder, (uint8_t)((expected_crc >> 8) & 0xFF), &decoded_frame); /* crc high byte second */

    check(status == PROTOCOL_DECODE_FRAME_READY, "feeding CRC low byte then high byte is accepted (little-endian)");
    check(decoded_frame.length == 2, "length built from low byte then high byte equals 2, not 512");
}

static void test_zero_length_value(void)
{
    uint8_t frame_buffer[32];
    uint16_t frame_size;
    ProtocolDecoder decoder;
    ProtocolFrame decoded_frame;
    ProtocolDecodeStatus status;
    uint16_t i;
    int frame_was_ready;

    printf("\nTest: A frame with a zero-length Value works (e.g. a 'get time' request)\n");

    frame_size = Protocol_EncodeFrame(0x07, NULL, 0, frame_buffer, (uint16_t)sizeof(frame_buffer));
    check(frame_size == PROTOCOL_FRAME_OVERHEAD, "a zero-length value produces a frame of exactly the overhead size");

    Protocol_DecoderInit(&decoder);
    frame_was_ready = 0;
    for (i = 0; i < frame_size; i++)
    {
        status = Protocol_FeedByte(&decoder, frame_buffer[i], &decoded_frame);
        if (status == PROTOCOL_DECODE_FRAME_READY)
        {
            frame_was_ready = 1;
        }
    }
    check(frame_was_ready == 1, "the zero-length frame decodes correctly");
    check(decoded_frame.length == 0, "decoded length is 0");
}

int main(void)
{
    printf("=== protocol.c test suite ===\n");

    test_crc_standard_vector();
    test_encode_decode_round_trip();
    test_wrong_crc();
    test_invalid_length_is_rejected();
    test_garbage_before_sof_is_ignored();
    test_resync_after_error();
    test_unknown_tag_is_not_rejected();
    test_maximum_frame_size();
    test_little_endian_length_and_crc();
    test_zero_length_value();

    printf("\n=== Summary: %d checks run, %d failed ===\n", g_tests_run, g_tests_failed);

    return (g_tests_failed == 0) ? 0 : 1;
}
