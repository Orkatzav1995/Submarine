/*
 * gs_communication.h
 *
 * Purpose:
 *   The Central Computer's Ground-Station-facing "listener" - the CC-side
 *   counterpart to communication::Communication (which faces the LNC).
 *   Owns the TCP server transport, decodes incoming bytes into TLV frames,
 *   and recognizes the Ground Station's own request tags. This is the
 *   CC<->GS analog of Communication's CC<->LNC role - same idea, a
 *   different transport (TCP instead of a serial port) and a different,
 *   much smaller tag vocabulary (GS only ever sends the 2 "retrieve by
 *   range" requests; it has no configuration/keep-alive/event traffic).
 *
 * Layer:
 *   Sits above Transport/Protocol/Message, below Application.
 *   (Transport -> Protocol -> Message -> GsCommunication -> Application)
 *
 * Scope as of Step 4d (see PROJECT_GUIDE.md):
 *   - Accept a Ground Station connection, decode bytes into frames, and
 *     recognize (parse) the two "retrieve by range" request tags via the
 *     Step 4b parsers - incrementing framesDispatched on each successful
 *     parse, same observability convention as communication::Communication.
 *   - On a successful parse, now (Step 4d) actually answers the request:
 *     queries dataStore_ for the matching rows, splits them into chunks of
 *     at most MAX_MEASUREMENTS_PER_CHUNK/MAX_EVENTS_PER_CHUNK records, and
 *     sends each chunk back using the existing Step 1 builders
 *     (buildMeasurementChunkResponse/buildEventChunkResponse) - see
 *     sendMeasurementsForRange()/sendEventsForRange() below. Fully
 *     synchronous, as agreed: CC already holds all the data locally, so
 *     the whole reply is built and sent within the same dispatch() call
 *     that received the request - no async correlation bookkeeping.
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, or CRC internals (Protocol's job).
 *   - Write to the database - only ever reads (getMeasurementsInRange/
 *     getEventsInRange), same read-only relationship report_generator
 *     already has with DataStore.
 *   - Touch cc_main.cpp or any application wiring - still not done, a
 *     later step.
 */

#ifndef CENTRAL_COMPUTER_GS_COMMUNICATION_H
#define CENTRAL_COMPUTER_GS_COMMUNICATION_H

#include "data_store.h"
#include "message.h"
#include "tcp_transport.h"
#include "tlv_codec.h"
#include <cstdint>

namespace gs_communication {

class GsCommunication
{
public:
    /* Non-owning reference, same pattern as ManagementCommand holding
       Communication& - not yet used anywhere in this class (see this
       file's header comment); stored now so later steps don't need to
       change this constructor's signature. */
    explicit GsCommunication(data_store::DataStore &dataStore);

    /* Observability counters - mirrors communication::Communication's
       framesDispatched/decodeErrors exactly. Public, read-only in spirit
       (tests and callers may read them; only this class writes them). */
    uint32_t framesDispatched = 0;
    uint32_t decodeErrors = 0;

    /*
     * Binds "port" and starts listening for a Ground Station connection.
     * Returns true on success. Closes any previous listening/client state
     * first (see transport::TcpServerSocket::startListening()).
     */
    bool startListening(uint16_t port);

    void close();

    bool isListening() const;

    bool isClientConnected() const;

    /*
     * Accepts a pending connection if none is connected yet, then makes at
     * most one byte of decode progress if one is - mirrors
     * communication::Communication::poll()'s single do-everything
     * non-blocking shape exactly. Call this repeatedly from the CC's main
     * loop (once wired in - not yet done, see this file's header comment).
     */
    void poll();

    /*
     * Feeds one byte into the TLV decoder. On a complete, valid frame:
     * dispatches it (see dispatch()). On a decode error (bad CRC/length):
     * increments decodeErrors. Public so tests can drive it directly with
     * hand-built frames, without a real TCP connection - same seam
     * communication::Communication::feedByte() already provides.
     */
    void feedByte(uint8_t byte);

private:
    void dispatch(const tlv::Frame &frame);

    /*
     * Each queries dataStore_ for the matching rows, splits them into
     * groups of at most MAX_MEASUREMENTS_PER_CHUNK/MAX_EVENTS_PER_CHUNK
     * records (tlv_common.h), and sends every chunk via serverTransport_,
     * echoing request.requestId in each one. A request matching zero rows
     * still sends exactly one chunk (moreDataFlag = false, zero records) -
     * same "always answer, even with nothing" contract
     * buildMeasurementChunkResponse/buildEventChunkResponse's own
     * zero-record case already supports (Step 1).
     */
    void sendMeasurementsForRange(const message::TimeRangeMessage &request);
    void sendEventsForRange(const message::TimeRangeMessage &request);

    transport::TcpServerSocket serverTransport_;
    tlv::Decoder decoder_;
    data_store::DataStore &dataStore_;
};

}  // namespace gs_communication

#endif  // CENTRAL_COMPUTER_GS_COMMUNICATION_H
