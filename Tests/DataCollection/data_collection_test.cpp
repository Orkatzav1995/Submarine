/*
 * data_collection_test.cpp
 *
 * Purpose:
 *   Stand-alone PC test program for CentralComputer/data_collection.h/.cpp.
 *   Wires a real Communication (never opened - no hardware needed) and a
 *   real in-memory DataStore (":memory:") together via DataCollection,
 *   then feeds hand-built frames through Communication::feedByte() (the
 *   same seam every other Communication-adjacent test uses) and checks
 *   the right rows land in the DataStore. Also drives the backfill flow:
 *   sends a request, feeds back chunk responses, and checks correlation
 *   (right requestId accepted, wrong one ignored), completion, and that a
 *   second backfill can't be started while one is already in flight.
 *
 * How to build and run (from this folder) - same two-step sqlite3.c
 * build as Tests/DataStore (see that test file for why):
 *   gcc -std=c11 -c "../../Common/ThirdParty/sqlite3/sqlite3.c" -o sqlite3.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c data_collection_test.cpp -o data_collection_test.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/data_collection.cpp" -o data_collection_impl.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/data_store.cpp" -o data_store_impl.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../CentralComputer/src/communication.cpp" -o communication_impl.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../CentralComputer/src/message.cpp" -o message.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../CentralComputer/src/serial_transport.cpp" -o serial_transport.o
 *   g++ -std=c++17 -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../Common/TLVCodec/tlv_codec.cpp" -o tlv_codec.o
 *   g++ data_collection_test.o data_collection_impl.o data_store_impl.o communication_impl.o message.o serial_transport.o tlv_codec.o sqlite3.o -o data_collection_test.exe
 *   ./data_collection_test.exe
 *
 * This file does NOT:
 *   - Open a real serial port or touch a real file on disk.
 */

#include "communication.h"
#include "data_collection.h"
#include "data_store.h"
#include "tlv_codec.h"
#include "tlv_common.h"
#include <cstdio>
#include <cstring>
#include <ctime>

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

static void feedFrame(communication::Communication &comm, const std::vector<uint8_t> &encodedFrame)
{
    for (uint8_t b : encodedFrame)
    {
        comm.feedByte(b);
    }
}

static std::vector<uint8_t> buildMeasurementSampleValue(uint32_t timestamp, float temperature, float humidity,
                                                          float light, float battery, uint8_t mode)
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

static std::vector<uint8_t> buildMeasurementChunkFrame(uint8_t requestId, uint16_t chunkSeq, uint8_t moreDataFlag,
                                                         const std::vector<message::MeasurementSample> &samples)
{
    std::vector<uint8_t> value;
    value.push_back(requestId);
    value.push_back(static_cast<uint8_t>(chunkSeq & 0xFF));
    value.push_back(static_cast<uint8_t>((chunkSeq >> 8) & 0xFF));
    value.push_back(moreDataFlag);
    for (const auto &sample : samples)
    {
        std::vector<uint8_t> record =
            buildMeasurementSampleValue(sample.timestamp, sample.temperature, sample.humidity, sample.light,
                                         sample.battery, sample.mode);
        value.insert(value.end(), record.begin(), record.end());
    }
    return tlv::encodeFrame(TAG_MEASUREMENT_CHUNK_RESPONSE, value);
}

static uint32_t CurrentUnixTime()
{
    return static_cast<uint32_t>(std::time(nullptr));
}

/* Fresh Communication + DataStore + DataCollection, wired together, ready
   for a test to feed frames into. The port is never opened - none of this
   needs real hardware. */
struct TestRig
{
    communication::Communication comm;
    data_store::DataStore store;
    data_collection::DataCollection collection{comm, store};

    TestRig() { store.open(":memory:"); }
};

static void testKeepAliveAndDataReportAreSaved()
{
    std::printf("\nTest: TAG_KEEPALIVE and TAG_DATA_REPORT are both saved as measurements\n");

    TestRig rig;

    /* Realistic, near-current timestamps - DataCollection prunes anything
       older than 7 days based on REAL wall-clock time on every insert
       (by design, see testRetentionUsesWallClockNotRecordTimestamp), so a
       toy timestamp like 1000 (1970) would be deleted immediately after
       being inserted, exactly as intended in production. */
    uint32_t t1 = CurrentUnixTime() - 200;
    uint32_t t2 = CurrentUnixTime() - 100;

    feedFrame(rig.comm, tlv::encodeFrame(TAG_KEEPALIVE, buildMeasurementSampleValue(t1, 20.0f, 50.0f, 300.0f, 3.7f, MODE_NORMAL)));
    feedFrame(rig.comm, tlv::encodeFrame(TAG_DATA_REPORT, buildMeasurementSampleValue(t2, 21.0f, 51.0f, 310.0f, 3.6f, MODE_WARNING)));

    std::vector<message::MeasurementSample> all = rig.store.getMeasurementsInRange(0, 4000000000u);
    check(all.size() == 2, "both a keepalive and a data report land in the measurements table");
    check(all[0].timestamp == t1 && all[1].timestamp == t2, "both rows have the right timestamps");
}

