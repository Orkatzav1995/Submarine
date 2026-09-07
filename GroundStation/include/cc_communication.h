/*
 * cc_communication.h
 *
 * Purpose:
 *   The Ground Station's link to the Central Computer - decodes incoming
 *   bytes into TLV frames, sends the 2 "retrieve by range" requests with a
 *   self-generated requestId (GS is the requester - CC only ever echoes
 *   it back, see PROJECT_GUIDE.md's Step 4 agreed asymmetry), correlates
 *   each arriving chunk response to the request that triggered it, and
 *   accumulates chunks until the reply is complete. This is the missing
 *   piece between GS's raw byte transport (tcp_transport.h) and its pure
 *   build/parse functions (message.h) - before this file, nothing on the
 *   GS side owned a tlv::Decoder or knew how to correlate a response to
 *   its request.
 *
 *   Named after the party it talks to (CC), the same way the CC's own
 *   gs_communication::GsCommunication is named after the party IT talks
 *   to (GS) - deliberately not reusing "GsCommunication" here, since this
 *   class is GS's link TO CC, not "GS's own communication."
 *
 *   Merges two roles CC keeps separate (communication::Communication for
 *   decode/dispatch, data_collection::DataCollection for request
 *   correlation/completion) into one class here, because GS's tag
 *   vocabulary is tiny (2 requests, 2 responses) - the same reasoning
 *   that already led the CC's own gs_communication::GsCommunication to
 *   merge decode+dispatch+DataStore-querying into one class instead of
 *   splitting it the way the LNC-facing side is split.
 *
 * Layer:
 *   Sits above Transport/Protocol/Message, below Application.
 *   (Transport -> Protocol -> Message -> CcCommunication -> Application)
 *
 * Responsibilities:
 *   - Own one transport::TcpSocket and one tlv::Decoder.
 *   - connect()/close()/isConnected(): thin forwarding to TcpSocket.
 *   - poll(): receive at most one byte if connected and feed the decoder -
 *     mirrors communication::Communication::poll()/
 *     gs_communication::GsCommunication::poll() exactly.
 *   - feedByte(): the actual decode+dispatch logic, exposed publicly so
 *     it can be unit-tested with hand-built frames - same seam
 *     Communication/GsCommunication already provide.
 *   - requestMeasurements()/requestEvents(): build the request via the
 *     existing message::buildGetMeasurementsByRangeRequest/
 *     buildGetEventsByRangeRequest, assign a self-incrementing requestId,
 *     send it, and remember the pending ID - mirrors
 *     DataCollection::requestMeasurementBackfill/requestEventBackfill,
 *     including its "no second request of the same type while one is
 *     already in flight" rule. A measurement request and an event
 *     request MAY be in flight at the same time (independent trackers) -
 *     only two of the *same* type may not overlap.
 *   - On a matching chunk response: accumulate its records in memory
 *     (unlike DataCollection, which saves each chunk straight to
 *     DataStore as it arrives - GS has no database, it only needs to
 *     retrieve and show data, Sec 4). On moreDataFlag == false: fire the
 *     matching completion callback with the full accumulated vector and
 *     clear the pending state. A chunk whose requestId doesn't match the
 *     current pending one is ignored (stale/unrelated) - same rule
 *     DataCollection already enforces.
 *   - Track framesDispatched/decodeErrors counters, same observability
 *     convention as Communication/GsCommunication.
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, or CRC internals (Protocol's job).
 *   - Persist anything - GS has no database.
 *   - Handle reconnection or in-flight-request recovery if the TCP
 *     connection drops mid-transfer - explicitly out of scope for Step 5,
 *     an open question for a later step (see PROJECT_GUIDE.md).
 *   - Build a console app or any UI - that is gs_main.cpp's job (Step 7,
 *     not yet built).
 */

#ifndef GROUND_STATION_CC_COMMUNICATION_H
#define GROUND_STATION_CC_COMMUNICATION_H

#include "message.h"
#include "tcp_transport.h"
#include "tlv_codec.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cc_communication {

class CcCommunication
{
public:
    CcCommunication() = default;

    /* Fired once the current request's last chunk (moreDataFlag == false)
       has arrived, with every record accumulated across all of that
       request's chunks. Left unset (default), completion is simply not
       announced anywhere. */
    std::function<void(const std::vector<message::MeasurementSample> &)> onMeasurementsReceived;
    std::function<void(const std::vector<message::EventRecord> &)> onEventsReceived;

    /* Observability counters - mirrors communication::Communication's and
       gs_communication::GsCommunication's framesDispatched/decodeErrors
       exactly. Public, read-only in spirit. */
    uint32_t framesDispatched = 0;
    uint32_t decodeErrors = 0;

    /*
     * Connects to the CC at "host"/"port" (production: 127.0.0.1:5000,
     * per PROJECT_GUIDE.md's Step 4 agreed port - tests may use any port).
     * Returns true on success. Closes any previously open connection first
     * (see transport::TcpSocket::connect()).
     */
    bool connect(const std::string &host, uint16_t port);

    void close();

    bool isConnected() const;

    /*
     * Tries to receive one byte from the connection (short timeout) and,
     * if one arrived, feeds it into the decoder via feedByte(). Call this
     * repeatedly from GS's main loop - one call makes at most one byte of
     * progress, same model as Communication::poll()/GsCommunication::poll().
     */
    void poll();

    /*
     * Feeds one byte into the TLV decoder. On a complete, valid frame:
     * dispatches it (see dispatch()). On a decode error (bad CRC/length):
     * increments decodeErrors. Public so tests can drive it directly with
     * hand-built frames, without a real TCP connection.
     */
    void feedByte(uint8_t byte);

    /*
     * Builds and sends a TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST /
     * ...EVENTS... for [startTime, endTime] with a freshly chosen
     * requestId (GS generates it; CC only ever echoes it back). Returns
     * false only if a request of that same type is already in progress -
     * NOT whether the underlying send was confirmed by the OS (same
     * "don't gate on an unconfirmable low-level result" reasoning
     * DataCollection's own backfill requests already document).
     */
    bool requestMeasurements(uint32_t startTime, uint32_t endTime);
    bool requestEvents(uint32_t startTime, uint32_t endTime);

    bool isMeasurementRequestInProgress() const;
    bool isEventRequestInProgress() const;

private:
    void dispatch(const tlv::Frame &frame);
    void handleMeasurementChunkResponse(const message::MeasurementChunkResponse &response);
    void handleEventChunkResponse(const message::EventChunkResponse &response);

    transport::TcpSocket socket_;
    tlv::Decoder decoder_;

    uint8_t nextRequestId_ = 0;
    std::optional<uint8_t> pendingMeasurementRequestId_;
    std::optional<uint8_t> pendingEventRequestId_;
    std::vector<message::MeasurementSample> accumulatedMeasurements_;
    std::vector<message::EventRecord> accumulatedEvents_;
};

}  // namespace cc_communication

#endif  // GROUND_STATION_CC_COMMUNICATION_H
