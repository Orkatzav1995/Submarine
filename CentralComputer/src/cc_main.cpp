/*
 * cc_main.cpp
 *
 * Purpose:
 *   The Central Computer's real entry point - the first file that
 *   constructs Communication, DataStore, DataCollection, ManagementCommand,
 *   and report_generator together and actually runs them, instead of each
 *   being exercised in isolation by its own test executable.
 *
 *   Unlike every other file in this codebase, this one is NOT unit-tested
 *   with a check()-based PC test - it's a real entry point with a real
 *   infinite loop talking to real hardware, which isn't something you
 *   unit-test the same way. Its "test" is manual exercise against the
 *   real board (see PROJECT_GUIDE.md).
 *
 * Layer:
 *   Application entry point. Sits above everything else, owns all of it.
 *
 * Responsibilities:
 *   - Construct the pieces in the right order (DataStore opened before
 *     DataCollection is built, so it's ready to receive inserts the
 *     moment DataCollection registers its callbacks).
 *   - Open the real COM10 link.
 *   - Run a single-threaded loop: poll() the connection (a real blocking
 *     ReadFile with a short timeout underneath - no busy-spinning, no
 *     osDelay-equivalent needed, unlike the LNC's bare-metal polling) and
 *     check for a keypress without blocking (_kbhit()/_getch() - this
 *     codebase is already Windows-only, see serial_transport.cpp).
 *   - Offer a tiny menu so the real hardware can actually be exercised on
 *     demand, since the LNC currently has no application layer of its own
 *     (see the menu actions' own comments below for exactly what each one
 *     does and does not prove).
 *   - Print visible status to the console - every module below this one
 *     is deliberately silent (no logging), so this is the one place
 *     that's allowed to talk to the operator.
 *
 * This file does NOT:
 *   - Add any new behavior beyond what the existing modules already do -
 *     it only calls their already-tested public functions.
 *   - Auto-sync time or auto-trigger a backfill at startup - the spec's
 *     Init concept (Sec 2.7) is LNC-only; nothing requires CC-side startup
 *     behavior, so none is invented here.
 *   - Use any config file or command-line arguments - the COM port name,
 *     baud rate, and database path are plain constants below.
 */

#include "communication.h"
#include "data_collection.h"
#include "data_store.h"
#include "management_command.h"
#include "report_generator.h"
#include "tlv_common.h"
#include <conio.h>
#include <cstdio>
#include <ctime>

