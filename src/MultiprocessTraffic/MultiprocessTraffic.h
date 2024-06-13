#ifndef MULTIPROCESS_TRAFFIC_H
#define MULTIPROCESS_TRAFFIC_H

#include "ChildProcess.h"
#include "ParentProcess.h"
#include "Pipe.h"
#include <sys/types.h>
#include <vector>

class MultiprocessTraffic
{
public:
    MultiprocessTraffic();
    void start();

private:
    int numChildren;
    std::vector<pid_t> childPids;
    std::vector<Pipe> pipesParentToChild;
    std::vector<Pipe> pipesChildToParent;

    std::vector<std::vector<const char*>> phases;
    std::vector<int> phaseDurations;

    void createPipes();
    void forkChildren();
    void loadPhasingInfo();
};

#endif
