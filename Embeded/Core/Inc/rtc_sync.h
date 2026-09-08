/*
 * rtc_sync.h
 *
 * Purpose:
 *   Phase 1 (LNC Timestamp/RTC Hardening). The minimal, shared API other
 *   modules use to (a) find out whether the RTC has ever been set from a
 *   real CC-supplied epoch, and (b) apply a newly-received epoch to it.
 *   Both are implemented in main.c, where the RTC handle (hrtc) and the
 *   internal "rtc_synchronized" flag already live - this header exists
 *   only so communication.c, monitor.c, object_detection.c, keepalive.c,
 *   and init.c can reach them without gaining a dependency on main.h
 *   (main.h is CubeMX's hardware/pin-definition header, currently
 *   included only by low-level peripheral drivers - no Application-layer
 *   module includes it, and this file does not change that).
 *
 * Layer:
 *   Sits alongside the Application layer (Monitor/Event/Communication/
 *   etc.) - it does not itself know about TLV, UART, or any message
 *   format, only about the RTC's synchronized/unsynchronized state.
 *
 * Responsibilities:
 *   - RtcSync_IsSynchronized(): report whether the RTC currently holds a
 *     real, CC-supplied time. Callers use this to decide whether a
 *     timestamped frame is safe to send to CC (see each caller's own
 *     comment for exactly what's gated).
 *   - RtcSync_ApplyEpoch(): the single shared path used by BOTH
 *     TAG_SET_RTC_DATETIME and TAG_SYSTEM_TIME_RESPONSE to actually set
 *     the RTC from a received Unix epoch, so both synchronization
 *     mechanisms stay consistent with each other.
 *
 * This file does NOT:
 *   - Expose the internal "rtc_synchronized" flag itself - only these two
 *     functions. No other file reads or writes that variable by name.
 *   - Know about TAG_SET_RTC_DATETIME, TAG_SYSTEM_TIME_RESPONSE, or any
 *     other wire concept - communication.c decides when to call these
 *     functions, this file only implements what happens once called.
 *   - Add any new wire-protocol field, message type, or DataStore change -
 *     this is purely an LNC-internal synchronization/gating mechanism.
 */

#ifndef RTC_SYNC_H
#define RTC_SYNC_H

#include <stdint.h>

/*
 * Returns 1 if the RTC currently holds a value derived from a real
 * CC-supplied epoch (via either sync mechanism this boot, or restored at
 * boot via the RTC_BKP_DR0 marker), 0 otherwise.
 */
int RtcSync_IsSynchronized(void);

/*
 * Validates "epochSeconds" is representable by this RTC's hardware
 * (RTC_DateTypeDef.Year is a uint8_t offset from 2000, so only years
 * 2000-2099 can actually be set), converts it to calendar fields, and
 * sets the RTC. Only on full success does this write the RTC_BKP_DR0
 * synchronization marker and mark the RTC synchronized (so a partially-
 * applied value is never trusted, this boot or after a future reboot).
 * Returns 1 on success, 0 on any failure - the RTC and synchronization
 * state are left exactly as they were before the call on failure.
 */
int RtcSync_ApplyEpoch(uint32_t epochSeconds);

#endif /* RTC_SYNC_H */
