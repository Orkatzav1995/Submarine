/*
 * gs_main.cpp
 *
 * Purpose:
 *   The Ground Station's real entry point - the first file to construct
 *   and actually run a real cc_communication::CcCommunication, instead of
 *   it being exercised only by its own test suite (Tests/CcCommunication).
 *   Mirrors CentralComputer/cc_main.cpp's role exactly: a thin driver over
 *   already-tested modules, adding no new communication logic of its own.
 *
 *   Unlike every other file in this codebase, this one is NOT unit-tested
 *   with a check()-based PC test - it's a real entry point with a real
 *   interactive loop, which isn't something you unit-test the same way.
 *   Its "test" is a clean build/link, a non-interactive startup smoke
 *   check, and best-effort real communication verification against a
 *   running CC (see PROJECT_GUIDE.md's Step 7 section).
 *
 * Layer:
 *   Application entry point. Sits above everything else on the GS side,
 *   owns the one CcCommunication instance.
 *
 * Responsibilities:
 *   - Construct one CcCommunication and register onMeasurementsReceived/
 *     onEventsReceived once, up front - each prints its result when it
 *     arrives, asynchronously, exactly like cc_main.cpp's own
 *     onSystemTimeResponse callback.
 *   - Attempt to connect to the CC (127.0.0.1:5000, Step 4's agreed
 *     production port) once at startup; on failure, print a WARNING and
 *     keep running - same pattern cc_main.cpp already uses for a failed
 *     COM10 open.
 *   - Offer a small interactive menu: manually (re)connect, request
 *     measurements or events over an operator-entered [startTime,
 *     endTime] range (raw Unix-epoch integers - no date-parsing code is
 *     invented here), and show status.
 *   - Guard against sending a request while not connected - checked here,
 *     in the application layer, NOT inside CcCommunication itself (whose
 *     own "don't gate on an unconfirmable send result" design is
 *     deliberate and unchanged).
 *   - Run a single-threaded loop: poll() the connection (non-blocking,
 *     short internal timeout) and check for a keypress without blocking
 *     (_kbhit()/_getch() - this codebase is already Windows-only, see
 *     serial_transport.cpp/cc_main.cpp).
 *
 * This file does NOT:
 *   - Add any new communication logic - it only calls CcCommunication's
 *     already-tested public functions.
 *   - Implement reconnection-after-drop, request timeouts, cancellation,
 *     or any retry architecture - CcCommunication has none of these by
 *     deliberate design (Step 5), and none is invented here either. A
 *     stuck pending request has no way to be cancelled or reset today -
 *     a known, documented gap (PROJECT_GUIDE.md), not silently worked
 *     around.
 *   - Use any config file or command-line arguments - the CC's host and
 *     port are plain constants below.
 */

#include "cc_communication.h"
#include "tlv_common.h"
#include <conio.h>
#include <cstdio>
#include <iostream>

