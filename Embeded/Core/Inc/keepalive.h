/*
 * keepalive.h
 *
 * Purpose:
 *   Every 6 seconds (Sec 2.8 of the spec): builds a TAG_KEEPALIVE frame
 *   from Monitor's most recent sample and the timestamp supplied by the
 *   caller, then hands it to the Sec 7 TX-priority-queue system
 *   (txQueueKeepAlive) so CommTxTask can send it to the Central Computer
 *   in the correct priority order.
 *
 * Layer:
 *   Application (uses Monitor's getter, the Message layer's existing
 *   Message_BuildKeepAlive(), and tx_queue.h's
 *   TxQueue_EnqueueKeepAlive() - never touches Transport_UART_Send()
 *   directly)
 *
 * Responsibilities:
 *   - Retrieve Monitor's last-known sample (temperature/humidity/light/
 *     battery/mode) via Monitor_GetLastSample().
 *   - Overwrite that sample's timestamp with the fresh one the caller
 *     supplies (the sample's own timestamp reflects when Monitor last
 *     sampled, not "now" - Sec 2.8 wants the keepalive's own send time).
 *   - Build one TAG_KEEPALIVE frame per call and enqueue it at
 *     keepalive priority (the highest tier, Sec 7).
 *
 * This file does NOT:
 *   - Read the RTC - the caller supplies the timestamp (same pattern
 *     Monitor_Sample()/Init_Start() already use - no Application module
 *     in this project reads the RTC internally).
 *   - Own a FreeRTOS task or its own osDelay() loop - KeepAliveTask
 *     (Sec 7) calls KeepAlive_Send() once per cycle and owns the 6s delay.
 *   - Send anything over UART itself, or manage the TX priority queues -
 *     CommTxTask (main.c) is the only task that ever calls
 *     Transport_UART_Send() now; this file only enqueues.
 */

#ifndef KEEPALIVE_H
#define KEEPALIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Builds a TAG_KEEPALIVE frame from Monitor's latest sample (with its
 * timestamp replaced by "timestamp") and enqueues it for CommTxTask to
 * send at keepalive priority.
 *
 * Returns the frame's length, or 0 if it could not be built (same
 * failure meaning as Message_BuildKeepAlive/Protocol_EncodeFrame) - a
 * successful build that the queue then drops because it was full is
 * not distinguished from a successful send; see tx_queue.h.
 */
uint16_t KeepAlive_Send(uint32_t timestamp);

#ifdef __cplusplus
}
#endif

#endif /* KEEPALIVE_H */
