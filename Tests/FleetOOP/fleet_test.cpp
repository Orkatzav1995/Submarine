/*
 * fleet_test.cpp
 *
 * Purpose:
 *   Stand-alone PC test program for the Fleet/OOP classes
 *   (PROJECT_GUIDE.md Sec 5). One test file for the whole Fleet/OOP layer,
 *   grown one section at a time as each class is implemented, so every
 *   implementation step automatically re-runs all previously-added checks.
 *
 *   Uses the same plain check()-based style as every other test in this
 *   project (see Tests/CentralComputer/central_computer_test.cpp) - no test
 *   library, no test framework.
 *
 * How to build and run (from this folder) - the object list grows as each
 * Fleet/OOP class is added:
 *   (CombatSubmarine owns a CentralComputer, so from this step on the test
 *   also links the Central Computer objects and their dependencies. None of
 *   them ever run - the Central Computer is constructed but never started.)
 *
 *   gcc -std=c11 -c "../../Common/ThirdParty/sqlite3/sqlite3.c" -o sqlite3.o
 *   g++ -std=c++17 -I "../../FleetOOP/include" -I "../../CentralComputer/include" -I "../../Common/TLVCodec" -I "../../Common/Protocol" -I "../../Common/ThirdParty/sqlite3" -c fleet_test.cpp -o fleet_test.o
 *   g++ ... (same include flags) -c "../../FleetOOP/src/submarine.cpp"          -o submarine.o
 *   g++ ... (same include flags) -c "../../FleetOOP/src/research_submarine.cpp" -o research_submarine.o
 *   g++ ... (same include flags) -c "../../FleetOOP/src/combat_submarine.cpp"   -o combat_submarine.o
 *   g++ ... (same include flags) -c "../../FleetOOP/src/fleet_message.cpp"      -o fleet_message.o
 *   g++ ... (same include flags) -c "../../FleetOOP/src/mission.cpp"            -o mission.o
 *   g++ ... (same include flags) -c "../../FleetOOP/src/fleet.cpp"              -o fleet.o
 *   g++ ... (same include flags) -c "../../CentralComputer/src/central_computer.cpp"  -o central_computer.o
 *   g++ ... (same include flags) -c "../../CentralComputer/src/communication.cpp"     -o communication.o
 *   g++ ... (same include flags) -c "../../CentralComputer/src/data_collection.cpp"   -o data_collection.o
 *   g++ ... (same include flags) -c "../../CentralComputer/src/data_store.cpp"        -o data_store.o
 *   g++ ... (same include flags) -c "../../CentralComputer/src/management_command.cpp" -o management_command.o
 *   g++ ... (same include flags) -c "../../CentralComputer/src/gs_communication.cpp"  -o gs_communication.o
 *   g++ ... (same include flags) -c "../../CentralComputer/src/message.cpp"           -o message.o
 *   g++ ... (same include flags) -c "../../CentralComputer/src/serial_transport.cpp"  -o serial_transport.o
 *   g++ ... (same include flags) -c "../../CentralComputer/src/tcp_transport.cpp"     -o tcp_transport.o
 *   g++ -std=c++17 -I "../../Common/TLVCodec" -I "../../Common/Protocol" -c "../../Common/TLVCodec/tlv_codec.cpp" -o tlv_codec.o
 *   g++ fleet_test.o submarine.o research_submarine.o combat_submarine.o fleet_message.o mission.o fleet.o central_computer.o communication.o data_collection.o data_store.o management_command.o gs_communication.o message.o serial_transport.o tcp_transport.o tlv_codec.o sqlite3.o -lws2_32 -o fleet_test.exe
 *   ./fleet_test.exe
 *
 * This file does NOT:
 *   - Touch the network, a database, a file, or any hardware. The Fleet/OOP
 *     layer is in-memory only.
 */

#include "combat_submarine.h"
#include "fleet.h"
#include "fleet_message.h"
#include "mission.h"
#include "research_submarine.h"
#include "submarine.h"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

static int g_testsRun = 0;
static int g_testsFailed = 0;

