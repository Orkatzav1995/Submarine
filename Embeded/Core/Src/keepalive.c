/*
 * keepalive.c
 *
 * See keepalive.h for purpose, layer, and responsibilities.
 */

#include "keepalive.h"
#include "message.h"
#include "monitor.h"
#include "tx_queue.h"

uint16_t KeepAlive_Send(uint32_t timestamp)
{
    MeasurementSample sample;
    uint8_t frame[32];
    uint16_t frameLength;

    Monitor_GetLastSample(&sample);
    sample.timestamp = timestamp;

    frameLength = Message_BuildKeepAlive(&sample, frame, sizeof(frame));

    if (frameLength > 0)
    {
        TxQueue_EnqueueKeepAlive(frame, frameLength);
    }

    return frameLength;
}
