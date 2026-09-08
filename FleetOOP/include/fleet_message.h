/*
 * fleet_message.h
 *
 * Purpose:
 *   A message sent from one combat submarine to another in the Fleet/OOP
 *   exercise (PROJECT_GUIDE.md Sec 5, Fleet menu operations 8 and 9).
 *   Sec 5 describes it as exactly two things: the content, and a reference
 *   to the sending submarine.
 *
 *   This has nothing to do with the protocol message layer used by the
 *   real-time LNC/CC/GS system (CentralComputer/include/message.h,
 *   GroundStation/include/message.h). Those carry TLV frames over a wire;
 *   this is a plain in-memory object in the OOP exercise. The file is named
 *   fleet_message.h rather than message.h to keep the two clearly apart and
 *   to avoid any include-path ambiguity with the protocol header.
 *
 * Layer:
 *   Fleet/OOP model. In-memory only.
 *
 * Responsibilities:
 *   - Hold the message content and the sender it came from.
 *
 * This file does NOT:
 *   - Store a receiver. A message lives in the receiving submarine's own
 *     inbox, so recording the receiver inside the message too would just
 *     duplicate that relationship.
 *   - Carry a timestamp, id, or delivery state - Sec 5 asks for none of
 *     these, and nothing in the ten menu operations needs them.
 *   - Own the sender. Fleet owns every submarine, and none of the ten menu
 *     operations removes one, so this non-owning pointer cannot dangle.
 */

#ifndef FLEET_OOP_FLEET_MESSAGE_H
#define FLEET_OOP_FLEET_MESSAGE_H

#include <string>

namespace fleet {

/* Only a pointer to CombatSubmarine is stored, so a forward declaration is
   enough here - and it keeps this header free of the Central Computer that
   combat_submarine.h pulls in. */
class CombatSubmarine;

class Message
{
public:
    Message(const CombatSubmarine *sender, const std::string &content);

    const CombatSubmarine *sender() const;
    const std::string &content() const;

private:
    const CombatSubmarine *sender_;
    std::string content_;
};

}  // namespace fleet

#endif  // FLEET_OOP_FLEET_MESSAGE_H
