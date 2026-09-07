/*
 * tcp_server_transport_test.cpp
 *
 * Purpose:
 *   Test program for CentralComputer/tcp_transport.h/.cpp
 *   (transport::TcpServerSocket - the CC's listening side of the
 *   CC<->Ground Station link). Mirrors Tests/TcpTransport/tcp_transport_test.cpp's
 *   approach but with the roles reversed: there, a test-only raw-Winsock
 *   LISTENER stood in for the not-yet-built CC so the real GS CLIENT class
 *   could be tested against something real. Here, a test-only raw-Winsock
 *   CLIENT stands in for the not-yet-built GS so the real CC SERVER class
 *   can be tested against something real - same "test against the real
 *   thing, never a mock" convention this project uses everywhere (real
 *   SQLite for DataStore, real byte decoding for Communication, a real
 *   loopback listener for GroundStation's TcpSocket).
 *
 *   TcpServerSocket has no port() getter (not part of the agreed API - see
 *   PROJECT_GUIDE.md's Step 4 design), so this test binds to a fixed,
 *   high, unlikely-to-collide port rather than an OS-assigned one.
 *
 * How to build and run (from this folder):
 *   g++ -std=c++17 -I "../../CentralComputer/include" tcp_server_transport_test.cpp "../../CentralComputer/src/tcp_transport.cpp" -lws2_32 -o tcp_server_transport_test.exe
 *   ./tcp_server_transport_test.exe
 *
 * This file does NOT:
 *   - Test the Message layer, GsCommunication, or any TLV concept.
 *   - Require a real Ground Station or any physical network hardware.
 */

#include "tcp_transport.h"
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

/* A fixed test port, deliberately far from the production port (5000) and
   any well-known service, to make an accidental collision on a dev
   machine unlikely. */
static const uint16_t kTestPort = 15931;

/* ============================================================
 * TEST-ONLY loopback client helper. Raw Winsock, deliberately kept out
 * of CentralComputer/src/ entirely - see this file's header comment for
 * why this is the right way to test a TCP server without a real GS.
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

    bool sendByte(uint8_t b)
    {
        char c = static_cast<char>(b);
        return send(sock_, &c, 1, 0) == 1;
    }

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

/* Gives an accept()/connect() race a moment to settle - both sides poll
   in a tight loop below, this is just an upper bound safety margin. */
void shortPause()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

}  // namespace

static void testStartListeningSucceedsAndIsListening()
{
    std::printf("\nTest: startListening() on a free port succeeds\n");

    transport::TcpServerSocket server;
    bool started = server.startListening(kTestPort);

    check(started, "startListening() succeeds on a currently-free port");
    check(server.isListening(), "isListening() is true after a successful startListening()");
    check(!server.isClientConnected(), "isClientConnected() is false before any client connects");

    server.close();
}

static void testTryAcceptClientFalseWhenNothingPending()
{
    std::printf("\nTest: tryAcceptClient() returns false when no connection is pending\n");

    transport::TcpServerSocket server;
    server.startListening(kTestPort);

    check(!server.tryAcceptClient(), "tryAcceptClient() returns false with nothing pending, does not block");

    server.close();
}

static void testAcceptRealClientConnectSendReceive()
{
    std::printf("\nTest: real loopback client connects, and send/receive both directions work\n");

    transport::TcpServerSocket server;
    check(server.startListening(kTestPort), "server starts listening");

    LoopbackClient client;
    bool connected = client.connectTo(kTestPort);
    check(connected, "a real client can connect to the listening server");

    shortPause();
    bool accepted = server.tryAcceptClient();
    check(accepted, "tryAcceptClient() accepts the pending real connection");
    check(server.isClientConnected(), "isClientConnected() is true after accepting");

    std::vector<uint8_t> testBytes = {0xAA, 0x01, 0x02, 0x03, 0xFF};
    bool sent = server.send(testBytes, 1000);
    check(sent, "server.send() of a real buffer succeeds");

    std::vector<uint8_t> received;
    bool allReceived = true;
    for (size_t i = 0; i < testBytes.size(); i++)
    {
        uint8_t b = 0;
        if (!client.receiveByte(b, 1000))
        {
            allReceived = false;
            break;
        }
        received.push_back(b);
    }
    check(allReceived && received == testBytes, "the client receives the server's bytes back, byte-for-byte, in order");

    bool clientSent = client.sendByte(0x42);
    check(clientSent, "client can send a byte to the server");

    uint8_t serverReceived = 0;
    bool serverGotIt = server.receiveByte(serverReceived, 1000);
    check(serverGotIt && serverReceived == 0x42, "server.receiveByte() reads the byte the client sent, correctly");

    client.disconnect();
    server.close();
}

