/*
 * led.h
 *
 * Purpose:
 *   Drives the RGB LED (3 separate GPIO pins, one per color channel).
 *   The LED has no PWM/dimming - each channel is just on or off - so a
 *   color is just a fixed combination of the 3 pins.
 *
 * Layer:
 *   Driver (sits above the STM32 HAL, below the Application layer)
 *
 * Responsibilities:
 *   - Turn the LED to one of a fixed set of colors (or off).
 *
 * This file does NOT:
 *   - Know about modes, events, or when to change color (that is the
 *     Event module's decision, Sec 2.3 of the spec).
 *   - Do any blinking, timing, or PWM/dimming.
 *   - Run as its own FreeRTOS task.
 *
 * Pin mapping (confirmed on real hardware - full color cycle observed
 * correctly: Red -> Yellow -> Green -> Off):
 *   RGB_LED_1 (PA9) -> Green
 *   RGB_LED_2 (PA8) -> Blue
 *   RGB_LED_3 (PC9) -> Red
 */

#ifndef LED_H
#define LED_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_COLOR_OFF,
    LED_COLOR_RED,
    LED_COLOR_YELLOW,
    LED_COLOR_GREEN
} LedColor;

void Led_SetColor(LedColor color);

#ifdef __cplusplus
}
#endif

#endif /* LED_H */
