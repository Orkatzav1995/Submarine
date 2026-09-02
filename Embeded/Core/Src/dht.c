/*
 * dht.c
 *
 * See dht.h for purpose, layer, and responsibilities.
 */

#include "dht.h"
#include "main.h"
#include "cmsis_os.h"

/* htim6 is defined (not just declared) in main.c, by the CubeMX-generated
 * code. It is a free-running 1 MHz counter (1 tick = 1 microsecond),
 * started by DHT_Init(), and used here purely as a stopwatch. */
extern TIM_HandleTypeDef htim6;

#define DHT_START_LOW_MS      20   /* datasheet requires >= 18 ms */
#define DHT_EDGE_TIMEOUT_US   200  /* generous margin over the ~80 us max real edge time */
#define DHT_BIT_THRESHOLD_US  40   /* between a '0' bit (~26-28 us high) and a '1' bit (~70 us) */

void DHT_Init(void)
{
    HAL_TIM_Base_Start(&htim6);
}

/* Waits for DHT_Pin to reach the given level. Returns 1 if it did, or 0 if
 * timeoutUs passed first. The counter is reset to 0 here at the start of
 * every call, since TIM6's period (999) is much smaller than a uint16_t's
 * range - a plain "current minus start" subtraction would only wrap
 * correctly at 65536, not at 1000, so it could report a huge bogus elapsed
 * value right when the counter rolls over. Resetting to 0 each time avoids
 * that entirely.
 *
 * IMPORTANT - this function is timing-critical (a single wait must
 * complete within tens of microseconds) and has no protection of its own
 * against being interrupted. Callers running under FreeRTOS must ensure
 * nothing preempts the calling task for the duration of a DHT_Read() call
 * (see DHT_Read()'s comment, and StartTask02 in main.c for how this is
 * currently done) - this file itself stays FreeRTOS-independent. */
static int WaitForLevel(GPIO_PinState level, uint16_t timeoutUs)
{
    __HAL_TIM_SET_COUNTER(&htim6, 0);

    while (HAL_GPIO_ReadPin(DHT_GPIO_Port, DHT_Pin) != level)
    {
        if (__HAL_TIM_GET_COUNTER(&htim6) > timeoutUs)
        {
            return 0;
        }
    }
    return 1;
}

/* IMPORTANT - timing-critical. This function's total run time is only a
 * few milliseconds, but every wait inside it must complete within tens of
 * microseconds of the real signal edge. On real hardware, letting FreeRTOS
 * preempt this task mid-read (e.g. another task's periodic osDelay(1) tick
 * landing in the middle of a wait) is enough to miss an edge and time out
 * - confirmed by direct testing. This function itself has no FreeRTOS
 * dependency and takes no protective action on its own; it is the
 * caller's responsibility to ensure this task is not preempted for the
 * duration of the call (currently done in StartTask02 in main.c, by
 * temporarily raising the calling task's own priority for the call). */
DhtResult DHT_Read(DhtReading *out)
{
    uint8_t bytes[5] = {0};

    /* Start signal: drive the line low for >= 18 ms. */
    HAL_GPIO_WritePin(DHT_GPIO_Port, DHT_Pin, GPIO_PIN_RESET);
    osDelay(DHT_START_LOW_MS);

    /* Release the line (open-drain: this lets the pull-up take it high,
     * or the sensor pull it low - we never need to write the pin again
     * after this point). */
    HAL_GPIO_WritePin(DHT_GPIO_Port, DHT_Pin, GPIO_PIN_SET);

    /* Sensor's response: 80 us low, then 80 us high, before data starts. */
    if (!WaitForLevel(GPIO_PIN_RESET, DHT_EDGE_TIMEOUT_US)) return DHT_ERROR_TIMEOUT;
    if (!WaitForLevel(GPIO_PIN_SET, DHT_EDGE_TIMEOUT_US))   return DHT_ERROR_TIMEOUT;
    if (!WaitForLevel(GPIO_PIN_RESET, DHT_EDGE_TIMEOUT_US)) return DHT_ERROR_TIMEOUT;

    /* 40 data bits, MSB first. Each bit is a 50 us low period followed by
     * either a short (~26-28 us, '0') or long (~70 us, '1') high period. */
    for (int i = 0; i < 40; i++)
    {
        if (!WaitForLevel(GPIO_PIN_SET, DHT_EDGE_TIMEOUT_US)) return DHT_ERROR_TIMEOUT;

        /* WaitForLevel() below resets the counter to 0 as soon as it is
         * entered, so the counter's value once it returns success is
         * exactly this bit's high-pulse duration - no separate "start"
         * snapshot needed. */
        if (!WaitForLevel(GPIO_PIN_RESET, DHT_EDGE_TIMEOUT_US)) return DHT_ERROR_TIMEOUT;
        uint16_t elapsed = __HAL_TIM_GET_COUNTER(&htim6);

        uint8_t bit = (elapsed > DHT_BIT_THRESHOLD_US) ? 1 : 0;
        bytes[i / 8] = (uint8_t)((bytes[i / 8] << 1) | bit);
    }

    uint8_t checksum = (uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]);
    if (checksum != bytes[4])
    {
        return DHT_ERROR_CHECKSUM;
    }

    /* DHT11: humidity/temperature are plain integers; the decimal bytes
     * are always 0 on a genuine DHT11. */
    out->humidityPercent = (float)bytes[0];
    out->temperatureC = (float)bytes[2];

    return DHT_OK;
}
