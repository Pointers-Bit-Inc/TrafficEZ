#ifndef HULLTRACKER_H
#define HULLTRACKER_H

#include "HullTrackable.h"
#include <unordered_map>
#include <vector>

class HullTracker
{
private:
    std::unordered_map<int, HullTrackable> trackedHulls;
    const double maxDistance; // Max allowed distance for matching
    const int maxFramesNotSeen; // Frames to wait before removing a hull

    void matchAndUpdateTrackables(
        const std::vector<std::vector<cv::Point>>& newHulls,
        std::vector<bool>& matched);
    void addNewTrackables(const std::vector<std::vector<cv::Point>>& newHulls,
                          const std::vector<bool>& matched);
    void removeStaleTrackables();

public:
    HullTracker();
    void update(const std::vector<std::vector<cv::Point>>& newHulls);
    const std::unordered_map<int, HullTrackable>& getTrackedHulls() const;
    void drawTrackedHulls(cv::Mat& frame) const;
};

#endif
