#include "VehicleGui.h"
#include <iostream>

void VehicleGui::display(const std::string& streamName,
                         const std::string& calibName) const
{
    std::cout << "Stream Name: " << streamName << std::endl;
    std::cout << "Calibration Name: " << calibName << std::endl;
    std::cerr << "No implementation yet for Vehicle Gui\n";
}
