#include "StorageSafety.h"

#include <algorithm>
#include <limits>

ExtentMappingResult mapVolumeExtentsToPhysical(
    const StorageTargetDescriptor& target,
    const std::vector<VolumeRelativeExtent>& extents)
{
    ExtentMappingResult result;
    const auto fail = [&result](const char* message) {
        result.extents.clear();
        result.errorMessage = message;
        return result;
    };
    const std::uint32_t sector = target.logicalSectorSize;
    if (target.volumeId.empty() || target.physicalDiskNumber == UINT32_MAX ||
        target.partitionLengthBytes == 0 || target.volumeCapacityBytes == 0 ||
        target.physicalDiskCapacityBytes == 0 || sector < 512 || sector > 65536 ||
        (sector & (sector - 1)) != 0)
        return fail("Target identity or geometry is incomplete");
    if (target.partitionOffsetBytes > target.physicalDiskCapacityBytes ||
        target.partitionLengthBytes > target.physicalDiskCapacityBytes - target.partitionOffsetBytes ||
        target.volumeCapacityBytes > target.partitionLengthBytes)
        return fail("Volume/partition geometry exceeds the physical disk boundary");
    const std::uint64_t partitionEnd = target.partitionOffsetBytes + target.partitionLengthBytes;
    if (target.partitionOffsetBytes % sector != 0 || partitionEnd % sector != 0)
        return fail("Partition boundaries are not sector aligned");
    if (extents.empty()) return fail("No volume-relative extents were supplied");

    result.extents.reserve(extents.size());
    for (const VolumeRelativeExtent& extent : extents) {
        if (extent.lengthBytes == 0 || extent.offsetBytes > target.volumeCapacityBytes ||
            extent.lengthBytes > target.volumeCapacityBytes - extent.offsetBytes)
            return fail("Volume-relative extent exceeds the validated volume boundary");
        if (extent.offsetBytes > std::numeric_limits<std::uint64_t>::max() - target.partitionOffsetBytes)
            return fail("Volume-to-physical offset conversion overflowed");
        const std::uint64_t physicalOffset = target.partitionOffsetBytes + extent.offsetBytes;
        if (physicalOffset > target.physicalDiskCapacityBytes ||
            extent.lengthBytes > target.physicalDiskCapacityBytes - physicalOffset)
            return fail("Mapped extent exceeds the physical disk boundary");
        const std::uint64_t physicalEnd = physicalOffset + extent.lengthBytes;
        const std::uint64_t alignedStart = physicalOffset - (physicalOffset % sector);
        const std::uint64_t alignedEnd = physicalEnd % sector == 0
            ? physicalEnd
            : physicalEnd + (sector - physicalEnd % sector);
        if (alignedEnd < physicalEnd || alignedStart < target.partitionOffsetBytes || alignedEnd > partitionEnd)
            return fail("Sector-aligned I/O would cross the validated partition boundary");
        result.extents.push_back({physicalOffset, extent.lengthBytes});
    }

    for (std::size_t i = 0; i < result.extents.size(); ++i) {
        const std::uint64_t aEnd = result.extents[i].offsetBytes + result.extents[i].lengthBytes;
        for (std::size_t j = i + 1; j < result.extents.size(); ++j) {
            const std::uint64_t bEnd = result.extents[j].offsetBytes + result.extents[j].lengthBytes;
            if (result.extents[i].offsetBytes < bEnd && result.extents[j].offsetBytes < aEnd)
                return fail("Mapped extents overlap");
        }
    }
    result.success = true;
    return result;
}
