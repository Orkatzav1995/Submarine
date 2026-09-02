/*
 * battery.c
 *
 * See battery.h for purpose, layer, and responsibilities.
 */

#include "battery.h"
#include "main.h"

/* hadc1 is defined (not just declared) in main.c, by the CubeMX-generated
 * MX_ADC1_Init(). Declared extern here the same way dht.c does for htim6. */
extern ADC_HandleTypeDef hadc1;

#define BATTERY_ADC_TIMEOUT_MS 10U
#define BATTERY_ADC_MAX_VALUE  4095.0f  /* 12-bit resolution: 2^12 - 1 */
#define BATTERY_VREF_VOLTS     3.3f

float Battery_ReadVoltage(void)
{
    uint32_t raw = 0;

    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, BATTERY_ADC_TIMEOUT_MS);
    raw = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    return ((float)raw / BATTERY_ADC_MAX_VALUE) * BATTERY_VREF_VOLTS;
}
