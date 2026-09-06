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

static uint16_t s_lastStartupFrameLength = 0;

void Init_Start(uint32_t timestamp)
{
    TimestampWithFlagMessage startup;
    uint8_t frame[64];

    Configuration_Init();
    Log_Init();
    Event_Init();

    startup.timestamp = timestamp;
    startup.flag = 0; /* no watchdog exists yet - PROJECT_GUIDE.md Open Question #2 */
    s_lastStartupFrameLength = Event_OnInitStartup(&startup, frame, sizeof(frame));

    /* Event's frame is now enqueued at event priority (Sec 7) so
     * CommTxTask actually sends it, instead of being discarded. */
    if (s_lastStartupFrameLength > 0)
    {
        TxQueue_EnqueueEvent(frame, s_lastStartupFrameLength);
    }
}

uint16_t Init_GetLastStartupFrameLength(void)
{
    return s_lastStartupFrameLength;
}
