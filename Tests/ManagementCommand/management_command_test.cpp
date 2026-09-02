/*
 * management_command_test.cpp
 *
 * Purpose:
 *   Stand-alone PC test program for
 *   CentralComputer/management_command.h/.cpp. No real serial port is
 *   opened - each of the 10 functions is called against a Communication
 *   instance that was never open()'d, so sendFrame() always returns
 *   false, but it still records exactly what it was asked to send in
 *   Communication::lastSentFrame. Each test recomputes the expected frame
 *   bytes independently via tlv::encodeFrame + message.h's own field
 *   layout (matching how cc_message_test.cpp already validates those
 *   builders) and compares byte-for-byte.
 *
 * How to build and run (from this folder):
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" management_command_test.cpp "../../CentralComputer/src/management_command.cpp" "../../CentralComputer/src/communication.cpp" "../../CentralComputer/src/message.cpp" "../../CentralComputer/src/serial_transport.cpp" "../../Common/TLVCodec/tlv_codec.cpp" -o management_command_test.exe
 *   ./management_command_test.exe
 *
 * This file does NOT:
 *   - Open a real serial port.
 */

#include "communication.h"
#include "management_command.h"
#include "tlv_codec.h"
#include "tlv_common.h"
#include <cstdio>
#include <cstring>

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

static bool framesEqual(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    return a == b;
}

static void testTemperatureRangeCommands()
{
    std::printf("\nTest: setTempNormalRange / setTempWarningRange send the correct frames\n");

    communication::Communication comm;
    management_command::ManagementCommand mgmt(comm);

    check(!mgmt.setTempNormalRange(10.0f, 30.0f), "returns false (port never opened)");
    std::vector<uint8_t> expectedNormal;
    appendFloatLE(expectedNormal, 10.0f);
    appendFloatLE(expectedNormal, 30.0f);
    check(framesEqual(comm.lastSentFrame, tlv::encodeFrame(TAG_SET_TEMP_NORMAL_RANGE, expectedNormal)),
          "lastSentFrame matches TAG_SET_TEMP_NORMAL_RANGE with low=10.0, high=30.0");

    mgmt.setTempWarningRange(5.0f, 35.0f);
    std::vector<uint8_t> expectedWarning;
    appendFloatLE(expectedWarning, 5.0f);
    appendFloatLE(expectedWarning, 35.0f);
    check(framesEqual(comm.lastSentFrame, tlv::encodeFrame(TAG_SET_TEMP_WARNING_RANGE, expectedWarning)),
          "lastSentFrame matches TAG_SET_TEMP_WARNING_RANGE with low=5.0, high=35.0");
}

static void testSingleLimitCommand(bool (management_command::ManagementCommand::*fn)(float),
                                    uint8_t expectedTag, float value, const char *description)
{
    communication::Communication comm;
    management_command::ManagementCommand mgmt(comm);

    (mgmt.*fn)(value);

    std::vector<uint8_t> expectedValue;
    appendFloatLE(expectedValue, value);
    check(framesEqual(comm.lastSentFrame, tlv::encodeFrame(expectedTag, expectedValue)), description);
}

static void testAllSingleLimitCommands()
{
    std::printf("\nTest: all 6 humidity/light/battery single-limit commands send the correct frames\n");

    testSingleLimitCommand(&management_command::ManagementCommand::setHumidityNormalLower,
                            TAG_SET_HUMIDITY_NORMAL_LOWER, 40.0f, "setHumidityNormalLower");
    testSingleLimitCommand(&management_command::ManagementCommand::setHumidityWarningLower,
                            TAG_SET_HUMIDITY_WARNING_LOWER, 20.0f, "setHumidityWarningLower");
    testSingleLimitCommand(&management_command::ManagementCommand::setLightNormalLower,
                            TAG_SET_LIGHT_NORMAL_LOWER, 100.0f, "setLightNormalLower");
    testSingleLimitCommand(&management_command::ManagementCommand::setLightWarningLower,
                            TAG_SET_LIGHT_WARNING_LOWER, 50.0f, "setLightWarningLower");
    testSingleLimitCommand(&management_command::ManagementCommand::setBatteryNormalLower,
                            TAG_SET_BATTERY_NORMAL_LOWER, 3.3f, "setBatteryNormalLower");
    testSingleLimitCommand(&management_command::ManagementCommand::setBatteryWarningLower,
                            TAG_SET_BATTERY_WARNING_LOWER, 3.0f, "setBatteryWarningLower");
}

static void testSetRtcDateTime()
{
    std::printf("\nTest: setRtcDateTime sends the correct frame\n");

    communication::Communication comm;
    management_command::ManagementCommand mgmt(comm);

    mgmt.setRtcDateTime(1735689600); /* 2025-01-01 00:00:00 UTC */

    std::vector<uint8_t> expectedValue;
    appendUint32LE(expectedValue, 1735689600);
    check(framesEqual(comm.lastSentFrame, tlv::encodeFrame(TAG_SET_RTC_DATETIME, expectedValue)),
          "lastSentFrame matches TAG_SET_RTC_DATETIME with the given timestamp");
}

static void testRequestSystemTime()
{
    std::printf("\nTest: requestSystemTime sends an empty TAG_GET_SYSTEM_TIME_REQUEST frame\n");

    communication::Communication comm;
    management_command::ManagementCommand mgmt(comm);

    mgmt.requestSystemTime();

    check(framesEqual(comm.lastSentFrame, tlv::encodeFrame(TAG_GET_SYSTEM_TIME_REQUEST, {})),
          "lastSentFrame matches an empty-payload TAG_GET_SYSTEM_TIME_REQUEST");
}

static void testReturnValuePropagatesFromSendFrame()
{
    std::printf("\nTest: return value reflects Communication::sendFrame's result (false when port not open)\n");

    communication::Communication comm;
    management_command::ManagementCommand mgmt(comm);

    check(!comm.isOpen(), "sanity check: the port was never opened");
    check(!mgmt.setBatteryNormalLower(3.5f), "setBatteryNormalLower returns false when the port isn't open");
    check(!mgmt.requestSystemTime(), "requestSystemTime returns false when the port isn't open");
}

int main()
{
    std::printf("=== CentralComputer management_command.cpp test suite ===\n");

    testTemperatureRangeCommands();
    testAllSingleLimitCommands();
    testSetRtcDateTime();
    testRequestSystemTime();
    testReturnValuePropagatesFromSendFrame();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
