/*
 * keepalive.c
 *
 * See keepalive.h for purpose, layer, and responsibilities.
 */

#include "keepalive.h"
#include "message.h"
#include "monitor.h"
#include "tx_queue.h"
#include "rtc_sync.h"

uint16_t KeepAlive_Send(uint32_t timestamp)
{
    MeasurementSample sample;
    uint8_t frame[32];
    uint16_t frameLength;

    Monitor_GetLastSample(&sample);
    sample.timestamp = timestamp;

    frameLength = Message_BuildKeepAlive(&sample, frame, sizeof(frame));

    /* Phase 1 (LNC Timestamp/RTC Hardening): gated on
     * RtcSync_IsSynchronized() - before the RTC has ever been set from a
     * real CC-supplied epoch, this frame's timestamp would be misleading
     * if it reached DataStore (KeepAlive rows share the measurements
     * table), so it is simply never sent. The frame is still built above
     * (frameLength still reflects Message_BuildKeepAlive()'s own success/
     * failure, unrelated to sync state) - only the transmission itself is
     * withheld. */
    if (frameLength > 0 && RtcSync_IsSynchronized())
    {
        TxQueue_EnqueueKeepAlive(frame, frameLength);
    }

    return frameLength;
}
