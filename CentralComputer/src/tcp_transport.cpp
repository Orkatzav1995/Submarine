/*
 * tcp_transport.cpp
 *
 * Implements the TCP server wrapper declared in tcp_transport.h using the
 * Win32 Winsock2 API - this codebase is already Windows-only (see
 * serial_transport.cpp and GroundStation/tcp_transport.cpp), so this
 * follows the same platform choice rather than introducing a second one.
 *
 * Build note: link with -lws2_32 (MinGW/g++) or ws2_32.lib (MSVC).
 *
 * Layer:
 *   Transport
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, CRC, or any TLV concept.
 */

#include "tcp_transport.h"

/* winsock2.h must be included before any accidental transitive windows.h -
   this file never includes windows.h directly, but this order is the
   standard, safe convention regardless. */
#include <winsock2.h>
#include <ws2tcpip.h>

namespace transport {

namespace {

/* Winsock reference-counts WSAStartup/WSACleanup pairs internally, so
   calling them once per startListening()/close() (rather than once
   globally) is simple and safe even with multiple TcpServerSocket
   instances - same "keep it simple, don't build a separate init
   mechanism" choice Sec 12 prefers, matching GroundStation/tcp_transport.cpp. */
bool startWinsock()
{
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

}  // namespace

TcpServerSocket::~TcpServerSocket()
{
    close();
}

bool TcpServerSocket::startListening(uint16_t port)
{
    close();  /* in case this instance was already listening */

    if (!startWinsock())
    {
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        WSACleanup();
        return false;
    }

    /* SO_EXCLUSIVEADDRUSE, not SO_REUSEADDR: on Windows, SO_REUSEADDR lets
       a second socket bind a port that already has an ACTIVE listener on
       it (unlike POSIX, where it only permits reusing a port stuck in
       TIME_WAIT) - it would silently defeat the "only one CC can listen
       on this port" guarantee. SO_EXCLUSIVEADDRUSE is the documented
       Windows-specific fix: it still allows a clean re-bind after this
       socket is properly closed, while blocking a second live listener
       from hijacking the port in the meantime. */
    BOOL exclusiveAddr = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char *>(&exclusiveAddr), sizeof(exclusiveAddr));

    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        closesocket(sock);
        WSACleanup();
        return false;
    }

    /* Backlog of 1: GS is a single-operator tool, only one client is ever
       expected at a time (Sec 12 - don't design for a hypothetical
       multi-client future). */
    if (listen(sock, 1) == SOCKET_ERROR)
    {
        closesocket(sock);
        WSACleanup();
        return false;
    }

    listenHandle_ = reinterpret_cast<void *>(sock);
    return true;
}

void TcpServerSocket::closeClient()
{
    if (clientHandle_ != nullptr)
    {
        SOCKET sock = reinterpret_cast<SOCKET>(clientHandle_);
        closesocket(sock);
        clientHandle_ = nullptr;
    }
}

void TcpServerSocket::close()
{
    closeClient();

    if (listenHandle_ != nullptr)
    {
        SOCKET sock = reinterpret_cast<SOCKET>(listenHandle_);
        closesocket(sock);
        listenHandle_ = nullptr;
        WSACleanup();
    }
}

bool TcpServerSocket::isListening() const
{
    return listenHandle_ != nullptr;
}

bool TcpServerSocket::isClientConnected() const
{
    return clientHandle_ != nullptr;
}

bool TcpServerSocket::tryAcceptClient()
{
    if (listenHandle_ == nullptr || clientHandle_ != nullptr)
    {
        /* Not listening, or a client is already connected - a second
           pending connection attempt simply waits in the OS backlog until
           this one disconnects; single-client-at-a-time by design. */
        return false;
    }

    SOCKET listenSock = reinterpret_cast<SOCKET>(listenHandle_);

    /* Non-blocking peek: select() with a zero timeout reports whether
       accept() would succeed immediately, without ever blocking - needed
       because tryAcceptClient() is meant to be called every poll() cycle
       and must never stall waiting for a connection that may never come. */
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(listenSock, &readSet);
    timeval zeroTimeout;
    zeroTimeout.tv_sec = 0;
    zeroTimeout.tv_usec = 0;

    int result = select(0, &readSet, nullptr, nullptr, &zeroTimeout);
    if (result <= 0)
    {
        return false;
    }

    SOCKET clientSock = accept(listenSock, nullptr, nullptr);
    if (clientSock == INVALID_SOCKET)
    {
        return false;
    }

    clientHandle_ = reinterpret_cast<void *>(clientSock);
    return true;
}

bool TcpServerSocket::send(const std::vector<uint8_t> &data, uint32_t timeoutMs)
{
    if (clientHandle_ == nullptr || data.empty())
    {
        return false;
    }

    SOCKET sock = reinterpret_cast<SOCKET>(clientHandle_);

    DWORD timeout = timeoutMs;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));

    /* TCP's send() is not transactional the way the serial port's
       WriteFile is - it can accept fewer bytes than requested in one
       call, so every byte must be confirmed sent before returning true. */
    size_t totalSent = 0;
    while (totalSent < data.size())
    {
        int sent = ::send(sock, reinterpret_cast<const char *>(data.data() + totalSent),
                           static_cast<int>(data.size() - totalSent), 0);
        if (sent == SOCKET_ERROR || sent == 0)
        {
            closeClient();  /* lost the client - keep listening for a new one */
            return false;
        }
        totalSent += static_cast<size_t>(sent);
    }

    return true;
}

bool TcpServerSocket::receiveByte(uint8_t &outByte, uint32_t timeoutMs)
{
    if (clientHandle_ == nullptr)
    {
        return false;
    }

    SOCKET sock = reinterpret_cast<SOCKET>(clientHandle_);

    /* Reconfiguring the receive timeout on every call is simpler to
       reason about than caching the last value, and the extra syscall is
       not a real cost on a PC - same reasoning serial_transport.cpp's and
       GroundStation/tcp_transport.cpp's receiveByte() already document. */
    DWORD timeout = timeoutMs;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));

    char byte = 0;
    int received = recv(sock, &byte, 1, 0);

    if (received == 0)
    {
        /* The peer closed the connection cleanly - drop just the client;
           the listening socket stays open for the next GS connection
           (PROJECT_GUIDE.md's Step 4 "reconnection is supported" decision). */
        closeClient();
        return false;
    }

    if (received == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSAETIMEDOUT)
        {
            /* A real error, not a timeout - treat it the same as a lost
               client so a broken socket cannot wedge future polls. */
            closeClient();
        }
        return false;
    }

    outByte = static_cast<uint8_t>(byte);
    return true;
}

}  // namespace transport
