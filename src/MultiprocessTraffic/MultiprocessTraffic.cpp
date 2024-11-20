#include "MultiprocessTraffic.h"
#include "Reports.h"
#include "TelnetRelayController.h"
#include <csignal>
#include <cstring>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

MultiprocessTraffic* MultiprocessTraffic::instance = nullptr;
int MultiprocessTraffic::standbyDuration = 60000; //milliseconds

MultiprocessTraffic::MultiprocessTraffic(const std::string& configFile,
                                         bool debug,
                                         bool verbose)
    : configFile(configFile)
    , debug(debug)
    , verbose(verbose)
{
    instance = this;
    loadJunctionConfig();

    TelnetRelayController::getInstance(
        relayUrl, relayUsername, relayPassword, phases, verbose);

    Reports::getInstance(
        httpUrl, tSecretKey, junctionId, junctionName, verbose);

    std::signal(SIGINT, MultiprocessTraffic::handleSignal);
    std::signal(SIGCHLD, MultiprocessTraffic::handleSignal);
}

void MultiprocessTraffic::start()
{
    createPipes();
    forkChildren();

    ParentProcess parentProcess(numVehicle,
                                numPedestrian,
                                pipesParentToChild,
                                pipesChildToParent,
                                phases,
                                phaseDurations,
                                verbose,
                                densityMultiplierGreenPhase,
                                densityMultiplierRedPhase,
                                densityMin,
                                densityMax,
                                minPhaseDurationMs,
                                minPedestrianDurationMs,
                                relayUrl,
                                junctionId,
                                junctionName);

    parentProcess.run();
}

void MultiprocessTraffic::handleSignal(int signal)
{
    if(instance == nullptr)
    {
        std::cerr << "MultiprocessTraffic instance is null\n";
        exit(EXIT_FAILURE);
    }

    if(signal == SIGINT)
    {
        TelnetRelayController& telnetRelay =
            TelnetRelayController::getInstance();

        // Turn off all relays before exiting
        telnetRelay.turnOffAllRelay();

        std::cout << "\nInterrupt signal received. Turning off all relays...\n";
        for(pid_t pid : instance->childPids)
        {
            std::cout << "Killing Child PID: " << pid << "\n";
            kill(pid, SIGTERM);
        }
        std::cout << "Exiting Parent PID: " << getpid() << "\n";
        exit(EXIT_SUCCESS);
    }

    if(signal == SIGCHLD)
    {
        static std::mutex standbyMutex;

        pid_t pid;
        while((pid = waitpid(-1, nullptr, WNOHANG)) > 0)
        {
            std::cout << "Reaped Child PID: " << pid << "\n";
            // Remove the terminated child PID from the child list
            instance->childPids.erase(std::remove(instance->childPids.begin(),
                                                  instance->childPids.end(),
                                                  pid),
                                      instance->childPids.end());
        }

        // Ensure that the standby mode logic is not entered concurrently
        std::lock_guard<std::mutex> lock(standbyMutex);

        TelnetRelayController& telnetRelay =
            TelnetRelayController::getInstance();

        // Enter standby mode, flashing yellow relay for standbyDuration before proceeding
        telnetRelay.standbyMode(standbyDuration);

        // Sleep for a short time to ensure relay off
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        telnetRelay.turnOffAllRelay();

        std::cout << "\nOne of the children unexpectedly crashed.\n";
        // Kill remaining child processes
        for(pid_t pid : instance->childPids)
        {
            std::cout << "Killing Child PID: " << pid << "\n";
            if(kill(pid, SIGTERM) == -1)
            {
                std::cerr << "Failed to kill child PID: " << pid
                          << ", error: " << strerror(errno) << "\n";
            }
        }

        std::cout << "Exiting Parent PID: " << getpid() << "\n";
        exit(EXIT_SUCCESS); // Exit the parent process
    }
}

void MultiprocessTraffic::createPipes()
{
    pipesParentToChild.resize(numChildren);
    pipesChildToParent.resize(numChildren);

    for(int i = 0; i < numChildren; ++i)
    {
        if(pipe(pipesParentToChild[i].fds) == -1 ||
           pipe(pipesChildToParent[i].fds) == -1)
        {
            std::cerr << "Pipe creation failed: " << strerror(errno) << "\n";
            exit(EXIT_FAILURE);
        }
    }
}

