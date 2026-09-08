/*
 * fleet.h
 *
 * Purpose:
 *   The Fleet/OOP exercise's top-level object (PROJECT_GUIDE.md Sec 5):
 *   it manages the submarine collection, owns the global mission-history
 *   list, and implements the logic behind menu operations 1-9. Operation
 *   10 is "exit", which belongs to the menu loop in fleet_main.cpp.
 *
 *   Fleet owns the mission history so individual submarines stay
 *   independent of the Fleet object and need no back-reference to it -
 *   Sec 5's locked decoupling decision.
 *
 * Layer:
 *   Fleet/OOP model. In-memory only.
 *
 * Responsibilities:
 *   - Own every submarine, stored as std::unique_ptr<Submarine> so one
 *     container can hold both concrete types and delete them correctly
 *     through the base pointer.
 *   - Own the mission history, one Mission per assignment.
 *   - Enforce the rules each menu operation needs: serial numbers are
 *     unique, an unknown serial number is reported rather than assumed,
 *     a submarine cannot be assigned twice, and the combat-only
 *     operations refuse research submarines.
 *
 * This file does NOT:
 *   - Prompt for input or read the keyboard. Every function here takes
 *     plain values and reports success with a bool; fleet_main.cpp owns
 *     all console interaction, including the type-specific prompting for
 *     operation 5 and the inbox display for operation 9.
 *   - Touch the Central Computer. A combat submarine owns one, but Fleet
 *     never starts, stops, or drives it.
 */

#ifndef FLEET_OOP_FLEET_H
#define FLEET_OOP_FLEET_H

#include "mission.h"
#include "submarine.h"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace fleet {

class Fleet
{
public:
    /* Operation 1. Refuses a submarine whose serial number is already in
       the fleet, so serial numbers stay unique and lookups stay
       unambiguous. Returns false and keeps the fleet unchanged in that
       case (the caller's submarine is then simply destroyed). */
    bool addSubmarine(std::unique_ptr<Submarine> submarine);

    /* Operation 2. */
    void printAllSubmarines() const;

    /* Operation 3, and the lookup every other operation below uses.
       Returns nullptr when no submarine has that serial number. */
    Submarine *findBySerialNumber(const std::string &serialNumber);

    /* Operation 4. Marks the submarine as assigned, records the mission in
       the fleet's history, and - for a combat submarine - stores the
       description on the submarine itself. Fails if the serial number is
       unknown or the submarine is already on a mission. */
    bool assignMission(const std::string &serialNumber, const std::string &description);

    /* Operation 6. Marks the submarine available again and, for a combat
       submarine, clears the allies it had on that mission. The mission
       stays in the fleet's history. Fails if the serial number is unknown
       or the submarine is not currently assigned. */
    bool endMission(const std::string &serialNumber);

    /* Operation 7. Records each combat submarine as the other's ally, so
       the relationship is the same seen from either side. Fails if either
       serial number is unknown, either submarine is not a combat
       submarine, they are the same submarine, either is not on a mission,
       or they are already allies. */
    bool associateCombatSubmarines(const std::string &firstSerialNumber, const std::string &secondSerialNumber);

    /* Operation 8. Delivers a message into the receiver's inbox. Fails if
       either serial number is unknown, either submarine is not a combat
       submarine, the two are not allies on the same mission, or the
       content is empty. */
    bool sendMessage(const std::string &senderSerialNumber, const std::string &receiverSerialNumber,
                      const std::string &content);

    const std::vector<Mission> &missionHistory() const;
    std::size_t submarineCount() const;

private:
    std::vector<std::unique_ptr<Submarine>> submarines_;
    std::vector<Mission> missionHistory_;
};

}  // namespace fleet

#endif  // FLEET_OOP_FLEET_H
