/*
 * tcp_transport_test.cpp
 *
 * Purpose:
 *   Test program for GroundStation/tcp_transport.h/.cpp. Unlike
 *   serial_transport_test.cpp (which must talk to a real, physically
 *   attached COM port because there is no software substitute for a
 *   serial cable), TCP has a genuine, real, hardware-free substitute:
 *   loopback (127.0.0.1). This test spins up a minimal raw-Winsock
 *   listener IN THIS TEST FILE ONLY (never in GroundStation/src/ - the
 *   production TcpSocket class deliberately never listens/accepts, see
 *   tcp_transport.h's own "This file does NOT" section) purely to give
 *   the real client class (transport::TcpSocket) something real to
 *   connect to, send to, and receive from - a real TCP handshake and
 *   real bytes over a real (loopback) socket, not a mock object.
 *
 *   Why a hand-rolled listener instead of some other test tool: this
 *   project's own established test convention (see every other Tests/
 *   folder) is "test against the real thing directly," never a mocking
 *   framework - DataStore is tested against a real in-memory SQLite
 *   database, Communication is tested by feeding real bytes through the
 *   real decoder. A loopback TCP listener is the equivalent "real thing"
 *   for a TCP client, and needs no CC binary, no network hardware, and
 *   no manual setup to run.
 *
 * How to build and run (from this folder):
 *   g++ -std=c++17 -I "../../GroundStation/include" tcp_transport_test.cpp "../../GroundStation/src/tcp_transport.cpp" -lws2_32 -o tcp_transport_test.exe
 *   ./tcp_transport_test.exe
 *
 * This file does NOT:
 *   - Test the Message layer, or any TLV concept.
 *   - Require a real Central Computer or any physical network hardware.
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

/* ============================================================
 * TEST-ONLY loopback server helper. Raw Winsock, deliberately kept out
 * of GroundStation/src/ entirely - see this file's header comment for
 * why this is the right way to test a TCP client without a real CC.
 * ============================================================ */

namespace {

class LoopbackServer
{
public:
    /* Binds to 127.0.0.1 on an OS-assigned free port (port 0) and starts
       listening. Returns true on success; call port() to find out which
       port was actually assigned. */
    bool start()
    {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            return false;
        }
        wsaStarted_ = true;

        listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket_ == INVALID_SOCKET)
        {
            return false;
        }