static void check(bool condition, const char *description)
{
    g_testsRun++;
    if (condition)
    {
        std::printf("  PASS: %s\n", description);
    }
    else
    {
        std::printf("  FAIL: %s\n", description);
        g_testsFailed++;
    }
}

namespace {

/* Submarine is abstract by design, so its own tests need the smallest
   possible concrete subclass. The real subclasses (ResearchSubmarine,
   CombatSubmarine) arrive in later steps and are tested there. */
class TestSubmarine : public fleet::Submarine
{
public:
    TestSubmarine(const std::string &serialNumber, const std::string &name)
        : fleet::Submarine(serialNumber, name)
    {
    }

    void print() const override
    {
    }

    std::string typeName() const override
    {
        return "Test";
    }
};

}  // namespace

static void testSubmarineStoresSerialNumberAndName()
{
    std::printf("\nTest: Submarine stores its serial number and name\n");

    TestSubmarine submarine("SN-001", "Nautilus");

    check(submarine.serialNumber() == "SN-001", "serialNumber() returns the value given to the constructor");
    check(submarine.name() == "Nautilus", "name() returns the value given to the constructor");
}

static void testNewSubmarineIsNotAssignedToAMission()
{
    std::printf("\nTest: a new Submarine starts unassigned\n");

    TestSubmarine submarine("SN-002", "Triton");

    check(!submarine.isAssignedToMission(), "isAssignedToMission() is false before any mission is assigned");
}

static void testAssignAndEndMissionFlipTheFlag()
{
    std::printf("\nTest: assignToMission() / endMission() flip the assignment flag\n");

    TestSubmarine submarine("SN-003", "Poseidon");

    submarine.assignToMission();
    check(submarine.isAssignedToMission(), "isAssignedToMission() is true after assignToMission()");

    submarine.endMission();
    check(!submarine.isAssignedToMission(), "isAssignedToMission() is false again after endMission()");
}

static void testSubmarineIsUsableThroughABasePointer()
{
    std::printf("\nTest: a Submarine works through a base-class pointer\n");

    std::unique_ptr<fleet::Submarine> submarine = std::make_unique<TestSubmarine>("SN-004", "Argo");

    check(submarine->typeName() == "Test", "the derived typeName() is called through Submarine*");
    check(submarine->serialNumber() == "SN-004", "base-class data is reachable through Submarine*");
}

static void testResearchSubmarineStoresItsOwnDetails()
{
    std::printf("\nTest: ResearchSubmarine stores its research topic\n");

    fleet::ResearchSubmarine submarine("SN-101", "Explorer", "Deep-sea currents");

    check(submarine.researchTopic() == "Deep-sea currents", "researchTopic() returns the constructor value");
    check(submarine.researchers().empty(), "a new research submarine has no researchers yet");
}

static void testResearchSubmarineKeepsTheBaseClassBehavior()
{
    std::printf("\nTest: ResearchSubmarine still behaves like a Submarine\n");

    fleet::ResearchSubmarine submarine("SN-102", "Seeker", "Coral mapping");

    check(submarine.serialNumber() == "SN-102", "serialNumber() comes from the base class");
    check(submarine.name() == "Seeker", "name() comes from the base class");
    check(!submarine.isAssignedToMission(), "a new research submarine starts unassigned");

    submarine.assignToMission();
    check(submarine.isAssignedToMission(), "assignToMission() works on the derived object");
}

static void testResearchSubmarineDetailsCanBeUpdated()
{
    std::printf("\nTest: research details can be updated (Fleet menu operation 5)\n");

    fleet::ResearchSubmarine submarine("SN-103", "Pioneer", "Old topic");

    submarine.setResearchTopic("Thermal vents");
    check(submarine.researchTopic() == "Thermal vents", "setResearchTopic() replaces the topic");

    submarine.addResearcher("Dr. Levi");
    submarine.addResearcher("Dr. Cohen");
    check(submarine.researchers().size() == 2, "addResearcher() appends each researcher");
    check(submarine.researchers()[0] == "Dr. Levi", "the first researcher is stored in order");
    check(submarine.researchers()[1] == "Dr. Cohen", "the second researcher is stored in order");
}

