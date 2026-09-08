/*
 * fleet.cpp
 *
 * Purpose:
 *   Implements the Fleet class declared in fleet.h.
 *
 * Layer:
 *   Fleet/OOP model.
 *
 * Note on dynamic_cast:
 *   Operations 7 and 8 exist only for combat submarines (Sec 5), so this
 *   file asks at runtime whether a submarine found by serial number is a
 *   CombatSubmarine. That check is the whole point of those operations'
 *   validation rules - a research submarine must be refused, not assumed.
 */

#include "fleet.h"
#include "combat_submarine.h"
#include <cstdio>

namespace fleet {

namespace {

/* Returns the submarine as a CombatSubmarine, or nullptr if it is not one
   (or does not exist at all). Keeps the two combat-only operations below
   from repeating the same null check and cast. */
CombatSubmarine *AsCombatSubmarine(Submarine *submarine)
{
    return dynamic_cast<CombatSubmarine *>(submarine);
}

}  // namespace

bool Fleet::addSubmarine(std::unique_ptr<Submarine> submarine)
{
    if (submarine == nullptr)
    {
        return false;
    }

    if (findBySerialNumber(submarine->serialNumber()) != nullptr)
    {
        return false;
    }

    submarines_.push_back(std::move(submarine));
    return true;
}

void Fleet::printAllSubmarines() const
{
    if (submarines_.empty())
    {
        std::printf("The fleet has no submarines yet.\n");
        return;
    }

    for (const std::unique_ptr<Submarine> &submarine : submarines_)
    {
        submarine->print();
    }
}

Submarine *Fleet::findBySerialNumber(const std::string &serialNumber)
{
    for (const std::unique_ptr<Submarine> &submarine : submarines_)
    {
        if (submarine->serialNumber() == serialNumber)
        {
            return submarine.get();
        }
    }
    return nullptr;
}

bool Fleet::assignMission(const std::string &serialNumber, const std::string &description)
{
    Submarine *submarine = findBySerialNumber(serialNumber);
    if (submarine == nullptr || submarine->isAssignedToMission())
    {
        return false;
    }

    submarine->assignToMission();

    /* A combat submarine keeps its own mission description (Sec 5), while
       the fleet-wide history entry is owned here either way. */
    CombatSubmarine *combat = AsCombatSubmarine(submarine);
    if (combat != nullptr)
    {
        combat->setMissionDescription(description);
    }

    missionHistory_.push_back(Mission(description, serialNumber));
    return true;
}

bool Fleet::endMission(const std::string &serialNumber)
{
    Submarine *submarine = findBySerialNumber(serialNumber);
    if (submarine == nullptr || !submarine->isAssignedToMission())
    {
        return false;
    }

    submarine->endMission();

    /* The allies were specific to the mission that just ended, so they are
       cleared here. The mission itself stays in the fleet's history. */
    CombatSubmarine *combat = AsCombatSubmarine(submarine);
    if (combat != nullptr)
    {
        combat->clearAllies();
    }

    return true;
}

bool Fleet::associateCombatSubmarines(const std::string &firstSerialNumber, const std::string &secondSerialNumber)
{
    if (firstSerialNumber == secondSerialNumber)
    {
        return false;
    }

    CombatSubmarine *first = AsCombatSubmarine(findBySerialNumber(firstSerialNumber));
    CombatSubmarine *second = AsCombatSubmarine(findBySerialNumber(secondSerialNumber));
    if (first == nullptr || second == nullptr)
    {
        return false;
    }

    if (!first->isAssignedToMission() || !second->isAssignedToMission())
    {
        return false;
    }

    if (first->isAllyOf(second))
    {
        return false;
    }

    first->addAlly(second);
    second->addAlly(first);
    return true;
}

bool Fleet::sendMessage(const std::string &senderSerialNumber, const std::string &receiverSerialNumber,
                          const std::string &content)
{
    if (content.empty())
    {
        return false;
    }

    CombatSubmarine *sender = AsCombatSubmarine(findBySerialNumber(senderSerialNumber));
    CombatSubmarine *receiver = AsCombatSubmarine(findBySerialNumber(receiverSerialNumber));
    if (sender == nullptr || receiver == nullptr)
    {
        return false;
    }

    /* Sec 5 limits messages to combat submarines "in the same mission",
       which is exactly what the ally relationship records. */
    if (!sender->isAllyOf(receiver))
    {
        return false;
    }

    receiver->receiveMessage(Message(sender, content));
    return true;
}

const std::vector<Mission> &Fleet::missionHistory() const
{
    return missionHistory_;
}

std::size_t Fleet::submarineCount() const
{
    return submarines_.size();
}

}  // namespace fleet
