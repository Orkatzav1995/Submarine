/*
 * tcp_transport.h
 *
 * Purpose:
 *   The Ground Station's link to the Central Computer - a TCP connection
 *   standing in for Sec 1.2/4's "Ethernet" (simulated on the same PC, no
 *   physical networking hardware anywhere in this project, same spirit as
 *   the LNC<->CC UART link being real hardware confined to one PC's
 *   peripherals). Sends and receives raw bytes only - it has no idea what
 *   a TLV frame is, mirroring CentralComputer/serial_transport.h exactly
 *   in spirit and shape.
 *
 * Layer:
 *   Transport (Transport -> Protocol -> Message -> Application)
 *
 * Responsibilities:
 *   - Connect to the CC at a given host/port (GS is the connecting side;
 *     the CC listens - see PROJECT_GUIDE.md's open design decisions).
 *   - Send raw bytes, with a timeout.
 *   - Receive one raw byte at a time, with a timeout - same contract as
 *     transport::SerialPort::receiveByte(): returns false on timeout,
 *     which is normal, not an error.
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, CRC, or any TLV concept.
 *   - Know about LNC commands, message types, or any application-level
 *     protocol semantics - it moves bytes, nothing else.
 *   - Listen for or accept incoming connections - GS only ever connects
 *     out to the CC. A listening/accepting counterpart is the CC's own,
 *     separate concern (not yet built).
 *   - Know about serial ports - this is the GS<->CC Ethernet link only,
 *     entirely separate from the CC's own LNC-facing serial_transport.h.
 */

#ifndef GROUND_STATION_TCP_TRANSPORT_H
#define GROUND_STATION_TCP_TRANSPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace transport {

class TcpSocket
{
public:
    TcpSocket() = default;
    ~TcpSocket();

    /* A TcpSocket owns one OS socket handle - copying it would let two
       objects both try to close the same handle, so copying is disabled
       outright (same reasoning as transport::SerialPort). */
    TcpSocket(const TcpSocket &) = delete;
    TcpSocket &operator=(const TcpSocket &) = delete;

    /*
     * Connects to "host" (e.g. "127.0.0.1") on "port". Returns true on
     * success. Closes any previously open connection first.
     */
    bool connect(const std::string &host, uint16_t port);

    void close();

    bool isConnected() const;

    /*
     * Sends every byte in "data", looping internally if the OS accepts
     * fewer bytes than requested in one call (a real possibility for TCP,
     * unlike a serial port's transactional WriteFile). Returns true on
     * success, false on failure or timeout.
     */
    bool send(const std::vector<uint8_t> &data, uint32_t timeoutMs);

    /*
     * Tries to receive exactly one byte, waiting at most "timeoutMs"
     * milliseconds. Returns true and writes the received byte into
     * "outByte" if one arrived in time. Returns false if the timeout
     * elapsed with no byte, or the connection was closed/lost - this
     * file does not distinguish the two, same as SerialPort's contract.
     */
    bool receiveByte(uint8_t &outByte, uint32_t timeoutMs);

private:
    void *handle_ = nullptr;  /* really a Winsock SOCKET - see tcp_transport.cpp.
                                  void* here for the same reason SerialPort uses
                                  void* for its HANDLE: keeps <winsock2.h>'s own
                                  macros/types out of every file that includes
                                  this header. */
};

}  // namespace transport

#endif  // GROUND_STATION_TCP_TRANSPORT_H
