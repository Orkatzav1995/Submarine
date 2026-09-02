/*
 * serial_transport.h
 *
 * Purpose:
 *   Thin wrapper around a Windows COM port - the Central Computer's link
 *   to the LNC (the same physical USART2 link, seen from the PC side via
 *   the Nucleo board's ST-LINK virtual COM port). Sends and receives raw
 *   bytes only - it has no idea what a TLV frame is, mirroring the LNC's
 *   comm_transport_uart.h exactly in spirit.
 *
 * Layer:
 *   Transport (Transport -> Protocol -> Message -> Application)
 *
 * Responsibilities:
 *   - Open/close a named COM port at a given baud rate (8 data bits, no
 *     parity, 1 stop bit - matches the LNC's fixed USART2 framing).
 *   - Send raw bytes, with a timeout.
 *   - Receive one raw byte at a time, with a timeout - same contract as
 *     the LNC's Transport_UART_ReceiveByte: returns false on timeout,
 *     which is normal, not an error.
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, CRC, or any TLV concept.
 *   - Know about sockets (this is the LNC-facing serial link only, not
 *     the CC <-> Ground Station Ethernet link).
 */

#ifndef CENTRAL_COMPUTER_SERIAL_TRANSPORT_H
#define CENTRAL_COMPUTER_SERIAL_TRANSPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace transport {

class SerialPort
{
public:
    SerialPort() = default;
    ~SerialPort();

    /* A SerialPort owns one OS handle - copying it would let two objects
       both try to close the same handle, so copying is disabled outright. */
    SerialPort(const SerialPort &) = delete;
    SerialPort &operator=(const SerialPort &) = delete;

    /*
     * Opens "portName" (e.g. "COM10") at "baudRate", 8-N-1 framing.
     * Returns true on success. Closes any previously open port first.
     */
    bool open(const std::string &portName, uint32_t baudRate);

    void close();

    bool isOpen() const;

    /*
     * Sends every byte in "data". Returns true on success, false on
     * failure or timeout.
     */
    bool send(const std::vector<uint8_t> &data, uint32_t timeoutMs);

    /*
     * Tries to receive exactly one byte, waiting at most "timeoutMs"
     * milliseconds. Returns true and writes the received byte into
     * "outByte" if one arrived in time. Returns false if the timeout
     * elapsed with no byte - this is normal, not an error.
     */
    bool receiveByte(uint8_t &outByte, uint32_t timeoutMs);

private:
    void *handle_ = nullptr;  /* really a Windows HANDLE - see serial_transport.cpp */
};

}  // namespace transport

#endif  // CENTRAL_COMPUTER_SERIAL_TRANSPORT_H
