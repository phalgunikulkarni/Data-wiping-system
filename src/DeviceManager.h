#pragma once
#include <vector>
#include <string>

struct DriveInfo {
    char letter;
    std::string path;
    std::string model;
    unsigned long long sizeBytes;
    int diskNumber;
};

class DeviceManager {
public:
    std::vector<DriveInfo> listRemovableDrives();
};