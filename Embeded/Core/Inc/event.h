/*
 * event.h
 *
 * Purpose:
 *   Reacts to events reported by other modules (Monitor's mode
 *   transitions, Configuration's changes, Init's startup message, and -
 *   later - Object Detection's detected/cleared signals), per Sec 2.3 of
 *   the spec: timestamps and writes each one to a dated events file on
 *   the SD card, drives the RGB LED and buzzer, and builds the matching
 *   outgoing TLV frame for the Central Computer.
 *
 * Layer:
 *   Application (uses Log's already-mounted SD card, the LED/Buzzer/
 *   Button drivers, and message.h's existing builders)
 *
 * Responsibilities:
 *   - Write one timestamped record per event to a dated events file
 *     (YYYYMMDD.TXT), retaining the last 7 days - same policy as Log's
 *     YYYYMMDD.LOG files (Sec 2.4), kept in a separate file/extension so
 *     the two never collide.
 *   - Set the LED color and buzzer state per the Sec 2.3.1 transition
 *     table.
 *   - Track whether the alarm (buzzer) is currently active, so
 *     Event_CheckAlarmButton() knows whether a button press should
 *     silence it.
 *   - Build the outgoing TAG_EVENT_* frame for each case, using
 *     message.h's existing builders.
 *
 * This file does NOT:
 *   - Decide WHEN a mode transition, object detection, or configuration
 *     change happened - callers (Monitor, Configuration, Object
 *     Detection - later) decide that and call these functions.
 *   - Read the RTC - every function takes a timestamp already filled in
 *     by the caller (same pattern as Log and every message.h struct).
 *   - Send the built frame over UART or manage the TX priority queues
 *     (Sec 7) - that is a future Communication/dispatch module's job.
 *     Event only builds the frame and returns it to the caller.
 *   - Implement "suppress non-essential operations" for Error mode
 *     (Sec 2.3.1) - the spec never defines which operations are
 *     non-essential (Open Question #3, PROJECT_GUIDE.md Sec 10), and no
 *     other module currently has anything to suppress.
 *   - Run as its own FreeRTOS task, or decide which task calls
 *     Event_CheckAlarmButton() - that is decided when the task that ends
 *     up owning it (most likely the future ObjectDetectionTask) is built.
 *   - Debounce the button - Event_CheckAlarmButton() does one raw
 *     Button_Read() per call, same as button.c itself.
 *   - Mount the SD card - assumes Log_Init() already did this (both
 *     modules share the one FatFs volume).
 */

#ifndef EVENT_H
#define EVENT_H

#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resets Event's internal alarm-active state. Call once at boot, after
 * Log_Init(). */
void Event_Init(void);

/*
 * Each function below:
 *   1. writes a timestamped record to today's events file,
 *   2. updates the LED/buzzer per Sec 2.3.1 (mode-transition/object-
 *      detection cases only - configuration-changed and startup do not
 *      touch the LED/buzzer, per spec),
 *   3. builds the matching TAG_EVENT_* frame into out_frame.
 * Returns the number of bytes written to out_frame (0 if building the
 * frame failed, e.g. out_frame_size too small) - the file/LED/buzzer
 * side effects still happen even if the frame build fails.
 */
uint16_t Event_OnModeTransition(const ModeTransitionMessage *transition, uint8_t *out_frame, uint16_t out_frame_size);
uint16_t Event_OnObjectDetection(const TimestampWithFlagMessage *detection, uint8_t *out_frame, uint16_t out_frame_size);
uint16_t Event_OnConfigurationChanged(const TimestampMessage *change, uint8_t *out_frame, uint16_t out_frame_size);
uint16_t Event_OnInitStartup(const TimestampWithFlagMessage *startup, uint8_t *out_frame, uint16_t out_frame_size);

/* Call periodically from whichever task ends up owning this (not yet
 * decided - see this file's header comment). If the alarm is currently
 * active and the button is pressed, silences the buzzer only (does not
 * change the LED or the mode - matches Sec 2.3's "button press stops
 * alarm", nothing more). */
void Event_CheckAlarmButton(void);

/* Returns 1 if the alarm (buzzer) is currently active, 0 otherwise.
 * Exposed for hardware-test observability, same spirit as
 * Config_WasLoadedFromFlash(). */
int Event_IsAlarmActive(void);

/* TEMPORARY diagnostics, same pattern as log.h - result of the last
 * events-file f_open()/f_write()/f_close() call, for real-hardware
 * testing. Remove once Event is confirmed working. */
int Event_GetLastOpenResult(void);
int Event_GetLastWriteResult(void);
int Event_GetLastCloseResult(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_H */
