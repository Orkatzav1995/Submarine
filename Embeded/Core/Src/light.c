/*
 * light.c
 *
 * See light.h for purpose, layer, and responsibilities.
 */

#include "light.h"
#include "main.h"

/* hadc2 is defined (not just declared) in main.c, by the CubeMX-generated
 * MX_ADC2_Init(). Declared extern here the same way battery.c does for
 * hadc1. */
extern ADC_HandleTypeDef hadc2;

#define LIGHT_ADC_TIMEOUT_MS 10U
#define LIGHT_ADC_MAX_VALUE  255.0f  /* 8-bit resolution: 2^8 - 1 */

float Light_ReadPercent(void)
{
    uint32_t raw = 0;

    HAL_ADC_Start(&hadc2);
    HAL_ADC_PollForConversion(&hadc2, LIGHT_ADC_TIMEOUT_MS);
    raw = HAL_ADC_GetValue(&hadc2);
    HAL_ADC_Stop(&hadc2);

    return ((float)raw / LIGHT_ADC_MAX_VALUE) * 100.0f;
}
