/*
 * monitor.h
 *
 * Purpose:
 *   Every 5 seconds (Sec 2.1 of the spec): reads all 4 sensors, compares
 *   each against Configuration's Normal/Warning limits to compute the
 *   current SystemMode (Sec 2.10), always logs the sample, and - only
 *   when the mode actually changes - reports the transition to Event.
 *
 * Layer:
 *   Application (uses the DHT/Battery/Light drivers, Configuration's
 *   getters, and Log/Event's already-public functions)
 *
 * Responsibilities:
 *   - Read temperature+humidity (DHT11), battery (potentiometer/ADC1),
 *     and light (ADC2).
 *   - Classify each reading against Configuration's limits and combine
 *     them into one overall SystemMode (the worst of the 4).
 *   - Build a MeasurementSample and call Log_Write() every cycle.
 *   - Track the previous mode; on a change, call Event_OnModeTransition().
 *
 * This file does NOT:
 *   - Read the RTC - the caller supplies the timestamp (same pattern as
 *     every other module in this project).
 *   - Decide what to DO about a mode change beyond calling Event - LED/
 *     buzzer/alarm state are entirely Event's responsibility.
 *   - Modify Log or Event - it only calls their existing, already-
 *     verified public functions.
 *   - Detect objects (Sec 2.2) - that is the separate, not-yet-built
 *     Object Detection Application module.
 *   - Send anything over UART - same deferred boundary as every other
 *     Application module right now (no Communication/TX-queue plumbing
 *     exists yet on the LNC side).
 *   - Run as its own generic "module task" - it IS what MonitorTask
 *     (Sec 7) calls once per cycle, not a separate concept from it.
 *
 * DHT read-failure handling:
 *   DHT_Read() can fail (timeout/checksum). On failure, Monitor keeps
 *   using the last successfully-read temperature/humidity rather than a
 *   bogus 0.0 (which could wrongly look like an Error-range reading).
 */

#ifndef MONITOR_H
#define MONITOR_H

#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resets Monitor's internal state (previous mode, last-known-good DHT
 * reading) and starts the DHT11 driver's timer (DHT_Init()). Call once,
 * at the top of MonitorTask, before the first Monitor_Sample(). */
void Monitor_Init(void);

/* Runs one full sampling cycle: reads all 4 sensors, computes the
 * overall mode, fills *out_sample, writes it to Log, and - only if the
 * mode changed since the last call - reports the transition to Event. */
void Monitor_Sample(uint32_t timestamp, MeasurementSample *out_sample);

#ifdef __cplusplus
}
#endif

#endif /* MONITOR_H */
