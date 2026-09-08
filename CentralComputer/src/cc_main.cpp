/*
 * cc_main.cpp
 *
 * Purpose:
 *   The Central Computer's real entry point - the first file that starts a
 *   CentralComputer (Sec 5's facade, owning Communication, DataStore,
 *   DataCollection, ManagementCommand, and GsCommunication together) and
 *   actually runs it, instead of each piece being exercised in isolation
 *   by its own test executable.
 *
 *   Unlike every other file in this codebase, this one is NOT unit-tested
 *   with a check()-based PC test - it's a real entry point with a real
 *   infinite loop talking to real hardware, which isn't something you
 *   unit-test the same way. Its "test" is manual exercise against the
 *   real board (see PROJECT_GUIDE.md).
 *
 * Layer:
 *   Application entry point. Sits above CentralComputer, owns it.
 *
 * Responsibilities:
 *   - Construct one CentralComputer and start() it (this opens the
 *     database, listens for a Ground Station connection on TCP port 5000,
 *     and opens the real COM10 link, in that order - see
 *     central_computer.h).
 *   - Run a single-threaded loop: poll() the CentralComputer (which polls
 *     both the LNC connection - a real blocking ReadFile with a short
 *     timeout underneath, no busy-spinning, no osDelay-equivalent needed,
 *     unlike the LNC's bare-metal polling - and the Ground Station
 *     connection) and check for a keypress without blocking
 *     (_kbhit()/_getch() - this codebase is already Windows-only, see
 *     serial_transport.cpp).
 *   - Offer a tiny menu so the real hardware can actually be exercised on
 *     demand, since the LNC currently has no application layer of its own
 *     (see the menu actions' own comments below for exactly what each one
 *     does and does not prove).
 *   - Print visible status to the console - every module below this one
 *     is deliberately silent (no logging), so this is the one place
 *     that's allowed to talk to the operator. CentralComputer itself
 *     prints nothing (see its own header comment) - all FATAL/WARNING/
 *     Connected messages below are decided and printed here.
 *
 * This file does NOT:
 *   - Add any new behavior beyond what the existing modules already do -
 *     it only calls their already-tested public functions.
 *   - Construct Communication/DataStore/ManagementCommand/DataCollection/
 *     GsCommunication itself anymore - central_computer.h is the only CC
 *     module header this file needs, plus report_generator.h (used
 *     directly against DataStore for the report menu option).
 *   - Auto-sync time or auto-trigger a backfill at startup - the spec's
 *     Init concept (Sec 2.7) is LNC-only; nothing requires CC-side startup
 *     behavior, so none is invented here.
 *   - Use any config file or command-line arguments - the COM port name,
 *     baud rate, and database path are plain constants below.
 */

#include "central_computer.h"
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
constexpr uint16_t kGsPort = 5000; /* PROJECT_GUIDE.md's Step 4 agreed production port */

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
    std::printf("[7] Configure sensor limits\n");
    std::printf("[q] Quit\n");
    std::printf("> ");
    std::fflush(stdout);
}

