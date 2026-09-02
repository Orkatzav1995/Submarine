/*
 * tlv_codec_test.cpp
 *
 * Purpose:
 *   Stand-alone PC test program for tlv_codec.h/tlv_codec.cpp (the Central
 *   Computer / Ground Station side Protocol layer). Mirrors protocol_test.c
 *   (the LNC's C test) test-for-test, so both independent implementations
 *   are checked against the same scenarios and the same CRC standard vector.
 *
 * Layer:
 *   Test harness for the Protocol layer only.
 *
 * How to build and run (from this folder):
 *   g++ -std=c++17 -I "../../Common/TLVCodec" tlv_codec_test.cpp "../../Common/TLVCodec/tlv_codec.cpp" -o tlv_codec_test.exe
 *   ./tlv_codec_test.exe
 *
 * This file does NOT:
 *   - Touch serial ports, sockets, or any hardware/OS transport.
 *   - Test the Transport, Message, or Application layers.
 */

#include "tlv_codec.h"
#include <cstdio>

static int g_testsRun = 0;
static int g_testsFailed = 0;

static void check(bool condition, const char *description)
{
    g_testsRun++;
    if (condition)
    {
        std::printf("  PASS: %s\n", description);
    }
    else
    {
        std::printf("  FAIL: %s\n", description);
        g_testsFailed++;
    }
}

static void testCrcStandardVector()
{
    const uint8_t data[] = "123456789";
    uint16_t crc = tlv::calculateCrc16(data, sizeof(data) - 1);

    std::printf("\nTest: CRC-16/CCITT-FALSE standard test vector\n");
    check(crc == 0x29B1, "CRC of \"123456789\" equals 0x29B1");
}

static void testEncodeDecodeRoundTrip()
{
    std::printf("\nTest: Encode then decode a simple frame\n");

    std::vector<uint8_t> value = { 0x11, 0x22, 0x33 };
    std::vector<uint8_t> frame = tlv::encodeFrame(0x05, value);

    check(frame.size() == static_cast<size_t>(tlv::kFrameOverhead) + value.size(),
          "encoded frame has the expected size");
    check(frame[0] == tlv::kSofByte, "encoded frame starts with SOF");

    tlv::Decoder decoder;
    tlv::Frame decoded;
    tlv::DecodeStatus status = tlv::DecodeStatus::InProgress;
    bool frameWasReady = false;

    for (uint8_t b : frame)
    {
        status = decoder.feedByte(b, decoded);
        if (status == tlv::DecodeStatus::FrameReady)
        {
            frameWasReady = true;
        }
    }

    check(frameWasReady, "decoder reports frame ready after the last byte");
    check(decoded.tag == 0x05, "decoded tag matches original tag");
    check(decoded.value == value, "decoded value bytes match original value bytes");
}

static void testWrongCrc()
{
    std::printf("\nTest: Wrong CRC is rejected\n");

    std::vector<uint8_t> value = { 0xAB };
    std::vector<uint8_t> frame = tlv::encodeFrame(0x01, value);

    frame.back() ^= 0xFF;  /* corrupt the last CRC byte */

    tlv::Decoder decoder;
    tlv::Frame decoded;
    tlv::DecodeStatus status = tlv::DecodeStatus::InProgress;

    for (uint8_t b : frame)
    {
        status = decoder.feedByte(b, decoded);
    }

    check(status == tlv::DecodeStatus::Error, "decoder reports an error for a corrupted CRC");
}

static void testInvalidLengthIsRejected()
{
    std::printf("\nTest: A Length beyond the 256-byte cap is rejected\n");

    tlv::Decoder decoder;
    tlv::Frame decoded;

    decoder.feedByte(tlv::kSofByte, decoded);  /* SOF */
    decoder.feedByte(0x01, decoded);           /* Tag */
    decoder.feedByte(0xFF, decoded);           /* Length low byte */
    tlv::DecodeStatus status = decoder.feedByte(0xFF, decoded);  /* Length high byte -> 0xFFFF, far too big */

    check(status == tlv::DecodeStatus::Error, "an oversized Length is rejected immediately");
}

static void testGarbageBeforeSofIsIgnored()
{
    std::printf("\nTest: Garbage bytes before SOF are ignored\n");

    std::vector<uint8_t> value = { 0x7A };
    std::vector<uint8_t> frame = tlv::encodeFrame(0x02, value);
    std::vector<uint8_t> garbage = { 0x00, 0x11, 0x22, 0x99 };  /* none of these is SOF */

    tlv::Decoder decoder;
    tlv::Frame decoded;
    bool garbageWasClean = true;

    for (uint8_t b : garbage)
    {
        tlv::DecodeStatus status = decoder.feedByte(b, decoded);
        if (status != tlv::DecodeStatus::InProgress)
        {
            garbageWasClean = false;
        }
    }
    check(garbageWasClean, "garbage bytes before SOF never produce an error or a frame");

    bool frameWasReady = false;
    for (uint8_t b : frame)
    {
        if (decoder.feedByte(b, decoded) == tlv::DecodeStatus::FrameReady)
        {
            frameWasReady = true;
        }
    }
    check(frameWasReady, "the real frame after the garbage still decodes correctly");
}

