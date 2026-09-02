/*
 * button.c
 *
 * See button.h for purpose, layer, responsibilities, and the UNCONFIRMED
 * polarity note - LOW = pressed is the current assumption, not yet
 * verified on real hardware.
 */

#include "button.h"
#include "main.h"

ButtonState Button_Read(void)
{
    GPIO_PinState pinState = HAL_GPIO_ReadPin(Button_GPIO_Port, Button_Pin);

    /* UNCONFIRMED polarity assumption - see button.h. */
    if (pinState == GPIO_PIN_RESET)
    {
        return BUTTON_PRESSED;
    }

    return BUTTON_NOT_PRESSED;
}
