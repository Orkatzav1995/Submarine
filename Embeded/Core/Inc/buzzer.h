/*
 * buzzer.h
 *
 * Purpose:
 *   Drives the buzzer on pin PB4 (Buzzer_Pin), using TIM3 channel 1 PWM,
 *   to produce the alarm tone.
 *
 * Layer:
 *   Driver (sits above the STM32 HAL, below the Application layer)
 *
 * Responsibilities:
 *   - Turn the alarm tone on (start PWM at ~50% duty) or off (stop PWM).
 *
 * This file does NOT:
 *   - Know about alarms, Event, or when to sound the buzzer (that is the
 *     Event module's decision, Sec 2.3 of the spec).
 *   - Support a variable pitch/frequency - the spec only needs a fixed
 *     alarm tone, on or off.
 *   - Run as its own FreeRTOS task.
 *
 * Hardware:
 *   TIM3 is already configured by CubeMX-generated code (MX_TIM3_Init(),
 *   in main.c): Period = 100, Prescaler = 399, giving a PWM frequency of
 *   about 80,000,000 / (400 * 101) =~ 1980 Hz (~2 kHz), an audible tone.
 *   This driver only starts/stops PWM output, it does not configure the
 *   peripheral itself.
 */

#ifndef BUZZER_H
#define BUZZER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the alarm tone (PWM at ~50% duty on TIM3 channel 1). */
void Buzzer_On(void);

/* Stops the alarm tone (PWM output stopped, buzzer silent). */
void Buzzer_Off(void);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_H */
