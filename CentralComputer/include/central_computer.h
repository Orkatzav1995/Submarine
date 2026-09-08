/*
 * central_computer.h
 *
 * Purpose:
 *   The facade Sec 5's "Decoupling rule" requires: a single class that owns
 *   and wires together every Central Computer module (Communication,
 *   DataStore, ManagementCommand, DataCollection, GsCommunication), so that
 *   cc_main.cpp doesn't have to construct and wire all 5 pieces itself, and
 *   so that a later, separate module (FleetOOP) can hold this one class by
 *   pointer instead of including any of the 5 headers it wraps directly.
 *
 *   Per Sec 5: "CentralComputer's constructor is inert; all I/O happens in
 *   start()/stop(), which FleetOOP never calls." That is exactly what this
 *   class does - the constructor only wires the 5 members to each other
 *   (no ports, no files, no sockets touched); start()/stop() are where the
 *   database is opened, the Ground Station listener starts, and the serial
 *   port opens.
 *
 * Layer:
 *   Application entry-point-adjacent. Sits above Communication, DataStore,
 *   ManagementCommand, DataCollection, and GsCommunication; owns all 5.
 *
 * Responsibilities:
 *   - Own the 5 modules in the order their references require
 *     (Communication and DataStore first, since ManagementCommand,
 *     DataCollection, and GsCommunication each hold a reference to one or
 *     both of them - same construction-order convention already used in
 *     cc_main.cpp and Tests/DataCollection/data_collection_test.cpp).
 *   - start(): open the database (the one fatal condition - returns false
 *     if this fails), start listening for a Ground Station connection, and
 *     open the serial port to the LNC - same order and same fatal/
 *     non-fatal split cc_main.cpp already used before this class existed.
 *   - stop(): close the serial port, the Ground Station listener, and the
 *     database, in that order.
 *   - poll(): advance both the LNC and Ground Station connections by one
 *     step. Call this repeatedly from the caller's main loop.
 *   - Expose each of the 5 modules by reference, so a caller can register
 *     callbacks, issue commands, and read status exactly as it could when
 *     it held all 5 objects itself.
 *
 * This file does NOT:
 *   - Print anything to the console, or decide what counts as a warning vs.
 *     a fatal error for display purposes - that is cc_main.cpp's job (see
 *     its own header comment: "every module below this one is deliberately
 *     silent").
 *   - Add any new business logic - it only calls the already-tested public
 *     functions of the 5 modules it owns, in the same order and with the
 *     same parameters cc_main.cpp used before this class existed.
 *   - Get start()ed/stop()ed/poll()ed by FleetOOP - per Sec 5, FleetOOP
 *     only ever constructs a CentralComputer, never starts it.
 */

#ifndef CENTRAL_COMPUTER_CENTRAL_COMPUTER_H
#define CENTRAL_COMPUTER_CENTRAL_COMPUTER_H

#include "communication.h"
#include "data_collection.h"
#include "data_store.h"
#include "gs_communication.h"
#include "management_command.h"
#include <cstdint>
#include <string>

namespace central_computer {

class CentralComputer
{
public:
    /* Inert: default-constructs Communication/DataStore and wires
       ManagementCommand/DataCollection/GsCommunication to reference them.
       No I/O happens here - see start()/stop() below.

       No copy-control is declared: dataCollection_ registers callbacks on
       "this" in its own constructor (see data_collection.h), which already
       deletes DataCollection's copy-ctor/assignment - that implicitly
       deletes CentralComputer's too, so nothing further is needed here. */
    CentralComputer();

    /*
     * Opens the database at dbPath (the only fatal condition - returns
     * false if this fails), then starts listening for a Ground Station on
     * gsPort and opens portName at baudRate (both non-fatal, matching
     * cc_main.cpp's pre-existing behavior). Prints nothing - check
     * dataStore().isOpen() / communication().isOpen() /
     * gsCommunication().isListening() afterward for the outcome of each.
     */
    bool start(const std::string &portName, uint32_t baudRate, const std::string &dbPath, uint16_t gsPort);

    /* Closes the serial port, the Ground Station listener, and the
       database, in that order. */
    void stop();

    /* Advances both connections by one step. Call repeatedly from the
       caller's main loop. */
    void poll();

    communication::Communication &communication();
    data_store::DataStore &dataStore();
    management_command::ManagementCommand &managementCommand();
    data_collection::DataCollection &dataCollection();
    gs_communication::GsCommunication &gsCommunication();

private:
    communication::Communication communication_;
    data_store::DataStore dataStore_;
    management_command::ManagementCommand managementCommand_;
    data_collection::DataCollection dataCollection_;
    gs_communication::GsCommunication gsCommunication_;
};

}  // namespace central_computer

#endif  // CENTRAL_COMPUTER_CENTRAL_COMPUTER_H
