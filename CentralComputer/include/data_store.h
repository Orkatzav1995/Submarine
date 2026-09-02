/*
 * data_store.h
 *
 * Purpose:
 *   The Central Computer's SQLite engine - part of Sec 3's "Data Collection
 *   & Analysis" module. Owns the local database: the two tables from Sec 8
 *   (measurements, events), both indexed on timestamp, with 7-day retention
 *   matching the LNC's own log rotation.
 *
 *   Row shapes are NOT duplicated here as new structs - message.h's
 *   MeasurementSample and EventRecord already have exactly the right
 *   fields (timestamp + the 4 sensor values + mode; timestamp + eventType +
 *   description), so this file reuses them directly. The database's own
 *   auto-generated row id is never exposed - nothing in the spec's usage
 *   (date-range queries) needs it.
 *
 * Layer:
 *   Application/Data. Sits below the (not yet built) DataCollection module.
 *
 * Responsibilities:
 *   - Open/create the SQLite database file (or an in-memory one, for tests),
 *     creating both tables and their timestamp indexes if they don't exist yet.
 *   - Insert one measurement or one event row.
 *   - Delete rows older than a given cutoff (7-day retention).
 *   - Query measurements or events within a timestamp range, ordered by time.
 *
 * This file does NOT:
 *   - Know about TLV, Communication, or the wire - never sees a tlv::Frame
 *     or a raw byte.
 *   - Decide WHEN to insert or prune - that is DataCollection's job (the
 *     next file), which calls these functions at the right moments.
 *   - Implement report/aggregation logic - Sec 8 says report_generator is
 *     its own namespace, not folded into this class.
 *   - Use any threading or connection pooling - the CC has no threads (see
 *     Communication's poll()-based design), so one sqlite3 handle used
 *     synchronously is all that's needed.
 *   - Log errors anywhere (no printf/std::cerr) - failures are reported via
 *     a false return or an empty vector, same convention as every other
 *     module in this codebase (SerialPort, Communication, message.cpp).
 */

#ifndef CENTRAL_COMPUTER_DATA_STORE_H
#define CENTRAL_COMPUTER_DATA_STORE_H

#include "message.h"
#include "sqlite3.h"
#include <cstdint>
#include <string>
#include <vector>

namespace data_store {

class DataStore
{
public:
    DataStore() = default;
    ~DataStore();

    /* Owns one sqlite3 handle - copying would let two objects both try to
       close the same handle, so copying is disabled outright (same reasoning
       as transport::SerialPort). */
    DataStore(const DataStore &) = delete;
    DataStore &operator=(const DataStore &) = delete;

    /*
     * Opens (creating if necessary) the database at "dbPath" - a real file
     * path, or ":memory:" for a private in-memory database (used by tests,
     * so no files are left on disk). Creates the measurements/events
     * tables and their timestamp indexes if they don't already exist.
     * Returns true on success. Closes any previously open database first.
     */
    bool open(const std::string &dbPath);

    void close();

    bool isOpen() const;

    bool insertMeasurement(const message::MeasurementSample &sample);
    bool insertEvent(const message::EventRecord &event);

    /* Deletes every row in both tables with timestamp < cutoffTimestamp. */
    bool pruneOlderThan(uint32_t cutoffTimestamp);

    /* Both inclusive of startTime and endTime, ordered oldest-first. */
    std::vector<message::MeasurementSample> getMeasurementsInRange(uint32_t startTime, uint32_t endTime);
    std::vector<message::EventRecord> getEventsInRange(uint32_t startTime, uint32_t endTime);

private:
    sqlite3 *db_ = nullptr;
};

}  // namespace data_store

#endif  // CENTRAL_COMPUTER_DATA_STORE_H
