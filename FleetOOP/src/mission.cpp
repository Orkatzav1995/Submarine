/*
 * mission.cpp
 *
 * Purpose:
 *   Implements the Mission class declared in mission.h.
 *
 * Layer:
 *   Fleet/OOP model.
 */

#include "mission.h"

namespace fleet {

Mission::Mission(const std::string &description, const std::string &submarineSerialNumber)
    : description_(description), submarineSerialNumber_(submarineSerialNumber)
{
}

const std::string &Mission::description() const
{
    return description_;
}

const std::string &Mission::submarineSerialNumber() const
{
    return submarineSerialNumber_;
}

}  // namespace fleet
