#include "DeviceManager.h"
#include <windows.h>
#include <string>
#include <utility>

DeviceEnumerationResult DeviceManager::enumerateRemovableDrives() const {
    DeviceEnumerationResult result;
    SetLastError(ERROR_SUCCESS);
    DWORD mask = GetLogicalDrives();
    const DWORD enumerationError = GetLastError();
    if (mask == 0 && enumerationError != ERROR_SUCCESS) {
        result.errorMessage = "Windows could not enumerate mounted drive letters. Error: " +
                              std::to_string(enumerationError);
        return result;
    }

    result.success = true;

    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        if (!(mask & (1 << (letter - 'A')))) continue;

        std::string root = std::string(1, letter) + ":\\";
        UINT type = GetDriveTypeA(root.c_str());

        if (type == DRIVE_REMOVABLE) {
            DriveInfo info{};
            info.letter = letter;
            info.mountPoint = root;
            info.connected = true;

            char volumeLabel[MAX_PATH + 1] = {};
            char fileSystem[MAX_PATH + 1] = {};
            const bool volumeInfoAvailable = GetVolumeInformationA(
                root.c_str(), volumeLabel, MAX_PATH + 1,
                nullptr, nullptr, nullptr, fileSystem, MAX_PATH + 1) != FALSE;
            if (volumeInfoAvailable) {
                info.volumeLabel = volumeLabel;
                info.fileSystem = fileSystem;
            }

            ULARGE_INTEGER availableBytes{};
            ULARGE_INTEGER totalBytes{};
            ULARGE_INTEGER totalFreeBytes{};
            if (GetDiskFreeSpaceExA(root.c_str(), &availableBytes,
                                    &totalBytes, &totalFreeBytes)) {
                info.capacityBytes = totalBytes.QuadPart;
                info.capacityAvailable = true;
            }

            // A removable drive letter can exist without mounted media. Only
            // present a volume as connected when a read-only metadata query worked.
            if (!volumeInfoAvailable && !info.capacityAvailable)
                continue;

            info.connected = true;
            result.devices.push_back(std::move(info));
        }
    }
    return result;
}