static void testResearchSubmarineReportsItsType()
{
    std::printf("\nTest: ResearchSubmarine reports its type through a base pointer\n");

    std::unique_ptr<fleet::Submarine> submarine =
        std::make_unique<fleet::ResearchSubmarine>("SN-104", "Voyager", "Salinity");

    check(submarine->typeName() == "Research", "typeName() returns \"Research\" through Submarine*");
}

static void testCombatSubmarineStoresItsOwnDetails()
{
    std::printf("\nTest: CombatSubmarine stores its combat details\n");

    fleet::CombatSubmarine submarine("SN-201", "Barracuda", "Cmdr. Avi", 45);

    check(submarine.commanderName() == "Cmdr. Avi", "commanderName() returns the constructor value");
    check(submarine.personnelCount() == 45, "personnelCount() returns the constructor value");
    check(submarine.missionDescription().empty(), "a new combat submarine has no mission description yet");
}

static void testCombatSubmarineKeepsTheBaseClassBehavior()
{
    std::printf("\nTest: CombatSubmarine still behaves like a Submarine\n");

    fleet::CombatSubmarine submarine("SN-202", "Marlin", "Cmdr. Dana", 38);

    check(submarine.serialNumber() == "SN-202", "serialNumber() comes from the base class");
    check(submarine.name() == "Marlin", "name() comes from the base class");
    check(!submarine.isAssignedToMission(), "a new combat submarine starts unassigned");

    submarine.assignToMission();
    check(submarine.isAssignedToMission(), "assignToMission() works on the derived object");
}

static void testCombatSubmarineDetailsCanBeUpdated()
{
    std::printf("\nTest: combat mission details can be updated (Fleet menu operation 5)\n");

    fleet::CombatSubmarine submarine("SN-203", "Orca", "Cmdr. Noa", 40);

    submarine.setMissionDetails("Patrol the northern corridor", "Cmdr. Yael", 52);

    check(submarine.missionDescription() == "Patrol the northern corridor", "setMissionDetails() sets the mission");
    check(submarine.commanderName() == "Cmdr. Yael", "setMissionDetails() replaces the commander");
    check(submarine.personnelCount() == 52, "setMissionDetails() replaces the personnel count");
}

static void testCombatSubmarineTracksAllies()
{
    std::printf("\nTest: allied combat submarines are tracked (Fleet menu operation 7)\n");

    fleet::CombatSubmarine first("SN-204", "Shark", "Cmdr. Ron", 30);
    fleet::CombatSubmarine second("SN-205", "Ray", "Cmdr. Gil", 33);

    check(first.allies().empty(), "a new combat submarine has no allies");
    check(!first.isAllyOf(&second), "isAllyOf() is false before they are associated");

    first.addAlly(&second);
    check(first.allies().size() == 1, "addAlly() records the ally");
    check(first.isAllyOf(&second), "isAllyOf() is true after association");

    first.addAlly(&second);
    check(first.allies().size() == 1, "adding the same ally twice does not duplicate it");

    first.addAlly(&first);
    check(first.allies().size() == 1, "a submarine cannot become its own ally");

    first.clearAllies();
    check(first.allies().empty(), "clearAllies() empties the list (used when a mission ends)");
}

static void testCombatSubmarineOwnsAnInactiveCentralComputer()
{
    std::printf("\nTest: CombatSubmarine owns a CentralComputer that is never started\n");

    fleet::CombatSubmarine submarine("SN-206", "Hammerhead", "Cmdr. Tal", 47);

    central_computer::CentralComputer *centralComputer = submarine.centralComputer();

    check(centralComputer != nullptr, "the CentralComputer exists after construction");
    check(!centralComputer->dataStore().isOpen(), "its database was never opened - it was not started");
    check(!centralComputer->communication().isOpen(), "its serial port was never opened - it was not started");
    check(!centralComputer->gsCommunication().isListening(),
          "its Ground Station listener was never started");
}

