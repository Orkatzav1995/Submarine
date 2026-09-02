/*
 * management_command.cpp
 *
 * Purpose:
 *   Implements the ManagementCommand class declared in management_command.h.
 *
 * Layer:
 *   Application (built on top of Communication)
 *
 * This file does NOT:
 *   - Know about SOF, Tag, Length, or CRC internals.
 *   - Touch a serial port directly - all sending goes through Communication.
 */

#include "management_command.h"
#include "message.h"

namespace management_command {

ManagementCommand::ManagementCommand(communication::Communication &communication)
    : communication_(communication)
{
}

bool ManagementCommand::setTempNormalRange(float low, float high)
{
    message::TemperatureRangeMessage m;
    m.low = low;
    m.high = high;
    return communication_.sendFrame(message::buildSetTempNormalRange(m));
}

bool ManagementCommand::setTempWarningRange(float low, float high)
{
    message::TemperatureRangeMessage m;
    m.low = low;
    m.high = high;
    return communication_.sendFrame(message::buildSetTempWarningRange(m));
}

bool ManagementCommand::setHumidityNormalLower(float value)
{
    message::SingleLimitMessage m;
    m.limitValue = value;
    return communication_.sendFrame(message::buildSetHumidityNormalLower(m));
}

bool ManagementCommand::setHumidityWarningLower(float value)
{
    message::SingleLimitMessage m;
    m.limitValue = value;
    return communication_.sendFrame(message::buildSetHumidityWarningLower(m));
}

bool ManagementCommand::setLightNormalLower(float value)
{
    message::SingleLimitMessage m;
    m.limitValue = value;
    return communication_.sendFrame(message::buildSetLightNormalLower(m));
}

bool ManagementCommand::setLightWarningLower(float value)
{
    message::SingleLimitMessage m;
    m.limitValue = value;
    return communication_.sendFrame(message::buildSetLightWarningLower(m));
}

bool ManagementCommand::setBatteryNormalLower(float value)
{
    message::SingleLimitMessage m;
    m.limitValue = value;
    return communication_.sendFrame(message::buildSetBatteryNormalLower(m));
}

bool ManagementCommand::setBatteryWarningLower(float value)
{
    message::SingleLimitMessage m;
    m.limitValue = value;
    return communication_.sendFrame(message::buildSetBatteryWarningLower(m));
}

bool ManagementCommand::setRtcDateTime(uint32_t timestamp)
{
    message::TimestampMessage m;
    m.timestamp = timestamp;
    return communication_.sendFrame(message::buildSetRtcDateTime(m));
}

bool ManagementCommand::requestSystemTime()
{
    return communication_.sendFrame(message::buildGetSystemTimeRequest());
}

}  // namespace management_command
