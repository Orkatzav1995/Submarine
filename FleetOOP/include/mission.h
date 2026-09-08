/*
 * mission.h
 *
 * Purpose:
 *   One entry in the fleet's mission history (PROJECT_GUIDE.md Sec 5,
 *   which describes Mission as "description + history" and puts the
 *   history itself on Fleet). A Mission records what the mission was and
 *   which submarine it was assigned to, so the history stays meaningful
 *   when it is displayed.
 *
 * Layer:
 *   Fleet/OOP model. In-memory only.
 *
 * Responsibilities:
 *   - Hold the mission description and the serial number of the submarine
 *     the mission was assigned to.
 *
 * This file does NOT:
 *   - Track participating submarines. Combat submarines already track the
 *     allies on their current mission themselves (see combat_submarine.h),
 *     so recording participants here too would duplicate that state.
 *   - Carry a status, timestamps, or a mission id - Sec 5 asks for none of
 *     these, and none of the ten menu operations needs them.
 *   - Point back at a Submarine or at Fleet. It stores the serial number,
 *     not a reference, which keeps mission history a plain value that
 *     Fleet can own and copy freely (Sec 5's decoupling decision).
 */

#ifndef FLEET_OOP_MISSION_H
#define FLEET_OOP_MISSION_H

#include <string>

namespace fleet {

class Mission
{
public:
    Mission(const std::string &description, const std::string &submarineSerialNumber);

    const std::string &description() const;
    const std::string &submarineSerialNumber() const;

private:
    std::string description_;
    std::string submarineSerialNumber_;
};

}  // namespace fleet

#endif  // FLEET_OOP_MISSION_H
