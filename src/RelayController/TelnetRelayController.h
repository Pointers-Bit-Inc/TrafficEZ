#ifndef TELNET_RELAY_CONTROLLER_H
#define TELNET_RELAY_CONTROLLER_H

#include "PhaseMessageType.h"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#define PORT 23

class TelnetRelayController
{
public:
    static TelnetRelayController& getInstance(
        const std::string& ip = "",
        const std::string& user = "",
        const std::string& pass = "",
        const std::vector<std::vector<PhaseMessageType>>& phaseData = {},
        bool verboseMode = false)
    {
        static TelnetRelayController instance(
            ip, user, pass, phaseData, verboseMode);
        return instance;
    }

    // Deleted copy constructor and assignment operator
    TelnetRelayController(const TelnetRelayController&) = delete;
    TelnetRelayController& operator=(const TelnetRelayController&) = delete;

    void sendCommand(const std::string& command);
    std::string receiveResponse(int retries = 5, int timeoutSeconds = 5);
    void turnOnRelay(int relayNumber);
    void turnOffRelay(int relayNumber);
    std::string getRelayStatus();

    std::string getHexCommand(const std::vector<int>& onChannels);

    void setPhaseCycle(int cycle);
    void executePhase();
    void executeTransitionPhase();

    std::vector<PhaseMessageType>
    deriveTransitionPhase(const std::vector<PhaseMessageType>& currentPhase,
                          const std::vector<PhaseMessageType>& nextPhase);

protected:
    bool connectToRelay();
    bool authenticate();
    bool reconnect();

private:
    TelnetRelayController(
        const std::string& ip,
        const std::string& user,
        const std::string& pass,
        const std::vector<std::vector<PhaseMessageType>>& phaseData,
        bool verboseMode);

    ~TelnetRelayController();

    std::string relayIP;
    std::string username;
    std::string password;
    int sock;
    std::vector<std::vector<PhaseMessageType>> phases;
    bool verbose;
    int currentCycle;
};

#endif
