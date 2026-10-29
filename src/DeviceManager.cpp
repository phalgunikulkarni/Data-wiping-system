#include "DeviceManager.h"
#include <windows.h>
#include <winioctl.h>
#include <string>
#include <utility>
#include <vector>
#include <cstring>

namespace {
std::string narrow(const wchar_t* value)
{
    if (!value || !*value) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), bytes, nullptr, nullptr);
    result.pop_back();
    return result;
}

class ReadHandle {
public:
    explicit ReadHandle(const std::wstring& path)
        : handle(CreateFileW(path.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
    ~ReadHandle() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    HANDLE get() const { return handle; }
private:
    HANDLE handle;
};

std::string busTypeName(STORAGE_BUS_TYPE type)
{
    switch (type) {
    case BusTypeUsb: return "USB";
    case BusTypeSata: return "SATA";
    case BusTypeNvme: return "NVMe";
    case BusTypeAta: return "ATA";
    case BusTypeSas: return "SAS";
    case BusTypeScsi: return "SCSI";
    case BusTypeVirtual: return "Virtual";
    case BusTypeRAID: return "RAID";
    case BusTypeSd: return "SD";
    case BusTypeMmc: return "MMC";
    default: return {};
    }
}

void readPhysicalMetadata(DWORD diskNumber, DriveInfo& info)
{
    info.physicalDiskNumber = std::to_string(diskNumber);
    ReadHandle disk(L"\\\\.\\PhysicalDrive" + std::to_wstring(diskNumber));
    if (disk.get() == INVALID_HANDLE_VALUE) return;

    std::vector<unsigned char> descriptorBuffer(sizeof(STORAGE_DEVICE_DESCRIPTOR) + 4096);
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    DWORD returned = 0;
    if (DeviceIoControl(disk.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                        descriptorBuffer.data(), static_cast<DWORD>(descriptorBuffer.size()),
                        &returned, nullptr) && returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(descriptorBuffer.data());
        const char* base = reinterpret_cast<const char*>(descriptorBuffer.data());
        if (descriptor->ProductIdOffset && descriptor->ProductIdOffset < returned) {
            const char* model = base + descriptor->ProductIdOffset;
            const std::size_t remaining = returned - descriptor->ProductIdOffset;
            const void* end = std::memchr(model, '\0', remaining);
            if (end)
                info.deviceModel.assign(model, static_cast<const char*>(end) - model);
        }
        info.busType = busTypeName(descriptor->BusType);
    }

    std::vector<unsigned char> geometryBuffer(sizeof(DISK_GEOMETRY_EX) + 128);
    if (DeviceIoControl(disk.get(), IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0,
                        geometryBuffer.data(), static_cast<DWORD>(geometryBuffer.size()),
                        &returned, nullptr) && returned >= sizeof(DISK_GEOMETRY_EX)) {
        const auto* geometry = reinterpret_cast<const DISK_GEOMETRY_EX*>(geometryBuffer.data());
        if (geometry->DiskSize.QuadPart >= 0) {
            info.physicalDiskCapacityBytes = static_cast<unsigned long long>(geometry->DiskSize.QuadPart);
            info.physicalDiskCapacityAvailable = true;
        }
    }
}

void readPartitionAndDiskMetadata(const std::wstring& volumePath, DriveInfo& info)
{
    ReadHandle volume(volumePath);
    if (volume.get() == INVALID_HANDLE_VALUE) return;

    PARTITION_INFORMATION_EX partition{};
    DWORD returned = 0;
    if (DeviceIoControl(volume.get(), IOCTL_DISK_GET_PARTITION_INFO_EX, nullptr, 0,
                        &partition, sizeof(partition), &returned, nullptr) &&
        returned >= sizeof(partition)) {
        info.partitionNumber = std::to_string(partition.PartitionNumber);
        if (partition.PartitionLength.QuadPart >= 0 && partition.StartingOffset.QuadPart >= 0) {
            info.partitionOffsetBytes = static_cast<unsigned long long>(partition.StartingOffset.QuadPart);
            info.partitionLengthBytes = static_cast<unsigned long long>(partition.PartitionLength.QuadPart);
            info.partitionInfoAvailable = true;
        }
    }

    DWORD bytes = sizeof(VOLUME_DISK_EXTENTS) + 8 * sizeof(DISK_EXTENT);
    std::vector<unsigned char> extentsBuffer(bytes);
    BOOL gotExtents = DeviceIoControl(volume.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
                                      extentsBuffer.data(), bytes, &returned, nullptr);
    if (!gotExtents && GetLastError() == ERROR_MORE_DATA && returned > bytes) {
        bytes = returned;
        extentsBuffer.resize(bytes);
        gotExtents = DeviceIoControl(volume.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
                                     extentsBuffer.data(), bytes, &returned, nullptr);
    }
    if (gotExtents &&
        returned >= sizeof(VOLUME_DISK_EXTENTS)) {
        const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(extentsBuffer.data());
        if (extents->NumberOfDiskExtents == 1) {
            readPhysicalMetadata(extents->Extents[0].DiskNumber, info);
        } else if (extents->NumberOfDiskExtents > 1) {
            for (DWORD index = 0; index < extents->NumberOfDiskExtents; ++index) {
                if (!info.physicalDiskNumber.empty()) info.physicalDiskNumber += ", ";
                info.physicalDiskNumber += std::to_string(extents->Extents[index].DiskNumber);
            }
        }
    }
}
}

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
            info.deviceType = "Removable volume";

            char volumeLabel[MAX_PATH + 1] = {};
            char fileSystem[MAX_PATH + 1] = {};
            const bool volumeInfoAvailable = GetVolumeInformationA(
                root.c_str(), volumeLabel, MAX_PATH + 1,
                nullptr, nullptr, nullptr, fileSystem, MAX_PATH + 1) != FALSE;
            if (volumeInfoAvailable) {
                info.volumeLabel = volumeLabel;
                info.fileSystem = fileSystem;
            }

            wchar_t volumeName[MAX_PATH + 1] = {};
            if (GetVolumeNameForVolumeMountPointW(
                    (std::wstring(1, static_cast<wchar_t>(letter)) + L":\\").c_str(),
                    volumeName, MAX_PATH + 1)) {
                info.volumeGuid = narrow(volumeName);
            }

            ULARGE_INTEGER availableBytes{};
            ULARGE_INTEGER totalBytes{};
            ULARGE_INTEGER totalFreeBytes{};
            if (GetDiskFreeSpaceExA(root.c_str(), &availableBytes,
                                    &totalBytes, &totalFreeBytes)) {
                info.capacityBytes = totalBytes.QuadPart;
                info.capacityAvailable = true;
                info.availableBytes = availableBytes.QuadPart;
                info.availableBytesAvailable = true;
            }
            DWORD sectorsPerCluster = 0;
            DWORD bytesPerSector = 0;
            DWORD freeClusters = 0;
            DWORD totalClusters = 0;
            if (GetDiskFreeSpaceA(root.c_str(), &sectorsPerCluster, &bytesPerSector,
                                  &freeClusters, &totalClusters) && bytesPerSector > 0) {
                info.logicalSectorSize = bytesPerSector;
            }

            // A removable drive letter can exist without mounted media. Only
            // present a volume as connected when a read-only metadata query worked.
            if (!volumeInfoAvailable && !info.capacityAvailable)
                continue;

            const std::wstring volumePath = L"\\\\.\\" + std::wstring(1, static_cast<wchar_t>(letter)) + L":";
            readPartitionAndDiskMetadata(volumePath, info);

            info.connected = true;
            result.devices.push_back(std::move(info));
        }
    }
    return result;
}