static void testResyncAfterError()
{
    std::printf("\nTest: Decoder resynchronizes after a bad frame\n");

    std::vector<uint8_t> value = { 0x5A, 0x5B };
    std::vector<uint8_t> badFrame = tlv::encodeFrame(0x03, value);
    badFrame.back() ^= 0xFF;  /* corrupt its CRC */
    std::vector<uint8_t> goodFrame = tlv::encodeFrame(0x04, value);

    tlv::Decoder decoder;
    tlv::Frame decoded;
    bool sawError = false;

    for (uint8_t b : badFrame)
    {
        if (decoder.feedByte(b, decoded) == tlv::DecodeStatus::Error)
        {
            sawError = true;
        }
    }
    check(sawError, "the corrupted frame is reported as an error");

    bool frameWasReady = false;
    for (uint8_t b : goodFrame)
    {
        if (decoder.feedByte(b, decoded) == tlv::DecodeStatus::FrameReady)
        {
            frameWasReady = true;
        }
    }
    check(frameWasReady, "the following good frame still decodes correctly");
    check(decoded.tag == 0x04, "the decoded frame is the good one, not leftover data from the bad one");
}

static void testUnknownTagIsNotRejected()
{
    std::printf("\nTest: An unrecognized Tag value is still framed/decoded correctly\n");

    std::vector<uint8_t> value = { 0x01 };
    std::vector<uint8_t> frame = tlv::encodeFrame(0xFF, value);

    tlv::Decoder decoder;
    tlv::Frame decoded;
    bool frameWasReady = false;

    for (uint8_t b : frame)
    {
        if (decoder.feedByte(b, decoded) == tlv::DecodeStatus::FrameReady)
        {
            frameWasReady = true;
        }
    }

    check(frameWasReady, "frame with an unrecognized tag still decodes");
    check(decoded.tag == 0xFF, "the unrecognized tag value is preserved");
}

static void testMaximumFrameSize()
{
    std::printf("\nTest: Maximum frame size\n");

    std::vector<uint8_t> value(static_cast<size_t>(tlv::kMaxValueSize));
    for (size_t i = 0; i < value.size(); i++)
    {
        value[i] = static_cast<uint8_t>(i);
    }

    std::vector<uint8_t> frame = tlv::encodeFrame(0x06, value);
    check(frame.size() == static_cast<size_t>(tlv::kMaxFrameSize),
          "a value filling the max payload produces exactly a 256-byte frame");

    std::vector<uint8_t> tooBigValue(static_cast<size_t>(tlv::kMaxValueSize) + 1);
    std::vector<uint8_t> tooBigFrame = tlv::encodeFrame(0x06, tooBigValue);
    check(tooBigFrame.empty(), "a value one byte over the maximum is refused by the encoder");
}

static void testLittleEndianLengthAndCrc()
{
    std::printf("\nTest: Length and CRC fields are read low-byte-first (little-endian)\n");

    std::vector<uint8_t> tagLengthValue = { 0x09, 0x02, 0x00, 0x10, 0x20 };  /* tag, length lo/hi, 2 value bytes */
    uint16_t expectedCrc = tlv::calculateCrc16(tagLengthValue.data(), tagLengthValue.size());

    tlv::Decoder decoder;
    tlv::Frame decoded;

    decoder.feedByte(tlv::kSofByte, decoded);
    for (uint8_t b : tagLengthValue)
    {
        decoder.feedByte(b, decoded);
    }
    decoder.feedByte(static_cast<uint8_t>(expectedCrc & 0xFF), decoded);          /* crc low byte first */
    tlv::DecodeStatus status = decoder.feedByte(static_cast<uint8_t>((expectedCrc >> 8) & 0xFF), decoded);

    check(status == tlv::DecodeStatus::FrameReady, "feeding CRC low byte then high byte is accepted (little-endian)");
    check(decoded.value.size() == 2, "length built from low byte then high byte equals 2, not 512");
}

static void testZeroLengthValue()
{
    std::printf("\nTest: A frame with a zero-length Value works (e.g. a 'get time' request)\n");

    std::vector<uint8_t> emptyValue;
    std::vector<uint8_t> frame = tlv::encodeFrame(0x07, emptyValue);

    check(frame.size() == static_cast<size_t>(tlv::kFrameOverhead),
          "a zero-length value produces a frame of exactly the overhead size");

    tlv::Decoder decoder;
    tlv::Frame decoded;
    bool frameWasReady = false;

    for (uint8_t b : frame)
    {
        if (decoder.feedByte(b, decoded) == tlv::DecodeStatus::FrameReady)
        {
            frameWasReady = true;
        }
    }
    check(frameWasReady, "the zero-length frame decodes correctly");
    check(decoded.value.empty(), "decoded value is empty");
}

int main()
{
    std::printf("=== tlv_codec.cpp test suite ===\n");

    testCrcStandardVector();
    testEncodeDecodeRoundTrip();
    testWrongCrc();
    testInvalidLengthIsRejected();
    testGarbageBeforeSofIsIgnored();
    testResyncAfterError();
    testUnknownTagIsNotRejected();
    testMaximumFrameSize();
    testLittleEndianLengthAndCrc();
    testZeroLengthValue();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
