/*
 * cc_communication_test.cpp
 *
 * Purpose:
 *   Test program for GroundStation/cc_communication.h/.cpp
 *   (cc_communication::CcCommunication) - Step 5, the GS-side dispatch/
 *   orchestration layer. Two parts:
 *
 *   1) Unit-style tests via feedByte() - no socket needed - covering the
 *      request-in-progress guards, stale/mismatched-requestId rejection,
 *      single- and multi-chunk accumulation firing the completion
 *      callback correctly, decode-error counting, and unknown-tag drop.
 *      Response frames are hand-built (GS's message.h has no *builder*
 *      for chunk responses - only CC ever builds those - so these tests
 *      assemble the wire bytes directly, same technique
 *      Tests/GSMessage/gs_message_test.cpp already uses).
 *
 *   2) A REAL end-to-end integration test against a REAL, separately-
 *      running gs_communication::GsCommunication (see
 *      cc_side_test_server.cpp in this same folder) backed by a REAL
 *      in-memory DataStore. This is this project's first genuine
 *      cross-executable integration test - both sides' actual production
 *      code, nothing mocked.
 *
 *   IMPORTANT - TWO SEPARATE EXECUTABLES, NOT ONE:
 *   cc_side_test_server.cpp's own header comment explains in full why the
 *   real GsCommunication (CC side) cannot be linked into this same test
 *   binary: GS's and CC's message.h each define an independent
 *   message::TimeRangeMessage (and friends) - two same-named,
 *   differently-defined types - so linking both message.o's together
 *   fails with "multiple definition of ...". This test's integration
 *   checks therefore require cc_side_test_server.exe to ALREADY BE
 *   RUNNING on kIntegrationTestPort before this executable starts - see
 *   "How to build and run" below for the exact sequence.
 *
 * How to build and run (from this folder):
 *   Step A - build and start the CC-side test double (leave it running):
 *     g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c cc_side_test_server.cpp -o cc_side_test_server.o
 *     g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/gs_communication.cpp" -o cc_side_gs_communication.o
 *     g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/data_store.cpp" -o cc_side_data_store.o
 *     g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../CentralComputer/src/message.cpp" -o cc_side_message.o
 *     g++ -std=c++17 -I "../../CentralComputer/include" -c "../../CentralComputer/src/tcp_transport.cpp" -o cc_side_tcp_transport.o
 *     gcc -std=c11 -c "../../Common/ThirdParty/sqlite3/sqlite3.c" -o cc_side_sqlite3.o
 *     g++ -std=c++17 -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../Common/TLVCodec/tlv_codec.cpp" -o cc_side_tlv_codec.o
 *     g++ cc_side_test_server.o cc_side_gs_communication.o cc_side_data_store.o cc_side_message.o cc_side_tcp_transport.o cc_side_sqlite3.o cc_side_tlv_codec.o -lws2_32 -o cc_side_test_server.exe
 *     ./cc_side_test_server.exe 15970 60 &     (or run in a separate terminal)
 *   Step B - build and run this actual test client:
 *     g++ -std=c++17 -I "../../GroundStation/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" cc_communication_test.cpp "../../GroundStation/src/cc_communication.cpp" "../../GroundStation/src/message.cpp" "../../GroundStation/src/tcp_transport.cpp" "../../Common/TLVCodec/tlv_codec.cpp" -lws2_32 -o cc_communication_test.exe
 *     ./cc_communication_test.exe
 *
 * This file does NOT:
 *   - Test cc_side_test_server.cpp's own correctness beyond what its
 *     already-existing 77/77-check test suite (Tests/GsCommunication)
 *     already verifies - this file trusts that suite and only exercises
 *     the GS-side CcCommunication class against it.
 *   - Test gs_main.cpp (Step 7, doesn't exist) or any UI/display logic.
 */

#include "cc_communication.h"
#include "tlv_common.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

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

/* Must match cc_side_test_server.cpp's own usage instructions - start it
   with this exact port before running this executable's integration test. */
static const uint16_t kIntegrationTestPort = 15970;

