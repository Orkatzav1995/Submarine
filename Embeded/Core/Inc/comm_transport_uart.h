/*
 * comm_transport_uart.h
 *
 * Purpose:
 *   Thin wrapper around USART2 (the LNC <-> Central Computer link). Sends
 *   and receives raw bytes only - it has no idea what a TLV frame is.
 *
 *   USART2 is used in POLLING mode only: no interrupts, no DMA. This is a
 *   deliberate, locked decision for this project (simplicity over
 *   performance). Because of that, whoever calls Transport_UART_ReceiveByte()
 *   in a loop (CommRxTask) MUST call osDelay() whenever it times out with no
 *   byte received - otherwise a lower-priority task (KeepAliveTask) could be
 *   starved of CPU time forever. See PROJECT_GUIDE.md, section 7, for why.
 *
 * Layer:
 *   Transport (Transport -> Protocol -> Message -> Application)
 *
 * Responsibilities:
 *   - Send raw bytes out over USART2.
 *   - Receive raw bytes from USART2, one at a time, with a timeout.
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, CRC, or any TLV concept.
 *   - Call osDelay() or touch FreeRTOS in any way.
 *   - Print debug text on USART2 (that UART is reserved for the protocol only).
 */

#ifndef COMM_TRANSPORT_UART_H
#define COMM_TRANSPORT_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sends "length" bytes from "data" over USART2. Blocks until every byte is
 * sent or "timeout_ms" milliseconds pass.
 *
 * Returns 1 on success, 0 on failure or timeout.
 */
int Transport_UART_Send(const uint8_t *data, uint16_t length, uint32_t timeout_ms);

/*
 * Tries to receive exactly one byte from USART2, waiting at most
 * "timeout_ms" milliseconds.
 *
 * Returns 1 and writes the received byte into "*out_byte" if a byte arrived
 * in time. Returns 0 if the timeout elapsed with no byte - this is normal,
 * not an error, it just means "nothing arrived yet".
 */
int Transport_UART_ReceiveByte(uint8_t *out_byte, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* COMM_TRANSPORT_UART_H */