static void testAllFourEventTypesAreSaved()
{
    std::printf("\nTest: all 4 event callback types are saved with sensible descriptions\n");

    TestRig rig;
    uint32_t now = CurrentUnixTime();

    std::vector<uint8_t> modeValue;
    appendUint32LE(modeValue, now - 400);
    modeValue.push_back(MODE_NORMAL);
    modeValue.push_back(MODE_WARNING);
    feedFrame(rig.comm, tlv::encodeFrame(TAG_EVENT_MODE_TRANSITION, modeValue));

    std::vector<uint8_t> detectValue;
    appendUint32LE(detectValue, now - 300);
    detectValue.push_back(1);
    feedFrame(rig.comm, tlv::encodeFrame(TAG_EVENT_OBJECT_DETECTION, detectValue));

    std::vector<uint8_t> configValue;
    appendUint32LE(configValue, now - 200);
    feedFrame(rig.comm, tlv::encodeFrame(TAG_EVENT_CONFIG_CHANGED, configValue));

    std::vector<uint8_t> startupValue;
    appendUint32LE(startupValue, now - 100);
    startupValue.push_back(1);
    feedFrame(rig.comm, tlv::encodeFrame(TAG_EVENT_STARTUP, startupValue));

    std::vector<message::EventRecord> all = rig.store.getEventsInRange(0, 4000000000u);
    check(all.size() == 4, "all 4 events land in the events table");
    check(all[0].eventType == TAG_EVENT_MODE_TRANSITION && all[0].description == "Mode change: Normal -> Warning",
          "mode transition description is human-readable");
    check(all[1].eventType == TAG_EVENT_OBJECT_DETECTION && all[1].description == "Object detected",
          "object detection description matches the flag");
    check(all[2].eventType == TAG_EVENT_CONFIG_CHANGED && all[2].description == "Configuration changed",
          "config changed description is correct");
    check(all[3].eventType == TAG_EVENT_STARTUP && all[3].description == "Startup after watchdog reset",
          "startup description matches the watchdog flag");
}

static void testBackfillRequestSendsCorrectFrame()
{
    std::printf("\nTest: requestMeasurementBackfill sends the right frame and sets in-progress state\n");

    TestRig rig;

    check(!rig.collection.isMeasurementBackfillInProgress(), "not in progress before any request");
    check(rig.collection.requestMeasurementBackfill(5000, 6000), "requestMeasurementBackfill returns true");
    check(rig.collection.isMeasurementBackfillInProgress(), "now in progress");

    message::TimeRangeMessage expected;
    expected.requestId = 0; /* first request handed out by a fresh DataCollection */
    expected.startTime = 5000;
    expected.endTime = 6000;
    check(rig.comm.lastSentFrame == message::buildGetMeasurementsByRangeRequest(expected),
          "the exact frame sent matches an independently-built expectation");
}

static void testBackfillCannotOverlapItself()
{
    std::printf("\nTest: a second backfill of the same type can't start while one is pending\n");

    TestRig rig;

    check(rig.collection.requestMeasurementBackfill(0, 100), "first request succeeds");
    check(!rig.collection.requestMeasurementBackfill(200, 300), "second request is refused while the first is pending");
}

