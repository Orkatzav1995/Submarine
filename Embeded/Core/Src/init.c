/*
 * init.c
 *
 * See init.h for purpose, layer, and responsibilities.
 */

#include "init.h"
#include "configuration.h"
#include "log.h"
#include "event.h"
#include "tx_queue.h"
#include "rtc_sync.h"
#include "message.h"

static uint16_t s_lastStartupFrameLength = 0;

void Init_Start(uint32_t timestamp, uint8_t wasWatchdogReset)
{
    TimestampWithFlagMessage startup;
    uint8_t frame[64];

    Configuration_Init();
    Log_Init();
    Event_Init();

    startup.timestamp = timestamp;
    startup.flag = wasWatchdogReset;

    /* Event_OnInitStartup() itself (local events-file record, LED/buzzer)
     * always runs, unconditionally, regardless of RTC synchronization -
     * only the CC-bound transmission below is gated. */
    s_lastStartupFrameLength = Event_OnInitStartup(&startup, frame, sizeof(frame));

    /* Event's frame is enqueued at event priority (Sec 7) so CommTxTask
     * actually sends it, instead of being discarded - but only once the
     * RTC has been synchronized (Phase 1, LNC Timestamp/RTC Hardening).
     * This function runs synchronously during boot, strictly before the
     * boot-time time-sync request below can possibly have been answered -
     * so on every real boot, this transmission is skipped, deterministically,
     * every time. This is a known, accepted consequence of gating at this
     * layer (see PROJECT_GUIDE.md), not a bug: the startup event is still
     * fully recorded locally (events file, LED, buzzer) via
     * Event_OnInitStartup() above, it simply never reaches CC. No
     * deferred/retry transmission is implemented for it in this phase, by
     * explicit decision. */
    if (s_lastStartupFrameLength > 0 && RtcSync_IsSynchronized())
    {
        TxQueue_EnqueueEvent(frame, s_lastStartupFrameLength);
    }

    /* Phase 1 (LNC Timestamp/RTC Hardening): Sec 2.7's "Init requests
     * time/date sync from CC" - the active, LNC-initiated half of RTC
     * synchronization (the existing TAG_SET_RTC_DATETIME push from CC
     * remains supported unchanged, see communication.c). Sent
     * unconditionally, once, every boot, regardless of RtcSync_IsSynchronized()
     * - sending this request is exactly how synchronization becomes
     * possible in the first place, so it must never be gated on the very
     * state it exists to change. No retry/timeout if CC never answers,
     * matching this project's established convention elsewhere
     * (DataCollection's backfill requests, CcCommunication's range
     * requests) - the LNC simply stays unsynchronized until an answer
     * eventually arrives, or a manual TAG_SET_RTC_DATETIME is sent instead. */
    {
        uint8_t requestFrame[16];
        uint16_t requestFrameLength = Message_BuildGetSystemTimeRequest(requestFrame, sizeof(requestFrame));

        if (requestFrameLength > 0)
        {
            TxQueue_EnqueueEvent(requestFrame, requestFrameLength);
        }
    }
}

uint16_t Init_GetLastStartupFrameLength(void)
{
    return s_lastStartupFrameLength;
}
