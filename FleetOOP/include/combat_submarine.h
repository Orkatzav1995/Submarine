/*
 * combat_submarine.h
 *
 * Purpose:
 *   The second concrete submarine type in the Fleet/OOP exercise
 *   (PROJECT_GUIDE.md Sec 5): a combat submarine adds a mission
 *   description, a commander name, and a personnel count, keeps track of
 *   the other combat submarines on its current mission, and - per Sec 5 -
 *   owns a CentralComputer, "an object that belongs to each combat
 *   submarine".
 *
 * Layer:
 *   Fleet/OOP model. In-memory only.
 *
 * Responsibilities:
 *   - Hold the three combat-specific fields Sec 5 gives this type, and
 *     allow them to be updated (Fleet menu operation 5).
 *   - Track allied combat submarines on the same mission (Fleet menu
 *     operation 7) and answer whether another submarine is one of them,
 *     which is what makes menu operation 8's "same mission" rule checkable.
 *   - Own one CentralComputer for its whole lifetime.
 *   - Provide the type-specific display Fleet menu operations 2 and 3 use.
 *
 * CentralComputer ownership (Sec 5's locked decoupling rule):
 *   The CentralComputer is created in this class's constructor and
 *   destroyed with it, but is deliberately NEVER started - nothing in
 *   FleetOOP calls start(), stop(), poll(), or drives it through its
 *   accessors, so no serial port, database, or socket is ever opened by
 *   the Fleet/OOP layer. This file is also the only place in FleetOOP that
 *   includes central_computer.h; no FleetOOP file ever includes
 *   Communication, GsCommunication, DataStore, or any protocol/transport
 *   header.
 *
 * This file does NOT:
 *   - Know about Fleet or Mission - a submarine stays independent of the
 *     fleet that owns it (Sec 5's decoupling decision).
 *   - Own the allied submarines it points at - Fleet owns every submarine.
 *     None of the ten menu operations removes a submarine from the fleet,
 *     so these non-owning pointers cannot outlive what they point to.
 *   - Prompt for input - fleet_main.cpp owns all console interaction.
 */

#ifndef FLEET_OOP_COMBAT_SUBMARINE_H
#define FLEET_OOP_COMBAT_SUBMARINE_H

#include "central_computer.h"
#include "fleet_message.h"
#include "submarine.h"
#include <memory>
#include <string>
#include <vector>

namespace fleet {

class CombatSubmarine : public Submarine
{
public:
    CombatSubmarine(const std::string &serialNumber, const std::string &name, const std::string &commanderName,
                     int personnelCount);
    ~CombatSubmarine() override = default;

    const std::string &commanderName() const;
    int personnelCount() const;
    const std::string &missionDescription() const;

    void setMissionDescription(const std::string &missionDescription);
    void setMissionDetails(const std::string &missionDescription, const std::string &commanderName,
                            int personnelCount);

    void addAlly(CombatSubmarine *ally);
    const std::vector<CombatSubmarine *> &allies() const;
    bool isAllyOf(const CombatSubmarine *other) const;
    void clearAllies();

    /* Messages are stored on the receiving submarine, which is what Fleet
       menu operation 9 displays. */
    void receiveMessage(const Message &message);
    const std::vector<Message> &inbox() const;

    /* Non-owning view of the owned CentralComputer, used to confirm it
       exists and was never started. FleetOOP itself never uses this to
       drive the Central Computer. */
    central_computer::CentralComputer *centralComputer() const;

    void print() const override;
    std::string typeName() const override;

private:
    std::string commanderName_;
    int personnelCount_ = 0;
    std::string missionDescription_;

    std::vector<CombatSubmarine *> allies_;
    std::vector<Message> inbox_;

    std::unique_ptr<central_computer::CentralComputer> centralComputer_;
};

}  // namespace fleet

#endif  // FLEET_OOP_COMBAT_SUBMARINE_H
