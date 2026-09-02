/*
 * serial_transport.cpp
 *
 * Purpose:
 *   Implements the COM port wrapper declared in serial_transport.h using
 *   the Win32 serial API (CreateFile/ReadFile/WriteFile/SetCommState).
 *
 * Layer:
 *   Transport
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, CRC, or any TLV concept.
 */

#include "serial_transport.h"
#include <windows.h>

namespace transport {

namespace {

/*
 * COM10 and above need a "\\.\" prefix on Windows (CreateFile("COM10")
 * fails, CreateFile("\\\\.\\COM10") works; COM1-COM9 accept either form).
 * Always adding the prefix keeps this correct for every port number.
 */
std::string normalizePortName(const std::string &portName)
{
    const std::string prefix = "\\\\.\\";
    if (portName.rfind(prefix, 0) == 0)
    {
        return portName;
    }
    return prefix + portName;
}

}  // namespace

SerialPort::~SerialPort()
{
    close();
}

bool SerialPort::open(const std::string &portName, uint32_t baudRate)
{
    close();  /* in case a different port was already open */

    std::string fullName = normalizePortName(portName);

    HANDLE h = CreateFileA(fullName.c_str(),
                           GENERIC_READ | GENERIC_WRITE,
                           0,        /* no sharing */
                           nullptr,
                           OPEN_EXISTING,
                           0,        /* no FILE_FLAG_OVERLAPPED - simple blocking I/O */
                           nullptr);

    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DCB dcb;
    ZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(h, &dcb))
    {
        CloseHandle(h);
        return false;
    }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;

    if (!SetCommState(h, &dcb))
    {
        CloseHandle(h);
        return false;
    }

    handle_ = h;
    return true;
}

void SerialPort::close()
{
    if (handle_ != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

bool SerialPort::isOpen() const
{
    return handle_ != nullptr;
}

bool SerialPort::send(const std::vector<uint8_t> &data, uint32_t timeoutMs)
{
    if (handle_ == nullptr || data.empty())
    {
        return false;
    }

    COMMTIMEOUTS timeouts;
    ZeroMemory(&timeouts, sizeof(timeouts));
    timeouts.WriteTotalTimeoutConstant = timeoutMs;
    SetCommTimeouts(static_cast<HANDLE>(handle_), &timeouts);

    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(static_cast<HANDLE>(handle_), data.data(),
                        static_cast<DWORD>(data.size()), &bytesWritten, nullptr);

    return ok && (bytesWritten == data.size());
}

bool SerialPort::receiveByte(uint8_t &outByte, uint32_t timeoutMs)
{
    if (handle_ == nullptr)
    {
        return false;
    }

    /* ReadIntervalTimeout = MAXDWORD together with a 0 multiplier and this
       constant is the documented Win32 idiom for "return after exactly
       timeoutMs ms, whether or not a byte arrived" - matching the LNC's
       Transport_UART_ReceiveByte contract exactly. Reconfiguring this on
       every call is simpler to reason about than caching the last value,
       and the extra syscall is not a real cost on a PC. */
    COMMTIMEOUTS timeouts;
    ZeroMemory(&timeouts, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = timeoutMs;
    SetCommTimeouts(static_cast<HANDLE>(handle_), &timeouts);

    DWORD bytesRead = 0;
    BOOL ok = ReadFile(static_cast<HANDLE>(handle_), &outByte, 1, &bytesRead, nullptr);

    return ok && (bytesRead == 1);
}

}  // namespace transport
