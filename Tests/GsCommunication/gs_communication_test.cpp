/*
 * gs_communication_test.cpp
 *
 * Purpose:
 *   Test program for CentralComputer/gs_communication.h/.cpp
 *   (gs_communication::GsCommunication). Covers Step 4c's scope
 *   (listening/accepting over a real TCP loopback connection, decoding
 *   real bytes into frames, recognizing the two "retrieve by range"
 *   request tags via the Step 4b parsers) PLUS Step 4d's scope (see
 *   PROJECT_GUIDE.md): a real end-to-end round trip - GS sends a range
 *   request, CC parses it, reads matching rows from a real (in-memory)
 *   DataStore, chunks them, and sends the chunked response(s) back,
 *   including the multi-chunk case.
 *
 *   Same "test against the real thing" convention as every other Tests/
 *   folder: a test-only raw-Winsock loopback client (never in production
 *   code, matching tcp_server_transport_test.cpp's own LoopbackClient)
 *   drives the real GsCommunication over a real TCP connection. Frame
 *   bytes are built with the real message::buildGetMeasurementsByRangeRequest/
 *   buildGetEventsByRangeRequest (already linked in via message.cpp) -
 *   byte-for-byte identical to what a real Ground Station would send,
 *   already proven in cc_message_test.cpp's round-trip tests. Response
 *   frames received by the test client are decoded with a real
 *   tlv::Decoder and parsed with the real, already-tested
 *   message::parseMeasurementChunkResponse/parseEventChunkResponse.
 *
 *   GsCommunication's constructor requires a real data_store::DataStore&
 *   (the already-agreed, documented shape - see PROJECT_GUIDE.md). Step
 *   4c's tests used one purely to satisfy the signature; Step 4d's new
 *   tests below actually open it (":memory:") and insert real rows before
 *   sending a request, so the response content can be checked against
 *   known, hand-inserted data.
 *
 * How to build and run (from this folder; sqlite3.c must be compiled
 * separately with gcc, not g++ - see data_store_test.cpp's own header
 * comment for why):
 *   gcc -std=c11 -c "../../Common/ThirdParty/sqlite3/sqlite3.c" -o sqlite3.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c gs_communication_test.cpp -o gs_communication_test.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/gs_communication.cpp" -o gs_communication_impl.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c "../../CentralComputer/src/data_store.cpp" -o data_store_impl.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../CentralComputer/src/message.cpp" -o message.o
 *   g++ -std=c++17 -I "../../CentralComputer/include" -c "../../CentralComputer/src/tcp_transport.cpp" -o tcp_transport.o
 *   g++ -std=c++17 -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../Common/TLVCodec/tlv_codec.cpp" -o tlv_codec.o
 *   g++ gs_communication_test.o gs_communication_impl.o data_store_impl.o message.o tcp_transport.o tlv_codec.o sqlite3.o -lws2_32 -o gs_communication_test.exe
 *   ./gs_communication_test.exe
 *
 * This file does NOT:
 *   - Test cc_main.cpp wiring - not yet done (a later step).
 *   - Require a real Ground Station binary or physical network hardware.
 */

#include "data_store.h"
#include "gs_communication.h"
#include "message.h"
#include "tlv_common.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <cstdio>
#include <thread>

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

static const uint16_t kTestPort = 15942; /* distinct from Step 4a's 15931 and production's 5000 */

/* ============================================================
 * TEST-ONLY loopback client helper, raw Winsock - see this file's header
 * comment for why this is the right way to test a TCP server without a
 * real Ground Station. Kept out of CentralComputer/src/ entirely.
 * ============================================================ */

namespace {

class LoopbackClient
{
public:
    bool connectTo(uint16_t port)
    {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            return false;
        }
        wsaStarted_ = true;

        sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_ == INVALID_SOCKET)
        {
            return false;
        }

