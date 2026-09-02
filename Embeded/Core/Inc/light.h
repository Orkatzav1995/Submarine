/*
 * light.h
 *
 * Purpose:
 *   Reads the light sensor on pin PA1 (ADC2, channel 6, 8-bit).
 *
 * Layer:
 *   Driver (sits above the STM32 HAL, below the Application layer)
 *
 * Responsibilities:
 *   - Run one ADC conversion and return the result as a percentage
 *     (0-100%), scaled from ADC2's 8-bit range (0-255).
 *
 * This file does NOT:
 *   - Know about Monitor, modes, or configured limits (that is the Monitor
 *     module's decision, Sec 2.1 of the spec).
 *   - Run as its own FreeRTOS task - it is called synchronously by whatever
 *     needs a reading (Monitor, later).
 *   - Average/filter multiple samples - one call = one conversion.
 *
 * Hardware:
 *   ADC2 is already configured by CubeMX-generated code (MX_ADC2_Init(),
 *   in main.c) - this driver only triggers conversions, it does not
 *   configure the peripheral itself. ADC2 is 8-bit (0-255), unlike ADC1
 *   (12-bit, used by battery.c) - a percentage is returned instead of a
 *   voltage since the sensor's raw voltage range is not a meaningful
 *   physical unit here.
 */

#ifndef LIGHT_H
#define LIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Runs one ADC conversion on PA1 and returns the result as a percentage
 * (0.0-100.0). Blocks briefly (microseconds) while the conversion runs. */
float Light_ReadPercent(void);

#ifdef __cplusplus
}
#endif

#endif /* LIGHT_H */
