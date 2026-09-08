/*
 * fleet_message.cpp
 *
 * Purpose:
 *   Implements the Message class declared in fleet_message.h.
 *
 * Layer:
 *   Fleet/OOP model.
 *
 * This file does NOT:
 *   - Include combat_submarine.h. The sender is only stored and handed
 *     back as a pointer, never dereferenced here, so the forward
 *     declaration in the header is all this class needs.
 */

#include "fleet_message.h"

namespace fleet {

Message::Message(const CombatSubmarine *sender, const std::string &content) : sender_(sender), content_(content)
{
}

const CombatSubmarine *Message::sender() const
{
    return sender_;
}

const std::string &Message::content() const
{
    return content_;
}

}  // namespace fleet
