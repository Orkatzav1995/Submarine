/*
 * central_computer.cpp
 *
 * Purpose:
 *   Implements the CentralComputer class declared in central_computer.h.
 *
 * Layer:
 *   Application entry-point-adjacent.
 *
 * This file does NOT:
 *   - Print anything - see central_computer.h's header comment.
 */

#include "central_computer.h"

namespace central_computer {

CentralComputer::CentralComputer()
    : communication_(),
      dataStore_(),
      managementCommand_(communication_),
      dataCollection_(communication_, dataStore_),
      gsCommunication_(dataStore_)
{
}

bool CentralComputer::start(const std::string &portName, uint32_t baudRate, const std::string &dbPath,
                             uint16_t gsPort)
{
    if (!dataStore_.open(dbPath))
    {
        return false;
    }

    gsCommunication_.startListening(gsPort);
    communication_.open(portName, baudRate);

    return true;
}

void CentralComputer::stop()
{
    communication_.close();
    gsCommunication_.close();
    dataStore_.close();
}

void CentralComputer::poll()
{
    communication_.poll();
    gsCommunication_.poll();
}

communication::Communication &CentralComputer::communication()
{
    return communication_;
}

data_store::DataStore &CentralComputer::dataStore()
{
    return dataStore_;
}

management_command::ManagementCommand &CentralComputer::managementCommand()
{
    return managementCommand_;
}

data_collection::DataCollection &CentralComputer::dataCollection()
{
    return dataCollection_;
}

gs_communication::GsCommunication &CentralComputer::gsCommunication()
{
    return gsCommunication_;
}

}  // namespace central_computer
