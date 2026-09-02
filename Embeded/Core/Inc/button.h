/*
 * button.h
 *
 * Purpose:
 *   Reads the alarm-stop button on pin PA10 (Button_Pin).
 *
 * Layer:
 *   Driver (sits above the STM32 HAL, below the Application layer)
 *
 * Responsibilities:
 *   - Read the current state of the button pin and return it as a simple
 *     pressed/not-pressed value.
 *
 * This file does NOT:
 *   - Know about alarms, Event, or what to do when the button is pressed
 *     (that is the Event module's decision, Sec 2.3 of the spec).
 *   - Run as its own FreeRTOS task - it is called synchronously by
 *     whatever needs to check the button (Event, later).
 *   - Debounce the reading - one call = one raw pin read. If real
 *     hardware testing shows bounce is a real problem, debouncing can be
 *     added later - not built preemptively.
 *
 * Hardware / polarity - UNCONFIRMED, pending a real-hardware test:
 *   Button_Pin (PA10) is a plain GPIO input, no pull (GPIO_NOPULL) - the
 *   MCU itself does not bias the line, so the idle (not-pressed) level
 *   depends on the external button module's own circuit. Current
 *   assumption, to be verified/corrected on real hardware: LOW = pressed,
 *   HIGH = not pressed (the common wiring for breakout push-button
 *   modules with an onboard pull-up). Update this comment and the
 *   mapping in button.c once confirmed.
 */

#ifndef BUTTON_H
#define BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUTTON_NOT_PRESSED,
    BUTTON_PRESSED
} ButtonState;

/* Reads the button pin once and returns BUTTON_PRESSED or
 * BUTTON_NOT_PRESSED. See the polarity note above - not yet confirmed on
 * real hardware. */
ButtonState Button_Read(void);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_H */