        sockaddr_in addr;
        ZeroMemory(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        return ::connect(sock_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != SOCKET_ERROR;
    }

    bool sendBytes(const std::vector<uint8_t> &data)
    {
        size_t totalSent = 0;
        while (totalSent < data.size())
        {
            int sent = send(sock_, reinterpret_cast<const char *>(data.data() + totalSent),
                             static_cast<int>(data.size() - totalSent), 0);
            if (sent <= 0)
            {
                return false;
            }
            totalSent += static_cast<size_t>(sent);
        }
        return true;
    }

    /* Mirrors transport::TcpSocket::receiveByte()'s contract: false means
       either the timeout elapsed or the peer is gone, this test has no
       need to distinguish the two. */
    bool receiveByte(uint8_t &outByte, uint32_t timeoutMs)
    {
        DWORD timeout = timeoutMs;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
        char c = 0;
        int received = recv(sock_, &c, 1, 0);
        if (received != 1)
        {
            return false;
        }
        outByte = static_cast<uint8_t>(c);
        return true;
    }

    void disconnect()
    {
        if (sock_ != INVALID_SOCKET)
        {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
        }
    }

    ~LoopbackClient()
    {
        disconnect();
        if (wsaStarted_)
        {
            WSACleanup();
        }
    }

private:
    SOCKET sock_ = INVALID_SOCKET;
    bool wsaStarted_ = false;
};

void shortPause()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

/* Drives gs.poll() until either "expectedFramesDispatched" is reached or
   a generous upper bound of attempts elapses (so a real bug shows up as a
   FAIL, not a hang). Each call only makes at most one byte of progress,
   matching GsCommunication::poll()'s real, intentionally-small-steps
   contract - a real CC main loop would call this in its own loop too. */
void pollUntilDispatched(gs_communication::GsCommunication &gs, uint32_t expectedFramesDispatched)
{
    for (int i = 0; i < 200 && gs.framesDispatched < expectedFramesDispatched; i++)
    {
        gs.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

/* Reads bytes from "client" one at a time, feeding a real tlv::Decoder,
   until one complete frame is ready (returns true, frame in "outFrame"),
   a decode error occurs, or "timeoutMs" elapses with nothing arriving
   (returns false either way). Mirrors how a real Ground Station would
   read its TCP stream. */
bool receiveFrame(LoopbackClient &client, tlv::Frame &outFrame, uint32_t timeoutMs)
{
    tlv::Decoder decoder;
    for (int i = 0; i < 1000; i++)
    {
        uint8_t b = 0;
        if (!client.receiveByte(b, timeoutMs))
        {
            return false;
        }
        tlv::DecodeStatus status = decoder.feedByte(b, outFrame);
        if (status == tlv::DecodeStatus::FrameReady)
        {
            return true;
        }
        if (status == tlv::DecodeStatus::Error)
        {
            return false;
        }
    }
    return false;
}

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

message::EventRecord makeEventRecord(uint32_t timestamp, uint8_t eventType)
{
    message::EventRecord record;
    record.timestamp = timestamp;
    record.eventType = eventType;
    record.description = "test event";
    return record;
}

}  // namespace

static void testInitialStateBeforeListening()
{
    std::printf("\nTest: initial state before startListening()\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:"); /* Step 4d: dispatch() now genuinely queries this - must be open,
                                    matching real cc_main.cpp usage (DataStore is always open there). */
    gs_communication::GsCommunication gs(dataStore);

    check(!gs.isListening(), "isListening() is false before startListening()");
    check(!gs.isClientConnected(), "isClientConnected() is false before startListening()");
    check(gs.framesDispatched == 0, "framesDispatched starts at 0");
    check(gs.decodeErrors == 0, "decodeErrors starts at 0");

    gs.poll(); /* must not crash with nothing listening */
    check(!gs.isClientConnected(), "poll() before startListening() does not crash and connects nothing");
}

static void testStartListeningAndAcceptRealClient()
{
    std::printf("\nTest: startListening() and accepting a real loopback client via poll()\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:"); /* Step 4d: dispatch() now genuinely queries this - must be open,
                                    matching real cc_main.cpp usage (DataStore is always open there). */
    gs_communication::GsCommunication gs(dataStore);

    check(gs.startListening(kTestPort), "startListening() succeeds on a free port");
    check(gs.isListening(), "isListening() is true after startListening()");

    LoopbackClient client;
    check(client.connectTo(kTestPort), "a real client can connect");

    shortPause();
    gs.poll();
    check(gs.isClientConnected(), "poll() accepts the pending real connection");

    client.disconnect();
    gs.close();
}

static void testRecognizesGetMeasurementsByRangeRequest()
{
    std::printf("\nTest: a real GET_MEASUREMENTS_BY_RANGE_REQUEST frame is recognized and counted\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:"); /* Step 4d: dispatch() now genuinely queries this - must be open,
                                    matching real cc_main.cpp usage (DataStore is always open there). */
    gs_communication::GsCommunication gs(dataStore);
    gs.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    gs.poll();

    message::TimeRangeMessage request;
    request.requestId = 3;
    request.startTime = 1700000000u;
    request.endTime = 1700086400u;
    std::vector<uint8_t> frameBytes = message::buildGetMeasurementsByRangeRequest(request);

    check(client.sendBytes(frameBytes), "client sends a real, well-formed request frame");

    pollUntilDispatched(gs, 1);
    check(gs.framesDispatched == 1, "framesDispatched increments to 1 after the request is decoded and parsed");
    check(gs.decodeErrors == 0, "decodeErrors stays at 0 for a well-formed frame");

    client.disconnect();
    gs.close();
}

static void testRecognizesGetEventsByRangeRequest()
{
    std::printf("\nTest: a real GET_EVENTS_BY_RANGE_REQUEST frame is recognized and counted\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:"); /* Step 4d: dispatch() now genuinely queries this - must be open,
                                    matching real cc_main.cpp usage (DataStore is always open there). */
    gs_communication::GsCommunication gs(dataStore);
    gs.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    gs.poll();

    message::TimeRangeMessage request;
    request.requestId = 9;
    request.startTime = 1650000000u;
    request.endTime = 1650100000u;
    std::vector<uint8_t> frameBytes = message::buildGetEventsByRangeRequest(request);

    check(client.sendBytes(frameBytes), "client sends a real, well-formed request frame");

    pollUntilDispatched(gs, 1);
    check(gs.framesDispatched == 1, "framesDispatched increments to 1 after the request is decoded and parsed");
    check(gs.decodeErrors == 0, "decodeErrors stays at 0 for a well-formed frame");

    client.disconnect();
    gs.close();
}

static void testUnknownTagDroppedSilently()
{
    std::printf("\nTest: a recognizable-but-irrelevant tag (KEEPALIVE) is dropped silently, not an error\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:"); /* Step 4d: dispatch() now genuinely queries this - must be open,
                                    matching real cc_main.cpp usage (DataStore is always open there). */
    gs_communication::GsCommunication gs(dataStore);
    gs.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    gs.poll();

    /* A well-formed, CRC-valid frame - just not a tag GsCommunication's
       dispatch() has a case for. Built directly (GsCommunication has no
       use for TAG_KEEPALIVE's own builder, which lives on the LNC side). */
    std::vector<uint8_t> value(21, 0); /* KEEPALIVE's 21-byte payload, contents irrelevant here */
    std::vector<uint8_t> frameBytes = tlv::encodeFrame(TAG_KEEPALIVE, value);

    check(client.sendBytes(frameBytes), "client sends a well-formed but irrelevant-tag frame");

    /* No dispatch is expected to ever happen, so just give poll() a fixed,
       generous number of chances rather than waiting for a counter that
       will never move. */
    for (int i = 0; i < 50; i++)
    {
        gs.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    check(gs.framesDispatched == 0, "framesDispatched stays at 0 - the tag has no case in dispatch()");
    check(gs.decodeErrors == 0, "decodeErrors stays at 0 - the frame itself decoded fine, it's just unrecognized");

    client.disconnect();
    gs.close();
}

static void testCorruptedFrameIncrementsDecodeErrors()
{
    std::printf("\nTest: a corrupted (bad CRC) frame increments decodeErrors, not framesDispatched\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:"); /* Step 4d: dispatch() now genuinely queries this - must be open,
                                    matching real cc_main.cpp usage (DataStore is always open there). */
    gs_communication::GsCommunication gs(dataStore);
    gs.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    gs.poll();

    message::TimeRangeMessage request;
    request.requestId = 1;
    request.startTime = 100;
    request.endTime = 200;
    std::vector<uint8_t> frameBytes = message::buildGetMeasurementsByRangeRequest(request);
    frameBytes.back() ^= 0xFF; /* flip the last CRC byte - guarantees a CRC mismatch */

    check(client.sendBytes(frameBytes), "client sends a frame with a corrupted CRC byte");

    for (int i = 0; i < 50 && gs.decodeErrors == 0; i++)
    {
        gs.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    check(gs.decodeErrors == 1, "decodeErrors increments once the corrupted frame is fully fed in");
    check(gs.framesDispatched == 0, "framesDispatched is NOT incremented for a corrupted frame");

    client.disconnect();
    gs.close();
}

static void testWrongLengthPayloadNotDispatched()
{
    std::printf("\nTest: a correctly-tagged but wrong-length payload decodes as a frame, but is not dispatched\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:"); /* Step 4d: dispatch() now genuinely queries this - must be open,
                                    matching real cc_main.cpp usage (DataStore is always open there). */
    gs_communication::GsCommunication gs(dataStore);
    gs.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    gs.poll();

    /* A genuinely well-formed TLV frame (correct CRC over its own shorter
       payload) - the TLV layer has no opinion on payload length, only the
       Message layer's parseGetMeasurementsByRangeRequest does (it requires
       exactly 9 bytes). This distinguishes a frame-level decode error from
       a message-level parse rejection - they must NOT be conflated. */
    std::vector<uint8_t> tooShortValue = {0x01, 0x02, 0x03};
    std::vector<uint8_t> frameBytes = tlv::encodeFrame(TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST, tooShortValue);

    check(client.sendBytes(frameBytes), "client sends a well-formed frame with a too-short inner payload");

    for (int i = 0; i < 50; i++)
    {
        gs.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    check(gs.framesDispatched == 0, "framesDispatched is NOT incremented - parseGetMeasurementsByRangeRequest rejects the length");
    check(gs.decodeErrors == 0, "decodeErrors is NOT incremented either - the TLV frame itself decoded validly");

    client.disconnect();
    gs.close();
}

static void testFeedByteDirectSeamWithoutRealSocket()
{
    std::printf("\nTest: feedByte() works as a direct test seam, no real TCP connection needed\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:"); /* Step 4d: dispatch() now genuinely queries this - must be open,
                                    matching real cc_main.cpp usage (DataStore is always open there). */
    gs_communication::GsCommunication gs(dataStore);
    /* Deliberately never calls startListening() - feedByte() must work
       purely as a decode+dispatch seam, same as
       communication::Communication::feedByte() is tested. */

    message::TimeRangeMessage request;
    request.requestId = 5;
    request.startTime = 1;
    request.endTime = 2;
    std::vector<uint8_t> frameBytes = message::buildGetEventsByRangeRequest(request);

    for (uint8_t b : frameBytes)
    {
        gs.feedByte(b);
    }

    check(gs.framesDispatched == 1, "feedByte(), called directly, dispatches a well-formed frame correctly");
}

static void testDisconnectThenReconnectStillDispatches()
{
    std::printf("\nTest: after a client disconnects and a new one connects, dispatch still works (reconnection)\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:"); /* Step 4d: dispatch() now genuinely queries this - must be open,
                                    matching real cc_main.cpp usage (DataStore is always open there). */
    gs_communication::GsCommunication gs(dataStore);
    gs.startListening(kTestPort);

    LoopbackClient firstClient;
    firstClient.connectTo(kTestPort);
    shortPause();
    gs.poll();
    check(gs.isClientConnected(), "first client is connected");

    firstClient.disconnect();
    /* Drain until the disconnect is detected (isClientConnected() flips
       false) - the underlying transport already proved this mechanism in
       Step 4a's own tests; this just confirms GsCommunication doesn't
       break it. */
    for (int i = 0; i < 50 && gs.isClientConnected(); i++)
    {
        gs.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(!gs.isClientConnected(), "isClientConnected() goes false once the disconnect is detected");
    check(gs.isListening(), "isListening() is still true - only the client was dropped");

    LoopbackClient secondClient;
    check(secondClient.connectTo(kTestPort), "a new client can connect to the still-listening GsCommunication");
    shortPause();
    gs.poll();
    check(gs.isClientConnected(), "the new client is accepted");

    message::TimeRangeMessage request;
    request.requestId = 11;
    request.startTime = 10;
    request.endTime = 20;
    std::vector<uint8_t> frameBytes = message::buildGetMeasurementsByRangeRequest(request);
    secondClient.sendBytes(frameBytes);

    pollUntilDispatched(gs, 1);
    check(gs.framesDispatched == 1, "a request from the NEW connection is dispatched correctly");

    secondClient.disconnect();
    gs.close();
}

/* ============================================================
 * Step 4d: real end-to-end round trips - GS sends a range request, CC
 * parses it, reads matching rows from a real DataStore, chunks them, and
 * sends the chunked response(s) back. The test client decodes and parses
 * the response(s) with the real tlv::Decoder/message::parseXxxChunkResponse,
 * the same way a real Ground Station would.
 * ============================================================ */

static void testEndToEndMeasurementsSingleChunk()
{
    std::printf("\nTest (4d): end-to-end measurements request - single chunk, exact content verified\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:");
    dataStore.insertMeasurement(makeMeasurementSample(1000));
    dataStore.insertMeasurement(makeMeasurementSample(1010));
    dataStore.insertMeasurement(makeMeasurementSample(1020));
    dataStore.insertMeasurement(makeMeasurementSample(5000)); /* outside the request range below */

    gs_communication::GsCommunication gs(dataStore);
    gs.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    gs.poll();

    message::TimeRangeMessage request;
    request.requestId = 21;
    request.startTime = 1000;
    request.endTime = 1020;
    client.sendBytes(message::buildGetMeasurementsByRangeRequest(request));

    pollUntilDispatched(gs, 1);
    check(gs.framesDispatched == 1, "the request is recognized and dispatched");

    tlv::Frame responseFrame;
    check(receiveFrame(client, responseFrame, 1000), "a response frame is received back from CC");

    std::optional<message::MeasurementChunkResponse> response =
        message::parseMeasurementChunkResponse(responseFrame);
    check(response.has_value(), "the response parses as a valid MeasurementChunkResponse");
    if (response.has_value())
    {
        check(response->requestId == request.requestId, "the response echoes the original requestId");
        check(response->chunkSeq == 0, "chunkSeq is 0 for the only chunk");
        check(!response->moreDataFlag, "moreDataFlag is false - this is the only chunk");
        check(response->records.size() == 3, "exactly the 3 in-range records are returned (the 4th, out of range, is excluded)");
        if (response->records.size() == 3)
        {
            check(response->records[0].timestamp == 1000 && response->records[1].timestamp == 1010
                      && response->records[2].timestamp == 1020,
                  "records are the correct rows, in ascending timestamp order");
        }
    }

    /* Single-chunk response: nothing more should ever arrive. */
    uint8_t extraByte = 0;
    check(!client.receiveByte(extraByte, 200), "no further bytes arrive after the single chunk");

    client.disconnect();
    gs.close();
}

static void testEndToEndEventsSingleChunk()
{
    std::printf("\nTest (4d): end-to-end events request - single chunk, exact content verified\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:");
    dataStore.insertEvent(makeEventRecord(2000, TAG_EVENT_OBJECT_DETECTION));
    dataStore.insertEvent(makeEventRecord(2010, TAG_EVENT_OBJECT_DETECTION));
    dataStore.insertEvent(makeEventRecord(9000, TAG_EVENT_OBJECT_DETECTION)); /* outside the request range below */

    gs_communication::GsCommunication gs(dataStore);
    gs.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    gs.poll();

    message::TimeRangeMessage request;
    request.requestId = 22;
    request.startTime = 2000;
    request.endTime = 2010;
    client.sendBytes(message::buildGetEventsByRangeRequest(request));

    pollUntilDispatched(gs, 1);
    check(gs.framesDispatched == 1, "the request is recognized and dispatched");

    tlv::Frame responseFrame;
    check(receiveFrame(client, responseFrame, 1000), "a response frame is received back from CC");

    std::optional<message::EventChunkResponse> response = message::parseEventChunkResponse(responseFrame);
    check(response.has_value(), "the response parses as a valid EventChunkResponse");
    if (response.has_value())
    {
        check(response->requestId == request.requestId, "the response echoes the original requestId");
        check(response->chunkSeq == 0, "chunkSeq is 0 for the only chunk");
        check(!response->moreDataFlag, "moreDataFlag is false - this is the only chunk");
        check(response->records.size() == 2, "exactly the 2 in-range records are returned (the 3rd, out of range, is excluded)");
        if (response->records.size() == 2)
        {
            check(response->records[0].timestamp == 2000 && response->records[1].timestamp == 2010,
                  "records are the correct rows, in ascending timestamp order");
            check(response->records[0].description == "test event", "description round-trips correctly");
        }
    }

    client.disconnect();
    gs.close();
}

static void testEndToEndMeasurementsMultiChunk()
{
    std::printf("\nTest (4d): end-to-end measurements request - multiple chunks (14 records, MAX_MEASUREMENTS_PER_CHUNK = 11)\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:");
    const uint32_t kBaseTime = 3000;
    const int kRecordCount = 14;
    for (int i = 0; i < kRecordCount; i++)
    {
        dataStore.insertMeasurement(makeMeasurementSample(kBaseTime + static_cast<uint32_t>(i)));
    }

    gs_communication::GsCommunication gs(dataStore);
    gs.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    gs.poll();

    message::TimeRangeMessage request;
    request.requestId = 23;
    request.startTime = kBaseTime;
    request.endTime = kBaseTime + kRecordCount - 1;
    client.sendBytes(message::buildGetMeasurementsByRangeRequest(request));

    pollUntilDispatched(gs, 1);

    tlv::Frame firstFrame;
    check(receiveFrame(client, firstFrame, 1000), "the first chunk is received");
    std::optional<message::MeasurementChunkResponse> firstResponse =
        message::parseMeasurementChunkResponse(firstFrame);
    check(firstResponse.has_value(), "the first chunk parses correctly");
    if (firstResponse.has_value())
    {
        check(firstResponse->chunkSeq == 0, "first chunk has chunkSeq 0");
        check(firstResponse->moreDataFlag, "first chunk's moreDataFlag is true - more data follows");
        check(firstResponse->records.size() == MAX_MEASUREMENTS_PER_CHUNK,
              "first chunk holds exactly MAX_MEASUREMENTS_PER_CHUNK (11) records");
    }

    tlv::Frame secondFrame;
    check(receiveFrame(client, secondFrame, 1000), "the second chunk is received");
    std::optional<message::MeasurementChunkResponse> secondResponse =
        message::parseMeasurementChunkResponse(secondFrame);
    check(secondResponse.has_value(), "the second chunk parses correctly");
    if (secondResponse.has_value())
    {
        check(secondResponse->chunkSeq == 1, "second chunk has chunkSeq 1");
        check(!secondResponse->moreDataFlag, "second chunk's moreDataFlag is false - it's the last one");
        check(secondResponse->records.size() == kRecordCount - MAX_MEASUREMENTS_PER_CHUNK,
              "second chunk holds exactly the remaining 3 records");
    }

    if (firstResponse.has_value() && secondResponse.has_value())
    {
        bool allTimestampsCorrect = true;
        uint32_t expectedTimestamp = kBaseTime;
        for (const auto &sample : firstResponse->records)
        {
            allTimestampsCorrect = allTimestampsCorrect && (sample.timestamp == expectedTimestamp);
            expectedTimestamp++;
        }
        for (const auto &sample : secondResponse->records)
        {
            allTimestampsCorrect = allTimestampsCorrect && (sample.timestamp == expectedTimestamp);
            expectedTimestamp++;
        }
        check(allTimestampsCorrect && expectedTimestamp == kBaseTime + kRecordCount,
              "across both chunks, all 14 records appear exactly once, in ascending timestamp order");
    }

    uint8_t extraByte = 0;
    check(!client.receiveByte(extraByte, 200), "no third chunk / no further bytes arrive");

    client.disconnect();
    gs.close();
}

static void testEndToEndEventsMultiChunk()
{
    std::printf("\nTest (4d): end-to-end events request - multiple chunks (8 records, MAX_EVENTS_PER_CHUNK = 6)\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:");
    const uint32_t kBaseTime = 4000;
    const int kRecordCount = 8;
    for (int i = 0; i < kRecordCount; i++)
    {
        dataStore.insertEvent(makeEventRecord(kBaseTime + static_cast<uint32_t>(i), TAG_EVENT_OBJECT_DETECTION));
    }

    gs_communication::GsCommunication gs(dataStore);
    gs.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    gs.poll();

    message::TimeRangeMessage request;
    request.requestId = 24;
    request.startTime = kBaseTime;
    request.endTime = kBaseTime + kRecordCount - 1;
    client.sendBytes(message::buildGetEventsByRangeRequest(request));

    pollUntilDispatched(gs, 1);

    tlv::Frame firstFrame;
    check(receiveFrame(client, firstFrame, 1000), "the first chunk is received");
    std::optional<message::EventChunkResponse> firstResponse = message::parseEventChunkResponse(firstFrame);
    check(firstResponse.has_value(), "the first chunk parses correctly");
    if (firstResponse.has_value())
    {
        check(firstResponse->chunkSeq == 0, "first chunk has chunkSeq 0");
        check(firstResponse->moreDataFlag, "first chunk's moreDataFlag is true - more data follows");
        check(firstResponse->records.size() == MAX_EVENTS_PER_CHUNK,
              "first chunk holds exactly MAX_EVENTS_PER_CHUNK (6) records");
    }

    tlv::Frame secondFrame;
    check(receiveFrame(client, secondFrame, 1000), "the second chunk is received");
    std::optional<message::EventChunkResponse> secondResponse = message::parseEventChunkResponse(secondFrame);
    check(secondResponse.has_value(), "the second chunk parses correctly");
    if (secondResponse.has_value())
    {
        check(secondResponse->chunkSeq == 1, "second chunk has chunkSeq 1");
        check(!secondResponse->moreDataFlag, "second chunk's moreDataFlag is false - it's the last one");
        check(secondResponse->records.size() == kRecordCount - MAX_EVENTS_PER_CHUNK,
              "second chunk holds exactly the remaining 2 records");
    }

    client.disconnect();
    gs.close();
}

static void testEndToEndZeroMatchesStillSendsOneEmptyChunk()
{
    std::printf("\nTest (4d): a request matching zero rows still sends exactly one, empty, well-formed chunk\n");

    data_store::DataStore dataStore;
    dataStore.open(":memory:");
    /* Deliberately empty - no rows inserted at all. */

    gs_communication::GsCommunication gs(dataStore);
    gs.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    gs.poll();

    message::TimeRangeMessage request;
    request.requestId = 25;
    request.startTime = 1;
    request.endTime = 2;
    client.sendBytes(message::buildGetMeasurementsByRangeRequest(request));

    pollUntilDispatched(gs, 1);

    tlv::Frame responseFrame;
    check(receiveFrame(client, responseFrame, 1000), "a response frame is received even though nothing matched");

    std::optional<message::MeasurementChunkResponse> response =
        message::parseMeasurementChunkResponse(responseFrame);
    check(response.has_value(), "the empty response still parses as a valid MeasurementChunkResponse");
    if (response.has_value())
    {
        check(response->requestId == request.requestId, "requestId is still echoed correctly");
        check(response->chunkSeq == 0, "chunkSeq is 0");
        check(!response->moreDataFlag, "moreDataFlag is false");
        check(response->records.empty(), "records is empty");
    }

    client.disconnect();
    gs.close();
}

int main()
{
    std::printf("=== gs_communication.cpp test suite (Step 4c + 4d) ===\n");
    std::printf("(uses a real local loopback client - no Ground Station binary or network hardware required)\n");

    testInitialStateBeforeListening();
    testStartListeningAndAcceptRealClient();
    testRecognizesGetMeasurementsByRangeRequest();
    testRecognizesGetEventsByRangeRequest();
    testUnknownTagDroppedSilently();
    testCorruptedFrameIncrementsDecodeErrors();
    testWrongLengthPayloadNotDispatched();
    testFeedByteDirectSeamWithoutRealSocket();
    testDisconnectThenReconnectStillDispatches();
    testEndToEndMeasurementsSingleChunk();
    testEndToEndEventsSingleChunk();
    testEndToEndMeasurementsMultiChunk();
    testEndToEndEventsMultiChunk();
    testEndToEndZeroMatchesStillSendsOneEmptyChunk();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