namespace {

constexpr const char *kCcHost = "127.0.0.1";
constexpr uint16_t kCcPort = 5000; /* PROJECT_GUIDE.md's Step 4 agreed production port */

void printMenu()
{
    std::printf("\n--- Menu ---\n");
    std::printf("[1] Connect to CC\n");
    std::printf("[2] Request measurements in a date/time range\n");
    std::printf("[3] Request events in a date/time range\n");
    std::printf("[4] Show status\n");
    std::printf("[q] Quit\n");
    std::printf("> ");
    std::fflush(stdout);
}

void printStatus(const cc_communication::CcCommunication &cc)
{
    std::printf("\n--- Status ---\n");
    std::printf("Connected: %s\n", cc.isConnected() ? "yes" : "no");
    std::printf("Measurement request in progress: %s\n", cc.isMeasurementRequestInProgress() ? "yes" : "no");
    std::printf("Event request in progress: %s\n", cc.isEventRequestInProgress() ? "yes" : "no");
    std::printf("Frames dispatched: %u\n", cc.framesDispatched);
    std::printf("Decode errors: %u\n", cc.decodeErrors);
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

void printMeasurements(const std::vector<message::MeasurementSample> &records)
{
    std::printf("\n[reply] %zu measurement(s) received:\n", records.size());
    for (const message::MeasurementSample &sample : records)
    {
        std::printf("  t=%u  temp=%.2f  humidity=%.2f  light=%.2f  battery=%.2f  mode=%u\n", sample.timestamp,
                     sample.temperature, sample.humidity, sample.light, sample.battery, sample.mode);
    }
}

void printEvents(const std::vector<message::EventRecord> &records)
{
    std::printf("\n[reply] %zu event(s) received:\n", records.size());
    for (const message::EventRecord &event : records)
    {
        std::printf("  t=%u  type=%s  \"%s\"\n", event.timestamp, EventTypeName(event.eventType),
                     event.description.c_str());
    }
}

/* [1]: (re)connect on demand - CcCommunication has no auto-reconnect
   logic by design (Step 5's explicit open question), so a manual retry
   here is the application-layer way to recover from a CC that wasn't up
   yet at GS startup, or a dropped connection. Calling connect() again on
   the same object is already proven safe (Step 4a's tests) - it simply
   replaces any prior connection. */
void handleConnect(cc_communication::CcCommunication &cc)
{
    bool connected = cc.connect(kCcHost, kCcPort);
    std::printf("\n[cmd] connect(%s:%u) -> %s\n", kCcHost, kCcPort, connected ? "ok" : "FAILED");
}

/* [2]/[3]: prompts for a raw Unix-epoch [startTime, endTime] range, then
   guards on isConnected() itself before ever calling into
   CcCommunication - that class's own requestMeasurements()/
   requestEvents() deliberately don't gate on connection state (same
   "don't gate on an unconfirmable low-level result" reasoning
   DataCollection's own backfill requests already document), so the
   "are we even connected" check belongs here, in the application layer. */
void handleRequestMeasurements(cc_communication::CcCommunication &cc)
{
    if (!cc.isConnected())
    {
        std::printf("\n[cmd] not connected - use [1] to connect to CC first.\n");
        return;
    }

    uint32_t startTime = 0;
    uint32_t endTime = 0;
    std::printf("\nEnter startTime (Unix epoch): ");
    std::cin >> startTime;
    std::printf("Enter endTime (Unix epoch): ");
    std::cin >> endTime;

    bool sent = cc.requestMeasurements(startTime, endTime);
    std::printf("[cmd] requestMeasurements(%u, %u) -> %s\n", startTime, endTime,
                sent ? "sent" : "FAILED (a measurement request is already in progress)");
}

void handleRequestEvents(cc_communication::CcCommunication &cc)
{
    if (!cc.isConnected())
    {
        std::printf("\n[cmd] not connected - use [1] to connect to CC first.\n");
        return;
    }

    uint32_t startTime = 0;
    uint32_t endTime = 0;
    std::printf("\nEnter startTime (Unix epoch): ");
    std::cin >> startTime;
    std::printf("Enter endTime (Unix epoch): ");
    std::cin >> endTime;

    bool sent = cc.requestEvents(startTime, endTime);
    std::printf("[cmd] requestEvents(%u, %u) -> %s\n", startTime, endTime,
                sent ? "sent" : "FAILED (an event request is already in progress)");
}

}  // namespace

int main()
{
    std::printf("=== Ground Station ===\n");

    cc_communication::CcCommunication cc;
    cc.onMeasurementsReceived = [](const std::vector<message::MeasurementSample> &records)
    { printMeasurements(records); };
    cc.onEventsReceived = [](const std::vector<message::EventRecord> &records) { printEvents(records); };

    if (cc.connect(kCcHost, kCcPort))
    {
        std::printf("Connected to CC at %s:%u.\n", kCcHost, kCcPort);
    }
    else
    {
        std::printf("WARNING: could not connect to CC at %s:%u - continuing without a live connection.\n", kCcHost,
                     kCcPort);
        std::printf("(Use [1] to retry once the CC is running.)\n");
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
                    handleConnect(cc);
                    break;
                case '2':
                    handleRequestMeasurements(cc);
                    break;
                case '3':
                    handleRequestEvents(cc);
                    break;
                case '4':
                    printStatus(cc);
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
    cc.close();
    return 0;
}
