/*
 * communication_test.cpp
 *
 * Purpose:
 *   Stand-alone PC test program for CentralComputer/communication.h/.cpp.
 *   Drives the decode+dispatch logic directly via Communication::feedByte()
 *   with hand-built frames - no real COM port is opened, so this runs
 *   anywhere. For each of the 7 LNC->CC message types: builds a valid
 *   frame, registers a callback that records what it received, feeds the
 *   frame in byte-by-byte, and checks the right callback fired with the
 *   right fields. Also checks: an unregistered callback doesn't crash, an
 *   unknown tag is silently ignored, and a bad-CRC frame bumps
 *   decodeErrors without dispatching anything.
 *
 * How to build and run (from this folder):
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" communication_test.cpp "../../CentralComputer/src/communication.cpp" "../../CentralComputer/src/message.cpp" "../../CentralComputer/src/serial_transport.cpp" "../../Common/TLVCodec/tlv_codec.cpp" -o communication_test.exe
 *   ./communication_test.exe
 *
 * This file does NOT:
 *   - Open a real serial port.
 */

#include "communication.h"
#include "tlv_codec.h"
#include "tlv_common.h"
#include <cstdio>
#include <cstring>

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

static void appendUint32LE(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static void appendFloatLE(std::vector<uint8_t> &out, float v)
{
    uint8_t bytes[sizeof(float)];
    std::memcpy(bytes, &v, sizeof(float));
    for (uint8_t b : bytes)
    {
        out.push_back(b);
    }
}

/* Feeds a complete, already-encoded frame into comm one byte at a time,
   exactly as Communication::poll() would if the bytes arrived over a
   real serial port. */
static void feedFrame(communication::Communication &comm, const std::vector<uint8_t> &encodedFrame)
{
    for (uint8_t b : encodedFrame)
    {
        comm.feedByte(b);
    }
}

static std::vector<uint8_t> buildMeasurementSampleValue(uint32_t timestamp, float temperature,
                                                          float humidity, float light,
                                                          float battery, uint8_t mode)
{
    std::vector<uint8_t> value;
    appendUint32LE(value, timestamp);
    appendFloatLE(value, temperature);
    appendFloatLE(value, humidity);
    appendFloatLE(value, light);
    appendFloatLE(value, battery);
    value.push_back(mode);
    return value;
}

static void testKeepAliveDispatch()
{
    std::printf("\nTest: TAG_KEEPALIVE dispatches to onKeepAlive with correct fields\n");

    communication::Communication comm;
    bool called = false;
    message::MeasurementSample received;

    comm.callbacks.onKeepAlive = [&](const message::MeasurementSample &sample)
    {
        called = true;
        received = sample;
    };

    std::vector<uint8_t> value = buildMeasurementSampleValue(1000, 21.5f, 55.0f, 300.0f, 3.7f, MODE_NORMAL);
    feedFrame(comm, tlv::encodeFrame(TAG_KEEPALIVE, value));

    check(called, "onKeepAlive callback fired");
    check(received.timestamp == 1000, "timestamp correct");
    check(received.temperature == 21.5f, "temperature correct");
    check(received.humidity == 55.0f, "humidity correct");
    check(received.light == 300.0f, "light correct");
    check(received.battery == 3.7f, "battery correct");
    check(received.mode == MODE_NORMAL, "mode correct");
    check(comm.framesDispatched == 1, "framesDispatched incremented");
    check(comm.decodeErrors == 0, "decodeErrors stayed at 0");
}

static void testDataReportDispatch()
{
    std::printf("\nTest: TAG_DATA_REPORT dispatches to onDataReport, not onKeepAlive\n");

    communication::Communication comm;
    bool keepAliveCalled = false;
    bool dataReportCalled = false;

    comm.callbacks.onKeepAlive = [&](const message::MeasurementSample &) { keepAliveCalled = true; };
    comm.callbacks.onDataReport = [&](const message::MeasurementSample &) { dataReportCalled = true; };

    std::vector<uint8_t> value = buildMeasurementSampleValue(2000, 18.0f, 40.0f, 100.0f, 4.0f, MODE_WARNING);
    feedFrame(comm, tlv::encodeFrame(TAG_DATA_REPORT, value));

    check(dataReportCalled, "onDataReport callback fired");
    check(!keepAliveCalled, "onKeepAlive did NOT fire for a DATA_REPORT frame");
}

static void testTimestampMessageDispatch()
{
    std::printf("\nTest: TAG_SYSTEM_TIME_RESPONSE and TAG_EVENT_CONFIG_CHANGED dispatch separately\n");

    communication::Communication comm;
    uint32_t systemTimeSeen = 0;
    uint32_t configChangedSeen = 0;

    comm.callbacks.onSystemTimeResponse = [&](const message::TimestampMessage &m) { systemTimeSeen = m.timestamp; };
    comm.callbacks.onEventConfigChanged = [&](const message::TimestampMessage &m) { configChangedSeen = m.timestamp; };

    std::vector<uint8_t> value;
    appendUint32LE(value, 12345);
    feedFrame(comm, tlv::encodeFrame(TAG_SYSTEM_TIME_RESPONSE, value));

    std::vector<uint8_t> value2;
    appendUint32LE(value2, 67890);
    feedFrame(comm, tlv::encodeFrame(TAG_EVENT_CONFIG_CHANGED, value2));

    check(systemTimeSeen == 12345, "onSystemTimeResponse got the right timestamp");
    check(configChangedSeen == 67890, "onEventConfigChanged got the right timestamp");
}

static void testTimestampWithFlagDispatch()
{
    std::printf("\nTest: TAG_EVENT_OBJECT_DETECTION and TAG_EVENT_STARTUP carry their flag correctly\n");

    communication::Communication comm;
    message::TimestampWithFlagMessage detectionSeen;
    message::TimestampWithFlagMessage startupSeen;
    bool detectionCalled = false;
    bool startupCalled = false;

    comm.callbacks.onEventObjectDetection = [&](const message::TimestampWithFlagMessage &m)
    {
        detectionCalled = true;
        detectionSeen = m;
    };
    comm.callbacks.onEventStartup = [&](const message::TimestampWithFlagMessage &m)
    {
        startupCalled = true;
        startupSeen = m;
    };

    std::vector<uint8_t> detectValue;
    appendUint32LE(detectValue, 111);
    detectValue.push_back(1); /* detected */
    feedFrame(comm, tlv::encodeFrame(TAG_EVENT_OBJECT_DETECTION, detectValue));

    std::vector<uint8_t> startupValue;
    appendUint32LE(startupValue, 222);
    startupValue.push_back(1); /* followed a watchdog reset */
    feedFrame(comm, tlv::encodeFrame(TAG_EVENT_STARTUP, startupValue));

    check(detectionCalled && detectionSeen.timestamp == 111 && detectionSeen.flag == 1,
          "onEventObjectDetection fired with correct timestamp+flag");
    check(startupCalled && startupSeen.timestamp == 222 && startupSeen.flag == 1,
          "onEventStartup fired with correct timestamp+flag");
}

static void testModeTransitionDispatch()
{
    std::printf("\nTest: TAG_EVENT_MODE_TRANSITION carries old/new mode correctly\n");

    communication::Communication comm;
    message::ModeTransitionMessage seen;
    bool called = false;

    comm.callbacks.onEventModeTransition = [&](const message::ModeTransitionMessage &m)
    {
        called = true;
        seen = m;
    };

    std::vector<uint8_t> value;
    appendUint32LE(value, 333);
    value.push_back(MODE_NORMAL);
    value.push_back(MODE_WARNING);
    feedFrame(comm, tlv::encodeFrame(TAG_EVENT_MODE_TRANSITION, value));

    check(called && seen.timestamp == 333 && seen.oldMode == MODE_NORMAL && seen.newMode == MODE_WARNING,
          "onEventModeTransition fired with correct timestamp+oldMode+newMode");
}

static void testMeasurementChunkResponseDispatch()
{
    std::printf("\nTest: TAG_MEASUREMENT_CHUNK_RESPONSE dispatches to onMeasurementChunkResponse\n");

    communication::Communication comm;
    bool called = false;
    message::MeasurementChunkResponse received;

    comm.callbacks.onMeasurementChunkResponse = [&](const message::MeasurementChunkResponse &r)
    {
        called = true;
        received = r;
    };

    std::vector<uint8_t> value;
    value.push_back(5);    /* requestId */
    value.push_back(0x02); /* chunkSeq low */
    value.push_back(0x00); /* chunkSeq high -> 2 */
    value.push_back(1);    /* moreDataFlag = true */
    std::vector<uint8_t> record = buildMeasurementSampleValue(500, 22.0f, 45.0f, 200.0f, 3.8f, MODE_NORMAL);
    value.insert(value.end(), record.begin(), record.end());

    feedFrame(comm, tlv::encodeFrame(TAG_MEASUREMENT_CHUNK_RESPONSE, value));

    check(called, "onMeasurementChunkResponse callback fired");
    check(received.requestId == 5 && received.chunkSeq == 2 && received.moreDataFlag == true
              && received.records.size() == 1 && received.records[0].timestamp == 500,
          "received chunk fields correct");
}

static void testEventChunkResponseDispatch()
{
    std::printf("\nTest: TAG_EVENT_CHUNK_RESPONSE dispatches to onEventChunkResponse\n");

    communication::Communication comm;
    bool called = false;
    message::EventChunkResponse received;

    comm.callbacks.onEventChunkResponse = [&](const message::EventChunkResponse &r)
    {
        called = true;
        received = r;
    };

    std::vector<uint8_t> value;
    value.push_back(6);    /* requestId */
    value.push_back(0x00); /* chunkSeq low */
    value.push_back(0x00); /* chunkSeq high -> 0 */
    value.push_back(0);    /* moreDataFlag = false */
    appendUint32LE(value, 999);
    value.push_back(TAG_EVENT_STARTUP);
    std::vector<uint8_t> descriptionField(EVENT_DESCRIPTION_SIZE, 0);
    const char *text = "Startup";
    std::memcpy(descriptionField.data(), text, std::strlen(text));
    value.insert(value.end(), descriptionField.begin(), descriptionField.end());

    feedFrame(comm, tlv::encodeFrame(TAG_EVENT_CHUNK_RESPONSE, value));

    check(called, "onEventChunkResponse callback fired");
    check(received.requestId == 6 && received.records.size() == 1
              && received.records[0].timestamp == 999
              && received.records[0].eventType == TAG_EVENT_STARTUP
              && received.records[0].description == "Startup",
          "received chunk fields correct, including description string");
}

static void testUnregisteredCallbackDoesNotCrash()
{
    std::printf("\nTest: a recognized tag with no callback registered is handled without crashing\n");

    communication::Communication comm; /* no callbacks set at all */

    std::vector<uint8_t> value = buildMeasurementSampleValue(1, 1.0f, 1.0f, 1.0f, 1.0f, MODE_NORMAL);
    feedFrame(comm, tlv::encodeFrame(TAG_KEEPALIVE, value));

    check(comm.framesDispatched == 1, "framesDispatched still increments even with no callback registered");
}

static void testUnknownTagIsIgnored()
{
    std::printf("\nTest: an unknown/unhandled tag is silently ignored\n");

    communication::Communication comm;
    bool anyCallbackCalled = false;
    comm.callbacks.onKeepAlive = [&](const message::MeasurementSample &) { anyCallbackCalled = true; };

    /* 0x99 is not any known tag. */
    std::vector<uint8_t> value = {0xAA, 0xBB};
    feedFrame(comm, tlv::encodeFrame(0x99, value));

    check(!anyCallbackCalled, "no callback fired for an unrecognized tag");
    check(comm.framesDispatched == 0, "framesDispatched did not increment for an unrecognized tag");
    check(comm.decodeErrors == 0, "an unrecognized (but well-formed) tag is not a decode error");
}

static void testBadCrcIsCountedAsDecodeError()
{
    std::printf("\nTest: a corrupted frame bumps decodeErrors and dispatches nothing\n");

    communication::Communication comm;
    bool anyCallbackCalled = false;
    comm.callbacks.onKeepAlive = [&](const message::MeasurementSample &) { anyCallbackCalled = true; };

    std::vector<uint8_t> value = buildMeasurementSampleValue(1, 1.0f, 1.0f, 1.0f, 1.0f, MODE_NORMAL);
    std::vector<uint8_t> frame = tlv::encodeFrame(TAG_KEEPALIVE, value);
    frame.back() ^= 0xFF; /* corrupt the last CRC byte */
    feedFrame(comm, frame);

    check(!anyCallbackCalled, "no callback fired for a corrupted frame");
    check(comm.framesDispatched == 0, "framesDispatched did not increment for a corrupted frame");
    check(comm.decodeErrors == 1, "decodeErrors incremented exactly once");
}

static void testSendFrameFailsWhenPortNotOpen()
{
    std::printf("\nTest: sendFrame fails cleanly when the port was never opened\n");

    communication::Communication comm;
    std::vector<uint8_t> someFrame = tlv::encodeFrame(TAG_GET_SYSTEM_TIME_REQUEST, {});

    check(!comm.isOpen(), "isOpen() is false before open() is ever called");
    check(!comm.sendFrame(someFrame), "sendFrame returns false when the port isn't open");
}

int main()
{
    std::printf("=== CentralComputer communication.cpp test suite ===\n");

    testKeepAliveDispatch();
    testDataReportDispatch();
    testTimestampMessageDispatch();
    testTimestampWithFlagDispatch();
    testModeTransitionDispatch();
    testMeasurementChunkResponseDispatch();
    testEventChunkResponseDispatch();
    testUnregisteredCallbackDoesNotCrash();
    testUnknownTagIsIgnored();
    testBadCrcIsCountedAsDecodeError();
    testSendFrameFailsWhenPortNotOpen();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