void printStatus(const communication::Communication &comm, const data_collection::DataCollection &collection,
                  const gs_communication::GsCommunication &gsComm)
{
    std::printf("\n--- Status ---\n");
    std::printf("Port open: %s\n", comm.isOpen() ? "yes" : "no");
    std::printf("Frames dispatched: %u\n", comm.framesDispatched);
    std::printf("Decode errors: %u\n", comm.decodeErrors);
    std::printf("Measurement backfill in progress: %s\n", collection.isMeasurementBackfillInProgress() ? "yes" : "no");
    std::printf("Event backfill in progress: %s\n", collection.isEventBackfillInProgress() ? "yes" : "no");
    std::printf("GS listening: %s\n", gsComm.isListening() ? "yes" : "no");
    std::printf("GS client connected: %s\n", gsComm.isClientConnected() ? "yes" : "no");
    std::printf("GS frames dispatched: %u\n", gsComm.framesDispatched);
    std::printf("GS decode errors: %u\n", gsComm.decodeErrors);
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
    constexpr float kTestLow = 20.0f;
    constexpr float kTestHigh = 30.0f;
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

/* [7]: the real, repeatable sensor-limit configuration CLI - as opposed
   to [5]'s hardcoded HW-B-C-02 test probe. Lets the operator pick any of
   the 8 already-implemented "set limit" commands, enter its value(s),
   and send it - repeatable as many times as needed in one run. */
void handleSetSensorLimit(management_command::ManagementCommand &mgmt)
{
    std::printf("\nSelect sensor:\n");
    std::printf("1. Temperature\n");
    std::printf("2. Humidity\n");
    std::printf("3. Light\n");
    std::printf("4. Battery\n");
    std::printf("> ");
    std::fflush(stdout);
    int sensor = _getch();
    std::printf("%c\n", sensor);

    if (sensor < '1' || sensor > '4')
    {
        std::printf("Invalid sensor selection.\n");
        return;
    }

    std::printf("Select mode:\n");
    std::printf("1. Normal\n");
    std::printf("2. Warning\n");
    std::printf("> ");
    std::fflush(stdout);
    int mode = _getch();
    std::printf("%c\n", mode);

    if (mode != '1' && mode != '2')
    {
        std::printf("Invalid mode selection.\n");
        return;
    }

    bool isNormal = (mode == '1');
    bool sent = false;

    if (sensor == '1')
    {
        float low = 0.0f;
        float high = 0.0f;
        std::printf("Enter lower limit: ");
        std::fflush(stdout);
        std::scanf("%f", &low);
        std::printf("Enter upper limit: ");
        std::fflush(stdout);
        std::scanf("%f", &high);
        sent = isNormal ? mgmt.setTempNormalRange(low, high) : mgmt.setTempWarningRange(low, high);
    }
    else
    {
        float value = 0.0f;
        std::printf("Enter lower limit: ");
        std::fflush(stdout);
        std::scanf("%f", &value);

        if (sensor == '2')
        {
            sent = isNormal ? mgmt.setHumidityNormalLower(value) : mgmt.setHumidityWarningLower(value);
        }
        else if (sensor == '3')
        {
            sent = isNormal ? mgmt.setLightNormalLower(value) : mgmt.setLightWarningLower(value);
        }
        else /* '4' */
        {
            sent = isNormal ? mgmt.setBatteryNormalLower(value) : mgmt.setBatteryWarningLower(value);
        }
    }

    std::printf("\n[cmd] sendFrame result: %s\n", sent ? "ok" : "FAILED (is the port open?)");
}

}  // namespace

int main()
{
    std::printf("=== Central Computer ===\n");

    central_computer::CentralComputer cc;

    if (!cc.start(kComPortName, kBaudRate, kDatabasePath, kGsPort))
    {
        std::printf("FATAL: could not open database '%s'\n", kDatabasePath);
        return 1;
    }
    std::printf("Database opened: %s\n", kDatabasePath);

    cc.dataCollection().onMeasurementBackfillComplete = []()
    { std::printf("\n[backfill] measurement backfill complete\n"); };
    cc.dataCollection().onEventBackfillComplete = []() { std::printf("\n[backfill] event backfill complete\n"); };

    /* HW-B-C-03: onSystemTimeResponse already existed as a callback slot
       on Communication but nothing registered a handler for it - this is
       the minimum needed to observe the reply to [6] below. */
    cc.communication().callbacks.onSystemTimeResponse = [](const message::TimestampMessage &response)
    { std::printf("\n[reply] system time response: timestamp=%u\n", response.timestamp); };

    /* GS's server side shares the same DataStore the LNC side already
       fills - a GS query answers from the exact same measurement/event
       history CC has collected, not a second, separate store. */
    if (cc.gsCommunication().isListening())
    {
        std::printf("Listening for Ground Station on TCP port %u.\n", kGsPort);
    }
    else
    {
        std::printf("WARNING: could not listen on TCP port %u - continuing without Ground Station support.\n",
                    kGsPort);
    }

    if (cc.communication().isOpen())
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
        cc.poll();

        if (_kbhit())
        {
            int key = _getch();
            switch (key)
            {
                case '1':
                    handleSendTestCommand(cc.managementCommand());
                    break;
                case '2':
                    handleRequestBackfill(cc.dataCollection());
                    break;
                case '3':
                    printStatus(cc.communication(), cc.dataCollection(), cc.gsCommunication());
                    break;
                case '4':
                    printReport(cc.dataStore());
                    break;
                case '5':
                    handleSetTempNormalRange(cc.managementCommand());
                    break;
                case '6':
                    handleRequestSystemTime(cc.managementCommand());
                    break;
                case '7':
                    handleSetSensorLimit(cc.managementCommand());
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
    cc.stop();
    return 0;
}
