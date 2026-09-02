/*
 * data_collection.h
 *
 * Purpose:
 *   Sec 3's "Data Collection & Analysis" module - the missing piece
 *   between Communication and DataStore. Owns neither the wire nor SQLite
 *   itself; it just translates what Communication hands it into DataStore
 *   calls.
 *
 *   Two jobs:
 *     1. Live collection: every keepalive/data-report/event that arrives
 *        from the LNC gets saved, via callbacks registered on the given
 *        Communication.
 *     2. Backfill: if the CC missed some history (e.g. it was offline),
 *        it can ask the LNC for measurements/events in a time range
 *        (Sec 2.5's "retrieval instructions") and save whatever comes
 *        back. Each response is chunked (Common/Protocol/tlv_common.h);
 *        records are saved to the database as each chunk arrives, rather
 *        than buffered in memory - so no generic "pending request
 *        registry" is needed, just one small optional<requestId> per
 *        data type, to know which chunks belong to the backfill this
 *        object is currently waiting for.
 *
 * Layer:
 *   Application. Sits on top of both Communication and DataStore, and
 *   holds references to both rather than owning them (the same
 *   Communication/DataStore instances may be used by other modules too).
 *
 * Responsibilities:
 *   - Register all 9 relevant callbacks on the given Communication in the
 *     constructor: 7 for live traffic, 2 for chunk responses.
 *   - Translate each arriving message into one DataStore insert, then
 *     prune anything older than 7 days (Sec 2.4's retention, matched here)
 *     - based on real wall-clock time, NOT the timestamp of whatever
 *     record just arrived, since a backfilled record is deliberately old.
 *   - requestMeasurementBackfill() / requestEventBackfill(): build and
 *     send a range request with a freshly chosen requestId, remembering
 *     it so the matching chunk-response callback knows these chunks are
 *     the ones it's waiting for (a chunk with any other requestId is
 *     ignored - stale or unrelated).
 *   - Fire onMeasurementBackfillComplete / onEventBackfillComplete once a
 *     backfill's last chunk (MoreDataFlag == false) has been saved.
 *
 * This file does NOT:
 *   - Read data back out for reports or the Ground Station - callers query
 *     DataStore directly for that (getMeasurementsInRange/getEventsInRange).
 *   - Implement report/aggregation logic (report_generator, deferred).
 *   - Own the Communication or DataStore it's given - both are held by
 *     reference, and must outlive this object.
 *   - Support more than one backfill in flight per data type at once -
 *     calling requestMeasurementBackfill() while one is already pending
 *     returns false rather than overwriting it.
 */

#ifndef CENTRAL_COMPUTER_DATA_COLLECTION_H
#define CENTRAL_COMPUTER_DATA_COLLECTION_H

#include "communication.h"
#include "data_store.h"
#include <cstdint>
#include <functional>
#include <optional>

namespace data_collection {

class DataCollection
{
public:
    DataCollection(communication::Communication &communication, data_store::DataStore &dataStore);

    /* Registers callbacks on "communication" that capture "this" - copying
       or moving this object would leave those callbacks pointing at a
       stale/wrong instance, so both are disabled outright. */
    DataCollection(const DataCollection &) = delete;
    DataCollection &operator=(const DataCollection &) = delete;

    /* Fired once the current backfill's last chunk has been saved. Left
       unset (default), completion is simply not announced anywhere. */
    std::function<void()> onMeasurementBackfillComplete;
    std::function<void()> onEventBackfillComplete;

    /*
     * Sends a TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST / ...EVENTS... for
     * [startTime, endTime] with a freshly chosen requestId, and marks that
     * type's backfill as in progress. Returns false only if a backfill of
     * that type is already in progress - NOT whether the underlying send
     * was confirmed by the OS (Communication::sendFrame's own result isn't
     * checked here, since there is no retry/timeout mechanism in this
     * design: a low-level send failure and "sent fine but the LNC never
     * replies" are already equally unrecoverable, so gating on the former
     * would add no real safety while making this flow untestable without
     * real hardware).
     */
    bool requestMeasurementBackfill(uint32_t startTime, uint32_t endTime);
    bool requestEventBackfill(uint32_t startTime, uint32_t endTime);

    bool isMeasurementBackfillInProgress() const;
    bool isEventBackfillInProgress() const;

private:
    void onKeepAliveOrDataReport(const message::MeasurementSample &sample);
    void onEventModeTransition(const message::ModeTransitionMessage &transition);
    void onEventObjectDetection(const message::TimestampWithFlagMessage &detection);
    void onEventConfigChanged(const message::TimestampMessage &configChanged);
    void onEventStartup(const message::TimestampWithFlagMessage &startup);
    void onMeasurementChunkResponse(const message::MeasurementChunkResponse &response);
    void onEventChunkResponse(const message::EventChunkResponse &response);

    void saveEvent(const message::EventRecord &event);

    communication::Communication &communication_;
    data_store::DataStore &dataStore_;

    uint8_t nextRequestId_ = 0;
    std::optional<uint8_t> pendingMeasurementRequestId_;
    std::optional<uint8_t> pendingEventRequestId_;
};

}  // namespace data_collection

#endif  // CENTRAL_COMPUTER_DATA_COLLECTION_H
