/*
 * tx_queue.c
 *
 * See tx_queue.h for purpose, layer, and responsibilities.
 */

#include "tx_queue.h"
#include "cmsis_os.h"
#include <string.h>

/* One queued frame: how many bytes are actually used, plus the bytes
 * themselves. TX_QUEUE_MAX_ITEM_SIZE (64) comfortably covers every
 * frame this project builds anywhere (Keep-Alive: 27 bytes, every
 * Event type, the system-time response - all well under 64). */
typedef struct
{
    uint16_t length;
    uint8_t data[TX_QUEUE_MAX_ITEM_SIZE];
} TxQueueItem;

#define TX_QUEUE_KEEPALIVE_DEPTH 2  /* Sec 7 */
#define TX_QUEUE_EVENT_DEPTH     8  /* Sec 7 */
#define TX_QUEUE_DATA_DEPTH      4  /* Sec 7 */

/* Statically allocated - NOT from the FreeRTOS heap. The heap's free
 * margin is already very tight (see PROJECT_GUIDE.md, Object Detection
 * investigation) and configTOTAL_HEAP_SIZE is frozen; a plain static
 * global array costs ordinary RAM instead, which this MCU has plenty
 * of. configSUPPORT_STATIC_ALLOCATION is already enabled
 * (FreeRTOSConfig.h) - this is exactly what it is for. */
static StaticQueue_t s_keepAliveControlBlock;
static uint8_t s_keepAliveStorage[TX_QUEUE_KEEPALIVE_DEPTH * sizeof(TxQueueItem)];
static osMessageQueueId_t s_keepAliveQueue;

static StaticQueue_t s_eventControlBlock;
static uint8_t s_eventStorage[TX_QUEUE_EVENT_DEPTH * sizeof(TxQueueItem)];
static osMessageQueueId_t s_eventQueue;

static StaticQueue_t s_dataControlBlock;
static uint8_t s_dataStorage[TX_QUEUE_DATA_DEPTH * sizeof(TxQueueItem)];
static osMessageQueueId_t s_dataQueue;

static osMessageQueueId_t CreateStaticQueue(uint32_t depth, StaticQueue_t *controlBlock, uint8_t *storage, uint32_t storageSize)
{
    osMessageQueueAttr_t attr = { 0 };

    attr.cb_mem = controlBlock;
    attr.cb_size = sizeof(StaticQueue_t);
    attr.mq_mem = storage;
    attr.mq_size = storageSize;

    return osMessageQueueNew(depth, sizeof(TxQueueItem), &attr);
}

void TxQueue_Init(void)
{
    s_keepAliveQueue = CreateStaticQueue(TX_QUEUE_KEEPALIVE_DEPTH, &s_keepAliveControlBlock,
                                          s_keepAliveStorage, sizeof(s_keepAliveStorage));
    s_eventQueue = CreateStaticQueue(TX_QUEUE_EVENT_DEPTH, &s_eventControlBlock,
                                      s_eventStorage, sizeof(s_eventStorage));
    s_dataQueue = CreateStaticQueue(TX_QUEUE_DATA_DEPTH, &s_dataControlBlock,
                                     s_dataStorage, sizeof(s_dataStorage));
}

/* Shared by all 3 Enqueue functions - only which queue differs. */
static int Enqueue(osMessageQueueId_t queue, const uint8_t *data, uint16_t length)
{
    TxQueueItem item;

    if (length > TX_QUEUE_MAX_ITEM_SIZE)
    {
        return 0;
    }

    item.length = length;
    memcpy(item.data, data, length);

    /* timeout = 0: never block the caller. If the queue is full, the
     * frame is dropped - same "no retry" philosophy already used
     * elsewhere in this project (DataCollection's backfill design). */
    return (osMessageQueuePut(queue, &item, 0, 0) == osOK) ? 1 : 0;
}

int TxQueue_EnqueueKeepAlive(const uint8_t *data, uint16_t length)
{
    return Enqueue(s_keepAliveQueue, data, length);
}

int TxQueue_EnqueueEvent(const uint8_t *data, uint16_t length)
{
    return Enqueue(s_eventQueue, data, length);
}

int TxQueue_EnqueueData(const uint8_t *data, uint16_t length)
{
    return Enqueue(s_dataQueue, data, length);
}

uint16_t TxQueue_DrainOne(uint8_t *out_buffer, uint16_t out_buffer_size)
{
    TxQueueItem item;
    osMessageQueueId_t queuesInPriorityOrder[3];
    int i;

    /* Sec 7: keepalive, then event, then data - checked in that strict
     * order every call, so a lower tier is only ever serviced when
     * every higher tier is currently empty. */
    queuesInPriorityOrder[0] = s_keepAliveQueue;
    queuesInPriorityOrder[1] = s_eventQueue;
    queuesInPriorityOrder[2] = s_dataQueue;

    for (i = 0; i < 3; i++)
    {
        if (osMessageQueueGet(queuesInPriorityOrder[i], &item, NULL, 0) == osOK)
        {
            uint16_t copyLength = (item.length < out_buffer_size) ? item.length : out_buffer_size;
            memcpy(out_buffer, item.data, copyLength);
            return copyLength;
        }
    }

    return 0;
}
