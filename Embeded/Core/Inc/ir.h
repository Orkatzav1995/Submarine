/*
 * ir.h
 *
 * Purpose:
 *   Reads the object-detection sensor on pin PB10 (IR_Pin). This is a
 *   single-pin digital IR proximity/obstacle sensor (VCC/GND/digital-OUT
 *   style module), not an ultrasonic sonar sensor - the spec calls this
 *   module's job "sonar," but only one GPIO pin is allocated to it in
 *   this project (Sec 9.1/Open Question #1), and a real sonar module
 *   would need a second pin (Trigger + Echo) plus timer input-capture for
 *   distance measurement, neither of which exists here.
 *
 * Layer:
 *   Driver (sits above the STM32 HAL, below the Application layer)
 *
 * Responsibilities:
 *   - Read the current state of the IR sensor pin and return it as a
 *     simple detected/not-detected value.
 *
 * This file does NOT:
 *   - Know about Event, alarms, or what to do when an object is detected
 *     (that is the Object Detection / Event modules' decision, Sec 2.2
 *     and 2.3 of the spec).
 *   - Run as its own FreeRTOS task - the future ObjectDetectionTask
 *     (Sec 7 of the project guide) will call this driver repeatedly; this
 *     file only reads the pin once per call.
 *   - Debounce or filter the reading - one call = one raw pin read.
 *
 * Hardware / polarity - UNCONFIRMED, pending a real-hardware test:
 *   IR_Pin (PB10) is a plain GPIO input, no pull (GPIO_NOPULL) - the
 *   sensor itself actively drives the line either way. Which level means
 *   "object detected" is NOT yet confirmed. Current assumption, to be
 *   verified/corrected on real hardware: LOW = detected, HIGH = not
 *   detected (the common wiring for this style of sensor). Update this
 *   comment and the mapping in ir.c once confirmed.
 */

#ifndef IR_H
#define IR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IR_NOT_DETECTED,
    IR_DETECTED
} IrState;

/* Reads the IR sensor pin once and returns IR_DETECTED or
 * IR_NOT_DETECTED. See the polarity note above - not yet confirmed on
 * real hardware. */
IrState IR_Read(void);

#ifdef __cplusplus
}
#endif

#endif /* IR_H */
