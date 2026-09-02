/*
 * ir.c
 *
 * See ir.h for purpose, layer, responsibilities, and the UNCONFIRMED
 * polarity note - LOW = detected is the current assumption, not yet
 * verified on real hardware.
 */

#include "ir.h"
#include "main.h"

IrState IR_Read(void)
{
    GPIO_PinState pinState = HAL_GPIO_ReadPin(IR_GPIO_Port, IR_Pin);

    /* UNCONFIRMED polarity assumption - see ir.h. */
    if (pinState == GPIO_PIN_RESET)
    {
        return IR_DETECTED;
    }

    return IR_NOT_DETECTED;
}