static void testCombatSubmarineReportsItsType()
{
    std::printf("\nTest: CombatSubmarine reports its type through a base pointer\n");

    std::unique_ptr<fleet::Submarine> submarine =
        std::make_unique<fleet::CombatSubmarine>("SN-207", "Tigershark", "Cmdr. Omer", 41);

    check(submarine->typeName() == "Combat", "typeName() returns \"Combat\" through Submarine*");
}

static void testMessageStoresContentAndSender()
{
    std::printf("\nTest: Message stores its content and sender\n");

    fleet::CombatSubmarine sender("SN-301", "Kraken", "Cmdr. Ido", 44);
    fleet::Message message(&sender, "Enemy contact at grid 7");

    check(message.content() == "Enemy contact at grid 7", "content() returns the constructor value");
    check(message.sender() == &sender, "sender() points at the sending submarine");
}

static void testCombatSubmarineStartsWithAnEmptyInbox()
{
    std::printf("\nTest: a new CombatSubmarine has no messages\n");

    fleet::CombatSubmarine submarine("SN-302", "Leviathan", "Cmdr. Maya", 39);

    check(submarine.inbox().empty(), "inbox() is empty before any message is received");
}

static void testReceivedMessagesAreStoredInOrder()
{
    std::printf("\nTest: received messages are stored on the receiver (Fleet menu operations 8 and 9)\n");

    fleet::CombatSubmarine sender("SN-303", "Nemesis", "Cmdr. Roi", 42);
    fleet::CombatSubmarine receiver("SN-304", "Sentinel", "Cmdr. Lior", 36);

    receiver.receiveMessage(fleet::Message(&sender, "First report"));
    receiver.receiveMessage(fleet::Message(&sender, "Second report"));

    check(receiver.inbox().size() == 2, "both messages are stored");
    check(receiver.inbox()[0].content() == "First report", "the first message is kept in order");
    check(receiver.inbox()[1].content() == "Second report", "the second message is kept in order");
    check(receiver.inbox()[0].sender() == &sender, "a received message still identifies its sender");
    check(sender.inbox().empty(), "the sender's own inbox is untouched");
}

static void testMessageSenderDetailsAreReachableForDisplay()
{
    std::printf("\nTest: a received message can show content and sender (Fleet menu operation 9)\n");

    fleet::CombatSubmarine sender("SN-305", "Vanguard", "Cmdr. Eden", 48);
    fleet::CombatSubmarine receiver("SN-306", "Guardian", "Cmdr. Adi", 37);

    receiver.receiveMessage(fleet::Message(&sender, "Holding position"));

    const fleet::Message &received = receiver.inbox()[0];
    check(received.sender()->name() == "Vanguard", "the sender's name is reachable from the message");
    check(received.sender()->serialNumber() == "SN-305", "the sender's serial number is reachable");
}

static void testMissionStoresDescriptionAndSubmarineSerialNumber()
{
    std::printf("\nTest: Mission stores its description and the submarine it was assigned to\n");

    fleet::Mission mission("Survey the trench", "SN-401");

    check(mission.description() == "Survey the trench", "description() returns the constructor value");
    check(mission.submarineSerialNumber() == "SN-401", "submarineSerialNumber() returns the constructor value");
}

static void testMissionsCanBeStoredAsHistoryEntries()
{
    std::printf("\nTest: missions are plain values Fleet can keep as history\n");

    std::vector<fleet::Mission> history;
    history.push_back(fleet::Mission("Escort convoy", "SN-402"));
    history.push_back(fleet::Mission("Map the ridge", "SN-403"));

    check(history.size() == 2, "both missions are stored");
    check(history[0].description() == "Escort convoy", "the first history entry keeps its description");
    check(history[0].submarineSerialNumber() == "SN-402", "the first history entry keeps its submarine serial");
    check(history[1].description() == "Map the ridge", "the second history entry keeps its description");
    check(history[1].submarineSerialNumber() == "SN-403", "the second history entry keeps its submarine serial");
}

/* Small helpers so the Fleet tests below read as one behavior each. */
static std::unique_ptr<fleet::Submarine> makeCombat(const std::string &serialNumber, const std::string &name)
{
    return std::make_unique<fleet::CombatSubmarine>(serialNumber, name, "Cmdr. Test", 40);
}

