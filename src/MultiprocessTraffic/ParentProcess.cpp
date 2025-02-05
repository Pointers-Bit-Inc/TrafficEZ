#include "ParentProcess.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <json.hpp>
#include <numeric>
#include <sys/wait.h>
#include <unistd.h>

ParentProcess::ParentProcess(int numVehicle,
                             int numPedestrian,
                             std::vector<Pipe>& pipesParentToChild,
                             std::vector<Pipe>& pipesChildToParent,
                             std::vector<std::vector<PhaseMessageType>>& phases,
                             std::vector<int>& phaseDurations,
                             bool verbose,
                             float densityMultiplierGreenPhase,
                             float densityMultiplierRedPhase,
                             float densityMin,
                             float densityMax,
                             int minPhaseDurationMs,
                             int minPedestrianDurationMs,
                             std::string relayUrl,
                             int subLocationId,
                             int junctionId,
                             std::string junctionName)
    : numVehicle(numVehicle)
    , numPedestrian(numPedestrian)
    , pipesParentToChild(pipesParentToChild)
    , pipesChildToParent(pipesChildToParent)
    , phases(phases)
    , phaseDurations(phaseDurations)
    , verbose(verbose)
    , densityMultiplierGreenPhase(densityMultiplierGreenPhase)
    , densityMultiplierRedPhase(densityMultiplierRedPhase)
    , densityMin(densityMin)
    , densityMax(densityMax)
    , minPhaseDurationMs(minPhaseDurationMs)
    , minPedestrianDurationMs(minPedestrianDurationMs)
    , relayUrl(relayUrl)
    , subLocationId(subLocationId)
    , junctionId(junctionId)
    , junctionName(junctionName)
{
    numChildren = numVehicle + numPedestrian;

    originalPhaseDurations = phaseDurations;

    fullCycleDurationMs = std::accumulate(
        originalPhaseDurations.begin(), originalPhaseDurations.end(), 0.0f);

    phaseRatio.clear();
    for(const auto& duration : originalPhaseDurations)
    {
        float ratio = static_cast<float>(duration) / fullCycleDurationMs;
        phaseRatio.push_back(ratio);
    }

    closeUnusedPipes();
}

void ParentProcess::run()
{
    int phaseIndex = 0;
    cycle = 0;

    std::vector<std::vector<float>> phaseDensities(
        phases.size(), std::vector<float>(numChildren, 0.0));

    std::vector<std::vector<float>> phaseSpeeds(
        phases.size(), std::vector<float>(numChildren, 0.0));

    std::vector<std::vector<std::unordered_map<std::string, int>>>
        phaseVehicles(
            phases.size(),
            std::vector<std::unordered_map<std::string, int>>(numChildren));

    // report.sendJunctionStatus();

    while(true)
    {
        // increment cycle by 1 when phaseIndex is 0
        phaseIndex == 0 ? cycle++ : cycle;
        if(verbose)
        {
            std::cout << "\n==================== Cycle: " << cycle
                      << " Phase: " << phaseIndex
                      << " ======================================\n";
        }

        telnetRelay.setPhaseCycle(phaseIndex);
        telnetRelay.executePhase();

        sendPhaseMessagesToChildren(phaseIndex);

        if(!receivePrevDataFromChildren(
               phaseIndex, phaseDensities, phaseSpeeds, phaseVehicles))
        {
            setDefaultPhaseDensities(phaseDensities);
        }

        handlePhaseTimer(phaseIndex);
        transitionToNextPhase(
            phaseIndex, phaseDensities, phaseSpeeds, phaseVehicles);
    }

    for(int i = 0; i < numChildren; ++i)
    {
        wait(nullptr); // Wait for child processes to finish
    }
}

void ParentProcess::sendPhaseMessagesToChildren(int phaseIndex)
{
    for(int i = 0; i < numChildren; ++i)
    {

        std::string phaseString;
        switch(phases[phaseIndex][i])
        {
        case GREEN_PHASE:
            phaseString = "GREEN_PHASE";
            break;
        case RED_PHASE:
            phaseString = "RED_PHASE";
            break;
        case GREEN_PED:
            phaseString = "GREEN_PED";
            break;
        case RED_PED:
            phaseString = "RED_PED";
            break;
        default:
            phaseString = "UNKNOWN";
            break;
        }

        if(write(pipesParentToChild[i].fds[1],
                 phaseString.c_str(),
                 phaseString.size() + 1) == -1)
        {
            std::cerr << "Parent: Failed to write to pipe: " << strerror(errno)
                      << "\n";
            break;
        }

        if(verbose)
        {
            std::cout << "Parent: Sending phase message to child " << i << ": "
                      << phaseString << "\n";
        }
    }
}

