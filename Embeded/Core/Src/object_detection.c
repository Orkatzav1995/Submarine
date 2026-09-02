/*
 * object_detection.c
 *
 * See object_detection.h for purpose, layer, and responsibilities.
 */

#include "object_detection.h"
#include "event.h"

/* The raw reading from the previous poll, so a change (= activity) can
 * be detected. */
static IrState s_lastRawReading;

/* Milliseconds since the raw reading last changed. Reset to 0 on every
 * change; advances by IR_POLL_INTERVAL_MS on every poll where nothing
 * changed. */
static uint32_t s_msSinceLastActivity;

/* The debounced/smoothed state actually reported to Event - only this
 * (not the raw reading) is compared to decide whether to call
 * Event_OnObjectDetection(). */
static IrState s_reportedState;

void ObjectDetection_Init(void)
{
    s_lastRawReading = IR_Read();
    s_msSinceLastActivity = IR_ACTIVITY_TIMEOUT_MS; /* start idle - no activity seen yet */
    s_reportedState = IR_NOT_DETECTED;
}

void ObjectDetection_Poll(uint32_t timestamp)
{
    IrState raw = IR_Read();
    IrState newReportedState;

    if (raw != s_lastRawReading)
    {
        /* the pin changed since last poll - that IS the activity signal */
        s_msSinceLastActivity = 0;
    }
    else
    {
        s_msSinceLastActivity += IR_POLL_INTERVAL_MS;
    }
    s_lastRawReading = raw;

    /* Activity seen recently -> report DETECTED (immediately, on the
     * very first pulse). Quiet for the full timeout -> report
     * NOT_DETECTED. */
    newReportedState = (s_msSinceLastActivity < IR_ACTIVITY_TIMEOUT_MS) ? IR_DETECTED : IR_NOT_DETECTED;

    if (newReportedState != s_reportedState)
    {
        TimestampWithFlagMessage detection;
        uint8_t frame[64];

        detection.timestamp = timestamp;
        detection.flag = (newReportedState == IR_DETECTED) ? 1 : 0;

        /* Event writes its own record, drives the LED/buzzer, and
         * returns the CC-bound frame - discarded here, same "build but
         * don't send" boundary every other Application module already
         * has (no Communication/TX-queue module exists yet). */
        (void)Event_OnObjectDetection(&detection, frame, sizeof(frame));

        s_reportedState = newReportedState;
    }
}

IrState ObjectDetection_GetCurrentState(void)
{
    return s_reportedState;
}

IrState ObjectDetection_GetRawReading(void)
{
    return s_lastRawReading;
}
