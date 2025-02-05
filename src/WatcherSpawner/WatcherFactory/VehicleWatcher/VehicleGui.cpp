#include "VehicleGui.h"

void VehicleGui::initialize(const std::string& streamName,
                            const std::string& calibName)
{
    if(!videoStreamer.openVideoStream(streamName) ||
       !videoStreamer.readCalibrationData(calibName))
    {
        std::cerr << "Error: Failed to initialize video stream or calibration "
                     "data.\n";
        return;
    }

    streamWindow = streamName + " Vehicle GUI";

    laneLength = videoStreamer.getLaneLength();
    laneWidth = videoStreamer.getLaneWidth();
    segModel = videoStreamer.getSegModel();

    videoStreamer.constructStreamWindow(streamWindow);
    videoStreamer.initializePerspectiveTransform(inputFrame, warpPerspective);

    // pipeDirector.setupDefaultPipeline(pipeBuilder);
    // // If you want to use the yaml config file, use the methods below:
    // pipeDirector.savePipelineConfig(pipeBuilder, calibName);
    pipeDirector.loadPipelineConfig(pipeBuilder, calibName);

    // // Commented out because Trackbar gets confusing with forked processes.
    // // To show Trackbar, be sure to only spawn one process then do the following:
    // // 1. Uncomment PipelineTrackbar instance.
    // // 2. Uncomment while loop below.
    // // 3. In processTrackingState method, use processDebugStack instead of process.

    // PipelineTrackbar pipeTrackbar(pipeBuilder, streamName);

    videoStreamer.applyFrameRoi(inputFrame, warpedFrame, warpPerspective);
    videoStreamer.resizeStreamWindow(warpedFrame);

    hullDetector.initDetectionBoundaries(warpedFrame);
    hullTracker.initExitBoundaryLine(hullDetector.getEndDetectionLine());

    // std::cout << "Press Escape to exit Trackbar loop.\n";
    // while(videoStreamer.applyFrameRoi(inputFrame, warpedFrame, warpPerspective))
    // {
    //     processTrackingState();

    //     if(cv::waitKey(30) == 27)
    //         break;
    // }

    std::unique_ptr<ISegmentationStrategy> strategy =
        std::make_unique<VehicleSegmentationStrategy>();
    segmentation.initializeModel(segModel, std::move(strategy));

    isTracking = false;
}

void VehicleGui::display()
{
    if(!videoStreamer.applyFrameRoi(inputFrame, warpedFrame, warpPerspective))
        return;

    if(!isTracking)
    {
        fpsHelper.startSample();
        isTracking = true;
    }

    (currentTrafficState == TrafficState::GREEN_PHASE)
        ? processTrackingState()
        : processSegmentationState(); // we only process YOLO result for gui

    cv::waitKey(1); // needed for imshow
}

float VehicleGui::getTrafficDensity()
{
    float density = 0;

    if(currentTrafficState == TrafficState::GREEN_PHASE)
    {
        float totalTime = fpsHelper.endSample() / 1000;
        float flow = hullTracker.getTotalHullArea() / totalTime;
        float average = hullTracker.getAveragedSpeed();
        density = (flow == 0) ? 0 : flow / (average * laneWidth);

        hullTracker.resetTrackerVariables();
    }

    else if(currentTrafficState == TrafficState::RED_PHASE)
    {
        float totalArea = segmentation.getWhiteArea(warpedMask);
        density = totalArea / (laneLength * laneWidth);
    }

    isTracking = false;

    return density;
}

int VehicleGui::getInstanceCount()
{
    int count = 0;

    if(currentTrafficState == TrafficState::GREEN_PHASE)
    {
        count = hullTracker.getHullCount();
    }

    else if(currentTrafficState == TrafficState::RED_PHASE)
    {
        count = segmentation.getDetectionResultSize();
    }

    return count;
}

void VehicleGui::processTrackingState()
{
    warpedFrame.copyTo(processFrame);
    pipeBuilder.process(processFrame);
    // pipeBuilder.processDebugStack(processFrame);

    std::vector<std::vector<cv::Point>> hulls;
    hullDetector.getHulls(processFrame, hulls);
    hullTracker.update(hulls);

    hullTracker.drawTrackedHulls(warpedFrame);
    hullTracker.drawLanesInfo(warpedFrame, laneLength, laneWidth);
    hullDetector.drawLengthBoundaries(warpedFrame);

    fpsHelper.avgFps();
    fpsHelper.displayFps(warpedFrame);

    cv::imshow(streamWindow, warpedFrame);
    cv::waitKey(30);
}

void VehicleGui::processSegmentationState()
{
    cv::Mat segMask = segmentation.generateMask(inputFrame);
    warpedMask = videoStreamer.applyPerspective(segMask, warpPerspective);

    cv::imshow(streamWindow + " segMask", warpedMask);
    // cv::waitKey(0);
}

std::unordered_map<std::string, int> VehicleGui::getVehicleTypeAndCount()
{
    if(currentTrafficState == TrafficState::GREEN_PHASE)
    {
        int hullCount = hullTracker.getHullCount();
        if(hullCount < 1)
        {
            return {};
        }
        return {{"unknown", hullCount}};
    }
    else if(currentTrafficState == TrafficState::RED_PHASE)
    {
        return segmentation.getClassTypeAndCounts();
    }

    return {};
}

float VehicleGui::getAverageSpeed()
{
    float avgSpeed = 0.0f;

    if(currentTrafficState == TrafficState::GREEN_PHASE)
    {
        avgSpeed = hullTracker.getAveragedSpeed();
    }

    // Default speed when not in GREEN_PHASE
    return avgSpeed;
}
