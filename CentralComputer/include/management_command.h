/*
 * management_command.h
 *
 * Purpose:
 *   The Central Computer's Management Command module (Sec 3). Turns a
 *   request to change an LNC configuration limit, set its RTC, or ask for
 *   its current time into an actual outgoing TLV frame - by calling the
 *   matching message.h builder and handing the result to
 *   Communication::sendFrame(). Callers work with plain scalar values
 *   (floats, a Unix timestamp) and never need to know message.h's struct
 *   shapes.
 *
 *   Covers exactly the 10 "management commands" from Sec 2.5 (the 8 set-
 *   limit commands + set-RTC + get-system-time) - NOT the 2 "retrieval
 *   instructions" (measurements/events by range), which Sec 2.5 lists as a
 *   separate category and which belong to the (not yet built) Data
 *   Collection & Analysis module instead.
 *
 * Layer:
 *   Application, built on top of Communication.
 *   (Transport -> Protocol -> Message -> Communication -> Application)
 *
 * Responsibilities:
 *   - One function per management command: build the frame via message.h,
 *     send it via the Communication reference given at construction,
 *     return whether the send succeeded.
 *
 * This file does NOT:
 *   - Handle the 2 retrieval-by-range requests (Data Collection & Analysis's job).
 *   - Parse or correlate responses - requestSystemTime() is fire-and-forget;
 *     the reply arrives later via Communication::callbacks.onSystemTimeResponse,
 *     handled by whoever registers it. No pending-request tracking exists yet.
 *   - Validate input values (e.g. low < high) - not specified by the spec.
 *   - Retry or queue anything - the CC has no TX priority queue requirement
 *     (that is LNC-only, Sec 2.5).
 *   - Own the Communication it sends through - it only holds a reference,
 *     since the same Communication instance is shared with other modules.
 */

#ifndef CENTRAL_COMPUTER_MANAGEMENT_COMMAND_H
#define CENTRAL_COMPUTER_MANAGEMENT_COMMAND_H

#include "communication.h"
#include <cstdint>

namespace management_command {

class ManagementCommand
{
public:
    explicit ManagementCommand(communication::Communication &communication);

    bool setTempNormalRange(float low, float high);
    bool setTempWarningRange(float low, float high);

    bool setHumidityNormalLower(float value);
    bool setHumidityWarningLower(float value);
    bool setLightNormalLower(float value);
    bool setLightWarningLower(float value);
    bool setBatteryNormalLower(float value);
    bool setBatteryWarningLower(float value);

    bool setRtcDateTime(uint32_t timestamp);

    /* Fire-and-forget: sends TAG_GET_SYSTEM_TIME_REQUEST. The LNC's reply
       arrives later, asynchronously, via the same Communication's
       callbacks.onSystemTimeResponse - not returned by this call. */
    bool requestSystemTime();

private:
    communication::Communication &communication_;
};

}  // namespace management_command

#endif  // CENTRAL_COMPUTER_MANAGEMENT_COMMAND_H