bool ParentProcess::receivePrevDataFromChildren(
    int phaseIndex,
    std::vector<std::vector<float>>& phaseDensities,
    std::vector<std::vector<float>>& phaseSpeeds,
    std::vector<std::vector<std::unordered_map<std::string, int>>>&
        phaseVehicles)
{
    char buffer[BUFFER_SIZE];

    int previousPhaseIndex =
        (phaseIndex == 0) ? phases.size() - 1 : phaseIndex - 1;

    for(int i = 0; i < numChildren; ++i)
    {
        float density;
        float speed;
        std::unordered_map<std::string, int> vehicles;
        if(!readDataFromChild(i, density, speed, vehicles))
        {
            return false;
        }

        PhaseMessageType previousPhaseType = phases[previousPhaseIndex][i];
        processDensityByPhaseType(previousPhaseType, density);
        density = std::clamp(density, densityMin, densityMax);

        if(verbose)
        {
            std::cout << "Previous child " << i << " data: " << density << "\n";
        }

        phaseDensities[previousPhaseIndex][i] = density;
        phaseSpeeds[previousPhaseIndex][i] = speed;
        phaseVehicles[previousPhaseIndex][i] = vehicles;
    }

    return true;
}

bool ParentProcess::readDataFromChild(
    int childIndex,
    float& density,
    float& speed,
    std::unordered_map<std::string, int>& vehicles)
{
    char buffer[BUFFER_SIZE];
    int bytesRead =
        read(pipesChildToParent[childIndex].fds[0], buffer, sizeof(buffer) - 1);
    if(bytesRead <= 0)
    {
        std::cerr << "Parent: Failed to read from pipe or EOF reached: "
                  << strerror(errno) << "\n";
        return false;
    }
    buffer[bytesRead] = '\0'; // Ensure null termination

    std::string data(buffer);

    // Locate the first and second semicolons
    auto firstDelimiterPos = data.find(';');
    auto secondDelimiterPos = data.find(';', firstDelimiterPos + 1);

    if(firstDelimiterPos == std::string::npos ||
       secondDelimiterPos == std::string::npos)
    {
        std::cerr << "Parent: Invalid format received from child " << childIndex
                  << "\n";
        return false;
    }

    // Parse density
    try
    {
        density = std::stof(data.substr(0, firstDelimiterPos));
    }
    catch(const std::invalid_argument&)
    {
        std::cerr << "Parent: Invalid density value received from child "
                  << childIndex << "\n";
        return false;
    }

    if(std::isnan(density) || (std::isnan(density) && std::signbit(density)))
    {
        std::cerr << "Parent: Detected NaN or negative NaN in traffic density "
                     "from child "
                  << childIndex << "\n";
        return false;
    }

    // Parse speed
    try
    {
        speed = std::stof(data.substr(
            firstDelimiterPos + 1, secondDelimiterPos - firstDelimiterPos - 1));
    }
    catch(const std::invalid_argument&)
    {
        std::cerr << "Parent: Invalid speed value received from child "
                  << childIndex << "\n";
        return false;
    }

    if(std::isnan(speed) || (std::isnan(speed) && std::signbit(speed)))
    {
        std::cerr << "Parent: Detected NaN or negative NaN in speed "
                     "from child "
                  << childIndex << "\n";
    }

    // Parse vehicle data
    std::string vehicleData = data.substr(secondDelimiterPos + 1);

    if(!vehicleData.empty())
    {
        if(verbose)
        {
            std::cout << "Parent: Vehicle data received from child "
                      << childIndex << "\n";
        }
    }

    vehicles.clear();
    std::istringstream vehicleStream(vehicleData);
    std::string vehicleEntry;
    while(std::getline(vehicleStream, vehicleEntry, ','))
    {
        auto pos = vehicleEntry.find(':');
        if(pos != std::string::npos)
        {
            std::string vehicleType = vehicleEntry.substr(0, pos);
            int count = std::stoi(vehicleEntry.substr(pos + 1));
            vehicles[vehicleType] = count;
        }
    }

    return true;
}