void MultiprocessTraffic::forkChildren()
{
    int pipeIndex = 0;

    if(verbose)
    {
        std::cout << "Parent PID: " << getpid() << "\n";
    }

    for(int i = 0; i < numVehicle; ++i)
    {
        pid_t pid = fork();
        if(pid < 0)
        {
            std::cerr << "Fork failed: " << strerror(errno) << "\n";
            exit(EXIT_FAILURE);
        }
        else if(pid == 0)
        {
            ChildProcess vehicleProcess(pipeIndex,
                                        pipesParentToChild[pipeIndex],
                                        pipesChildToParent[pipeIndex],
                                        verbose);
            vehicleProcess.runVehicle(
                debug, streamConfigs[pipeIndex], streamLinks[pipeIndex]);
            exit(EXIT_SUCCESS);
        }
        else
        {
            childPids.push_back(pid);
            if(verbose)
            {
                std::cout << "Vehicle Child " << pipeIndex << " PID: " << pid
                          << "\n";
            }
        }
        ++pipeIndex;
    }

    for(int i = 0; i < numPedestrian; ++i)
    {
        pid_t pid = fork();
        if(pid < 0)
        {
            std::cerr << "Fork failed: " << strerror(errno) << "\n";
            exit(EXIT_FAILURE);
        }
        else if(pid == 0)
        {
            ChildProcess pedestrianProcess(pipeIndex,
                                           pipesParentToChild[pipeIndex],
                                           pipesChildToParent[pipeIndex],
                                           verbose);
            pedestrianProcess.runPedestrian(
                debug, streamConfigs[pipeIndex], streamLinks[pipeIndex]);
            exit(EXIT_SUCCESS);
        }
        else
        {
            childPids.push_back(pid);
            if(verbose)
            {
                std::cout << "Pedestrian Child " << pipeIndex << " PID: " << pid
                          << "\n";
            }
        }
        ++pipeIndex;
    }
}

void MultiprocessTraffic::calibrate()
{
    WatcherSpawner spawner;

    for(int i = 0; i < numChildren; ++i)
    {
        // calibration can only be done with GUI, not headless
        Watcher* calibrateWatcherGui =
            spawner.spawnWatcher(WatcherType::CALIBRATE,
                                 RenderMode::GUI,
                                 streamLinks[i],
                                 streamConfigs[i]);
        delete calibrateWatcherGui;
    }
}

void MultiprocessTraffic::loadJunctionConfig()
{
    YAML::Node config = YAML::LoadFile(configFile);

    loadJunctionInfo(config);
    loadPhases(config);
    loadPhaseDurations(config);
    loadDensitySettings(config);
    loadStreamInfo(config);
    loadRelayInfo(config);
    loadHttpInfo(config);

    setVehicleAndPedestrianCount();
}

void MultiprocessTraffic::loadJunctionInfo(const YAML::Node& config)
{

    if(!config["junctionId"] || !config["junctionName"])
    {
        std::cerr << "Missing junction information in configuration file!"
                  << std::endl;
        exit(EXIT_FAILURE);
    }

    junctionId = config["junctionId"].as<int>();
    junctionName = config["junctionName"].as<std::string>();
}

void MultiprocessTraffic::loadHttpInfo(const YAML::Node& config)
{
    if(config["httpUrl"])
    {
        httpUrl = config["httpUrl"].as<std::string>();
    }
    else
    {
        httpUrl = "https://55qdnlqk-5234.asse.devtunnels.ms"; // Default value
        std::cerr << "httpUrl not provided, using default: " << httpUrl
                  << std::endl;
    }

    if(config["tSecretKey"])
    {
        tSecretKey = config["tSecretKey"].as<std::string>();
    }
    else
    {
        tSecretKey = "TrafficEz-001-002-003-004"; // Default value
        std::cerr << "tSecretKey not provided, using default: " << tSecretKey
                  << std::endl;
    }
}