namespace {

constexpr const char *kComPortName = "COM10";
constexpr uint32_t kBaudRate = 115200;
constexpr const char *kDatabasePath = "central_computer.db";
constexpr uint32_t kSecondsPerDay = 24u * 60u * 60u;

uint32_t CurrentUnixTime()
{
    return static_cast<uint32_t>(std::time(nullptr));
}

const char *ModeName(uint8_t mode)
{
    switch (mode)
    {
        case MODE_NORMAL:
            return "Normal";
        case MODE_WARNING:
            return "Warning";
        case MODE_ERROR:
            return "Error";
        default:
            return "Unknown";
    }
}

const char *EventTypeName(uint8_t eventType)
{
    switch (eventType)
    {
        case TAG_EVENT_MODE_TRANSITION:
            return "ModeTransition";
        case TAG_EVENT_OBJECT_DETECTION:
            return "ObjectDetection";
        case TAG_EVENT_CONFIG_CHANGED:
            return "ConfigChanged";
        case TAG_EVENT_STARTUP:
            return "Startup";
        default:
            return "Unknown";
    }
}

void printMenu()
{
    std::printf("\n--- Menu ---\n");
    std::printf("[1] Send test command: set the LNC's RTC to the current time\n");
    std::printf("[2] Request a measurement backfill (last hour)\n");
    std::printf("[3] Show status\n");
    std::printf("[4] Show a report (last 7 days)\n");
    std::printf("[5] Send test command: set temperature Normal range (HW-B-C-02)\n");
    std::printf("[6] Request the LNC's system time (HW-B-C-03)\n");
    std::printf("[q] Quit\n");
    std::printf("> ");
    std::fflush(stdout);
}

void printStatus(const communication::Communication &comm, const data_collection::DataCollection &collection)
{
    std::printf("\n--- Status ---\n");
    std::printf("Port open: %s\n", comm.isOpen() ? "yes" : "no");
    std::printf("Frames dispatched: %u\n", comm.framesDispatched);
    std::printf("Decode errors: %u\n", comm.decodeErrors);
    std::printf("Measurement backfill in progress: %s\n", collection.isMeasurementBackfillInProgress() ? "yes" : "no");
    std::printf("Event backfill in progress: %s\n", collection.isEventBackfillInProgress() ? "yes" : "no");
}

void printReport(data_store::DataStore &store)
{
    uint32_t now = CurrentUnixTime();
    uint32_t sevenDaysAgo = now - (7u * kSecondsPerDay);

    report_generator::MeasurementReport measurementReport =
        report_generator::generateMeasurementReport(store, sevenDaysAgo, now);
    report_generator::EventReport eventReport = report_generator::generateEventReport(store, sevenDaysAgo, now);

    std::printf("\n--- Report: last 7 days ---\n");
    std::printf("Measurements: %u total (Normal=%u, Warning=%u, Error=%u)\n", measurementReport.totalCount,
                 measurementReport.normalCount, measurementReport.warningCount, measurementReport.errorCount);
    std::printf("  Temperature: min=%.2f max=%.2f avg=%.2f\n", measurementReport.temperature.min,
                 measurementReport.temperature.max, measurementReport.temperature.average);
    std::printf("  Humidity:    min=%.2f max=%.2f avg=%.2f\n", measurementReport.humidity.min,
                 measurementReport.humidity.max, measurementReport.humidity.average);
    std::printf("  Light:       min=%.2f max=%.2f avg=%.2f\n", measurementReport.light.min,
                 measurementReport.light.max, measurementReport.light.average);
    std::printf("  Battery:     min=%.2f max=%.2f avg=%.2f\n", measurementReport.battery.min,
                 measurementReport.battery.max, measurementReport.battery.average);

    std::printf("Events: %u total\n", eventReport.totalCount);
    for (const report_generator::EventTypeCount &entry : eventReport.countsByType)
    {
        std::printf("  %s: %u\n", EventTypeName(entry.eventType), entry.count);
    }
}

/* [1]: the one genuinely meaningful real-hardware test available right
   now (see PROJECT_GUIDE.md) - the LNC has no application layer yet, so
   this can't get a reply, but sending it and confirming the LNC's own
   g_frames_decoded_ok counter (watched live in the STM32 debugger)
   increases by 1 proves CC -> LNC delivery, the direction the earlier
   hex-capture test didn't cover. */
void handleSendTestCommand(management_command::ManagementCommand &mgmt)
{
    uint32_t now = CurrentUnixTime();
    bool sent = mgmt.setRtcDateTime(now);
    std::printf("\n[cmd] setRtcDateTime(%u) -> sendFrame result: %s\n", now, sent ? "ok" : "FAILED (is the port open?)");
    std::printf("      Check the STM32 debugger's g_frames_decoded_ok counter - it should increase by 1.\n");
}

/* [2]: proves CC can correctly build and send a retrieval request. The
   LNC won't reply - it has no Configuration/Log module yet to answer
   from - so this is an honest, expected limit, not a bug. */
void handleRequestBackfill(data_collection::DataCollection &collection)
{
    uint32_t now = CurrentUnixTime();
    uint32_t oneHourAgo = now - 3600u;
    bool started = collection.requestMeasurementBackfill(oneHourAgo, now);
    std::printf("\n[cmd] requestMeasurementBackfill(%u, %u) -> %s\n", oneHourAgo, now,
                started ? "started" : "FAILED (already in progress?)");
    std::printf("      Note: the LNC has no application layer yet, so it will not reply - this only proves\n");
    std::printf("      CC builds and sends the request correctly.\n");
}

/* [5]: exercises B's "set limit" dispatch path (HW-B-C-02). The CC has
   8 of these commands; this sends just one (temperature Normal range)
   as a representative test, using a value deliberately different from
   Configuration's default (15.0/30.0) so the change is unambiguous. */
void handleSetTempNormalRange(management_command::ManagementCommand &mgmt)
{
    constexpr float kTestLow = 18.0f;
    constexpr float kTestHigh = 28.0f;
    bool sent = mgmt.setTempNormalRange(kTestLow, kTestHigh);
    std::printf("\n[cmd] setTempNormalRange(%.1f, %.1f) -> sendFrame result: %s\n", kTestLow, kTestHigh,
                sent ? "ok" : "FAILED (is the port open?)");
    std::printf("      Check the STM32 debugger: g_dispatch_set_limit_count should increase by 1, and\n");
    std::printf("      g_config_temp_normal_low/g_config_temp_normal_high should become %.1f/%.1f.\n", kTestLow,
                kTestHigh);
}

/* [6]: exercises B's GET_SYSTEM_TIME_REQUEST/RESPONSE round trip
   (HW-B-C-03). The reply arrives asynchronously via
   Communication::callbacks.onSystemTimeResponse, registered in main()
   below - it prints itself when it arrives, not here. */
void handleRequestSystemTime(management_command::ManagementCommand &mgmt)
{
    bool sent = mgmt.requestSystemTime();
    std::printf("\n[cmd] requestSystemTime() -> sendFrame result: %s\n", sent ? "ok" : "FAILED (is the port open?)");
    std::printf("      Check the STM32 debugger's g_dispatch_system_time_request_count (should increase by 1).\n");
    std::printf("      Waiting for the reply - it prints automatically when it arrives.\n");
}

}  // namespace

