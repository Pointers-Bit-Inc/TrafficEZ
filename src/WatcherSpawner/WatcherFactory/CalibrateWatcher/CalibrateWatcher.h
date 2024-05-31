#ifndef CALIBRATE_WATCHER_H
#define CALIBRATE_WATCHER_H

#include "Gui.h"
#include "Headless.h"
#include "Watcher.h"

class CalibrateWatcher : public Watcher
{
public:
    void spawn(RenderMode mode,
               const std::string& streamName,
               const std::string& calibName) override;

    void processFrame() override;

    void setCurrentTrafficState(TrafficState state) override;
    float getTrafficDensity() override;

private:
    Gui* gui;
    Headless* headless;
    RenderMode currentMode;
};

#endif
