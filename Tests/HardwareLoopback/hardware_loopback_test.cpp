/*
 * hardware_loopback_test.cpp
 *
 * Purpose:
 *   Real hardware test: confirms the LNC's CommTxTask is actually sending
 *   TLV frames out over USART2, and that they arrive at the PC correctly
 *   formed, by reading real bytes from COM10 (the board's ST-LINK Virtual
 *   COM Port) and decoding them with the same tlv::Decoder the Central
 *   Computer will use in production.
 *
 *   IMPORTANT: do NOT add a physical jumper wire between PA2 and PA3 on
 *   the board for this test. USART2 is deliberately shared with the
 *   ST-LINK Virtual COM Port (see PROJECT_GUIDE.md) - PA2/PA3 are already
 *   electrically connected to the ST-LINK's own UART bridge via on-board
 *   solder bridges. An external jumper on top of that would tie the MCU's
 *   TX driver, the MCU's RX input, and the ST-LINK's own TX/RX lines onto
 *   the same two wires - two active transmitters (the MCU and the
 *   ST-LINK bridge chip) would then be driving the same node at once,
 *   which garbles communication rather than testing it cleanly. Reading
 *   directly from COM10 already IS the real loopback (PC <-> board, over
 *   the actual wire) - nothing extra to wire up.
 *
 * What this checks:
 *   - At least one complete, CRC-valid frame arrives within a few seconds.
 *   - Its Tag and Value match the CommTxTask test scaffold in main.c
 *     (Tag 0x01, Value {0x01, 0x02, 0x03} - bring-up scaffolding, not a
 *     real application message, see PROJECT_GUIDE.md).
 *   - If a second frame arrives, the gap between them is close to the
 *     2-second period CommTxTask uses - proving the periodic send timing
 *     (and the whole FreeRTOS + polling-UART design) works on real
 *     silicon, not just in our heads.
 *
 * How to build and run (from this folder):
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" hardware_loopback_test.cpp "../../CentralComputer/src/serial_transport.cpp" "../../Common/TLVCodec/tlv_codec.cpp" -o hardware_loopback_test.exe
 *   ./hardware_loopback_test.exe
 */

#include "serial_transport.h"
#include "tlv_codec.h"
#include <chrono>
#include <cstdio>
#include <vector>

struct ReceivedFrame
{
    tlv::Frame frame;
    std::chrono::steady_clock::time_point when;
};

int main()
{
    std::printf("=== Hardware loopback test: reading real frames from COM10 ===\n");
    std::printf("(no jumper wire needed or wanted - see the header comment for why)\n\n");

    transport::SerialPort port;
    if (!port.open("COM10", 115200))
    {
        std::printf("FAIL: could not open COM10 - is the board plugged in?\n");
        return 1;
    }
    std::printf("Opened COM10 at 115200 baud. Listening for up to 6 seconds...\n\n");

    tlv::Decoder decoder;
    std::vector<ReceivedFrame> receivedFrames;

    auto testStart = std::chrono::steady_clock::now();
    const auto testDuration = std::chrono::seconds(6);

    while (std::chrono::steady_clock::now() - testStart < testDuration)
    {
        uint8_t b;
        if (port.receiveByte(b, 100))
        {
            tlv::Frame decoded;
            if (decoder.feedByte(b, decoded) == tlv::DecodeStatus::FrameReady)
            {
                receivedFrames.push_back({decoded, std::chrono::steady_clock::now()});
                std::printf("  Received a frame: tag=0x%02X, %zu value byte(s)\n",
                            decoded.tag, decoded.value.size());
            }
        }
    }

    port.close();

    std::printf("\n--- Results ---\n");

    int checksRun = 0;
    int checksFailed = 0;
    auto check = [&](bool condition, const char *description)
    {
        checksRun++;
        if (condition)
        {
            std::printf("  PASS: %s\n", description);
        }
        else
        {
            std::printf("  FAIL: %s\n", description);
            checksFailed++;
        }
    };

    check(!receivedFrames.empty(),
          "at least one complete, CRC-valid frame arrived from the board");

    if (!receivedFrames.empty())
    {
        const tlv::Frame &first = receivedFrames.front().frame;
        std::vector<uint8_t> expectedValue = {0x01, 0x02, 0x03};

        check(first.tag == 0x01, "frame tag matches the CommTxTask test scaffold (0x01)");
        check(first.value == expectedValue, "frame value matches the CommTxTask test scaffold ({0x01,0x02,0x03})");
    }

    if (receivedFrames.size() >= 2)
    {
        auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(
            receivedFrames[1].when - receivedFrames[0].when).count();
        std::printf("  Gap between frame 1 and frame 2: %lld ms (CommTxTask sends every ~2000 ms)\n",
                    static_cast<long long>(gap));
        check(gap > 1500 && gap < 2500, "gap between frames is close to the expected 2-second period");
    }
    else
    {
        std::printf("  (only one frame arrived in 6 seconds - can't check the send period; "
                    "run again with a longer window if you want to confirm periodicity)\n");
    }

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", checksRun, checksFailed);
    return (checksFailed == 0) ? 0 : 1;
}
