/*
 * data_store.cpp
 *
 * Purpose:
 *   Implements the DataStore class declared in data_store.h.
 *
 * Layer:
 *   Application/Data
 *
 * This file does NOT:
 *   - Know about TLV, Communication, or the wire.
 *   - Log errors anywhere - failures are reported via a false return or an
 *     empty vector, same convention as every other module in this codebase.
 */

#include "data_store.h"

namespace data_store {

namespace {

const char *kCreateMeasurementsTable =
    "CREATE TABLE IF NOT EXISTS measurements ("
    "  id INTEGER PRIMARY KEY,"
    "  timestamp INTEGER NOT NULL,"
    "  temperature REAL NOT NULL,"
    "  humidity REAL NOT NULL,"
    "  light REAL NOT NULL,"
    "  battery REAL NOT NULL,"
    "  mode INTEGER NOT NULL"
    ");";

const char *kCreateMeasurementsIndex =
    "CREATE INDEX IF NOT EXISTS idx_measurements_timestamp ON measurements(timestamp);";

const char *kCreateEventsTable =
    "CREATE TABLE IF NOT EXISTS events ("
    "  id INTEGER PRIMARY KEY,"
    "  timestamp INTEGER NOT NULL,"
    "  event_type INTEGER NOT NULL,"
    "  description TEXT NOT NULL"
    ");";

const char *kCreateEventsIndex =
    "CREATE INDEX IF NOT EXISTS idx_events_timestamp ON events(timestamp);";

/* Runs one DDL/no-result statement (no parameters to bind). Returns true on success. */
bool ExecuteStatement(sqlite3 *db, const char *sql)
{
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

}  // namespace

DataStore::~DataStore()
{
    close();
}

bool DataStore::open(const std::string &dbPath)
{
    close();

    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK)
    {
        close();
        return false;
    }

    if (!ExecuteStatement(db_, kCreateMeasurementsTable) || !ExecuteStatement(db_, kCreateMeasurementsIndex)
        || !ExecuteStatement(db_, kCreateEventsTable) || !ExecuteStatement(db_, kCreateEventsIndex))
    {
        close();
        return false;
    }

    return true;
}

void DataStore::close()
{
    if (db_ != nullptr)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool DataStore::isOpen() const
{
    return db_ != nullptr;
}

bool DataStore::insertMeasurement(const message::MeasurementSample &sample)
{
    static const char *kSql =
        "INSERT INTO measurements (timestamp, temperature, humidity, light, battery, mode) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &statement, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(sample.timestamp));
    sqlite3_bind_double(statement, 2, static_cast<double>(sample.temperature));
    sqlite3_bind_double(statement, 3, static_cast<double>(sample.humidity));
    sqlite3_bind_double(statement, 4, static_cast<double>(sample.light));
    sqlite3_bind_double(statement, 5, static_cast<double>(sample.battery));
    sqlite3_bind_int(statement, 6, sample.mode);

    bool succeeded = (sqlite3_step(statement) == SQLITE_DONE);
    sqlite3_finalize(statement);
    return succeeded;
}

bool DataStore::insertEvent(const message::EventRecord &event)
{
    static const char *kSql = "INSERT INTO events (timestamp, event_type, description) VALUES (?, ?, ?);";

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &statement, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(event.timestamp));
    sqlite3_bind_int(statement, 2, event.eventType);
    sqlite3_bind_text(statement, 3, event.description.c_str(), -1, SQLITE_TRANSIENT);

    bool succeeded = (sqlite3_step(statement) == SQLITE_DONE);
    sqlite3_finalize(statement);
    return succeeded;
}

bool DataStore::pruneOlderThan(uint32_t cutoffTimestamp)
{
    static const char *kDeleteMeasurementsSql = "DELETE FROM measurements WHERE timestamp < ?;";
    static const char *kDeleteEventsSql = "DELETE FROM events WHERE timestamp < ?;";

    sqlite3_stmt *statement = nullptr;

    if (sqlite3_prepare_v2(db_, kDeleteMeasurementsSql, -1, &statement, nullptr) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(cutoffTimestamp));
    bool measurementsSucceeded = (sqlite3_step(statement) == SQLITE_DONE);
    sqlite3_finalize(statement);

    if (sqlite3_prepare_v2(db_, kDeleteEventsSql, -1, &statement, nullptr) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(cutoffTimestamp));
    bool eventsSucceeded = (sqlite3_step(statement) == SQLITE_DONE);
    sqlite3_finalize(statement);

    return measurementsSucceeded && eventsSucceeded;
}

std::vector<message::MeasurementSample> DataStore::getMeasurementsInRange(uint32_t startTime, uint32_t endTime)
{
    static const char *kSql =
        "SELECT timestamp, temperature, humidity, light, battery, mode FROM measurements "
        "WHERE timestamp >= ? AND timestamp <= ? ORDER BY timestamp;";

    std::vector<message::MeasurementSample> results;

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &statement, nullptr) != SQLITE_OK)
    {
        return results;
    }

    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(startTime));
    sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(endTime));

    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        message::MeasurementSample sample;
        sample.timestamp = static_cast<uint32_t>(sqlite3_column_int64(statement, 0));
        sample.temperature = static_cast<float>(sqlite3_column_double(statement, 1));
        sample.humidity = static_cast<float>(sqlite3_column_double(statement, 2));
        sample.light = static_cast<float>(sqlite3_column_double(statement, 3));
        sample.battery = static_cast<float>(sqlite3_column_double(statement, 4));
        sample.mode = static_cast<uint8_t>(sqlite3_column_int(statement, 5));
        results.push_back(sample);
    }

    sqlite3_finalize(statement);
    return results;
}

std::vector<message::EventRecord> DataStore::getEventsInRange(uint32_t startTime, uint32_t endTime)
{
    static const char *kSql =
        "SELECT timestamp, event_type, description FROM events "
        "WHERE timestamp >= ? AND timestamp <= ? ORDER BY timestamp;";

    std::vector<message::EventRecord> results;

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &statement, nullptr) != SQLITE_OK)
    {
        return results;
    }

    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(startTime));
    sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(endTime));

    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        message::EventRecord event;
        event.timestamp = static_cast<uint32_t>(sqlite3_column_int64(statement, 0));
        event.eventType = static_cast<uint8_t>(sqlite3_column_int(statement, 1));
        const unsigned char *text = sqlite3_column_text(statement, 2);
        event.description = (text != nullptr) ? std::string(reinterpret_cast<const char *>(text)) : std::string();
        results.push_back(event);
    }

    sqlite3_finalize(statement);
    return results;
}

}  // namespace data_store