int main()
{
    std::printf("=== Central Computer ===\n");

    communication::Communication comm;

    data_store::DataStore store;
    if (!store.open(kDatabasePath))
    {
        std::printf("FATAL: could not open database '%s'\n", kDatabasePath);
        return 1;
    }
    std::printf("Database opened: %s\n", kDatabasePath);

    data_collection::DataCollection collection(comm, store);
    collection.onMeasurementBackfillComplete = []() { std::printf("\n[backfill] measurement backfill complete\n"); };
    collection.onEventBackfillComplete = []() { std::printf("\n[backfill] event backfill complete\n"); };

    /* HW-B-C-03: onSystemTimeResponse already existed as a callback slot
       on Communication but nothing registered a handler for it - this is
       the minimum needed to observe the reply to [6] below. */
    comm.callbacks.onSystemTimeResponse = [](const message::TimestampMessage &response)
    { std::printf("\n[reply] system time response: timestamp=%u\n", response.timestamp); };

    management_command::ManagementCommand mgmt(comm);

    if (comm.open(kComPortName, kBaudRate))
    {
        std::printf("Connected to %s at %u baud.\n", kComPortName, kBaudRate);
    }
    else
    {
        std::printf("WARNING: could not open %s - continuing without a live connection.\n", kComPortName);
        std::printf("(Menu options that talk to the LNC will simply fail; status/report still work.)\n");
    }

    printMenu();

    bool running = true;
    while (running)
    {
        comm.poll();

        if (_kbhit())
        {
            int key = _getch();
            switch (key)
            {
                case '1':
                    handleSendTestCommand(mgmt);
                    break;
                case '2':
                    handleRequestBackfill(collection);
                    break;
                case '3':
                    printStatus(comm, collection);
                    break;
                case '4':
                    printReport(store);
                    break;
                case '5':
                    handleSetTempNormalRange(mgmt);
                    break;
                case '6':
                    handleRequestSystemTime(mgmt);
                    break;
                case 'q':
                case 'Q':
                    running = false;
                    break;
                default:
                    break;
            }

            if (running)
            {
                printMenu();
            }
        }
    }

    std::printf("\nClosing...\n");
    comm.close();
    store.close();
    return 0;
}