void ParentProcess::processDensityByPhaseType(PhaseMessageType phaseType,
                                              float& density)
{
    switch(phaseType)
    {
    case GREEN_PHASE:
        density *= densityMultiplierGreenPhase;
        break;
    case RED_PHASE:
        density = (densityMax - density) * densityMultiplierRedPhase;
        break;
    case RED_PED:
        density = 0;
        break;
    case GREEN_PED:
        // No specific handling needed
        break;
    default:
        break;
    }
}

void ParentProcess::handlePhaseTimer(int phaseIndex)
{
    float phaseTime = phaseDurations[phaseIndex] / 1000;
    int remainingTime = phaseTime;

    if(verbose)
    {
        std::cout << "Phase " << phaseIndex << " Duration: " << phaseTime;
    }

    while(remainingTime > 0)
    {
        std::cout << "\rRemaining time for phase " << phaseIndex << ": "
                  << remainingTime << " seconds.";
        std::cout.flush();
        sleep(1);

        if(remainingTime == 5)
        {
            telnetRelay.executeTransitionPhase();
        }

        --remainingTime;
    }

    std::cout << std::endl;
}

void ParentProcess::transitionToNextPhase(
    int& phaseIndex,
    std::vector<std::vector<float>>& phaseDensities,
    std::vector<std::vector<float>>& phaseSpeeds,
    std::vector<std::vector<std::unordered_map<std::string, int>>>&
        phaseVehicles)
{
    phaseIndex = (phaseIndex + 1) % phases.size();

    if(phaseIndex == 0)
    {
        updatePhaseDurations(phaseDensities, phaseSpeeds, phaseVehicles);
    }
}

void ParentProcess::setDefaultPhaseDensities(
    std::vector<std::vector<float>>& phaseDensities)
{
    if(phaseDensities.size() != phaseRatio.size())
    {
        std::cerr << "Size of phase density and ratio do not match!\n";
        exit(EXIT_FAILURE);
    }

    for(int i = 0; i < phaseRatio.size(); ++i)
    {
        phaseDensities[i] = std::vector<float>(numChildren, phaseRatio[i]);
    }

    std::cout << "Parent: Phase densities set to default values.\n";
}

