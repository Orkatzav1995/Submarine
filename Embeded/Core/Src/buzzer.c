/*
 * buzzer.c
 *
 * See buzzer.h for purpose, layer, and responsibilities.
 */

#include "buzzer.h"
#include "main.h"

/* htim3 is defined (not just declared) in main.c, by the CubeMX-generated
 * MX_TIM3_Init(). Declared extern here the same way battery.c does for
 * hadc1. */
extern TIM_HandleTypeDef htim3;

#define BUZZER_DUTY_CYCLE 50U  /* out of Period = 100, so about 50% */

void Buzzer_On(void)
{
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, BUZZER_DUTY_CYCLE);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
}

void Buzzer_Off(void)
{
    HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
}
