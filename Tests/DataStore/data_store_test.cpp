/*
 * data_store_test.cpp
 *
 * Purpose:
 *   Stand-alone PC test program for CentralComputer/data_store.h/.cpp.
 *   Uses a real, private in-memory SQLite database (":memory:") - no files
 *   are left on disk, and no mocking: this exercises the actual SQLite
 *   engine, compiled from the vendored amalgamation in
 *   Common/ThirdParty/sqlite3/.
 *
 * How to build and run (from this folder):
 *   IMPORTANT: sqlite3.c must be compiled as C (gcc), not C++ - g++ forces
 *   C++ rules onto any file it compiles regardless of its .c extension,
 *   and sqlite3.c relies on implicit void*-to-T* conversions that are
 *   legal in C but not in C++. So this is a two-step build: compile
 *   sqlite3.c with gcc, everything else with g++, then link together.
 *
 *   gcc -std=c11 -c "../../Common/ThirdParty/sqlite3/sqlite3.c" -o sqlite3.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c data_store_test.cpp -o data_store_test.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/data_store.cpp" -o data_store_impl.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../CentralComputer/src/message.cpp" -o message.o
 *   g++ -std=c++17 -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../Common/TLVCodec/tlv_codec.cpp" -o tlv_codec.o
 *   g++ data_store_test.o data_store_impl.o message.o tlv_codec.o sqlite3.o -o data_store_test.exe
 *   ./data_store_test.exe
 *
 * This file does NOT:
 *   - Touch a real file on disk (uses ":memory:").
 *   - Use a system SQLite installation or any DLL - sqlite3.c is compiled
 *     straight into this test executable, from the vendored amalgamation.
 */

#include "data_store.h"
#include "message.h"
#include "tlv_common.h"
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

static message::MeasurementSample makeSample(uint32_t timestamp, float temperature, uint8_t mode)
{
    message::MeasurementSample sample;
    sample.timestamp = timestamp;
    sample.temperature = temperature;
    sample.humidity = 50.0f;
    sample.light = 300.0f;
    sample.battery = 3.7f;
    sample.mode = mode;
    return sample;
}

static message::EventRecord makeEvent(uint32_t timestamp, uint8_t eventType, const std::string &description)
{
    message::EventRecord event;
    event.timestamp = timestamp;
    event.eventType = eventType;
    event.description = description;
    return event;
}

static void testOpenCreatesTablesAndIsIdempotent()
{
    std::printf("\nTest: open() creates tables and can be called again without failing\n");

    data_store::DataStore store;
    check(!store.isOpen(), "isOpen() is false before open()");
    check(store.open(":memory:"), "open(\":memory:\") succeeds");
    check(store.isOpen(), "isOpen() is true after open()");

    /* Re-opening (e.g. as if the app restarted against the same path) must
       not fail just because the tables already exist. */
    check(store.open(":memory:"), "open() can be called again without failing");
}

static void testInsertAndQueryMeasurements()
{
    std::printf("\nTest: insertMeasurement / getMeasurementsInRange\n");

    data_store::DataStore store;
    store.open(":memory:");

    check(store.insertMeasurement(makeSample(1000, 20.0f, MODE_NORMAL)), "insert 1 succeeds");
    check(store.insertMeasurement(makeSample(2000, 25.0f, MODE_WARNING)), "insert 2 succeeds");
    check(store.insertMeasurement(makeSample(3000, 30.0f, MODE_ERROR)), "insert 3 succeeds");

    std::vector<message::MeasurementSample> all = store.getMeasurementsInRange(0, 999999);
    check(all.size() == 3, "range query covering everything returns 3 rows");
    check(all[0].timestamp == 1000 && all[1].timestamp == 2000 && all[2].timestamp == 3000,
          "rows come back ordered oldest-first");
    check(all[0].temperature == 20.0f && all[0].mode == MODE_NORMAL, "first row's fields round-trip correctly");

    std::vector<message::MeasurementSample> narrow = store.getMeasurementsInRange(1500, 2500);
    check(narrow.size() == 1 && narrow[0].timestamp == 2000, "a narrower range returns only the matching row");

    std::vector<message::MeasurementSample> none = store.getMeasurementsInRange(5000, 6000);
    check(none.empty(), "a range with no matches returns an empty vector");

    std::vector<message::MeasurementSample> inclusive = store.getMeasurementsInRange(1000, 1000);
    check(inclusive.size() == 1, "range bounds are inclusive on both ends");
}

static void testInsertAndQueryEvents()
{
    std::printf("\nTest: insertEvent / getEventsInRange\n");

    data_store::DataStore store;
    store.open(":memory:");

    check(store.insertEvent(makeEvent(100, TAG_EVENT_STARTUP, "Startup after watchdog reset")),
          "insert 1 succeeds");
    check(store.insertEvent(makeEvent(200, TAG_EVENT_OBJECT_DETECTION, "Object detected")), "insert 2 succeeds");

    std::vector<message::EventRecord> all = store.getEventsInRange(0, 999999);
    check(all.size() == 2, "range query returns both rows");
    check(all[0].eventType == TAG_EVENT_STARTUP && all[0].description == "Startup after watchdog reset",
          "first row's fields round-trip correctly, including the description string");
    check(all[1].eventType == TAG_EVENT_OBJECT_DETECTION && all[1].description == "Object detected",
          "second row's fields round-trip correctly");
}

static void testMeasurementsAndEventsAreSeparateTables()
{
    std::printf("\nTest: measurements and events are stored independently\n");

    data_store::DataStore store;
    store.open(":memory:");

    store.insertMeasurement(makeSample(1, 1.0f, MODE_NORMAL));
    store.insertEvent(makeEvent(1, TAG_EVENT_STARTUP, "Startup"));

    check(store.getMeasurementsInRange(0, 10).size() == 1, "one measurement present");
    check(store.getEventsInRange(0, 10).size() == 1, "one event present, independently of the measurement");
}

static void testPruneOlderThan()
{
    std::printf("\nTest: pruneOlderThan removes only rows older than the cutoff, in both tables\n");

    data_store::DataStore store;
    store.open(":memory:");

    store.insertMeasurement(makeSample(1000, 1.0f, MODE_NORMAL));
    store.insertMeasurement(makeSample(2000, 2.0f, MODE_NORMAL));
    store.insertMeasurement(makeSample(3000, 3.0f, MODE_NORMAL));
    store.insertEvent(makeEvent(1000, TAG_EVENT_STARTUP, "old event"));
    store.insertEvent(makeEvent(3000, TAG_EVENT_STARTUP, "new event"));

    check(store.pruneOlderThan(2000), "pruneOlderThan succeeds");

    std::vector<message::MeasurementSample> remainingMeasurements = store.getMeasurementsInRange(0, 999999);
    check(remainingMeasurements.size() == 2, "the one measurement older than the cutoff was deleted");
    check(remainingMeasurements[0].timestamp == 2000 && remainingMeasurements[1].timestamp == 3000,
          "the two remaining measurements are the ones at/after the cutoff");

    std::vector<message::EventRecord> remainingEvents = store.getEventsInRange(0, 999999);
    check(remainingEvents.size() == 1 && remainingEvents[0].description == "new event",
          "the old event was deleted, the new one remains");
}

int main()
{
    std::printf("=== CentralComputer data_store.cpp test suite ===\n");

    testOpenCreatesTablesAndIsIdempotent();
    testInsertAndQueryMeasurements();
    testInsertAndQueryEvents();
    testMeasurementsAndEventsAreSeparateTables();
    testPruneOlderThan();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
