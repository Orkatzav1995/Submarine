/*
 * report_generator.h
 *
 * Purpose:
 *   Sec 3's "prepares reports broken down by criteria" requirement, and
 *   Sec 8's frozen decision that report_generator is a namespace, not a
 *   class. Turns raw rows already sitting in DataStore into a small
 *   summary: how many measurements fell in each mode, min/max/average per
 *   sensor, and how many events occurred of each type, all within a given
 *   time range.
 *
 *   Deliberately does NOT add any new SQL - it calls DataStore's existing,
 *   already-tested getMeasurementsInRange/getEventsInRange and does all
 *   counting/min/max/averaging in plain C++ over the returned rows. Simple
 *   to read, and it keeps DataStore exactly what its own header comment
 *   already says it is ("does NOT implement report/aggregation logic").
 *
 * Layer:
 *   Application. Read-only consumer of DataStore - never writes to it,
 *   never touches Communication or DataCollection.
 *
 * Responsibilities:
 *   - generateMeasurementReport(): total count, a count per SystemMode,
 *     and min/max/average for each of the 4 sensors, over a time range.
 *   - generateEventReport(): total count, and a count per event type
 *     (whichever eventType values actually appear in the range - not
 *     hardcoded to the 4 currently-known types, so it never silently
 *     misses a real row).
 *
 * This file does NOT:
 *   - Write to the database, or decide when a report should be generated -
 *     that's up to whatever calls these functions (not yet built - a
 *     future CLI, or whatever eventually answers Ground Station requests).
 *   - Query SQL directly - everything here operates on the
 *     std::vector<MeasurementSample>/std::vector<EventRecord> that
 *     DataStore's existing range queries already return.
 */

#ifndef CENTRAL_COMPUTER_REPORT_GENERATOR_H
#define CENTRAL_COMPUTER_REPORT_GENERATOR_H

#include "data_store.h"
#include <cstdint>
#include <vector>

namespace report_generator {

/* min/max/average of one sensor's readings over a range. All 0 if there
   were no records in the range - not std::optional, since "no data" is a
   perfectly normal, easy-to-read result for an empty range. */
struct SensorStats
{
    float min = 0.0f;
    float max = 0.0f;
    float average = 0.0f;
};

struct MeasurementReport
{
    uint32_t startTime = 0;
    uint32_t endTime = 0;

    uint32_t totalCount = 0;
    uint32_t normalCount = 0;
    uint32_t warningCount = 0;
    uint32_t errorCount = 0;

    SensorStats temperature;
    SensorStats humidity;
    SensorStats light;
    SensorStats battery;
};

struct EventTypeCount
{
    uint8_t eventType = 0;
    uint32_t count = 0;
};

struct EventReport
{
    uint32_t startTime = 0;
    uint32_t endTime = 0;

    uint32_t totalCount = 0;
    /* One entry per distinct eventType actually seen in the range, in the
       order first encountered - not a fixed list of the 4 currently-known
       types, so a future new event type is still counted correctly. */
    std::vector<EventTypeCount> countsByType;
};

MeasurementReport generateMeasurementReport(data_store::DataStore &dataStore, uint32_t startTime, uint32_t endTime);
EventReport generateEventReport(data_store::DataStore &dataStore, uint32_t startTime, uint32_t endTime);

}  // namespace report_generator

#endif  // CENTRAL_COMPUTER_REPORT_GENERATOR_H
