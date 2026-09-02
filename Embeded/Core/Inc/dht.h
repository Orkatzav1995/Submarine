/*
 * dht.h
 *
 * Purpose:
 *   Reads temperature and humidity from a DHT11 sensor on pin DHT_Pin (PB5),
 *   using its single-wire bit-banged protocol.
 *
 * Layer:
 *   Driver (sits above the STM32 HAL, below the Application layer)
 *
 * Responsibilities:
 *   - Start the microsecond timer (TIM6) this driver needs for bit timing.
 *   - Run the DHT11 read sequence and return one temperature+humidity sample.
 *
 * This file does NOT:
 *   - Know about Monitor, modes, or configured limits (that is the Monitor
 *     module's decision, Sec 2.1 of the spec).
 *   - Run as its own FreeRTOS task - it is called synchronously by whatever
 *     needs a reading (Monitor, later).
 *   - Use interrupts/EXTI - the protocol is timed by busy-waiting on TIM6's
 *     free-running counter, since the pulses involved (tens of microseconds)
 *     are far shorter than a single osDelay() tick (1 ms).
 *
 * Hardware:
 *   DHT_Pin (PB5) is configured as Output Open-Drain with an internal
 *   pull-up (set in CubeMX). This means the line never needs to switch
 *   GPIO mode: writing HIGH releases it (pulled high, or driven low by the
 *   sensor), writing LOW drives it low, and it can be read at any time.
 */

#ifndef DHT_H
#define DHT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperatureC;
    float humidityPercent;
} DhtReading;

typedef enum {
    DHT_OK,
    DHT_ERROR_TIMEOUT,   /* sensor did not respond, or a pulse ran too long */
    DHT_ERROR_CHECKSUM   /* data received, but the checksum byte did not match */
} DhtResult;

/* Starts the microsecond timer (TIM6) this driver needs. Call once at boot,
 * before the first DHT_Read(). */
void DHT_Init(void);

/* Runs one full DHT11 read. Blocks for a few milliseconds while the sensor
 * responds. Returns DHT_OK and fills *out on success. */
DhtResult DHT_Read(DhtReading *out);

#ifdef __cplusplus
}
#endif

#endif /* DHT_H */
