#include "TrafficManager.h"
#include "CalibrateVideoStreamer.h"
#include "VideoStreamer.h"
#include <iostream>
#include <opencv2/opencv.hpp>

TrafficManager::TrafficManager(int numCars,
                               int numPedestrians,
                               bool debug,
                               bool calib)
    : numberOfCars(numCars)
    , numberOfPedestrians(numPedestrians)
    , debugMode(debug)
    , calibMode(calib)
{}

void TrafficManager::start()
{
    std::cout << "TrafficManager starting...\n";
    std::cout << "Number of Cars: " << numberOfCars << "\n";
    std::cout << "Number of Pedestrians: " << numberOfPedestrians << "\n";
    std::cout << "Debug Mode: " << (debugMode ? "true" : "false") << "\n";

    if(calibMode)
    {
        calibrateStreamPoints();
    }
    if(debugMode)
    {
        // This is blocking! change this later
        spawnCarObserverDebug();
    }

    std::cout << "TrafficManager ended.\n";
}

void TrafficManager::calibrateStreamPoints()
{
    CalibrateVideoStreamer calibrateStreamer;

    cv::String calibWindow = "Calibrate Points";
    cv::String previewWindow = "Preview Warp";
    cv::String calibFilename = "calib_points.yaml";

    if(!calibrateStreamer.openVideoStream("debug.mp4"))
    {
        return;
    }

    cv::Mat frame;
    cv::Mat warpedFrame;
    bool previewToggle = false;

    calibrateStreamer.constructStreamWindow(calibWindow);
    calibrateStreamer.initCalibrationPoints(calibWindow);

    while(calibrateStreamer.settingCalibrationPoints(frame))
    {
        calibrateStreamer.showCalibrationPoints(frame);

        cv::imshow(calibWindow, frame);

        if(previewToggle)
        {
            if(!calibrateStreamer.applyFrameRoi(frame, warpedFrame))
                return;
            cv::imshow(previewWindow, warpedFrame);
        }

        int key = cv::waitKey(30);
        switch(key)
        {
        case 27: // 'Esc' key to exit by interruption
            std::cout << "Calibration interrupted.\n";
            return;
        case 'r': // 'r' key to reset
        case 'R':
            calibrateStreamer.resetCalibrationPoints();
            break;
        case 'p': // 'p' key to preview warped frame
        case 'P':
            if(!calibrateStreamer.haveSetFourPoints())
                break;
            previewToggle = !previewToggle;
            if(previewToggle)
                calibrateStreamer.initPreviewWarp();
            else
                cv::destroyWindow(previewWindow);
            break;
        case 's': // 's' key to exit successfully
        case 'S':
            calibrateStreamer.saveCalibrationPoints(calibFilename);
            break;
        }
    }
}

void TrafficManager::spawnCarObserverDebug()
{
    VideoStreamer videoStreamer;

    if(!videoStreamer.openVideoStream("debug.mp4"))
    {
        return;
    }

    if(!videoStreamer.readCalibrationPoints("calib_points.yaml"))
    {
        return;
    }

    cv::Mat frame;
    cv::Mat warpedFrame;

    videoStreamer.initializePerspectiveTransform();

    while(videoStreamer.applyFrameRoi(frame, warpedFrame))
    {
        cv::imshow("Debug Warped Video", warpedFrame);

        if(cv::waitKey(30) == 27)
            break;
    }

    cv::destroyAllWindows();
}