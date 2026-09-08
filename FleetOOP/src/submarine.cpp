/*
 * submarine.cpp
 *
 * Purpose:
 *   Implements the Submarine base class declared in submarine.h.
 *
 * Layer:
 *   Fleet/OOP model.
 *
 * This file does NOT:
 *   - Print anything - display is each concrete submarine's own print().
 */

#include "submarine.h"

namespace fleet {

Submarine::Submarine(const std::string &serialNumber, const std::string &name)
    : serialNumber_(serialNumber), name_(name)
{
}

const std::string &Submarine::serialNumber() const
{
    return serialNumber_;
}

const std::string &Submarine::name() const
{
    return name_;
}

bool Submarine::isAssignedToMission() const
{
    return assignedToMission_;
}

void Submarine::assignToMission()
{
    assignedToMission_ = true;
}

void Submarine::endMission()
{
    assignedToMission_ = false;
}

}  // namespace fleet
