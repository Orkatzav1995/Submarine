/*
 * init.h
 *
 * Purpose:
 *   The LNC's one-shot boot sequence (Sec 2.7 of the spec, "Init"
 *   module). "Starts all system activities" by calling every other
 *   Application module's own one-shot startup function, in order, then
 *   reports a startup event to Event.
 *
 * Layer:
 *   Application (calls Configuration_Init(), Log_Init(), Event_Init(),
 *   and Event_OnInitStartup() - all already-built, hardware-verified
 *   modules; introduces nothing new below it)
 *
 * Responsibilities:
 *   - Call Configuration_Init(), Log_Init(), Event_Init(), in that
 *     order - replacing the ad-hoc calls that used to live directly in
 *     main.c's StartTask02, with no change to what each call does.
 *   - Report a startup event to Event via Event_OnInitStartup(), using
 *     the timestamp and wasWatchdogReset flag the caller passes in.
 *
 * This file does NOT:
 *   - Read the hardware RTC, or any other time source, itself.
 *     Init_Start() takes the current timestamp as a parameter, the
 *     same convention Monitor_Sample()/ObjectDetection_Poll() already
 *     use - this parameter IS the seam a later real time source (RTC
 *     hardware, or a future CC time-sync reply) plugs into: whoever
 *     calls Init_Start() just needs to pass a real timestamp instead of
 *     today's placeholder test clock. No RTC-specific code, and no
 *     separate "time received" hook function, exists in this file.
 *   - Implement any LNC->CC time-sync round trip - deferred, needs new
 *     message-layer pieces that don't exist yet (PROJECT_GUIDE.md Sec
 *     15).
 *   - Contain any watchdog logic itself (no IWDG access, no reset-flag
 *     reading) - it only receives the already-determined wasWatchdogReset
 *     flag as a plain parameter, the same convention as the timestamp
 *     parameter above. Reading RCC_FLAG_IWDGRST is main()'s
 *     responsibility, at boot, before this function is ever called.
 *   - Own a FreeRTOS task - called once from the existing InitTask
 *     (StartTask02) in main.c, same as before this file existed.
 *   - Initialize Monitor or Object Detection - each of those still
 *     self-initializes from its own task (Monitor_Init()/
 *     ObjectDetection_Init()), unchanged by this file.
 *   - Fire a Configuration-changed event - that only happens from a
 *     real Configuration setter call (Sec 2.6), never at boot.
 */

#ifndef INIT_H
#define INIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runs the LNC's one-shot boot sequence: Configuration_Init(),
 * Log_Init(), Event_Init(), then reports a startup event to Event
 * using the given timestamp and wasWatchdogReset flag. Call exactly
 * once, at boot, from InitTask. */
void Init_Start(uint32_t timestamp, uint8_t wasWatchdogReset);

/* Test-observability getter, same spirit as every other module's
 * (Config_WasLoadedFromFlash(), Event_IsAlarmActive(), etc.) - the byte
 * count Event_OnInitStartup() returned for the startup frame it just
 * built (0 would mean the CC-frame build failed; a nonzero value also
 * doubles as confirmation that Init_Start() has run at least once). */
uint16_t Init_GetLastStartupFrameLength(void);

#ifdef __cplusplus
}
#endif

#endif /* INIT_H */
