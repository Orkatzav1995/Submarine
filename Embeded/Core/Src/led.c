/*
 * led.c
 *
 * See led.h for purpose, layer, and responsibilities.
 */

#include "led.h"
#include "main.h"

void Led_SetColor(LedColor color)
{
    GPIO_PinState red = GPIO_PIN_RESET;
    GPIO_PinState green = GPIO_PIN_RESET;
    GPIO_PinState blue = GPIO_PIN_RESET;

    switch (color)
    {
        case LED_COLOR_RED:
            red = GPIO_PIN_SET;
            break;

        case LED_COLOR_GREEN:
            green = GPIO_PIN_SET;
            break;

        case LED_COLOR_YELLOW:
            red = GPIO_PIN_SET;
            green = GPIO_PIN_SET;
            break;

        case LED_COLOR_OFF:
        default:
            break;
    }

    HAL_GPIO_WritePin(RGB_LED_3_GPIO_Port, RGB_LED_3_Pin, red);
    HAL_GPIO_WritePin(RGB_LED_1_GPIO_Port, RGB_LED_1_Pin, green);
    HAL_GPIO_WritePin(RGB_LED_2_GPIO_Port, RGB_LED_2_Pin, blue);
}
