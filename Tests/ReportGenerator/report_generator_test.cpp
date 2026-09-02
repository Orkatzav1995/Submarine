/*
 * report_generator_test.cpp
 *
 * Purpose:
 *   Stand-alone PC test program for CentralComputer/report_generator.h/.cpp.
 *   Inserts known rows directly into a real, private in-memory DataStore
 *   (":memory:"), then checks generateMeasurementReport/generateEventReport
 *   compute the right counts, min/max/average, and per-type breakdowns.
 *
 * How to build and run (from this folder) - same two-step sqlite3.c build
 * as Tests/DataStore (see that test file for why):
 *   gcc -std=c11 -c "../../Common/ThirdParty/sqlite3/sqlite3.c" -o sqlite3.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c report_generator_test.cpp -o report_generator_test.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/report_generator.cpp" -o report_generator_impl.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/data_store.cpp" -o data_store_impl.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../CentralComputer/src/message.cpp" -o message.o
 *   g++ -std=c++17 -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../Common/TLVCodec/tlv_codec.cpp" -o tlv_codec.o
 *   g++ report_generator_test.o report_generator_impl.o data_store_impl.o message.o tlv_codec.o sqlite3.o -o report_generator_test.exe
 *   ./report_generator_test.exe
 *
 * This file does NOT:
 *   - Touch a real file on disk (uses ":memory:").
 */

#include "data_store.h"
#include "report_generator.h"
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

static bool nearlyEqual(float a, float b)
{
    float diff = (a > b) ? (a - b) : (b - a);
    return diff < 0.001f;
}

static message::MeasurementSample makeSample(uint32_t timestamp, float temperature, float humidity, float light,
                                              float battery, uint8_t mode)
{
    message::MeasurementSample sample;
    sample.timestamp = timestamp;
    sample.temperature = temperature;
    sample.humidity = humidity;
    sample.light = light;
    sample.battery = battery;
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

static void testMeasurementReportOnEmptyRange()
{
    std::printf("\nTest: generateMeasurementReport on an empty range\n");

    data_store::DataStore store;
    store.open(":memory:");

    report_generator::MeasurementReport report = report_generator::generateMeasurementReport(store, 0, 1000);

    check(report.totalCount == 0, "totalCount is 0");
    check(report.normalCount == 0 && report.warningCount == 0 && report.errorCount == 0, "all mode counts are 0");
    check(report.temperature.min == 0.0f && report.temperature.max == 0.0f && report.temperature.average == 0.0f,
          "sensor stats default to 0 when there's no data, not garbage or a crash");
}

static void testMeasurementReportCountsAndStats()
{
    std::printf("\nTest: generateMeasurementReport computes correct counts and min/max/average\n");

    data_store::DataStore store;
    store.open(":memory:");

    store.insertMeasurement(makeSample(100, 10.0f, 40.0f, 100.0f, 3.0f, MODE_NORMAL));
    store.insertMeasurement(makeSample(200, 20.0f, 50.0f, 200.0f, 3.5f, MODE_NORMAL));
    store.insertMeasurement(makeSample(300, 30.0f, 60.0f, 300.0f, 4.0f, MODE_WARNING));
    store.insertMeasurement(makeSample(400, 40.0f, 70.0f, 400.0f, 2.5f, MODE_ERROR));

    report_generator::MeasurementReport report = report_generator::generateMeasurementReport(store, 0, 100000);

    check(report.totalCount == 4, "totalCount matches the number of inserted rows");
    check(report.normalCount == 2 && report.warningCount == 1 && report.errorCount == 1,
          "mode counts match (2 Normal, 1 Warning, 1 Error)");

    check(nearlyEqual(report.temperature.min, 10.0f) && nearlyEqual(report.temperature.max, 40.0f)
              && nearlyEqual(report.temperature.average, 25.0f),
          "temperature min/max/average are correct");
    check(nearlyEqual(report.battery.min, 2.5f) && nearlyEqual(report.battery.max, 4.0f)
              && nearlyEqual(report.battery.average, 3.25f),
          "battery min/max/average are correct");
}

static void testMeasurementReportRespectsTimeRange()
{
    std::printf("\nTest: generateMeasurementReport only includes rows within the given range\n");

    data_store::DataStore store;
    store.open(":memory:");

    store.insertMeasurement(makeSample(100, 10.0f, 0, 0, 0, MODE_NORMAL));
    store.insertMeasurement(makeSample(500, 50.0f, 0, 0, 0, MODE_NORMAL));
    store.insertMeasurement(makeSample(900, 90.0f, 0, 0, 0, MODE_NORMAL));

    report_generator::MeasurementReport narrow = report_generator::generateMeasurementReport(store, 200, 700);
    check(narrow.totalCount == 1, "only the row inside [200, 700] is counted");
    check(nearlyEqual(narrow.temperature.average, 50.0f), "the single included row's temperature is the average");
}

static void testEventReportOnEmptyRange()
{
    std::printf("\nTest: generateEventReport on an empty range\n");

    data_store::DataStore store;
    store.open(":memory:");

    report_generator::EventReport report = report_generator::generateEventReport(store, 0, 1000);

    check(report.totalCount == 0, "totalCount is 0");
    check(report.countsByType.empty(), "countsByType is empty, not garbage or a crash");
}

static void testEventReportCountsByType()
{
    std::printf("\nTest: generateEventReport breaks down counts by event type\n");

    data_store::DataStore store;
    store.open(":memory:");

    store.insertEvent(makeEvent(100, TAG_EVENT_STARTUP, "Startup"));
    store.insertEvent(makeEvent(200, TAG_EVENT_OBJECT_DETECTION, "Object detected"));
    store.insertEvent(makeEvent(300, TAG_EVENT_OBJECT_DETECTION, "Object cleared"));
    store.insertEvent(makeEvent(400, TAG_EVENT_MODE_TRANSITION, "Mode change: Normal -> Warning"));
    store.insertEvent(makeEvent(500, TAG_EVENT_OBJECT_DETECTION, "Object detected"));

    report_generator::EventReport report = report_generator::generateEventReport(store, 0, 100000);

    check(report.totalCount == 5, "totalCount matches the number of inserted rows");
    check(report.countsByType.size() == 3, "3 distinct event types were seen");

    uint32_t startupCount = 0, detectionCount = 0, modeCount = 0;
    for (const auto &entry : report.countsByType)
    {
        if (entry.eventType == TAG_EVENT_STARTUP) startupCount = entry.count;
        if (entry.eventType == TAG_EVENT_OBJECT_DETECTION) detectionCount = entry.count;
        if (entry.eventType == TAG_EVENT_MODE_TRANSITION) modeCount = entry.count;
    }
    check(startupCount == 1, "1 startup event");
    check(detectionCount == 3, "3 object-detection events");
    check(modeCount == 1, "1 mode-transition event");
}

int main()
{
    std::printf("=== CentralComputer report_generator.cpp test suite ===\n");

    testMeasurementReportOnEmptyRange();
    testMeasurementReportCountsAndStats();
    testMeasurementReportRespectsTimeRange();
    testEventReportOnEmptyRange();
    testEventReportCountsByType();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