static std::unique_ptr<fleet::Submarine> makeResearch(const std::string &serialNumber, const std::string &name)
{
    return std::make_unique<fleet::ResearchSubmarine>(serialNumber, name, "Test topic");
}

static void testFleetAddsAndFindsSubmarines()
{
    std::printf("\nTest: Fleet stores submarines and finds them by serial number (operations 1 and 3)\n");

    fleet::Fleet fleet;

    check(fleet.submarineCount() == 0, "a new fleet has no submarines");
    check(fleet.addSubmarine(makeCombat("SN-501", "Falcon")), "addSubmarine() accepts a new submarine");
    check(fleet.addSubmarine(makeResearch("SN-502", "Coral")), "addSubmarine() accepts a second submarine");
    check(fleet.submarineCount() == 2, "both submarines are stored");

    fleet::Submarine *found = fleet.findBySerialNumber("SN-502");
    check(found != nullptr, "findBySerialNumber() finds an existing submarine");
    check(found->name() == "Coral", "findBySerialNumber() returns the right submarine");
}

static void testFleetRejectsDuplicateSerialNumbers()
{
    std::printf("\nTest: Fleet rejects a duplicate serial number (operation 1 validation)\n");

    fleet::Fleet fleet;
    fleet.addSubmarine(makeCombat("SN-503", "Original"));

    check(!fleet.addSubmarine(makeCombat("SN-503", "Duplicate")), "addSubmarine() refuses a serial already in use");
    check(fleet.submarineCount() == 1, "the fleet still holds only the original submarine");
    check(fleet.findBySerialNumber("SN-503")->name() == "Original", "the original submarine was not replaced");
}

static void testFleetHandlesAnUnknownSerialNumber()
{
    std::printf("\nTest: Fleet handles an unknown serial number safely (operation 3)\n");

    fleet::Fleet fleet;
    fleet.addSubmarine(makeCombat("SN-504", "Known"));

    check(fleet.findBySerialNumber("SN-999") == nullptr, "findBySerialNumber() returns nullptr when not found");
    check(!fleet.assignMission("SN-999", "Ghost mission"), "assignMission() refuses an unknown serial");
    check(!fleet.endMission("SN-999"), "endMission() refuses an unknown serial");
    check(fleet.missionHistory().empty(), "no history entry is created for a failed assignment");
}

static void testFleetAssignsMissionsAndRecordsHistory()
{
    std::printf("\nTest: assigning a mission updates the submarine and the fleet history (operation 4)\n");

    fleet::Fleet fleet;
    fleet.addSubmarine(makeCombat("SN-505", "Hunter"));
    fleet.addSubmarine(makeResearch("SN-506", "Scholar"));

    check(fleet.assignMission("SN-505", "Patrol the strait"), "assignMission() succeeds for an available submarine");
    check(fleet.findBySerialNumber("SN-505")->isAssignedToMission(), "the submarine is now marked as assigned");
    check(fleet.missionHistory().size() == 1, "one mission was recorded in the fleet history");
    check(fleet.missionHistory()[0].description() == "Patrol the strait", "the history entry keeps the description");
    check(fleet.missionHistory()[0].submarineSerialNumber() == "SN-505",
          "the history entry records which submarine was assigned");

    fleet::CombatSubmarine *combat = dynamic_cast<fleet::CombatSubmarine *>(fleet.findBySerialNumber("SN-505"));
    check(combat != nullptr && combat->missionDescription() == "Patrol the strait",
          "a combat submarine also stores the mission description itself");

    check(!fleet.assignMission("SN-505", "Second mission"), "a submarine already on a mission cannot be reassigned");
    check(fleet.missionHistory().size() == 1, "the refused assignment added no history entry");

    check(fleet.assignMission("SN-506", "Survey the reef"), "a research submarine can also be assigned");
    check(fleet.missionHistory().size() == 2, "the fleet history now holds both missions");
}