static void testBackfillChunksAreSavedAndCompletionFires()
{
    std::printf("\nTest: backfill chunks are saved as they arrive, completion callback fires on the last one\n");

    TestRig rig;
    bool completed = false;
    rig.collection.onMeasurementBackfillComplete = [&]() { completed = true; };

    rig.collection.requestMeasurementBackfill(0, 100000);

    /* Backfilled records are still typically recent (e.g. filling a gap
       from a brief CC outage) - a realistic, near-current timestamp here
       so the post-insert retention prune (based on real wall-clock time)
       doesn't delete them, same reasoning as the live-traffic tests above. */
    uint32_t now = CurrentUnixTime();

    /* First chunk: more data still coming. */
    message::MeasurementSample sample1;
    sample1.timestamp = now - 200;
    sample1.temperature = 15.0f;
    sample1.mode = MODE_NORMAL;
    feedFrame(rig.comm, buildMeasurementChunkFrame(0, 0, 1, {sample1}));

    check(rig.store.getMeasurementsInRange(0, 4000000000u).size() == 1, "first chunk's record is saved immediately");
    check(!completed, "completion callback has not fired yet - more data was flagged");
    check(rig.collection.isMeasurementBackfillInProgress(), "still in progress after a non-final chunk");

    /* Second chunk: this is the last one. */
    message::MeasurementSample sample2;
    sample2.timestamp = now - 100;
    sample2.temperature = 16.0f;
    sample2.mode = MODE_NORMAL;
    feedFrame(rig.comm, buildMeasurementChunkFrame(0, 1, 0, {sample2}));

    check(rig.store.getMeasurementsInRange(0, 4000000000u).size() == 2, "second chunk's record is also saved");
    check(completed, "completion callback fired after the final chunk");
    check(!rig.collection.isMeasurementBackfillInProgress(), "no longer in progress after completion");
}

static void testMismatchedRequestIdIsIgnored()
{
    std::printf("\nTest: a chunk response with the wrong (or no pending) requestId is ignored\n");

    TestRig rig;

    /* This record is never actually saved in either case below, so its
       timestamp being an ancient toy value doesn't matter here. */
    message::MeasurementSample sample;
    sample.timestamp = 1;

    /* No backfill requested at all - a stray chunk response must be ignored, not crash. */
    feedFrame(rig.comm, buildMeasurementChunkFrame(0, 0, 0, {sample}));
    check(rig.store.getMeasurementsInRange(0, 4000000000u).empty(),
          "a chunk response with nothing pending is ignored, not saved");

    /* A real backfill is pending under requestId 0; a chunk claiming a
       different id must also be ignored. */
    rig.collection.requestMeasurementBackfill(0, 100);
    feedFrame(rig.comm, buildMeasurementChunkFrame(99, 0, 0, {sample}));
    check(rig.store.getMeasurementsInRange(0, 4000000000u).empty(),
          "a chunk with a mismatched requestId is ignored while a different one is pending");
    check(rig.collection.isMeasurementBackfillInProgress(), "the real pending backfill is unaffected");
}

static void testRetentionUsesWallClockNotRecordTimestamp()
{
    std::printf("\nTest: retention pruning is based on real current time, not the inserted record's timestamp\n");

    TestRig rig;

    uint32_t eightDaysAgo = CurrentUnixTime() - (8u * 24u * 60u * 60u);

    /* Insert directly (bypassing DataCollection) so this old row exists
       without itself triggering a prune. */
    message::MeasurementSample oldSample;
    oldSample.timestamp = eightDaysAgo;
    oldSample.mode = MODE_NORMAL;
    rig.store.insertMeasurement(oldSample);

    message::EventRecord oldEvent;
    oldEvent.timestamp = eightDaysAgo;
    oldEvent.eventType = TAG_EVENT_STARTUP;
    oldEvent.description = "old";
    rig.store.insertEvent(oldEvent);

    check(rig.store.getMeasurementsInRange(0, 4000000000u).size() == 1, "the old measurement exists before any live traffic");

    /* Now feed one live, recent KEEPALIVE through DataCollection - even
       though ITS timestamp is recent (not 8 days old), the prune it
       triggers must still be based on real "now", which correctly
       removes the 8-day-old rows. */
    feedFrame(rig.comm, tlv::encodeFrame(TAG_KEEPALIVE, buildMeasurementSampleValue(CurrentUnixTime(), 20.0f, 50.0f, 300.0f, 3.7f, MODE_NORMAL)));

    std::vector<message::MeasurementSample> remainingMeasurements = rig.store.getMeasurementsInRange(0, 4000000000u);
    check(remainingMeasurements.size() == 1, "the 8-day-old measurement was pruned, only the fresh one remains");

    std::vector<message::EventRecord> remainingEvents = rig.store.getEventsInRange(0, 4000000000u);
    check(remainingEvents.empty(), "the 8-day-old event was pruned too, even though no new event was inserted");
}

int main()
{
    std::printf("=== CentralComputer data_collection.cpp test suite ===\n");

    testKeepAliveAndDataReportAreSaved();
    testAllFourEventTypesAreSaved();
    testBackfillRequestSendsCorrectFrame();
    testBackfillCannotOverlapItself();
    testBackfillChunksAreSavedAndCompletionFires();
    testMismatchedRequestIdIsIgnored();
    testRetentionUsesWallClockNotRecordTimestamp();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
