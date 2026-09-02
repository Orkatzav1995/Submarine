/*
 * report_generator.cpp
 *
 * Purpose:
 *   Implements the functions declared in report_generator.h.
 *
 * Layer:
 *   Application
 *
 * This file does NOT:
 *   - Query SQL directly - only reads the vectors DataStore's existing
 *     range queries already return.
 */

#include "report_generator.h"
#include "tlv_common.h"

namespace report_generator {

namespace {

/* Folds one more reading into "stats" - the first reading seeds min/max,
   every reading after that just compares/accumulates. */
void AccumulateReading(SensorStats &stats, float reading, bool isFirstReading, float &sumForAverage)
{
    if (isFirstReading)
    {
        stats.min = reading;
        stats.max = reading;
    }
    else
    {
        if (reading < stats.min)
        {
            stats.min = reading;
        }
        if (reading > stats.max)
        {
            stats.max = reading;
        }
    }
    sumForAverage += reading;
}

}  // namespace

MeasurementReport generateMeasurementReport(data_store::DataStore &dataStore, uint32_t startTime, uint32_t endTime)
{
    MeasurementReport report;
    report.startTime = startTime;
    report.endTime = endTime;

    std::vector<message::MeasurementSample> samples = dataStore.getMeasurementsInRange(startTime, endTime);
    report.totalCount = static_cast<uint32_t>(samples.size());

    float temperatureSum = 0.0f;
    float humiditySum = 0.0f;
    float lightSum = 0.0f;
    float batterySum = 0.0f;

    for (size_t i = 0; i < samples.size(); i++)
    {
        const message::MeasurementSample &sample = samples[i];
        bool isFirstReading = (i == 0);

        AccumulateReading(report.temperature, sample.temperature, isFirstReading, temperatureSum);
        AccumulateReading(report.humidity, sample.humidity, isFirstReading, humiditySum);
        AccumulateReading(report.light, sample.light, isFirstReading, lightSum);
        AccumulateReading(report.battery, sample.battery, isFirstReading, batterySum);

        switch (sample.mode)
        {
            case MODE_NORMAL:
                report.normalCount++;
                break;
            case MODE_WARNING:
                report.warningCount++;
                break;
            case MODE_ERROR:
                report.errorCount++;
                break;
            default:
                break; /* not a valid SystemMode value - ignored, same "unknown dropped silently" spirit as elsewhere */
        }
    }

    if (report.totalCount > 0)
    {
        report.temperature.average = temperatureSum / static_cast<float>(report.totalCount);
        report.humidity.average = humiditySum / static_cast<float>(report.totalCount);
        report.light.average = lightSum / static_cast<float>(report.totalCount);
        report.battery.average = batterySum / static_cast<float>(report.totalCount);
    }

    return report;
}

EventReport generateEventReport(data_store::DataStore &dataStore, uint32_t startTime, uint32_t endTime)
{
    EventReport report;
    report.startTime = startTime;
    report.endTime = endTime;

    std::vector<message::EventRecord> events = dataStore.getEventsInRange(startTime, endTime);
    report.totalCount = static_cast<uint32_t>(events.size());

    for (const message::EventRecord &event : events)
    {
        bool found = false;
        for (EventTypeCount &entry : report.countsByType)
        {
            if (entry.eventType == event.eventType)
            {
                entry.count++;
                found = true;
                break;
            }
        }
        if (!found)
        {
            report.countsByType.push_back(EventTypeCount{event.eventType, 1});
        }
    }

    return report;
}

}  // namespace report_generator