void ParentProcess::updatePhaseDurations(
    const std::vector<std::vector<float>>& phaseDensities,
    const std::vector<std::vector<float>>& phaseSpeeds,
    std::vector<std::vector<std::unordered_map<std::string, int>>>&
        phaseVehicles)
{
    float totalDensity = 0.0;
    float totalPedestrianCount = 0.0;
    std::vector<float> phaseTotals(phases.size(), 0.0);
    std::vector<float> pedestrianTotals(phases.size(), 0.0);

    if(verbose)
    {
        std::cout << "----- Cycle " << cycle
                  << " Density Distribution ------------\n";
    }

    nlohmann::json junctionReport;
    junctionReport["subLocationId"] = subLocationId;
    junctionReport["name"] = junctionName;
    junctionReport["description"] = "Junction " + std::to_string(junctionId) +
                                    " Report: Cycle " + std::to_string(cycle);

    nlohmann::json nextCyclePhaseDurations = nlohmann::json::array();
    nlohmann::json cycleData = nlohmann::json::array();
    nlohmann::json phaseData;

    for(int phase = 0; phase < phases.size(); ++phase)
    {
        float phaseDuration = phaseDurations[phase] / 1000.0;
        phaseData["junctionId"] = junctionId;
        phaseData["phase"] = phase;
        phaseData["phaseDuration"] = phaseDuration;

        nlohmann::json vehicleLaneDensities = nlohmann::json::array();
        nlohmann::json pedestrianLaneCounts = nlohmann::json::array();
        nlohmann::json vehicleLaneDensity;

        // vehicles density
        for(int child = 0; child < numVehicle; ++child)
        {
            nlohmann::json vehicles = nlohmann::json::array();
            vehicleLaneDensity["laneId"] = "Lane_" + std::to_string(child);
            vehicleLaneDensity["laneName"] =
                "Vehicle Lane " + std::to_string(child);

            phaseTotals[phase] += phaseDensities[phase][child];
            if(verbose)
            {
                std::cout << "Phase " << phase << " - child " << child
                          << "\n  density: " << phaseDensities[phase][child]
                          << "\n";
            }
            vehicleLaneDensity["density"] = phaseDensities[phase][child];

            if(verbose)
            {
                std::cout << "  avgspeed: " << phaseSpeeds[phase][child]
                          << "\n";
            }

            for(const auto& entry : phaseVehicles[phase][child])
            {
                if(verbose)
                {
                    std::cout << "  " << entry.first << ": " << entry.second
                              << std::endl;
                }

                nlohmann::json vehicle;
                vehicle["type"] = entry.first;
                vehicle["count"] = entry.second;
                vehicles.push_back(vehicle);
            }
            vehicleLaneDensity["vehicles"] = vehicles;
            vehicleLaneDensities.push_back(vehicleLaneDensity);
        }
        totalDensity += phaseTotals[phase];
        phaseData["vehicleLaneDensities"] = vehicleLaneDensities;

        // pedestrian counts
        for(int child = numVehicle; child < numChildren; ++child)
        {
            nlohmann::json pedestrianLaneCount;
            pedestrianLaneCount["laneId"] = "Lane_" + std::to_string(child);
            pedestrianLaneCount["laneName"] =
                "Pedestrian Lane " + std::to_string(child);
            pedestrianTotals[phase] += phaseDensities[phase][child];

            if(verbose)
            {
                std::cout << "Phase " << phase << " - child " << child
                          << " pedestrian: " << phaseDensities[phase][child]
                          << "\n";
            }

            pedestrianLaneCount["count"] = phaseDensities[phase][child];
            pedestrianLaneCounts.push_back(pedestrianLaneCount);
        }
        totalPedestrianCount += pedestrianTotals[phase];
        phaseData["pedestrianLaneDensities"] = pedestrianLaneCounts;
        phaseData["id"] = 0;
        cycleData.push_back(phaseData);
    }

    std::cout << "----------------------------------------------------------\n";

    // Update phase durations based on the density ratios
    bool validDurations = true;

    for(int phase = 0; phase < phases.size(); ++phase)
    {
        phaseDurations[phase] = static_cast<int>(
            (totalDensity == 0.0 ? 1.0 : phaseTotals[phase] / totalDensity) *
            fullCycleDurationMs);

        // so we always have enough time for yellow
        if(phaseDurations[phase] < minPhaseDurationMs)
        {
            phaseDurations[phase] = minPhaseDurationMs;
        }

        if(phaseDurations[phase] > fullCycleDurationMs)
        {
            validDurations = false;
            break;
        }

        // if there is waiting pedestrian, prioritize them
        if(pedestrianTotals[phase] > 0 &&
           phaseDurations[phase] < minPedestrianDurationMs)
        {
            phaseDurations[phase] = minPedestrianDurationMs;
        }
        std::cout << "Phase " << phase << " allocated time: "
                  << phaseDurations[phase] / 1000.0 // Convert to seconds
                  << " seconds.\n";
    }

    std::cout << "----------------------------------------------------------\n";

    if(!validDurations)
    {
        phaseDurations = originalPhaseDurations;
        std::cerr << "Parent: Phase durations set to original values due to "
                     "invalid duration.\n";
    }

    // final updated phase duration after all validations
    for(int phase = 0; phase < phases.size(); ++phase)
    {
        float allocatedTime = (phaseDurations[phase] / 1000.0);
        nextCyclePhaseDurations.push_back(allocatedTime);
    }
    junctionReport["nextCyclePhaseDurations"] = nextCyclePhaseDurations;
    junctionReport["cycleData"] = cycleData;

    // can be uncommented for reviewing junction report format
    /*
    if(verbose)
    {
        std::cout << "\n------------- Junction " << junctionId
                  << " Report: Cycle " + std::to_string(cycle) +
                         " to Send -------------\n";
        std::cout << junctionReport.dump(2) << "\n";
    }
    */
    std::string reportData = junctionReport.dump();
    report.sendJunctionReport(reportData);
}

void ParentProcess::closeUnusedPipes()
{
    for(int i = 0; i < numChildren; ++i)
    {
        close(pipesParentToChild[i].fds[0]);
        close(pipesChildToParent[i].fds[1]);
    }
}