static void testReceiveByteNeverExceedsTimeout()
{
    std::printf("\nTest: receiveByte() never blocks materially longer than requested\n");

    transport::TcpServerSocket server;
    server.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    server.tryAcceptClient();

    const uint32_t timeoutMs = 200;
    uint8_t b = 0;

    auto start = std::chrono::steady_clock::now();
    bool gotByte = server.receiveByte(b, timeoutMs);
    auto end = std::chrono::steady_clock::now();

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    check(!gotByte, "receiveByte() correctly reports no byte when the client sends nothing");
    check(elapsedMs <= static_cast<long long>(timeoutMs) + 500,
          "receiveByte() returned within a reasonable margin of the requested timeout, did not hang");

    client.disconnect();
    server.close();
}

static void testDisconnectDropsClientButKeepsListening()
{
    std::printf("\nTest: a client disconnecting drops only the client, listening continues (reconnection support)\n");

    transport::TcpServerSocket server;
    server.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    server.tryAcceptClient();
    check(server.isClientConnected(), "client is connected before disconnecting");

    client.disconnect();

    uint8_t b = 0;
    bool gotByte = server.receiveByte(b, 500);
    check(!gotByte, "receiveByte() returns false once the client has disconnected");
    check(!server.isClientConnected(), "isClientConnected() is false after the disconnect is detected");
    check(server.isListening(), "isListening() is STILL true - the listening socket was not affected");

    LoopbackClient secondClient;
    bool reconnected = secondClient.connectTo(kTestPort);
    check(reconnected, "a NEW client can connect to the still-listening server");

    shortPause();
    bool acceptedSecond = server.tryAcceptClient();
    check(acceptedSecond, "tryAcceptClient() accepts the new client after the old one disconnected");

    secondClient.disconnect();
    server.close();
}

static void testOperationsBeforeListeningFailGracefully()
{
    std::printf("\nTest: operations on a never-listening/never-connected server fail cleanly, don't crash\n");

    transport::TcpServerSocket server;  /* never listening */
    uint8_t b = 0;

    check(!server.tryAcceptClient(), "tryAcceptClient() before startListening() returns false");
    check(server.send({0x01}, 100) == false, "send() with no client connected returns false");
    check(server.receiveByte(b, 100) == false, "receiveByte() with no client connected returns false");
    check(server.send({}, 100) == false, "send() of an empty buffer returns false");
}

static void testSecondListenerOnSamePortFails()
{
    std::printf("\nTest: a second server cannot bind the same port a live listener already holds\n");

    transport::TcpServerSocket first;
    check(first.startListening(kTestPort), "first server starts listening");

    transport::TcpServerSocket second;
    bool secondStarted = second.startListening(kTestPort);
    check(!secondStarted, "a second startListening() on the same, still-bound port fails");

    first.close();
}

static void testCloseStopsListeningEntirely()
{
    std::printf("\nTest: close() stops listening and drops any connected client\n");

    transport::TcpServerSocket server;
    server.startListening(kTestPort);

    LoopbackClient client;
    client.connectTo(kTestPort);
    shortPause();
    server.tryAcceptClient();
    check(server.isClientConnected(), "client is connected before close()");

    server.close();

    check(!server.isListening(), "isListening() is false after close()");
    check(!server.isClientConnected(), "isClientConnected() is false after close()");
    check(!server.tryAcceptClient(), "tryAcceptClient() returns false after close(), does not crash");

    client.disconnect();
}

int main()
{
    std::printf("=== tcp_transport.cpp (CC TcpServerSocket) test suite ===\n");
    std::printf("(uses a real local loopback client - no GS binary or network hardware required)\n");

    testStartListeningSucceedsAndIsListening();
    testTryAcceptClientFalseWhenNothingPending();
    testAcceptRealClientConnectSendReceive();
    testReceiveByteNeverExceedsTimeout();
    testDisconnectDropsClientButKeepsListening();
    testOperationsBeforeListeningFailGracefully();
    testSecondListenerOnSamePortFails();
    testCloseStopsListeningEntirely();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
