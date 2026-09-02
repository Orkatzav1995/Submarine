/*
 * serial_transport_test.cpp
 *
 * Purpose:
 *   Test program for CentralComputer/serial_transport.h/.cpp. Unlike the
 *   PC-only tests for Protocol/Message, this one talks to a REAL COM port
 *   (the Nucleo board's ST-LINK Virtual COM Port, found attached to this
 *   machine as COM10 - see PROJECT_GUIDE.md). Since the firmware currently
 *   flashed on the board is unknown, this test only checks things that
 *   are true regardless of what the LNC is doing: opening/closing a real
 *   port works, a nonexistent port fails gracefully, a send() completes
 *   at the OS level, and receiveByte()'s timeout never takes materially
 *   longer than requested (whether or not a byte actually arrived).
 *
 *   If no board is attached when this runs, the COM10-specific checks
 *   will fail - that is expected and not a bug in this code.
 *
 * How to build and run (from this folder):
 *   g++ -std=c++17 -I "../../CentralComputer/include" serial_transport_test.cpp "../../CentralComputer/src/serial_transport.cpp" -o serial_transport_test.exe
 *   ./serial_transport_test.exe
 *
 * This file does NOT:
 *   - Test the Protocol or Message layers.
 *   - Assume anything about what firmware is running on the LNC.
 */

#include "serial_transport.h"
#include <chrono>
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

static void testNonexistentPortFailsGracefully()
{
    std::printf("\nTest: opening a nonexistent port fails cleanly\n");

    transport::SerialPort port;
    bool opened = port.open("COM999", 115200);

    check(!opened, "open() returns false for a port that does not exist");
    check(!port.isOpen(), "isOpen() is false after a failed open");
}

static void testRealPortOpenClose()
{
    std::printf("\nTest: opening the real board (COM10) works\n");

    transport::SerialPort port;
    bool opened = port.open("COM10", 115200);

    check(opened, "open(\"COM10\", 115200) succeeds");
    check(port.isOpen(), "isOpen() is true after a successful open");

    port.close();
    check(!port.isOpen(), "isOpen() is false after close()");

    /* Reopening after a close should work too - not a one-shot object. */
    bool reopened = port.open("COM10", 115200);
    check(reopened, "the same SerialPort object can open again after close()");
    port.close();
}

static void testReceiveByteNeverExceedsTimeout()
{
    std::printf("\nTest: receiveByte() never blocks materially longer than requested\n");

    transport::SerialPort port;
    if (!port.open("COM10", 115200))
    {
        check(false, "could not open COM10 - skipping timing test (board not attached?)");
        return;
    }

    const uint32_t timeoutMs = 200;
    uint8_t receivedByte = 0;

    auto start = std::chrono::steady_clock::now();
    bool gotByte = port.receiveByte(receivedByte, timeoutMs);
    auto end = std::chrono::steady_clock::now();
    (void)gotByte; /* we don't know if the board is sending anything - only timing matters here */

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    /* Generous upper bound (timeout + 500ms slack for OS scheduling jitter) -
       this is checking "did not hang", not measuring precise timing. */
    check(elapsedMs <= static_cast<long long>(timeoutMs) + 500,
          "receiveByte() returned within a reasonable margin of the requested timeout");

    std::printf("    (took %lld ms for a %u ms timeout, byte received: %s)\n",
                elapsedMs, timeoutMs, gotByte ? "yes" : "no");

    port.close();
}

static void testSendCompletesAtOsLevel()
{
    std::printf("\nTest: send() completes without an OS-level error\n");

    transport::SerialPort port;
    if (!port.open("COM10", 115200))
    {
        check(false, "could not open COM10 - skipping send test (board not attached?)");
        return;
    }

    std::vector<uint8_t> testBytes = {0xAA, 0x01, 0x00, 0x00};
    bool sent = port.send(testBytes, 500);

    check(sent, "send() of a small test buffer completes successfully");

    port.close();
}

static void testOperationsAfterCloseFailGracefully()
{
    std::printf("\nTest: send()/receiveByte() on a closed port fail cleanly, don't crash\n");

    transport::SerialPort port; /* never opened */
    uint8_t b = 0;

    check(port.send({0x01}, 100) == false, "send() on an unopened port returns false");
    check(port.receiveByte(b, 100) == false, "receiveByte() on an unopened port returns false");
}

int main()
{
    std::printf("=== serial_transport.cpp test suite ===\n");
    std::printf("(talks to a real COM port - COM10, the board's ST-LINK VCP)\n");

    testNonexistentPortFailsGracefully();
    testRealPortOpenClose();
    testReceiveByteNeverExceedsTimeout();
    testSendCompletesAtOsLevel();
    testOperationsAfterCloseFailGracefully();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
