#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Offsets passed to the FAT/exFAT layer are relative to the start of a volume.
struct VolumeRelativeExtent {
    std::uint64_t offsetBytes = 0;
    std::uint64_t lengthBytes = 0;
};

// Snapshot captured from read-only Windows metadata. A backend must revalidate
// this snapshot immediately before locking or performing any I/O.
struct StorageTargetDescriptor {
    std::string volumeId;
    std::uint32_t physicalDiskNumber = UINT32_MAX;
    std::uint64_t partitionOffsetBytes = 0;
    std::uint64_t partitionLengthBytes = 0;
    std::uint64_t volumeCapacityBytes = 0;
    std::uint64_t physicalDiskCapacityBytes = 0;
    std::uint32_t logicalSectorSize = 0;
};

struct PhysicalExtent {
    std::uint64_t offsetBytes = 0;
    std::uint64_t lengthBytes = 0;
};

struct ExtentMappingResult {
    bool success = false;
    std::vector<PhysicalExtent> extents;
    std::string errorMessage;
};

ExtentMappingResult mapVolumeExtentsToPhysical(
    const StorageTargetDescriptor& target,
    const std::vector<VolumeRelativeExtent>& extents);

// This interface is deliberately small so destructive paths can be tested
// against a fake and cannot fall through to a real disk in unit tests.
class IStorageDevice {
public:
    virtual ~IStorageDevice() = default;
    // Must confirm that volumeId still maps to exactly this disk/partition and
    // that sector size and capacities are unchanged. Implementations must keep
    // their opened handles bound to that same device through the operation.
    virtual bool revalidate(const StorageTargetDescriptor& target, std::string& error) = 0;
    // Lock/unlock the volume handle, never the physical-disk handle.
    virtual bool lockVolume(std::string& error) = 0;
    virtual bool unlockVolume(std::string& error) noexcept = 0;
    // Return true only when exactly length bytes were transferred.
    virtual bool readAt(std::uint64_t offset, unsigned char* data,
                        std::uint32_t length, std::string& error) = 0;
    virtual bool writeAt(std::uint64_t offset, const unsigned char* data,
                         std::uint32_t length, std::string& error) = 0;
    virtual bool flush(std::string& error) = 0;
};
