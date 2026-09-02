/*
 * comm_transport_uart.c
 *
 * Purpose:
 *   Implements the raw byte send/receive functions declared in
 *   comm_transport_uart.h, using the STM32 HAL polling functions on USART2.
 *
 * Layer:
 *   Transport
 *
 * Responsibilities:
 *   - Send bytes over USART2 (HAL_UART_Transmit, polling).
 *   - Receive one byte from USART2 with a timeout (HAL_UART_Receive, polling).
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, CRC, or any TLV concept.
 *   - Call osDelay() or touch FreeRTOS in any way.
 *   - Use interrupts or DMA - USART2 is polling-only, by design.
 */

#include "comm_transport_uart.h"
#include "main.h"

/* huart2 is defined (not just declared) in main.c, by the CubeMX-generated
   MX_USART2_UART_Init(), which already runs before any task starts. */
extern UART_HandleTypeDef huart2;

int Transport_UART_Send(const uint8_t *data, uint16_t length, uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;

    /* HAL_UART_Transmit's signature takes a non-const pointer even though it
       never modifies the data - this cast just works around that. */
    status = HAL_UART_Transmit(&huart2, (uint8_t *)data, length, timeout_ms);

    return (status == HAL_OK) ? 1 : 0;
}

int Transport_UART_ReceiveByte(uint8_t *out_byte, uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;

    status = HAL_UART_Receive(&huart2, out_byte, 1, timeout_ms);

    /* Any non-OK result (timeout or error) just means "no byte right now" -
       the caller (CommRxTask) treats both the same way: try again later. */
    return (status == HAL_OK) ? 1 : 0;
}
