/*
 * data_collection.cpp
 *
 * Purpose:
 *   Implements the DataCollection class declared in data_collection.h.
 *
 * Layer:
 *   Application
 *
 * This file does NOT:
 *   - Know about TLV/wire internals directly - only through Communication
 *     and message.h's already-parsed structs.
 */

#include "data_collection.h"
#include "tlv_common.h"
#include <ctime>

namespace data_collection {

namespace {

constexpr uint32_t kRetentionSeconds = 7u * 24u * 60u * 60u; /* 7 days, matches the LNC's own retention */

/* Real wall-clock "now" - NEVER the timestamp of whatever record is being
   inserted, since a backfilled record is deliberately old and using its
   timestamp as "now" would prune data incorrectly (possibly the very data
   just backfilled). */
uint32_t CurrentUnixTime()
{
    return static_cast<uint32_t>(std::time(nullptr));
}

const char *ModeName(uint8_t mode)
{
    switch (mode)
    {
        case MODE_NORMAL:
            return "Normal";
        case MODE_WARNING:
            return "Warning";
        case MODE_ERROR:
            return "Error";
        default:
            return "Unknown";
    }
}

}  // namespace

DataCollection::DataCollection(communication::Communication &communication, data_store::DataStore &dataStore)
    : communication_(communication), dataStore_(dataStore)
{
    communication_.callbacks.onKeepAlive = [this](const message::MeasurementSample &sample)
    { onKeepAliveOrDataReport(sample); };
    communication_.callbacks.onDataReport = [this](const message::MeasurementSample &sample)
    { onKeepAliveOrDataReport(sample); };
    communication_.callbacks.onEventModeTransition = [this](const message::ModeTransitionMessage &transition)
    { onEventModeTransition(transition); };
    communication_.callbacks.onEventObjectDetection = [this](const message::TimestampWithFlagMessage &detection)
    { onEventObjectDetection(detection); };
    communication_.callbacks.onEventConfigChanged = [this](const message::TimestampMessage &configChanged)
    { onEventConfigChanged(configChanged); };
    communication_.callbacks.onEventStartup = [this](const message::TimestampWithFlagMessage &startup)
    { onEventStartup(startup); };
    communication_.callbacks.onMeasurementChunkResponse =
        [this](const message::MeasurementChunkResponse &response) { onMeasurementChunkResponse(response); };
    communication_.callbacks.onEventChunkResponse =
        [this](const message::EventChunkResponse &response) { onEventChunkResponse(response); };
}

void DataCollection::onKeepAliveOrDataReport(const message::MeasurementSample &sample)
{
    dataStore_.insertMeasurement(sample);
    dataStore_.pruneOlderThan(CurrentUnixTime() - kRetentionSeconds);
}

void DataCollection::saveEvent(const message::EventRecord &event)
{
    dataStore_.insertEvent(event);
    dataStore_.pruneOlderThan(CurrentUnixTime() - kRetentionSeconds);
}

void DataCollection::onEventModeTransition(const message::ModeTransitionMessage &transition)
{
    message::EventRecord event;
    event.timestamp = transition.timestamp;
    event.eventType = TAG_EVENT_MODE_TRANSITION;
    event.description =
        std::string("Mode change: ") + ModeName(transition.oldMode) + " -> " + ModeName(transition.newMode);
    saveEvent(event);
}

void DataCollection::onEventObjectDetection(const message::TimestampWithFlagMessage &detection)
{
    message::EventRecord event;
    event.timestamp = detection.timestamp;
    event.eventType = TAG_EVENT_OBJECT_DETECTION;
    event.description = (detection.flag != 0) ? "Object detected" : "Object cleared";
    saveEvent(event);
}

void DataCollection::onEventConfigChanged(const message::TimestampMessage &configChanged)
{
    message::EventRecord event;
    event.timestamp = configChanged.timestamp;
    event.eventType = TAG_EVENT_CONFIG_CHANGED;
    event.description = "Configuration changed";
    saveEvent(event);
}

void DataCollection::onEventStartup(const message::TimestampWithFlagMessage &startup)
{
    message::EventRecord event;
    event.timestamp = startup.timestamp;
    event.eventType = TAG_EVENT_STARTUP;
    event.description = (startup.flag != 0) ? "Startup after watchdog reset" : "Startup";
    saveEvent(event);
}

bool DataCollection::requestMeasurementBackfill(uint32_t startTime, uint32_t endTime)
{
    if (pendingMeasurementRequestId_.has_value())
    {
        return false;
    }

    uint8_t requestId = nextRequestId_++;

    message::TimeRangeMessage request;
    request.requestId = requestId;
    request.startTime = startTime;
    request.endTime = endTime;

    /* sendFrame()'s own true/false result (whether the OS confirmed the
       write) is not checked here: there is no retry/timeout mechanism in
       this design, so a low-level send failure and "sent fine but the LNC
       never replies" are already indistinguishable, equally unrecoverable
       failure modes. Gating "pending" on the former would add no real
       safety while making this flow untestable without real hardware. */
    communication_.sendFrame(message::buildGetMeasurementsByRangeRequest(request));
    pendingMeasurementRequestId_ = requestId;
    return true;
}

bool DataCollection::requestEventBackfill(uint32_t startTime, uint32_t endTime)
{
    if (pendingEventRequestId_.has_value())
    {
        return false;
    }

    uint8_t requestId = nextRequestId_++;

    message::TimeRangeMessage request;
    request.requestId = requestId;
    request.startTime = startTime;
    request.endTime = endTime;

    communication_.sendFrame(message::buildGetEventsByRangeRequest(request));
    pendingEventRequestId_ = requestId;
    return true;
}

bool DataCollection::isMeasurementBackfillInProgress() const
{
    return pendingMeasurementRequestId_.has_value();
}

bool DataCollection::isEventBackfillInProgress() const
{
    return pendingEventRequestId_.has_value();
}

void DataCollection::onMeasurementChunkResponse(const message::MeasurementChunkResponse &response)
{
    if (!pendingMeasurementRequestId_.has_value() || response.requestId != *pendingMeasurementRequestId_)
    {
        return; /* stale or unrelated - not the backfill we're waiting for */
    }

    for (const message::MeasurementSample &sample : response.records)
    {
        dataStore_.insertMeasurement(sample);
    }
    dataStore_.pruneOlderThan(CurrentUnixTime() - kRetentionSeconds);

    if (!response.moreDataFlag)
    {
        pendingMeasurementRequestId_.reset();
        if (onMeasurementBackfillComplete)
        {
            onMeasurementBackfillComplete();
        }
    }
}

void DataCollection::onEventChunkResponse(const message::EventChunkResponse &response)
{
    if (!pendingEventRequestId_.has_value() || response.requestId != *pendingEventRequestId_)
    {
        return;
    }

    for (const message::EventRecord &event : response.records)
    {
        dataStore_.insertEvent(event);
    }
    dataStore_.pruneOlderThan(CurrentUnixTime() - kRetentionSeconds);

    if (!response.moreDataFlag)
    {
        pendingEventRequestId_.reset();
        if (onEventBackfillComplete)
        {
            onEventBackfillComplete();
        }
    }
}

}  // namespace data_collection
