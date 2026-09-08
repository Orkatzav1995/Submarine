/*
 * fleet_main.cpp
 *
 * Purpose:
 *   The Fleet/OOP exercise's entry point (PROJECT_GUIDE.md Sec 5): a small
 *   console menu offering the ten operations Sec 5 lists, driven entirely
 *   through Fleet's public API.
 *
 *   This is a separate program from cc_main.exe. Sec 11's directory tree
 *   already anticipates exactly that ("cc_main.cpp + fleet_main.cpp"), and
 *   it keeps the real-time Central Computer application and the in-memory
 *   OOP exercise cleanly separable.
 *
 * Layer:
 *   Application entry point. Sits above Fleet and the model classes.
 *
 * Responsibilities:
 *   - Display the menu, read the operator's input, and print results.
 *   - Ask for the details each operation needs and hand them to Fleet.
 *   - Use dynamic_cast for the two operations that are type-specific
 *     (updating a submarine's details, and showing a combat submarine's
 *     received messages), since those need the concrete type.
 *
 * This file does NOT:
 *   - Contain any fleet rules. Whether a serial number is already taken,
 *     whether two submarines may be associated, or whether a message may
 *     be delivered is decided by Fleet - this file only reports what
 *     Fleet returned.
 *   - Touch the Central Computer a combat submarine owns. It is
 *     constructed with the submarine and never started (see
 *     combat_submarine.h).
 */

#include "combat_submarine.h"
#include "fleet.h"
#include "research_submarine.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

/* Every prompt reads a whole line, so text with spaces (submarine names,
   mission descriptions, message content) arrives intact and there is no
   mixing of >> and getline to get wrong. Returns false at end of input,
   which ends the program the same way choosing [10] does. */
bool readLine(const char *prompt, std::string &outLine)
{
    std::printf("%s", prompt);
    std::fflush(stdout);
    return static_cast<bool>(std::getline(std::cin, outLine));
}

int readInt(const char *prompt)
{
    std::string line;
    if (!readLine(prompt, line))
    {
        return 0;
    }
    return std::atoi(line.c_str());
}

void printMenu()
{
    std::printf("\n--- Submarine Fleet ---\n");
    std::printf("[1]  Add a submarine\n");
    std::printf("[2]  Display all submarines\n");
    std::printf("[3]  Search for a submarine by serial number\n");
    std::printf("[4]  Assign a mission to a submarine\n");
    std::printf("[5]  Update a submarine's mission details\n");
    std::printf("[6]  End a submarine's mission\n");
    std::printf("[7]  Associate two combat submarines with the same mission\n");
    std::printf("[8]  Send a message between allied combat submarines\n");
    std::printf("[9]  Display the messages a submarine received\n");
    std::printf("[10] Exit\n");
    std::printf("> ");
    std::fflush(stdout);
}

void handleAddSubmarine(fleet::Fleet &fleet)
{
    std::string type;
    if (!readLine("Type - [1] research, [2] combat: ", type))
    {
        return;
    }
    if (type != "1" && type != "2")
    {
        std::printf("Unknown type - nothing was added.\n");
        return;
    }

    std::string serialNumber;
    std::string name;
    if (!readLine("Serial number: ", serialNumber) || !readLine("Name: ", name))
    {
        return;
    }

    std::unique_ptr<fleet::Submarine> submarine;
    if (type == "1")
    {
        std::string researchTopic;
        if (!readLine("Research topic: ", researchTopic))
        {
            return;
        }
        submarine = std::make_unique<fleet::ResearchSubmarine>(serialNumber, name, researchTopic);
    }
    else
    {
        std::string commanderName;
        if (!readLine("Commander name: ", commanderName))
        {
            return;
        }
        int personnelCount = readInt("Personnel count: ");
        submarine = std::make_unique<fleet::CombatSubmarine>(serialNumber, name, commanderName, personnelCount);
    }

    if (fleet.addSubmarine(std::move(submarine)))
    {
        std::printf("Added %s.\n", serialNumber.c_str());
    }
    else
    {
        std::printf("Could not add it - serial number %s is already in the fleet.\n", serialNumber.c_str());
    }
}

void handleSearch(fleet::Fleet &fleet)
{
    std::string serialNumber;
    if (!readLine("Serial number: ", serialNumber))
    {
        return;
    }

    fleet::Submarine *submarine = fleet.findBySerialNumber(serialNumber);
    if (submarine == nullptr)
    {
        std::printf("No submarine with serial number %s.\n", serialNumber.c_str());
        return;
    }
    submarine->print();
}

void handleAssignMission(fleet::Fleet &fleet)
{
    std::string serialNumber;
    std::string description;
    if (!readLine("Serial number: ", serialNumber) || !readLine("Mission description: ", description))
    {
        return;
    }

    if (fleet.assignMission(serialNumber, description))
    {
        std::printf("Mission assigned to %s.\n", serialNumber.c_str());
    }
    else
    {
        std::printf("Could not assign - unknown serial number, or the submarine is already on a mission.\n");
    }
}

/* Operation 5 is the one place the concrete type matters for input: a
   research submarine and a combat submarine have different details to
   update, so the menu asks the object what it actually is. */