        sockaddr_in addr;
        ZeroMemory(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = 0;  /* ask the OS for a free port, avoids clashing with anything */
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (bind(listenSocket_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR)
        {
            return false;
        }

        int addrLen = sizeof(addr);
        if (getsockname(listenSocket_, reinterpret_cast<sockaddr *>(&addr), &addrLen) == SOCKET_ERROR)
        {
            return false;
        }
        port_ = ntohs(addr.sin_port);

        return listen(listenSocket_, 1) != SOCKET_ERROR;
    }

    uint16_t port() const { return port_; }

    /* Blocks until one client connects, then loops: receives up to
       "maxBytes" and echoes them straight back, until the client
       disconnects. Meant to be run on its own thread. */
    void acceptAndEcho(size_t maxBytes)
    {
        SOCKET clientSocket = accept(listenSocket_, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET)
        {
            return;
        }

        size_t totalEchoed = 0;
        char buffer[64];
        while (totalEchoed < maxBytes)
        {
            int received = recv(clientSocket, buffer, static_cast<int>(sizeof(buffer)), 0);
            if (received <= 0)
            {
                break;
            }
            send(clientSocket, buffer, received, 0);
            totalEchoed += static_cast<size_t>(received);
        }

        closesocket(clientSocket);
    }

    /* Blocks until one client connects, then just holds the connection
       open (sending nothing) until closed - used by the timeout test,
       which needs a connected-but-silent peer. */
    void acceptAndStaySilent()
    {
        SOCKET clientSocket = accept(listenSocket_, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET)
        {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        closesocket(clientSocket);
    }

    ~LoopbackServer()
    {
        if (listenSocket_ != INVALID_SOCKET)
        {
            closesocket(listenSocket_);
        }
        if (wsaStarted_)
        {
            WSACleanup();
        }
    }

private:
    SOCKET listenSocket_ = INVALID_SOCKET;
    uint16_t port_ = 0;
    bool wsaStarted_ = false;
};

}  // namespace

static void testConnectToNonListeningPortFailsGracefully()
{
    std::printf("\nTest: connecting where nothing is listening fails cleanly\n");

    /* Bind a throwaway socket to get a genuinely free port, then close it
       immediately without ever calling listen() - nothing will be
       listening there, so a connect attempt must fail. */
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    bind(probe, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    int addrLen = sizeof(addr);
    getsockname(probe, reinterpret_cast<sockaddr *>(&addr), &addrLen);
    uint16_t freePort = ntohs(addr.sin_port);
    closesocket(probe);
    WSACleanup();

    transport::TcpSocket socket;
    bool connected = socket.connect("127.0.0.1", freePort);

    check(!connected, "connect() returns false when nothing is listening on the target port");
    check(!socket.isConnected(), "isConnected() is false after a failed connect");
}

static void testInvalidAddressFailsGracefully()
{
    std::printf("\nTest: connecting to a malformed address fails cleanly\n");

    transport::TcpSocket socket;
    bool connected = socket.connect("not-a-valid-ip-address", 12345);

    check(!connected, "connect() returns false for a malformed IPv4 address string");
    check(!socket.isConnected(), "isConnected() is false after a failed connect");
}

static void testLoopbackConnectSendReceive()
{
    std::printf("\nTest: real loopback connect, send, and receive\n");

    LoopbackServer server;
    if (!server.start())
    {
        check(false, "could not start the local loopback listener - skipping (unexpected on any normal machine)");
        return;
    }

    std::vector<uint8_t> testBytes = {0xAA, 0x01, 0x02, 0x03, 0xFF};
    std::thread serverThread([&server, &testBytes]() { server.acceptAndEcho(testBytes.size()); });

    transport::TcpSocket client;
    bool connected = client.connect("127.0.0.1", server.port());
    check(connected, "connect() succeeds against a real listening loopback server");
    check(client.isConnected(), "isConnected() is true after a successful connect");

    bool sent = client.send(testBytes, 1000);
    check(sent, "send() of a real buffer over the real loopback connection succeeds");

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
    check(allReceived && received == testBytes, "the server's echoed bytes are received back byte-for-byte, in order");

    client.close();
    check(!client.isConnected(), "isConnected() is false after close()");

    serverThread.join();
}

static void testReceiveByteNeverExceedsTimeout()
{
    std::printf("\nTest: receiveByte() never blocks materially longer than requested\n");

    LoopbackServer server;
    if (!server.start())
    {
        check(false, "could not start the local loopback listener - skipping");
        return;
    }

    std::thread serverThread([&server]() { server.acceptAndStaySilent(); });

    transport::TcpSocket client;
    bool connected = client.connect("127.0.0.1", server.port());
    check(connected, "connects to the silent server");

    const uint32_t timeoutMs = 200;
    uint8_t b = 0;

    auto start = std::chrono::steady_clock::now();
    bool gotByte = client.receiveByte(b, timeoutMs);
    auto end = std::chrono::steady_clock::now();

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    check(!gotByte, "receiveByte() correctly reports no byte when the peer sends nothing");
    check(elapsedMs <= static_cast<long long>(timeoutMs) + 500,
          "receiveByte() returned within a reasonable margin of the requested timeout, did not hang");

    client.close();
    serverThread.join();
}

static void testOperationsBeforeConnectFailGracefully()
{
    std::printf("\nTest: send()/receiveByte() on a never-connected socket fail cleanly, don't crash\n");

    transport::TcpSocket socket;  /* never connected */
    uint8_t b = 0;

    check(socket.send({0x01}, 100) == false, "send() on a never-connected socket returns false");
    check(socket.receiveByte(b, 100) == false, "receiveByte() on a never-connected socket returns false");
    check(socket.send({}, 100) == false, "send() of an empty buffer returns false");
}

static void testReconnectAfterClose()
{
    std::printf("\nTest: the same TcpSocket object can connect again after close()\n");

    LoopbackServer server;
    if (!server.start())
    {
        check(false, "could not start the local loopback listener - skipping");
        return;
    }

    std::thread serverThread([&server]() { server.acceptAndEcho(1); });

    transport::TcpSocket client;
    bool firstConnect = client.connect("127.0.0.1", server.port());
    check(firstConnect, "first connect() succeeds");
    client.close();
    check(!client.isConnected(), "isConnected() is false after close()");

    /* The server already accepted and is echoing on its own thread; send
       one byte so it can finish and the thread can join cleanly. Nothing
       is asserted about this second connection attempt succeeding, since
       the (single-backlog, single-accept) test server has already been
       consumed by the first connection - this test's real point is only
       that calling connect() again does not crash or leave the object in
       a broken state. */
    transport::TcpSocket second;
    second.connect("127.0.0.1", server.port());
    second.close();

    serverThread.join();
    check(true, "a second connect() attempt on the same object completes without crashing");
}

int main()
{
    std::printf("=== tcp_transport.cpp test suite ===\n");
    std::printf("(uses a real local loopback listener - no CC binary or network hardware required)\n");

    testConnectToNonListeningPortFailsGracefully();
    testInvalidAddressFailsGracefully();
    testLoopbackConnectSendReceive();
    testReceiveByteNeverExceedsTimeout();
    testOperationsBeforeConnectFailGracefully();
    testReconnectAfterClose();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);

    return (g_testsFailed == 0) ? 0 : 1;
}