namespace {

/* ============================================================
 * Hand-built response frames - GS's message.h has no *builder* for chunk
 * responses (only CC ever builds those), so these mirror
 * Tests/GSMessage/gs_message_test.cpp's own technique of assembling wire
 * bytes directly.
 * ============================================================ */

void appendUint32LE(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendFloatLE(std::vector<uint8_t> &out, float v)
{
    uint8_t bytes[sizeof(float)];
    std::memcpy(bytes, &v, sizeof(float));
    for (uint8_t b : bytes)
    {
        out.push_back(b);
    }
}

message::MeasurementSample makeMeasurementSample(uint32_t timestamp)
{
    message::MeasurementSample sample;
    sample.timestamp = timestamp;
    sample.temperature = 20.0f;
    sample.humidity = 40.0f;
    sample.light = 500.0f;
    sample.battery = 90.0f;
    sample.mode = 0;
    return sample;
}

message::EventRecord makeEventRecord(uint32_t timestamp)
{
    message::EventRecord record;
    record.timestamp = timestamp;
    record.eventType = TAG_EVENT_OBJECT_DETECTION;
    record.description = "test event";
    return record;
}

std::vector<uint8_t> buildMeasurementChunkResponseFrame(uint8_t requestId, uint16_t chunkSeq, bool moreDataFlag,
                                                          const std::vector<message::MeasurementSample> &records)
{
    std::vector<uint8_t> value;
    value.push_back(requestId);
    value.push_back(static_cast<uint8_t>(chunkSeq & 0xFF));
    value.push_back(static_cast<uint8_t>((chunkSeq >> 8) & 0xFF));
    value.push_back(moreDataFlag ? 1 : 0);
    for (const message::MeasurementSample &sample : records)
    {
        appendUint32LE(value, sample.timestamp);
        appendFloatLE(value, sample.temperature);
        appendFloatLE(value, sample.humidity);
        appendFloatLE(value, sample.light);
        appendFloatLE(value, sample.battery);
        value.push_back(sample.mode);
    }
    return tlv::encodeFrame(TAG_MEASUREMENT_CHUNK_RESPONSE, value);
}

std::vector<uint8_t> buildEventChunkResponseFrame(uint8_t requestId, uint16_t chunkSeq, bool moreDataFlag,
                                                    const std::vector<message::EventRecord> &records)
{
    std::vector<uint8_t> value;
    value.push_back(requestId);
    value.push_back(static_cast<uint8_t>(chunkSeq & 0xFF));
    value.push_back(static_cast<uint8_t>((chunkSeq >> 8) & 0xFF));
    value.push_back(moreDataFlag ? 1 : 0);
    for (const message::EventRecord &record : records)
    {
        appendUint32LE(value, record.timestamp);
        value.push_back(record.eventType);
        std::vector<uint8_t> descriptionField(EVENT_DESCRIPTION_SIZE, 0);
        size_t copyLength = std::min(record.description.size(), static_cast<size_t>(EVENT_DESCRIPTION_SIZE - 1));
        std::memcpy(descriptionField.data(), record.description.data(), copyLength);
        value.insert(value.end(), descriptionField.begin(), descriptionField.end());
    }
    return tlv::encodeFrame(TAG_EVENT_CHUNK_RESPONSE, value);
}

void feedFrame(cc_communication::CcCommunication &cc, const std::vector<uint8_t> &frameBytes)
{
    for (uint8_t b : frameBytes)
    {
        cc.feedByte(b);
    }
}

}  // namespace

/* ============================================================
 * Part 1: unit-style tests via feedByte() - no socket needed.
 * ============================================================ */

static void testInitialState()
{
    std::printf("\nTest: initial state of a freshly constructed CcCommunication\n");

    cc_communication::CcCommunication cc;
    check(!cc.isConnected(), "isConnected() is false before connect()");
    check(!cc.isMeasurementRequestInProgress(), "no measurement request in progress initially");
    check(!cc.isEventRequestInProgress(), "no event request in progress initially");
    check(cc.framesDispatched == 0, "framesDispatched starts at 0");
    check(cc.decodeErrors == 0, "decodeErrors starts at 0");
}

static void testRequestGuardsAgainstOverlapOfSameType()
{
    std::printf("\nTest: a second request of the SAME type while one is pending is refused; different types don't conflict\n");

    cc_communication::CcCommunication cc;

    check(cc.requestMeasurements(100, 200), "first measurement request is accepted");
    check(cc.isMeasurementRequestInProgress(), "measurement request now shows as in progress");
    check(!cc.requestMeasurements(300, 400), "a second, overlapping measurement request is refused");

    check(cc.requestEvents(100, 200), "an event request is accepted even while a measurement request is pending - independent trackers");
    check(cc.isEventRequestInProgress(), "event request now shows as in progress too");
    check(!cc.requestEvents(300, 400), "a second, overlapping event request is refused");
}

static void testSingleChunkFiresCallbackWithCorrectRecords()
{
    std::printf("\nTest: a single-chunk measurement response fires onMeasurementsReceived with the right records\n");

    cc_communication::CcCommunication cc;

    bool callbackFired = false;
    std::vector<message::MeasurementSample> received;
    cc.onMeasurementsReceived = [&](const std::vector<message::MeasurementSample> &records)
    {
        callbackFired = true;
        received = records;
    };

    check(cc.requestMeasurements(1000, 1020), "request is sent (requestId 0 - first request from a fresh object)");

    std::vector<message::MeasurementSample> records = {makeMeasurementSample(1000), makeMeasurementSample(1010)};
    feedFrame(cc, buildMeasurementChunkResponseFrame(0, 0, false, records));

    check(cc.framesDispatched == 1, "framesDispatched increments once");
    check(callbackFired, "onMeasurementsReceived fired");
    check(received.size() == 2 && received[0].timestamp == 1000 && received[1].timestamp == 1010,
          "the callback received exactly the 2 records, in order");
    check(!cc.isMeasurementRequestInProgress(), "the request is no longer in progress after the single chunk completes");
}

static void testMultiChunkAccumulatesBeforeFiring()
{
    std::printf("\nTest: a multi-chunk event response accumulates across chunks, firing only once, with everything merged\n");

    cc_communication::CcCommunication cc;

    int callbackFireCount = 0;
    std::vector<message::EventRecord> received;
    cc.onEventsReceived = [&](const std::vector<message::EventRecord> &records)
    {
        callbackFireCount++;
        received = records;
    };

    check(cc.requestEvents(4000, 4007), "request is sent (requestId 0)");

    std::vector<message::EventRecord> chunk0 = {makeEventRecord(4000), makeEventRecord(4001), makeEventRecord(4002)};
    feedFrame(cc, buildEventChunkResponseFrame(0, 0, true, chunk0));
    check(callbackFireCount == 0, "the callback has NOT fired yet - moreDataFlag was true");
    check(cc.isEventRequestInProgress(), "the request is still shown as in progress after a non-final chunk");

    std::vector<message::EventRecord> chunk1 = {makeEventRecord(4003), makeEventRecord(4004)};
    feedFrame(cc, buildEventChunkResponseFrame(0, 1, false, chunk1));
    check(callbackFireCount == 1, "the callback fires exactly once, after the final chunk");
    check(!cc.isEventRequestInProgress(), "the request is no longer in progress");
    check(received.size() == 5, "all 5 records across both chunks are present in the single callback invocation");
    if (received.size() == 5)
    {
        check(received[0].timestamp == 4000 && received[4].timestamp == 4004,
              "records appear in the correct order, chunk 0's records before chunk 1's");
    }
}

static void testMismatchedRequestIdIgnored()
{
    std::printf("\nTest: a chunk claiming a different (stale/unrelated) requestId is ignored\n");

    cc_communication::CcCommunication cc;

    bool callbackFired = false;
    cc.onMeasurementsReceived = [&](const std::vector<message::MeasurementSample> &) { callbackFired = true; };

    check(cc.requestMeasurements(1, 2), "request sent under requestId 0");

    std::vector<message::MeasurementSample> records = {makeMeasurementSample(1)};
    feedFrame(cc, buildMeasurementChunkResponseFrame(99, 0, false, records)); /* wrong requestId */

    check(cc.framesDispatched == 1, "framesDispatched still increments - the frame itself decoded and parsed fine");
    check(!callbackFired, "the callback did NOT fire - this chunk wasn't for the request we're waiting on");
    check(cc.isMeasurementRequestInProgress(), "the original request (id 0) is still shown as in progress, untouched");
}

static void testChunkWithNoRequestPendingIsIgnored()
{
    std::printf("\nTest: a chunk response arriving with no request pending at all is ignored, doesn't crash\n");

    cc_communication::CcCommunication cc;

    bool callbackFired = false;
    cc.onMeasurementsReceived = [&](const std::vector<message::MeasurementSample> &) { callbackFired = true; };

    std::vector<message::MeasurementSample> records = {makeMeasurementSample(1)};
    feedFrame(cc, buildMeasurementChunkResponseFrame(0, 0, false, records)); /* nothing was ever requested */

    check(cc.framesDispatched == 1, "the frame still decodes and parses fine");
    check(!callbackFired, "the callback does not fire - there was no pending request to match");
}

static void testUnknownTagDroppedSilently()
{
    std::printf("\nTest: a well-formed but irrelevant tag (KEEPALIVE) is dropped silently, not an error\n");

    cc_communication::CcCommunication cc;
    std::vector<uint8_t> value(21, 0);
    feedFrame(cc, tlv::encodeFrame(TAG_KEEPALIVE, value));

    check(cc.framesDispatched == 0, "framesDispatched stays at 0 - the tag has no case in dispatch()");
    check(cc.decodeErrors == 0, "decodeErrors stays at 0 - the frame itself decoded fine, it's just unrecognized");
}

static void testCorruptedFrameIncrementsDecodeErrors()
{
    std::printf("\nTest: a corrupted (bad CRC) frame increments decodeErrors, not framesDispatched\n");

    cc_communication::CcCommunication cc;

    std::vector<message::MeasurementSample> records = {makeMeasurementSample(1)};
    std::vector<uint8_t> frameBytes = buildMeasurementChunkResponseFrame(0, 0, false, records);
    frameBytes.back() ^= 0xFF; /* flip the last CRC byte */

    feedFrame(cc, frameBytes);

    check(cc.decodeErrors == 1, "decodeErrors increments once the corrupted frame is fully fed in");
    check(cc.framesDispatched == 0, "framesDispatched is NOT incremented for a corrupted frame");
}

static void testOperationsBeforeConnectDontCrash()
{
    std::printf("\nTest: requestMeasurements()/poll() before connect() don't crash\n");

    cc_communication::CcCommunication cc;
    check(cc.requestMeasurements(1, 2),
          "requestMeasurements() still returns true even with no connection - matches DataCollection's "
          "'don't gate on an unconfirmable low-level send result' convention");
    cc.poll(); /* must not crash */
    check(true, "poll() with no connection completes without crashing");
}

/* ============================================================
 * Part 2: real end-to-end integration test against a REAL, separately-
 * running gs_communication::GsCommunication (cc_side_test_server.exe).
 * ============================================================ */

namespace {

bool pollUntil(cc_communication::CcCommunication &cc, const bool &flag, int maxAttempts = 2000)
{
    for (int i = 0; i < maxAttempts && !flag; i++)
    {
        cc.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return flag;
}

}  // namespace

static void testRealEndToEndIntegration()
{
    std::printf("\n=== Integration test: real CcCommunication <-> real, separately-running GsCommunication ===\n");
    std::printf("(requires cc_side_test_server.exe already running on port %u - see this file's header)\n",
                 kIntegrationTestPort);

    cc_communication::CcCommunication cc;
    bool connected = cc.connect("127.0.0.1", kIntegrationTestPort);
    check(connected, "connects to the real, separately-running CC-side test double");
    if (!connected)
    {
        std::printf("  SKIPPING the rest of the integration test - is cc_side_test_server.exe running on port %u?\n",
                     kIntegrationTestPort);
        return;
    }

    /* --- single-chunk measurements --- */
    {
        bool done = false;
        std::vector<message::MeasurementSample> received;
        cc.onMeasurementsReceived = [&](const std::vector<message::MeasurementSample> &records)
        {
            done = true;
            received = records;
        };

        check(cc.requestMeasurements(1000, 1020), "single-chunk measurements request sent");
        check(pollUntil(cc, done), "a response arrives within the timeout");
        check(received.size() == 3, "exactly the 3 in-range records are returned (the 4th, at 5000, is excluded)");
        if (received.size() == 3)
        {
            check(received[0].timestamp == 1000 && received[1].timestamp == 1010 && received[2].timestamp == 1020,
                  "records are correct and in ascending order");
        }
    }

    /* --- single-chunk events --- */
    {
        bool done = false;
        std::vector<message::EventRecord> received;
        cc.onEventsReceived = [&](const std::vector<message::EventRecord> &records)
        {
            done = true;
            received = records;
        };

        check(cc.requestEvents(2000, 2010), "single-chunk events request sent");
        check(pollUntil(cc, done), "a response arrives within the timeout");
        check(received.size() == 2, "exactly the 2 in-range records are returned (the 3rd, at 9000, is excluded)");
        if (received.size() == 2)
        {
            check(received[0].timestamp == 2000 && received[1].timestamp == 2010, "records are correct and in order");
            check(received[0].description == "test event", "description round-trips correctly");
        }
    }

    /* --- multi-chunk measurements: 14 records, MAX_MEASUREMENTS_PER_CHUNK = 11, transparently merged --- */
    {
        bool done = false;
        std::vector<message::MeasurementSample> received;
        cc.onMeasurementsReceived = [&](const std::vector<message::MeasurementSample> &records)
        {
            done = true;
            received = records;
        };

        check(cc.requestMeasurements(3000, 3013), "multi-chunk measurements request sent");
        check(pollUntil(cc, done), "both wire chunks arrive and the callback fires exactly once");
        check(received.size() == 14, "all 14 records are present, transparently merged across 2 wire chunks");
        if (received.size() == 14)
        {
            bool inOrder = true;
            for (size_t i = 0; i < received.size(); i++)
            {
                inOrder = inOrder && (received[i].timestamp == 3000u + static_cast<uint32_t>(i));
            }
            check(inOrder, "all 14 records are present exactly once, in ascending order across the chunk boundary");
        }
    }

    /* --- multi-chunk events: 8 records, MAX_EVENTS_PER_CHUNK = 6 --- */
    {
        bool done = false;
        std::vector<message::EventRecord> received;
        cc.onEventsReceived = [&](const std::vector<message::EventRecord> &records)
        {
            done = true;
            received = records;
        };

        check(cc.requestEvents(4000, 4007), "multi-chunk events request sent");
        check(pollUntil(cc, done), "both wire chunks arrive and the callback fires exactly once");
        check(received.size() == 8, "all 8 records are present, transparently merged across 2 wire chunks");
    }

    /* --- zero-match still fires the callback, with an empty vector --- */
    {
        bool done = false;
        std::vector<message::MeasurementSample> received;
        received.push_back(makeMeasurementSample(123)); /* pre-seed with something, to prove it gets cleared */
        cc.onMeasurementsReceived = [&](const std::vector<message::MeasurementSample> &records)
        {
            done = true;
            received = records;
        };

        check(cc.requestMeasurements(1, 2), "a request matching nothing is sent");
        check(pollUntil(cc, done), "a response still arrives (CC always answers, even with zero matches)");
        check(received.empty(), "the callback fires with an empty vector, not silence");
    }

    /* --- overlap guard against the real server: deterministic, no timing race --- */
    {
        check(cc.requestMeasurements(1, 2), "a fresh measurement request is accepted");
        check(!cc.requestMeasurements(3, 4), "an immediate second measurement request is refused while the first is in flight");
        /* Drain the pending response so it doesn't bleed into a later check. */
        bool done = false;
        cc.onMeasurementsReceived = [&](const std::vector<message::MeasurementSample> &) { done = true; };
        pollUntil(cc, done);
    }

    cc.close();
    check(!cc.isConnected(), "isConnected() is false after close()");
}

int main()
{
    std::printf("=== cc_communication.cpp test suite (Step 5) ===\n");

    testInitialState();
    testRequestGuardsAgainstOverlapOfSameType();
    testSingleChunkFiresCallbackWithCorrectRecords();
    testMultiChunkAccumulatesBeforeFiring();
    testMismatchedRequestIdIgnored();
    testChunkWithNoRequestPendingIsIgnored();
    testUnknownTagDroppedSilently();
    testCorruptedFrameIncrementsDecodeErrors();
    testOperationsBeforeConnectDontCrash();

    testRealEndToEndIntegration();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