static void testFleetEndsMissions()
{
    std::printf("\nTest: ending a mission frees the submarine but keeps the history (operation 6)\n");

    fleet::Fleet fleet;
    fleet.addSubmarine(makeCombat("SN-507", "Ranger"));
    fleet.assignMission("SN-507", "Escort duty");

    check(!fleet.endMission("SN-508"), "endMission() refuses an unknown serial");
    check(fleet.endMission("SN-507"), "endMission() succeeds for an assigned submarine");
    check(!fleet.findBySerialNumber("SN-507")->isAssignedToMission(), "the submarine is available again");
    check(fleet.missionHistory().size() == 1, "the completed mission stays in the fleet history");
    check(!fleet.endMission("SN-507"), "endMission() refuses a submarine that is not on a mission");
}

static void testFleetAssociatesCombatSubmarines()
{
    std::printf("\nTest: combat submarines can be associated with the same mission (operation 7)\n");

    fleet::Fleet fleet;
    fleet.addSubmarine(makeCombat("SN-510", "Alpha"));
    fleet.addSubmarine(makeCombat("SN-511", "Bravo"));
    fleet.addSubmarine(makeResearch("SN-512", "Scholar"));
    fleet.assignMission("SN-510", "Joint patrol");
    fleet.assignMission("SN-511", "Joint patrol");
    fleet.assignMission("SN-512", "Reef survey");

    check(!fleet.associateCombatSubmarines("SN-510", "SN-510"), "a submarine cannot be associated with itself");
    check(!fleet.associateCombatSubmarines("SN-510", "SN-999"), "an unknown serial is refused");
    check(!fleet.associateCombatSubmarines("SN-510", "SN-512"), "a research submarine is refused");

    check(fleet.associateCombatSubmarines("SN-510", "SN-511"), "two assigned combat submarines can be associated");

    fleet::CombatSubmarine *alpha = dynamic_cast<fleet::CombatSubmarine *>(fleet.findBySerialNumber("SN-510"));
    fleet::CombatSubmarine *bravo = dynamic_cast<fleet::CombatSubmarine *>(fleet.findBySerialNumber("SN-511"));
    check(alpha->isAllyOf(bravo), "the first submarine records the second as an ally");
    check(bravo->isAllyOf(alpha), "the association is recorded on both sides");

    check(!fleet.associateCombatSubmarines("SN-510", "SN-511"), "an existing association is not repeated");
}

static void testFleetRefusesAssociationWithoutAMission()
{
    std::printf("\nTest: combat submarines must be on a mission to be associated (operation 7 validation)\n");

    fleet::Fleet fleet;
    fleet.addSubmarine(makeCombat("SN-513", "Idle One"));
    fleet.addSubmarine(makeCombat("SN-514", "Idle Two"));

    check(!fleet.associateCombatSubmarines("SN-513", "SN-514"), "unassigned combat submarines cannot be associated");

    fleet.assignMission("SN-513", "Solo patrol");
    check(!fleet.associateCombatSubmarines("SN-513", "SN-514"), "one assigned and one idle is still refused");
}

static void testEndingAMissionClearsAllies()
{
    std::printf("\nTest: ending a mission clears that submarine's allies (operation 6)\n");

    fleet::Fleet fleet;
    fleet.addSubmarine(makeCombat("SN-515", "Echo"));
    fleet.addSubmarine(makeCombat("SN-516", "Foxtrot"));
    fleet.assignMission("SN-515", "Shared mission");
    fleet.assignMission("SN-516", "Shared mission");
    fleet.associateCombatSubmarines("SN-515", "SN-516");

    fleet.endMission("SN-515");

    fleet::CombatSubmarine *echo = dynamic_cast<fleet::CombatSubmarine *>(fleet.findBySerialNumber("SN-515"));
    check(echo->allies().empty(), "the submarine whose mission ended has no allies left");
}

