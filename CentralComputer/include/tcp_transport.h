/*
 * tcp_transport.h
 *
 * Purpose:
 *   The Central Computer's listening side of the CC<->Ground Station link
 *   - Sec 1.2/4's "Ethernet" (simulated on the same PC, no physical
 *     networking hardware anywhere in this project, same spirit as the
 *     LNC<->CC UART link being real hardware confined to one PC's
 *     peripherals). Sends and receives raw bytes only - it has no idea
 *     what a TLV frame is, mirroring CentralComputer/serial_transport.h
 *     and GroundStation/tcp_transport.h exactly in spirit and shape.
 *
 * Layer:
 *   Transport (Transport -> Protocol -> Message -> Application)
 *
 * Responsibilities:
 *   - Listen on a given port for an incoming connection from the Ground
 *     Station (CC is the listening side; GS is the connecting side - see
 *     PROJECT_GUIDE.md's Step 4 agreed decisions).
 *   - Accept at most one connected client at a time - GS is a
 *     single-operator tool, no multi-client support is needed (Sec 12:
 *     don't design for a hypothetical future requirement).
 *   - Support reconnection: losing the connected client (cleanly or via a
 *     socket error) drops only that client - the listening socket stays
 *     open so a fresh GS connection can be accepted later, matching CC's
 *     role as the long-running server.
 *   - Send raw bytes, with a timeout.
 *   - Receive one raw byte at a time, with a timeout - same contract as
 *     transport::SerialPort::receiveByte() and GroundStation's
 *     transport::TcpSocket::receiveByte(): returns false on timeout OR
 *     disconnection, without distinguishing the two.
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, CRC, or any TLV concept.
 *   - Know about LNC commands, message types, or any application-level
 *     protocol semantics - it moves bytes, nothing else.
 *   - Connect out to anything - this class only ever listens/accepts. A
 *     connecting counterpart is GroundStation/tcp_transport.h's own,
 *     separate concern.
 *   - Know about serial ports - this is the CC<->Ground Station Ethernet
 *     link only, entirely separate from CC's own LNC-facing
 *     serial_transport.h.
 */

#ifndef CENTRAL_COMPUTER_TCP_TRANSPORT_H
#define CENTRAL_COMPUTER_TCP_TRANSPORT_H

#include <cstdint>
#include <vector>

namespace transport {

class TcpServerSocket
{
public:
    TcpServerSocket() = default;
    ~TcpServerSocket();

    /* A TcpServerSocket owns OS socket handles - copying it would let two
       objects both try to close the same handles, so copying is disabled
       outright (same reasoning as transport::SerialPort and
       transport::TcpSocket). */
    TcpServerSocket(const TcpServerSocket &) = delete;
    TcpServerSocket &operator=(const TcpServerSocket &) = delete;

    /*
     * Binds all local interfaces on "port" and starts listening. Returns
     * true on success. Closes any previous listening/client state first.
     */
    bool startListening(uint16_t port);

    /*
     * Stops listening AND drops any connected client. Use this for a full
     * shutdown - to drop just a lost client while continuing to listen,
     * no separate call is needed: send()/receiveByte() already do that
     * internally the moment they detect the client is gone.
     */
    void close();

    bool isListening() const;

    bool isClientConnected() const;

    /*
     * Non-blocking: if no client is currently connected and one is
     * waiting to be accepted, accepts it and returns true. Returns false
     * otherwise (nothing pending, not listening, or a client is already
     * connected - only one client at a time is ever accepted). Safe to
     * call every poll cycle; never blocks.
     */
    bool tryAcceptClient();

    /*
     * Sends every byte in "data", looping internally if the OS accepts
     * fewer bytes than requested in one call. Returns true on success,
     * false on failure, timeout, or if no client is connected. A send
     * failure drops the client (see close()'s comment) but leaves the
     * listening socket open.
     */
    bool send(const std::vector<uint8_t> &data, uint32_t timeoutMs);

    /*
     * Tries to receive exactly one byte from the connected client,
     * waiting at most "timeoutMs" milliseconds. Returns true and writes
     * the received byte into "outByte" if one arrived in time. Returns
     * false if the timeout elapsed with no byte, if the client
     * disconnected, or if no client is connected - this function does
     * not distinguish those cases, same contract as SerialPort's and
     * TcpSocket's receiveByte(). A detected disconnect drops the client
     * (see close()'s comment) but leaves the listening socket open.
     */
    bool receiveByte(uint8_t &outByte, uint32_t timeoutMs);

private:
    void closeClient();

    void *listenHandle_ = nullptr;  /* really a Winsock SOCKET - see tcp_transport.cpp */
    void *clientHandle_ = nullptr;  /* really a Winsock SOCKET, or nullptr if no client is connected */
};

}  // namespace transport

#endif  // CENTRAL_COMPUTER_TCP_TRANSPORT_H