void handleUpdateDetails(fleet::Fleet &fleet)
{
    std::string serialNumber;
    if (!readLine("Serial number: ", serialNumber))
    {
        return;
    }

    fleet::Submarine *submarine = fleet.findBySerialNumber(serialNumber);
    if (submarine == nullptr)
    {
        std::printf("No submarine with serial number %s.\n", serialNumber.c_str());
        return;
    }
    if (!submarine->isAssignedToMission())
    {
        std::printf("%s is not on a mission, so it has no mission details to update.\n", serialNumber.c_str());
        return;
    }

    if (fleet::ResearchSubmarine *research = dynamic_cast<fleet::ResearchSubmarine *>(submarine))
    {
        std::string topic;
        std::string researcher;
        if (!readLine("New research topic: ", topic) || !readLine("Researcher to add (blank for none): ", researcher))
        {
            return;
        }
        research->setResearchTopic(topic);
        if (!researcher.empty())
        {
            research->addResearcher(researcher);
        }
        std::printf("Updated %s.\n", serialNumber.c_str());
        return;
    }

    if (fleet::CombatSubmarine *combat = dynamic_cast<fleet::CombatSubmarine *>(submarine))
    {
        std::string description;
        std::string commanderName;
        if (!readLine("New mission description: ", description) || !readLine("Commander name: ", commanderName))
        {
            return;
        }
        int personnelCount = readInt("Personnel count: ");
        combat->setMissionDetails(description, commanderName, personnelCount);
        std::printf("Updated %s.\n", serialNumber.c_str());
    }
}

void handleEndMission(fleet::Fleet &fleet)
{
    std::string serialNumber;
    if (!readLine("Serial number: ", serialNumber))
    {
        return;
    }

    if (fleet.endMission(serialNumber))
    {
        std::printf("%s is available again.\n", serialNumber.c_str());
    }
    else
    {
        std::printf("Could not end it - unknown serial number, or the submarine is not on a mission.\n");
    }
}

void handleAssociate(fleet::Fleet &fleet)
{
    std::string firstSerialNumber;
    std::string secondSerialNumber;
    if (!readLine("First combat submarine's serial number: ", firstSerialNumber)
        || !readLine("Second combat submarine's serial number: ", secondSerialNumber))
    {
        return;
    }

    if (fleet.associateCombatSubmarines(firstSerialNumber, secondSerialNumber))
    {
        std::printf("%s and %s are now on the same mission.\n", firstSerialNumber.c_str(),
                     secondSerialNumber.c_str());
    }
    else
    {
        std::printf("Could not associate them - both must be different combat submarines that are on a mission "
                     "and not already associated.\n");
    }
}

void handleSendMessage(fleet::Fleet &fleet)
{
    std::string senderSerialNumber;
    std::string receiverSerialNumber;
    std::string content;
    if (!readLine("Sender's serial number: ", senderSerialNumber)
        || !readLine("Receiver's serial number: ", receiverSerialNumber) || !readLine("Message: ", content))
    {
        return;
    }

    if (fleet.sendMessage(senderSerialNumber, receiverSerialNumber, content))
    {
        std::printf("Message delivered to %s.\n", receiverSerialNumber.c_str());
    }
    else
    {
        std::printf("Could not send it - both must be combat submarines associated with the same mission, and "
                     "the message must not be empty.\n");
    }
}

void handleShowMessages(fleet::Fleet &fleet)
{
    std::string serialNumber;
    if (!readLine("Serial number: ", serialNumber))
    {
        return;
    }

    fleet::Submarine *submarine = fleet.findBySerialNumber(serialNumber);
    if (submarine == nullptr)
    {
        std::printf("No submarine with serial number %s.\n", serialNumber.c_str());
        return;
    }

    fleet::CombatSubmarine *combat = dynamic_cast<fleet::CombatSubmarine *>(submarine);
    if (combat == nullptr)
    {
        std::printf("Only combat submarines exchange messages.\n");
        return;
    }

    if (combat->inbox().empty())
    {
        std::printf("%s has received no messages.\n", serialNumber.c_str());
        return;
    }

    std::printf("Messages received by %s:\n", serialNumber.c_str());
    for (const fleet::Message &message : combat->inbox())
    {
        std::printf("  \"%s\" - from %s (%s)\n", message.content().c_str(), message.sender()->name().c_str(),
                     message.sender()->serialNumber().c_str());
    }
}

}  // namespace

int main()
{
    std::printf("=== Submarine Fleet Management ===\n");

    fleet::Fleet fleet;

    bool running = true;
    while (running)
    {
        printMenu();

        std::string choice;
        if (!std::getline(std::cin, choice))
        {
            break;
        }

        if (choice == "1")
        {
            handleAddSubmarine(fleet);
        }
        else if (choice == "2")
        {
            fleet.printAllSubmarines();
        }
        else if (choice == "3")
        {
            handleSearch(fleet);
        }
        else if (choice == "4")
        {
            handleAssignMission(fleet);
        }
        else if (choice == "5")
        {
            handleUpdateDetails(fleet);
        }
        else if (choice == "6")
        {
            handleEndMission(fleet);
        }
        else if (choice == "7")
        {
            handleAssociate(fleet);
        }
        else if (choice == "8")
        {
            handleSendMessage(fleet);
        }
        else if (choice == "9")
        {
            handleShowMessages(fleet);
        }
        else if (choice == "10")
        {
            running = false;
        }
        else
        {
            std::printf("Unknown option.\n");
        }
    }

    std::printf("\nExiting.\n");
    return 0;
}
