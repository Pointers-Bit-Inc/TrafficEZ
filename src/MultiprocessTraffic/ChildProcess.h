#ifndef CHILD_PROCESS_H
#define CHILD_PROCESS_H

#include "PhaseMessageType.h"
#include "Pipe.h"
#include "WatcherSpawner.h"

class ChildProcess
{
public:
    ChildProcess(int childIndex,
                 Pipe& pipeParentToChild,
                 Pipe& pipeChildToParent,
                 bool verbose = false);

    void runVehicle(bool debug,
                    const std::string& streamConfig,
                    const std::string& streamLink);
    void runPedestrian(bool debug,
                       const std::string& streamConfig,
                       const std::string& streamLink);

private:
    static constexpr int BUFFER_SIZE = 128;
    static constexpr int CPU_SLEEP_US = 1000;

    bool verbose;

    WatcherSpawner spawner;

    int childIndex;
    Pipe& pipeParentToChild;
    Pipe& pipeChildToParent;

    static void handleSignal(int signal);
    static ChildProcess* instance;

    Watcher* createWatcher(WatcherType watcherType,
                           bool debug,
                           const std::string& streamConfig,
                           const std::string& streamLink);

    void processMessageBuffer(int bytesRead,
                              char* buffer,
                              bool& isStateGreen,
                              Watcher* watcher);

    void handlePhaseMessage(PhaseMessageType phaseType,
                            Watcher* watcher,
                            bool& isStateGreen);

    void sendPhaseMessageToParent(
        float density,
        float speed,
        std::unordered_map<std::string, int> vehicles = {});

    void closeUnusedPipes();
};

#endif
