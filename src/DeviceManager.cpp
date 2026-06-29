#include "DeviceManager.h"
#include <windows.h>
#include <winioctl.h>
#include <string>

std::vector<DriveInfo> DeviceManager::listRemovableDrives() {
    std::vector<DriveInfo> drives;
    DWORD mask = GetLogicalDrives();

    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        if (!(mask & (1 << (letter - 'A')))) continue;

        std::string root = std::string(1, letter) + ":\\";
        UINT type = GetDriveTypeA(root.c_str());

        if (type == DRIVE_REMOVABLE) {
            DriveInfo info;
            info.letter = letter;
            info.model = "USB Drive";

            std::string volumePath = "\\\\.\\" + std::string(1, letter) + ":";
            HANDLE hVolume = CreateFileA(volumePath.c_str(), 0,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL, OPEN_EXISTING, 0, NULL);

            if (hVolume != INVALID_HANDLE_VALUE) {
                VOLUME_DISK_EXTENTS extents;
                DWORD bytesReturned;

                if (DeviceIoControl(hVolume,
                    IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                    NULL, 0, &extents, sizeof(extents),
                    &bytesReturned, NULL)) {

                    info.diskNumber = extents.Extents[0].DiskNumber;
                    info.path = "\\\\.\\PhysicalDrive" +
                                std::to_string(info.diskNumber);

                    HANDLE hPhysical = CreateFileA(info.path.c_str(), 0,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_EXISTING, 0, NULL);

                    if (hPhysical != INVALID_HANDLE_VALUE) {
                        GET_LENGTH_INFORMATION lengthInfo;
                        DWORD br;
                        if (DeviceIoControl(hPhysical,
                            IOCTL_DISK_GET_LENGTH_INFO,
                            NULL, 0, &lengthInfo,
                            sizeof(lengthInfo), &br, NULL)) {
                            info.sizeBytes = lengthInfo.Length.QuadPart;
                        }
                        CloseHandle(hPhysical);
                    }
                }
                CloseHandle(hVolume);
            }
            drives.push_back(info);
        }
    }
    return drives;
}