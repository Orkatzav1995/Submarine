/*
 * tx_queue.h
 *
 * Purpose:
 *   Sec 7's frozen TX-priority-queue design: three queues -
 *   txQueueKeepAlive > txQueueEvent > txQueueData, in strict priority
 *   order - so CommTxTask can send whichever already-built frame is
 *   most urgent, instead of every sender writing to the UART directly.
 *   This is the piece that was still missing after Keep-Alive and the
 *   Communication/Dispatch module: both built frames and sent them
 *   straight to Transport_UART_Send() themselves, since nothing else
 *   existed yet to queue behind.
 *
 * Layer:
 *   Sits between Application code (Keep-Alive, Event's callers,
 *   Communication) and the Transport layer - callers hand it an
 *   already-encoded frame; only CommTxTask (main.c) ever calls
 *   Transport_UART_Send() now.
 *
 * Responsibilities:
 *   - Own 3 FreeRTOS message queues, statically allocated (see the .c
 *     file for why - the FreeRTOS heap margin is already very tight,
 *     see PROJECT_GUIDE.md; static allocation uses ordinary global
 *     memory instead, which this MCU has plenty of).
 *   - Accept an already-encoded frame (bytes + length) for one of the
 *     3 tiers, non-blocking - if that tier's queue is full, the frame
 *     is dropped rather than blocking the caller (same "no retry"
 *     philosophy already used elsewhere in this project, e.g.
 *     DataCollection's backfill design).
 *   - Hand CommTxTask (main.c) the single highest-priority frame
 *     currently waiting, if any, so it can send it.
 *
 * This file does NOT:
 *   - Know about TLV, Tags, or any Message-layer concept - a queued
 *     item is just "some bytes, this long." Building the frame is
 *     always the caller's job (Message_Build*()).
 *   - Call Transport_UART_Send() itself - CommTxTask does that, after
 *     calling TxQueue_DrainOne().
 *   - Decide WHICH tier a given message belongs to - each Enqueue
 *     function is for exactly one tier; callers pick the right one.
 *   - Run as its own FreeRTOS task.
 */

#ifndef TX_QUEUE_H
#define TX_QUEUE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The largest frame this project builds anywhere is well under 64
 * bytes (Keep-Alive: 27, every Event type, the system-time response) -
 * see tx_queue.c for the exact figure this is sized against. */
#define TX_QUEUE_MAX_ITEM_SIZE 64

/* Creates the 3 static queues. Call once, before the scheduler starts
 * (main()'s existing "USER CODE BEGIN RTOS_QUEUES" section is the
 * intended spot - CubeMX's own regeneration-safe marker for this). */
void TxQueue_Init(void);

/*
 * Each of these copies "length" bytes from "data" into that tier's
 * queue, non-blocking. Returns 1 if the frame was queued, 0 if
 * "length" is too large (> TX_QUEUE_MAX_ITEM_SIZE) or the queue is
 * currently full (dropped, not retried).
 */
int TxQueue_EnqueueKeepAlive(const uint8_t *data, uint16_t length);
int TxQueue_EnqueueEvent(const uint8_t *data, uint16_t length);
int TxQueue_EnqueueData(const uint8_t *data, uint16_t length);

/*
 * Checks txQueueKeepAlive, then txQueueEvent, then txQueueData, in that
 * strict priority order (Sec 7) - non-blocking. Copies the first
 * available item's bytes into "out_buffer" and returns its length, or
 * returns 0 if all 3 queues are currently empty.
 */
uint16_t TxQueue_DrainOne(uint8_t *out_buffer, uint16_t out_buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* TX_QUEUE_H */