static void testFleetDeliversMessagesBetweenAllies()
{
    std::printf("\nTest: messages are delivered only between allied combat submarines (operation 8)\n");

    fleet::Fleet fleet;
    fleet.addSubmarine(makeCombat("SN-520", "Sender"));
    fleet.addSubmarine(makeCombat("SN-521", "Receiver"));
    fleet.addSubmarine(makeCombat("SN-522", "Outsider"));
    fleet.addSubmarine(makeResearch("SN-523", "Scholar"));
    fleet.assignMission("SN-520", "Joint mission");
    fleet.assignMission("SN-521", "Joint mission");
    fleet.assignMission("SN-522", "Other mission");

    check(!fleet.sendMessage("SN-520", "SN-521", ""), "an empty message is refused");
    check(!fleet.sendMessage("SN-520", "SN-522", "Hello"), "a submarine on another mission is refused");
    check(!fleet.sendMessage("SN-520", "SN-523", "Hello"), "a research submarine cannot receive a message");
    check(!fleet.sendMessage("SN-999", "SN-521", "Hello"), "an unknown sender is refused");

    fleet.associateCombatSubmarines("SN-520", "SN-521");
    check(fleet.sendMessage("SN-520", "SN-521", "Position confirmed"), "an allied submarine receives the message");

    fleet::CombatSubmarine *receiver = dynamic_cast<fleet::CombatSubmarine *>(fleet.findBySerialNumber("SN-521"));
    fleet::CombatSubmarine *sender = dynamic_cast<fleet::CombatSubmarine *>(fleet.findBySerialNumber("SN-520"));
    check(receiver->inbox().size() == 1, "the message is stored in the receiver's inbox (operation 9)");
    check(receiver->inbox()[0].content() == "Position confirmed", "the message content is preserved");
    check(receiver->inbox()[0].sender() == sender, "the message identifies its sender");
    check(sender->inbox().empty(), "the sender's own inbox stays empty");
}

static void testFleetHoldsBothSubmarineTypesPolymorphically()
{
    std::printf("\nTest: one Fleet holds both submarine types and dispatches virtually\n");

    fleet::Fleet fleet;
    fleet.addSubmarine(makeCombat("SN-530", "Warship"));
    fleet.addSubmarine(makeResearch("SN-531", "Lab"));

    check(fleet.findBySerialNumber("SN-530")->typeName() == "Combat",
          "the combat submarine reports its own type through Submarine*");
    check(fleet.findBySerialNumber("SN-531")->typeName() == "Research",
          "the research submarine reports its own type through Submarine*");
}

int main()
{
    testSubmarineStoresSerialNumberAndName();
    testNewSubmarineIsNotAssignedToAMission();
    testAssignAndEndMissionFlipTheFlag();
    testSubmarineIsUsableThroughABasePointer();

    testResearchSubmarineStoresItsOwnDetails();
    testResearchSubmarineKeepsTheBaseClassBehavior();
    testResearchSubmarineDetailsCanBeUpdated();
    testResearchSubmarineReportsItsType();

    testCombatSubmarineStoresItsOwnDetails();
    testCombatSubmarineKeepsTheBaseClassBehavior();
    testCombatSubmarineDetailsCanBeUpdated();
    testCombatSubmarineTracksAllies();
    testCombatSubmarineOwnsAnInactiveCentralComputer();
    testCombatSubmarineReportsItsType();

    testMessageStoresContentAndSender();
    testCombatSubmarineStartsWithAnEmptyInbox();
    testReceivedMessagesAreStoredInOrder();
    testMessageSenderDetailsAreReachableForDisplay();

    testMissionStoresDescriptionAndSubmarineSerialNumber();
    testMissionsCanBeStoredAsHistoryEntries();

    testFleetAddsAndFindsSubmarines();
    testFleetRejectsDuplicateSerialNumbers();
    testFleetHandlesAnUnknownSerialNumber();
    testFleetAssignsMissionsAndRecordsHistory();
    testFleetEndsMissions();
    testFleetAssociatesCombatSubmarines();
    testFleetRefusesAssociationWithoutAMission();
    testEndingAMissionClearsAllies();
    testFleetDeliversMessagesBetweenAllies();
    testFleetHoldsBothSubmarineTypesPolymorphically();

    std::printf("\n=== Summary: %d checks run, %d failed ===\n", g_testsRun, g_testsFailed);
    return g_testsFailed == 0 ? 0 : 1;
}
