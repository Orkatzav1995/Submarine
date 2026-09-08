/*
 * combat_submarine.cpp
 *
 * Purpose:
 *   Implements the CombatSubmarine class declared in combat_submarine.h.
 *
 * Layer:
 *   Fleet/OOP model.
 */

#include "combat_submarine.h"
#include <cstdio>

namespace fleet {

CombatSubmarine::CombatSubmarine(const std::string &serialNumber, const std::string &name,
                                   const std::string &commanderName, int personnelCount)
    : Submarine(serialNumber, name),
      commanderName_(commanderName),
      personnelCount_(personnelCount),
      centralComputer_(std::make_unique<central_computer::CentralComputer>())
{
    /* The CentralComputer is constructed here and never started - its own
       constructor is inert by design, so this opens no port, file, or
       socket (see central_computer.h). */
}

const std::string &CombatSubmarine::commanderName() const
{
    return commanderName_;
}

int CombatSubmarine::personnelCount() const
{
    return personnelCount_;
}

const std::string &CombatSubmarine::missionDescription() const
{
    return missionDescription_;
}

void CombatSubmarine::setMissionDescription(const std::string &missionDescription)
{
    missionDescription_ = missionDescription;
}

void CombatSubmarine::setMissionDetails(const std::string &missionDescription, const std::string &commanderName,
                                          int personnelCount)
{
    missionDescription_ = missionDescription;
    commanderName_ = commanderName;
    personnelCount_ = personnelCount;
}

void CombatSubmarine::addAlly(CombatSubmarine *ally)
{
    /* Ignore a null pointer, this submarine itself, and anyone already
       recorded, so the ally list stays a clean set of other submarines. */
    if (ally == nullptr || ally == this || isAllyOf(ally))
    {
        return;
    }
    allies_.push_back(ally);
}

const std::vector<CombatSubmarine *> &CombatSubmarine::allies() const
{
    return allies_;
}

bool CombatSubmarine::isAllyOf(const CombatSubmarine *other) const
{
    for (const CombatSubmarine *ally : allies_)
    {
        if (ally == other)
        {
            return true;
        }
    }
    return false;
}

void CombatSubmarine::clearAllies()
{
    allies_.clear();
}

void CombatSubmarine::receiveMessage(const Message &message)
{
    inbox_.push_back(message);
}

const std::vector<Message> &CombatSubmarine::inbox() const
{
    return inbox_;
}

central_computer::CentralComputer *CombatSubmarine::centralComputer() const
{
    return centralComputer_.get();
}

void CombatSubmarine::print() const
{
    std::printf("[%s] %s (%s) - %s\n", typeName().c_str(), name_.c_str(), serialNumber_.c_str(),
                 assignedToMission_ ? "assigned to a mission" : "available");
    std::printf("  Mission: %s\n", missionDescription_.empty() ? "(none)" : missionDescription_.c_str());
    std::printf("  Commander: %s\n", commanderName_.c_str());
    std::printf("  Personnel: %d\n", personnelCount_);

    if (allies_.empty())
    {
        std::printf("  Allied submarines on this mission: none\n");
    }
    else
    {
        std::printf("  Allied submarines on this mission:\n");
        for (const CombatSubmarine *ally : allies_)
        {
            std::printf("    - %s (%s)\n", ally->name().c_str(), ally->serialNumber().c_str());
        }
    }
}

std::string CombatSubmarine::typeName() const
{
    return "Combat";
}

}  // namespace fleet
