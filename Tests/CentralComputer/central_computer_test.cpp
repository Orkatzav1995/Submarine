/*
 * central_computer_test.cpp
 *
 * Purpose:
 *   Stand-alone PC test program for
 *   CentralComputer/central_computer.h/.cpp. Does not exercise any real
 *   hardware: uses ":memory:" for the database, a COM port name guaranteed
 *   not to exist on any test machine, and a real (but ephemeral, local-only)
 *   TCP port for the Ground Station listener.
 *
 *   This file only checks the facade's own wiring and lifecycle (inert
 *   construction, start()'s fatal/non-fatal split, stop(), and that the
 *   accessors reach the same objects start()/stop() acted on) - it does
 *   NOT re-verify Communication's, DataStore's, ManagementCommand's,
 *   DataCollection's, or GsCommunication's own internal behavior, which
 *   communication_test.cpp / data_store_test.cpp / management_command_test.cpp
 *   / data_collection_test.cpp / gs_communication_test.cpp already cover
 *   exhaustively.
 *
 * How to build and run (from this folder) - same two-step sqlite3.c build
 * every DataStore-adjacent test uses (see Tests/DataStore for why):
 *   gcc -std=c11 -c "../../Common/ThirdParty/sqlite3/sqlite3.c" -o sqlite3.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c central_computer_test.cpp -o central_computer_test.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/central_computer.cpp" -o central_computer_impl.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/communication.cpp" -o communication.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/data_collection.cpp" -o data_collection.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/data_store.cpp" -o data_store.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../CentralComputer/src/management_command.cpp" -o management_command.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../CentralComputer/src/gs_communication.cpp" -o gs_communication.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../CentralComputer/src/message.cpp" -o message.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../CentralComputer/src/serial_transport.cpp" -o serial_transport.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../CentralComputer/src/tcp_transport.cpp" -o tcp_transport.o
 *   g++ -std=c++17 -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../Common/TLVCodec/tlv_codec.cpp" -o tlv_codec.o
 *   g++ central_computer_test.o central_computer_impl.o communication.o data_collection.o data_store.o management_command.o gs_communication.o message.o serial_transport.o tcp_transport.o tlv_codec.o sqlite3.o -lws2_32 -o central_computer_test.exe
 *   ./central_computer_test.exe
 *
 * This file does NOT:
 *   - Open a real serial port or leave any file on disk.
 *   - Re-test any of the 5 wrapped modules' own internal behavior.
 */

#include "central_computer.h"
#include "tlv_codec.h"
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

static void appendUint32LE(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

/* Guaranteed not to exist on any real machine - proves the non-fatal path
   without needing actual hardware. */
static const char *kNoSuchComPort = "COM250";

static void testConstructorIsInert()
{
    std::printf("\nTest: constructor performs no I/O\n");

    central_computer::CentralComputer cc;

    check(!cc.dataStore().isOpen(), "dataStore().isOpen() is false before start()");
    check(!cc.communication().isOpen(), "communication().isOpen() is false before start()");
    check(!cc.gsCommunication().isListening(), "gsCommunication().isListening() is false before start()");
}

static void testStartSucceedsWithInMemoryDatabase()
{
    std::printf("\nTest: start() with a valid database path succeeds\n");

    central_computer::CentralComputer cc;
    bool started = cc.start(kNoSuchComPort, 115200, ":memory:", 15990);

    check(started, "start() returns true when the database opens");
    check(cc.dataStore().isOpen(), "dataStore().isOpen() is true after start()");
}

static void testStartFailsWhenDatabaseCannotOpen()
{
    std::printf("\nTest: start() fails when the database path is unusable\n");

    central_computer::CentralComputer cc;
    /* No such directory exists relative to this test's working folder -
       sqlite3_open() cannot create the file and fails, same as a real
       unwritable/missing path would in production. */
    bool started = cc.start(kNoSuchComPort, 115200, "no_such_directory/central_computer_test.db", 15991);

    check(!started, "start() returns false when the database cannot be opened");
}

static void testStartIsNonFatalWhenSerialPortUnavailable()
{
    std::printf("\nTest: start() still succeeds when the serial port cannot be opened\n");

    central_computer::CentralComputer cc;
    bool started = cc.start(kNoSuchComPort, 115200, ":memory:", 15992);

    check(started, "start() returns true even though the COM port doesn't exist");
    check(!cc.communication().isOpen(), "communication().isOpen() is false - the caller can detect this itself");
}

static void testStartListensOnTheGivenGsPort()
{
    std::printf("\nTest: start() opens the Ground Station listener on the requested port\n");

    central_computer::CentralComputer cc;
    cc.start(kNoSuchComPort, 115200, ":memory:", 15993);

    check(cc.gsCommunication().isListening(), "gsCommunication().isListening() is true after start()");
}

static void testManagementCommandGoesThroughTheOwnedCommunication()
{
    std::printf("\nTest: managementCommand() sends through the same Communication communication() returns\n");

    central_computer::CentralComputer cc;
    cc.start(kNoSuchComPort, 115200, ":memory:", 15994);

    cc.managementCommand().setRtcDateTime(1735689600); /* 2025-01-01 00:00:00 UTC */

    std::vector<uint8_t> expectedValue;
    appendUint32LE(expectedValue, 1735689600);
    check(cc.communication().lastSentFrame == tlv::encodeFrame(TAG_SET_RTC_DATETIME, expectedValue),
          "communication().lastSentFrame matches the frame managementCommand() just sent");
}

static void testDataCollectionGoesThroughTheOwnedCommunication()
{
    std::printf("\nTest: dataCollection() sends through the same Communication communication() returns\n");

    central_computer::CentralComputer cc;
    cc.start(kNoSuchComPort, 115200, ":memory:", 15995);

    bool started = cc.dataCollection().requestMeasurementBackfill(1000, 2000);

    check(started, "requestMeasurementBackfill() starts (nothing else in progress yet)");
    check(cc.communication().lastSentFrame.size() > 1
              && cc.communication().lastSentFrame[1] == TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST,
          "communication().lastSentFrame carries the tag dataCollection() just sent");
}

static void testStopClosesEverything()
{
    std::printf("\nTest: stop() closes the serial port, GS listener, and database\n");

    central_computer::CentralComputer cc;
    cc.start(kNoSuchComPort, 115200, ":memory:", 15996);

    cc.stop();

    check(!cc.communication().isOpen(), "communication().isOpen() is false after stop()");
    check(!cc.gsCommunication().isListening(), "gsCommunication().isListening() is false after stop()");
    check(!cc.dataStore().isOpen(), "dataStore().isOpen() is false after stop()");
}

static void testPollDoesNotCrashWithNothingConnected()
{
    std::printf("\nTest: poll() does not crash with no serial port and no GS client connected\n");

    central_computer::CentralComputer cc;
    cc.start(kNoSuchComPort, 115200, ":memory:", 15997);

    for (int i = 0; i < 5; i++)
    {
        cc.poll();
    }

    check(true, "poll() returned without crashing");
}

int main()
{
    testConstructorIsInert();
    testStartSucceedsWithInMemoryDatabase();
    testStartFailsWhenDatabaseCannotOpen();
    testStartIsNonFatalWhenSerialPortUnavailable();
    testStartListensOnTheGivenGsPort();
    testManagementCommandGoesThroughTheOwnedCommunication();
    testDataCollectionGoesThroughTheOwnedCommunication();
    testStopClosesEverything();
    testPollDoesNotCrashWithNothingConnected();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);
    return g_testsFailed == 0 ? 0 : 1;
}
