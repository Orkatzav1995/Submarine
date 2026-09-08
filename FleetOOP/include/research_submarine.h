/*
 * research_submarine.h
 *
 * Purpose:
 *   One of the two concrete submarine types in the Fleet/OOP exercise
 *   (PROJECT_GUIDE.md Sec 5): a research submarine adds a research topic
 *   and the list of researchers on board to the serial number, name, and
 *   mission flag every submarine already has.
 *
 * Layer:
 *   Fleet/OOP model. In-memory only.
 *
 * Responsibilities:
 *   - Hold the two research-specific fields Sec 5 gives this type.
 *   - Allow those fields to be updated (Fleet menu operation 5, which
 *     updates a submarine's mission details "per type").
 *   - Provide the type-specific display Fleet menu operations 2 and 3 use.
 *
 * This file does NOT:
 *   - Own a CentralComputer - Sec 5 gives that only to combat submarines.
 *   - Know about Fleet, Mission, or Message.
 *   - Prompt for input - fleet_main.cpp owns all console interaction; this
 *     class only prints what it already holds.
 */

#ifndef FLEET_OOP_RESEARCH_SUBMARINE_H
#define FLEET_OOP_RESEARCH_SUBMARINE_H

#include "submarine.h"
#include <string>
#include <vector>

namespace fleet {

class ResearchSubmarine : public Submarine
{
public:
    ResearchSubmarine(const std::string &serialNumber, const std::string &name,
                       const std::string &researchTopic);
    ~ResearchSubmarine() override = default;

    const std::string &researchTopic() const;
    const std::vector<std::string> &researchers() const;

    void setResearchTopic(const std::string &researchTopic);
    void addResearcher(const std::string &researcher);

    void print() const override;
    std::string typeName() const override;

private:
    std::string researchTopic_;
    std::vector<std::string> researchers_;
};

}  // namespace fleet

#endif  // FLEET_OOP_RESEARCH_SUBMARINE_H
