/*
 * object_detection.h
 *
 * Purpose:
 *   Continuously watches the IR sensor (Sec 2.2 of the spec) and turns
 *   its raw pulse activity into a stable "object present" / "no object"
 *   signal, reporting only real changes to Event.
 *
 * Layer:
 *   Application (uses the ir.h driver and Event's already-public
 *   function - does not touch Log, LED, or Buzzer directly)
 *
 * Responsibilities:
 *   - Poll IR_Read() at a fast, fixed interval (ObjectDetection_Poll()
 *     is meant to be called every IR_POLL_INTERVAL_MS).
 *   - Track how long it has been since the raw reading last changed.
 *   - Report IR_DETECTED as soon as any pulse activity is seen, and
 *     IR_NOT_DETECTED only after IR_ACTIVITY_TIMEOUT_MS of no activity.
 *   - Call Event_OnObjectDetection() only when the reported state
 *     actually changes, and enqueue the frame it returns at event
 *     priority (tx_queue.h's TxQueue_EnqueueEvent(), Sec 7).
 *
 * Why polling instead of a driver/EXTI rewrite (PROJECT_GUIDE.md Open
 * Question #12): the IR sensor is known to produce a pulse train (not a
 * steady level) when it detects something, so a single slow read could
 * easily miss it. Rather than rewriting ir.c to decode edges (which
 * would also require deciding on and matching a specific remote
 * protocol - overkill for "is something there"), this module just reads
 * the same simple ir.c driver fast enough, and treats "the reading keeps
 * changing" as presence. This also makes ir.c's still-unconfirmed HIGH/
 * LOW polarity irrelevant here - this module only cares whether the
 * raw reading CHANGED between polls, not which specific value means
 * what.
 *
 * This file does NOT:
 *   - Modify or reinterpret ir.c/ir.h - IR_Read() is called exactly as
 *     it already exists, unchanged.
 *   - Touch the LED or buzzer - that is entirely Event's job.
 *   - Write to the SD card - Event's own Event_OnObjectDetection()
 *     already writes the events file.
 *   - Call Transport_UART_Send() itself - CommTxTask (main.c) is the
 *     only task that ever does now; this file only enqueues.
 *   - Run its own FreeRTOS task by itself - it IS what
 *     ObjectDetectionTask (Sec 7) calls once per poll, not a separate
 *     concept from it.
 */

#ifndef OBJECT_DETECTION_H
#define OBJECT_DETECTION_H

#include "ir.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How often ObjectDetection_Poll() is meant to be called, and how long
 * the raw reading must stay unchanged before "no object" is reported.
 * Starting values agreed with the user - tune after the real-hardware
 * test if needed, nothing else in this file depends on the exact
 * numbers. */
#define IR_POLL_INTERVAL_MS    20
#define IR_ACTIVITY_TIMEOUT_MS 400

/* Resets internal state to "no object" (a safe starting assumption,
 * same reasoning as Monitor_Init() defaulting to MODE_NORMAL). Call
 * once, at the top of ObjectDetectionTask, before the first
 * ObjectDetection_Poll(). */
void ObjectDetection_Init(void);

/* Reads the IR pin once, updates the activity timer, and - only if the
 * reported state actually changes - calls Event_OnObjectDetection()
 * with the given timestamp. Meant to be called every
 * IR_POLL_INTERVAL_MS. */
void ObjectDetection_Poll(uint32_t timestamp);

/* Returns the current reported state (debounced/smoothed, not the raw
 * pin reading). Exposed for hardware-test observability, same spirit as
 * Event_IsAlarmActive(). */
IrState ObjectDetection_GetCurrentState(void);

/* DIAGNOSTIC (added to investigate a reported "stuck at DETECTED, no
 * CLEARED" hardware issue) - returns the raw IR_Read() value observed on
 * the most recent poll, NOT the debounced/smoothed state. Purely
 * additive observability; the detection algorithm itself is unchanged.
 * Remove once the raw IR behavior is understood and the algorithm (if
 * needed) is fixed. */
IrState ObjectDetection_GetRawReading(void);

#ifdef __cplusplus
}
#endif

#endif /* OBJECT_DETECTION_H */
