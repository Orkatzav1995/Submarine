/*
 * tcp_transport.cpp
 *
 * Implements the TCP client wrapper declared in tcp_transport.h using the
 * Win32 Winsock2 API - this codebase is already Windows-only (see
 * serial_transport.cpp), so this follows the same platform choice rather
 * than introducing a second one.
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
   calling them once per connect()/close() (rather than once globally) is
   simple and safe even with multiple TcpSocket instances - same "keep it
   simple, don't build a separate init mechanism" choice Sec 12 prefers. */
bool startWinsock()
{
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

}  // namespace

TcpSocket::~TcpSocket()
{
    close();
}

bool TcpSocket::connect(const std::string &host, uint16_t port)
{
    close();  /* in case a different connection was already open */

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

    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
    {
        closesocket(sock);
        WSACleanup();
        return false;  /* not a valid IPv4 address string */
    }

    if (::connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        closesocket(sock);
        WSACleanup();
        return false;
    }

    handle_ = reinterpret_cast<void *>(sock);
    return true;
}

void TcpSocket::close()
{
    if (handle_ != nullptr)
    {
        SOCKET sock = reinterpret_cast<SOCKET>(handle_);
        closesocket(sock);
        WSACleanup();
        handle_ = nullptr;
    }
}

bool TcpSocket::isConnected() const
{
    return handle_ != nullptr;
}

bool TcpSocket::send(const std::vector<uint8_t> &data, uint32_t timeoutMs)
{
    if (handle_ == nullptr || data.empty())
    {
        return false;
    }

    SOCKET sock = reinterpret_cast<SOCKET>(handle_);

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
            return false;
        }
        totalSent += static_cast<size_t>(sent);
    }

    return true;
}

bool TcpSocket::receiveByte(uint8_t &outByte, uint32_t timeoutMs)
{
    if (handle_ == nullptr)
    {
        return false;
    }

    SOCKET sock = reinterpret_cast<SOCKET>(handle_);

    /* Reconfiguring the receive timeout on every call is simpler to
       reason about than caching the last value, and the extra syscall is
       not a real cost on a PC - same reasoning serial_transport.cpp's
       receiveByte() already documents for its own COMMTIMEOUTS call. */
    DWORD timeout = timeoutMs;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));

    char byte = 0;
    int received = recv(sock, &byte, 1, 0);

    if (received != 1)
    {
        /* received == 0: the peer closed the connection cleanly.
           received == SOCKET_ERROR with WSAETIMEDOUT: the timeout elapsed,
           no byte arrived - normal, not an error. This function does not
           distinguish the two, same contract as SerialPort::receiveByte(). */
        return false;
    }

    outByte = static_cast<uint8_t>(byte);
    return true;
}

}  // namespace transport
