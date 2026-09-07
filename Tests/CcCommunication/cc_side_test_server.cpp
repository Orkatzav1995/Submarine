/*
 * cc_side_test_server.cpp
 *
 * Purpose:
 *   NOT a test file itself - this is the CC-side test double for
 *   cc_communication_test.cpp's real end-to-end integration checks. It
 *   uses 100% real CC production code (gs_communication::GsCommunication
 *   + data_store::DataStore), pre-populated with a fixed, documented
 *   dataset, and listens on a real TCP port as its own separate OS
 *   process.
 *
 *   WHY THIS IS A SEPARATE EXECUTABLE, NOT JUST ANOTHER FILE LINKED INTO
 *   THE SAME TEST BINARY AS cc_communication_test.cpp:
 *   GroundStation/include/message.h and CentralComputer/include/message.h
 *   both define an identically-named type, message::TimeRangeMessage (and
 *   several others) - two INDEPENDENT struct definitions sharing the
 *   exact same fully-qualified name, by deliberate design (see either
 *   file's own header comment: "never share Application-facing struct
 *   definitions across executables"). Linking gs_communication.cpp
 *   (which needs CC's message.o) and cc_communication.cpp (which needs
 *   GS's message.o) into ONE executable fails at link time with
 *   "multiple definition of message::buildGetMeasurementsByRangeRequest(...)"
 *   - a correct, hard linker error, not a workaround-able inconvenience:
 *   it is the exact guarantee the "separate message layers" design
 *   promises. Real CC and real GS are always two separate binaries in
 *   production; this test fixture honors that by genuinely being a
 *   second, separate binary too - two real processes talking over a
 *   real loopback socket, exactly like production, instead of one
 *   process pretending to be both.
 *
 * Usage:
 *   cc_side_test_server.exe <port> <durationSeconds>
 *   Opens an in-memory DataStore, inserts the fixed dataset below, starts
 *   listening on 127.0.0.1:<port>, prints "READY" once listening, polls
 *   for exactly <durationSeconds>, then closes and exits 0.
 *
 * Fixed dataset (must stay in sync with cc_communication_test.cpp's own
 * expectations - see that file's integration test for which check uses
 * which range):
 *   Measurements: 1000, 1010, 1020 (single-chunk range check) + 5000
 *     (deliberately out of range) + 3000..3013 inclusive, 14 records
 *     (multi-chunk: MAX_MEASUREMENTS_PER_CHUNK = 11, so this splits into
 *     an 11-record chunk and a 3-record chunk).
 *   Events: 2000, 2010 (+ 9000 out of range) + 4000..4007 inclusive, 8
 *     records (multi-chunk: MAX_EVENTS_PER_CHUNK = 6, splitting into a
 *     6-record chunk and a 2-record chunk).
 */

#include "data_store.h"
#include "gs_communication.h"
#include "tlv_common.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {

message::MeasurementSample makeMeasurementSample(uint32_t timestamp)
{
    message::MeasurementSample sample;
    sample.timestamp = timestamp;
    sample.temperature = 20.0f + static_cast<float>(timestamp % 10);
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

}  // namespace

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: %s <port> <durationSeconds>\n", argv[0]);
        return 1;
    }

    uint16_t port = static_cast<uint16_t>(std::atoi(argv[1]));
    int durationSeconds = std::atoi(argv[2]);

    data_store::DataStore store;
    if (!store.open(":memory:"))
    {
        std::fprintf(stderr, "FATAL: could not open in-memory DataStore\n");
        return 1;
    }

    store.insertMeasurement(makeMeasurementSample(1000));
    store.insertMeasurement(makeMeasurementSample(1010));
    store.insertMeasurement(makeMeasurementSample(1020));
    store.insertMeasurement(makeMeasurementSample(5000));
    for (int i = 0; i < 14; i++)
    {
        store.insertMeasurement(makeMeasurementSample(3000u + static_cast<uint32_t>(i)));
    }

    store.insertEvent(makeEventRecord(2000));
    store.insertEvent(makeEventRecord(2010));
    store.insertEvent(makeEventRecord(9000));
    for (int i = 0; i < 8; i++)
    {
        store.insertEvent(makeEventRecord(4000u + static_cast<uint32_t>(i)));
    }

    gs_communication::GsCommunication gsComm(store);
    if (!gsComm.startListening(port))
    {
        std::fprintf(stderr, "FATAL: could not listen on port %u\n", port);
        return 1;
    }

    std::printf("READY\n");
    std::fflush(stdout);

    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count()
           < durationSeconds)
    {
        gsComm.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    gsComm.close();
    store.close();
    return 0;
}
