/*
 * submarine.h
 *
 * Purpose:
 *   The abstract base of the Fleet/OOP exercise (PROJECT_GUIDE.md Sec 5).
 *   Every submarine in the fleet has a serial number, a name, and a flag
 *   saying whether it is currently assigned to a mission. Concrete types
 *   (ResearchSubmarine, CombatSubmarine) add their own type-specific
 *   details and decide how they are displayed.
 *
 *   Abstract on purpose: Sec 5 describes no "plain" submarine - every
 *   submarine is either a research or a combat one, and Fleet stores them
 *   polymorphically as std::unique_ptr<Submarine>.
 *
 * Layer:
 *   Fleet/OOP model. Completely separate from the real-time LNC/CC/GS
 *   system - in-memory only, no networking, no database, no protocol.
 *
 * Responsibilities:
 *   - Hold the three fields Sec 5 gives every submarine.
 *   - Provide the mission-assignment flag and the two operations that
 *     change it (Fleet menu operations 4 and 6).
 *   - Declare the two virtual functions that make display type-specific.
 *
 * This file does NOT:
 *   - Know about Fleet, Mission, or Message. A submarine stays independent
 *     of the fleet that owns it - Sec 5's locked decoupling decision puts
 *     mission history on Fleet, not on each submarine, so there is
 *     deliberately no back-reference of any kind here.
 *   - Prompt for input or contain menu logic - fleet_main.cpp owns all
 *     console interaction.
 */

#ifndef FLEET_OOP_SUBMARINE_H
#define FLEET_OOP_SUBMARINE_H

#include <string>

namespace fleet {

class Submarine
{
public:
    Submarine(const std::string &serialNumber, const std::string &name);

    /* Fleet deletes submarines through Submarine*, so this must be virtual
       for the derived destructor to run. */
    virtual ~Submarine() = default;

    const std::string &serialNumber() const;
    const std::string &name() const;
    bool isAssignedToMission() const;

    void assignToMission();
    void endMission();

    /* Type-specific display and label, used by Fleet's menu operations 2
       and 3. Pure virtual - each concrete submarine knows what its own
       details are. */
    virtual void print() const = 0;
    virtual std::string typeName() const = 0;

protected:
    std::string serialNumber_;
    std::string name_;
    bool assignedToMission_ = false;
};

}  // namespace fleet

#endif  // FLEET_OOP_SUBMARINE_H