void MultiprocessTraffic::loadPhases(const YAML::Node& config)
{
    if(!config["phases"])
    {
        std::cerr << "No phases config found!\n";
        exit(EXIT_FAILURE);
    }

    phases.clear();
    for(const auto& phase : config["phases"])
    {
        std::vector<PhaseMessageType> phaseVector;
        for(const auto& phaseStr : phase)
        {
            std::string phaseString = phaseStr.as<std::string>();
            PhaseMessageType phaseType = getPhaseMessageType(phaseString);
            phaseVector.push_back(phaseType);
        }
        phases.push_back(phaseVector);
    }
}

void MultiprocessTraffic::loadPhaseDurations(const YAML::Node& config)
{
    if(!config["phaseDurations"])
    {
        std::cerr << "No phaseDurations config found!\n";
        exit(EXIT_FAILURE);
    }

    phaseDurations.clear();
    for(const auto& duration : config["phaseDurations"])
    {
        phaseDurations.push_back(duration.as<int>());
    }

    if(phases.size() != phaseDurations.size())
    {
        std::cerr << "Size of phase info and duration do not match!\n";
        exit(EXIT_FAILURE);
    }

    if(config["standbyDuration"])
    {
        standbyDuration = config["standbyDuration"].as<int>();
    }
    else
    {
        standbyDuration = 60000; // Default value in milliseconds
        std::cerr << "standbyDuration not provided, using default: "
                  << standbyDuration << std::endl;
    }
}

void MultiprocessTraffic::loadDensitySettings(const YAML::Node& config)
{
    if(!config["densityMultiplierGreenPhase"] ||
       !config["densityMultiplierRedPhase"] || !config["densityMin"] ||
       !config["densityMax"] || !config["minPhaseDurationMs"] ||
       !config["minPedestrianDurationMs"])
    {
        std::cerr << "Missing density settings in configuration file!\n";
        exit(EXIT_FAILURE);
    }

    densityMultiplierGreenPhase =
        config["densityMultiplierGreenPhase"].as<float>();
    densityMultiplierRedPhase = config["densityMultiplierRedPhase"].as<float>();
    densityMin = config["densityMin"].as<float>();
    densityMax = config["densityMax"].as<float>();
    minPhaseDurationMs = config["minPhaseDurationMs"].as<int>();
    minPedestrianDurationMs = config["minPedestrianDurationMs"].as<int>();
}

void MultiprocessTraffic::loadStreamInfo(const YAML::Node& config)
{
    streamConfigs.clear();
    streamLinks.clear();

    for(const auto& stream : config["streamInfo"])
    {
        if(stream.size() != 2)
        {
            std::cerr << "Invalid streamInfo entry!\n";
            exit(EXIT_FAILURE);
        }

        streamConfigs.push_back(stream[0].as<std::string>());
        streamLinks.push_back(stream[1].as<std::string>());
    }
}

void MultiprocessTraffic::loadRelayInfo(const YAML::Node& config)
{
    if(!config["relayUrl"] || !config["relayUsername"] ||
       !config["relayPassword"])
    {
        std::cerr << "Missing relay info in configuration file!\n";
        exit(EXIT_FAILURE);
    }

    relayUrl = config["relayUrl"].as<std::string>();
    relayUsername = config["relayUsername"].as<std::string>();
    relayPassword = config["relayPassword"].as<std::string>();
}

void MultiprocessTraffic::setVehicleAndPedestrianCount()
{
    numVehicle = 0;
    numPedestrian = 0;
    numChildren = phases[0].size();

    for(const auto& phase : phases[0])
    {
        switch(phase)
        {
        case GREEN_PHASE:
        case RED_PHASE:
            numVehicle++;
            break;
        case GREEN_PED:
        case RED_PED:
            numPedestrian++;
            break;
        default:
            std::cerr << "Unknown phase type!\n";
            exit(EXIT_FAILURE);
        }
    }

    if(numChildren != numVehicle + numPedestrian)
    {
        std::cerr << "Count of children(" << numChildren
                  << ") do not match Vehicle(" << numVehicle
                  << ") + Pedestrian(" << numPedestrian << ")\n";
        exit(EXIT_FAILURE);
    }

    if(numChildren != streamConfigs.size() || numChildren != streamLinks.size())
    {
        std::cerr << "Count of children(" << numChildren
                  << ") do not match streamConfigs(" << streamConfigs.size()
                  << ") or streamLinks(" << streamLinks.size() << ")\n";
        exit(EXIT_FAILURE);
    }
}
