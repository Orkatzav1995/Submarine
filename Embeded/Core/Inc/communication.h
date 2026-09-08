/*
 * communication.h
 *
 * Purpose:
 *   Sec 2.5 of the spec: "receives management commands from CC" and
 *   "receives retrieval instructions from CC". This is the piece that
 *   was still missing - CommRxTask (main.c) could already decode a raw
 *   TLV frame, but nothing looked at its Tag and actually did anything
 *   with it. Communication_Dispatch() is that missing piece: given one
 *   already-decoded frame, it parses it (Message layer) and calls the
 *   matching Application-layer function (Configuration/Event, or builds
 *   a reply and enqueues it).
 *
 * Layer:
 *   Application (uses the Message layer's existing parsers/builders,
 *   Configuration's setters, Event's existing "build but don't send"
 *   functions, and tx_queue.h's TxQueue_EnqueueEvent() - never calls
 *   Transport_UART_Send() directly; CommTxTask, Sec 7, is the only task
 *   that does now)
 *
 * Responsibilities (this pass - see "This file does NOT" for what's
 * deliberately left out):
 *   - The 8 "set limit" commands: parse, apply via the matching
 *     Config_Set*() setter, then report the change to Event
 *     (Event_OnConfigurationChanged()) and enqueue the resulting frame
 *     at event priority - same as Sec 2.6 requires ("Receives
 *     configuration changes from Communication").
 *   - TAG_GET_SYSTEM_TIME_REQUEST: parse, build a TAG_SYSTEM_TIME_RESPONSE
 *     with the timestamp the caller supplies, and enqueue it at event
 *     priority (not because it IS an event - see communication.c's
 *     SendSystemTimeResponse() for why that tier was chosen among the
 *     3 Sec 7 defines) - the first real request/response round trip on
 *     the LNC.
 *   - Phase 1 (LNC Timestamp/RTC Hardening): TAG_SET_RTC_DATETIME - parse,
 *     then apply the epoch to the RTC via RtcSync_ApplyEpoch() (rtc_sync.h).
 *     The existing manual CC-push synchronization mechanism.
 *   - Phase 1: TAG_SYSTEM_TIME_RESPONSE - parse, then apply via the same
 *     RtcSync_ApplyEpoch() - CC's reply to the LNC's own boot-time
 *     TAG_GET_SYSTEM_TIME_REQUEST (Init_Start(), init.c). The new,
 *     active, LNC-initiated synchronization mechanism; both mechanisms
 *     share the same RTC-application logic and are equally supported.
 *   - Count what happened (see the getters below) so hardware testing can
 *     watch dispatch activity live in the debugger, same convention as
 *     every other module.
 *
 * This file does NOT:
 *   - Read the RTC directly - the caller supplies "timestamp" for the
 *     tags that need one (same pattern as every other module:
 *     Monitor_Sample(), Init_Start(), KeepAlive_Send()); RTC reads/writes
 *     for TAG_SET_RTC_DATETIME/TAG_SYSTEM_TIME_RESPONSE go through
 *     rtc_sync.h's RtcSync_ApplyEpoch(), implemented in main.c where the
 *     RTC handle lives - this file never touches HAL RTC functions itself.
 *   - Act on TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST or
 *     TAG_GET_EVENTS_BY_RANGE_REQUEST. Deliberately deferred: replying
 *     needs historical data read back from the SD card's dated .LOG/.TXT
 *     files and turned into chunked responses - a separate, not-yet-built
 *     feature, not a small addition here.
 *   - Own a FreeRTOS task, or read bytes off UART itself - CommRxTask
 *     (main.c) still owns decoding; it calls Communication_Dispatch()
 *     once per successfully decoded frame.
 */

#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include "protocol.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Looks at "frame"'s Tag and acts on it (see this file's header comment
 * for exactly which tags are handled this pass vs. deliberately
 * deferred). "timestamp" is used wherever a handled tag needs one (the
 * config-changed event report, the system-time response).
 */
void Communication_Dispatch(const ProtocolFrame *frame, uint32_t timestamp);

/* --- Hardware-test observability, same spirit as every other module's
 * getters (Config_WasLoadedFromFlash(), Event_IsAlarmActive(), etc.) --- */

/* The Tag of the most recently dispatched frame (any outcome). */
uint8_t Communication_GetLastDispatchedTag(void);

/* Number of "set limit" commands successfully applied (parse succeeded). */
uint32_t Communication_GetSetLimitCount(void);

/* Number of GET_SYSTEM_TIME_REQUEST frames successfully replied to. */
uint32_t Communication_GetSystemTimeRequestCount(void);

/* Number of frames whose Tag is recognized but intentionally not yet
 * acted upon (the 2 retrieve-by-range requests - SET_RTC_DATETIME is no
 * longer deferred as of Phase 1, LNC Timestamp/RTC Hardening). */
uint32_t Communication_GetDeferredCount(void);

/* Number of frames whose Tag is not recognized at all (matches the
 * frozen protocol's "unknown tags dropped silently", Sec 6) - also
 * counts a recognized tag whose payload failed to parse. */
uint32_t Communication_GetUnknownTagCount(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMUNICATION_H */
