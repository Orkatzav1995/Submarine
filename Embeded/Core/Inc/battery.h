/*
 * battery.h
 *
 * Purpose:
 *   Reads the potentiometer standing in for a battery voltage gauge, on
 *   pin PA0 (ADC1, channel 5, 12-bit).
 *
 * Layer:
 *   Driver (sits above the STM32 HAL, below the Application layer)
 *
 * Responsibilities:
 *   - Run one ADC conversion and return the result as a voltage (0.0-3.3V).
 *
 * This file does NOT:
 *   - Know about Monitor, modes, or configured limits (that is the Monitor
 *     module's decision, Sec 2.1 of the spec).
 *   - Run as its own FreeRTOS task - it is called synchronously by whatever
 *     needs a reading (Monitor, later).
 *   - Average/filter multiple samples - one call = one conversion.
 *
 * Hardware:
 *   The potentiometer is wired across the full 3.3V rail, wiper on PA0.
 *   ADC1 is already configured by CubeMX-generated code (MX_ADC1_Init(),
 *   in main.c) - this driver only triggers conversions, it does not
 *   configure the peripheral itself.
 */

#ifndef BATTERY_H
#define BATTERY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Runs one ADC conversion on PA0 and returns the result as a voltage
 * (0.0V-3.3V). Blocks briefly (microseconds) while the conversion runs. */
float Battery_ReadVoltage(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_H */
