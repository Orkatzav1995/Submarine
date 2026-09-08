/*
 * research_submarine.cpp
 *
 * Purpose:
 *   Implements the ResearchSubmarine class declared in research_submarine.h.
 *
 * Layer:
 *   Fleet/OOP model.
 */

#include "research_submarine.h"
#include <cstdio>

namespace fleet {

ResearchSubmarine::ResearchSubmarine(const std::string &serialNumber, const std::string &name,
                                       const std::string &researchTopic)
    : Submarine(serialNumber, name), researchTopic_(researchTopic)
{
}

const std::string &ResearchSubmarine::researchTopic() const
{
    return researchTopic_;
}

const std::vector<std::string> &ResearchSubmarine::researchers() const
{
    return researchers_;
}

void ResearchSubmarine::setResearchTopic(const std::string &researchTopic)
{
    researchTopic_ = researchTopic;
}

void ResearchSubmarine::addResearcher(const std::string &researcher)
{
    researchers_.push_back(researcher);
}

void ResearchSubmarine::print() const
{
    std::printf("[%s] %s (%s) - %s\n", typeName().c_str(), name_.c_str(), serialNumber_.c_str(),
                 assignedToMission_ ? "assigned to a mission" : "available");
    std::printf("  Research topic: %s\n", researchTopic_.c_str());

    if (researchers_.empty())
    {
        std::printf("  Researchers: none\n");
    }
    else
    {
        std::printf("  Researchers:\n");
        for (const std::string &researcher : researchers_)
        {
            std::printf("    - %s\n", researcher.c_str());
        }
    }
}

std::string ResearchSubmarine::typeName() const
{
    return "Research";
}

}  // namespace fleet
