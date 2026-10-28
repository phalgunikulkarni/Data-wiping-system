#pragma once
#include <vector>
#include <string>

struct DriveInfo {
    char letter = 0;
    std::string mountPoint;
    std::string volumeLabel;
    std::string fileSystem;
    unsigned long long capacityBytes = 0;
    bool capacityAvailable = false;
    bool connected = false;
};

struct DeviceEnumerationResult {
    bool success = false;
    std::vector<DriveInfo> devices;
    std::string errorMessage;
};

class DeviceManager {
public:
    // Read-only enumeration of currently mounted removable volumes.
    DeviceEnumerationResult enumerateRemovableDrives() const;
};
