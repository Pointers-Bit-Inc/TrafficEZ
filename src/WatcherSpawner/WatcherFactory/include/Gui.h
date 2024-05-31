#ifndef GUI_H
#define GUI_H

#include "TrafficState.h"
#include <string>

class Gui
{
public:
    virtual ~Gui() {}

    virtual void initialize(const std::string& streamName,
                            const std::string& calibName) = 0;

    virtual void display() = 0;

    virtual float getTrafficDensity() = 0;

    void setCurrentTrafficState(TrafficState state)
    {
        currentTrafficState = state;
    }

protected:
    TrafficState currentTrafficState;
};

#endif
