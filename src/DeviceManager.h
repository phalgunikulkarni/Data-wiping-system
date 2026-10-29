#pragma once
#include <vector>
#include <string>

struct DriveInfo {
    char letter = 0;
    std::string mountPoint;
    std::string volumeLabel;
    std::string fileSystem;
    std::string volumeGuid;
    unsigned long long capacityBytes = 0;
    bool capacityAvailable = false;
    unsigned int logicalSectorSize = 0;
    unsigned long long availableBytes = 0;
    bool availableBytesAvailable = false;
    unsigned long long totalFreeBytes = 0;
    bool totalFreeBytesAvailable = false;
    std::string deviceType;
    std::string physicalDiskNumber;
    std::string deviceModel;
    std::string busType;
    std::string partitionNumber;
    unsigned long long partitionOffsetBytes = 0;
    unsigned long long partitionLengthBytes = 0;
    bool partitionInfoAvailable = false;
    unsigned long long physicalDiskCapacityBytes = 0;
    bool physicalDiskCapacityAvailable = false;
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
