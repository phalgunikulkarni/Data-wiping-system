#include "WipeEngine.h"
#include <QCryptographicHash>
#include <algorithm>
#include <limits>
#include <mutex>
#include <random>
#include <vector>

namespace {
std::mutex g_storageOperationMutex;
WipeResult disabledWipe(const char* message)
{
    WipeResult result{};
    result.status = WipeStatus::Unsupported;
    result.errorMessage = message;
    return result;
}
}

WipeResult WipeEngine::wipe(const std::string&, unsigned long long, WipeMethod,
                            std::function<void(int, int, int)>)
{
    return disabledWipe("Raw physical-path wipe is disabled; use a revalidated storage target");
}

WipeResult WipeEngine::wipeSectors(const std::string&, unsigned long long,
                                   unsigned long long, WipeMethod,
                                   std::function<void(int, int, int)>)
{
    return disabledWipe("Raw sector wipe is disabled; use a revalidated storage target");
}

WipeResult WipeEngine::wipeExtents(const std::string&, const std::string&,
                                   const std::vector<std::pair<unsigned long long, unsigned long long>>&,
                                   WipeMethod, std::function<void(int, int, int)>)
{
    return disabledWipe("Ambiguous raw extents are disabled; use validated volume-relative extents");
}
WipeResult WipeEngine::wipeVolumeExtents(
    const StorageTargetDescriptor& target,
    const std::vector<VolumeRelativeExtent>& extents,
    WipeMethod method,
    IStorageDevice& device,
    std::function<void(int, int, int)> progressCallback)
{
    WipeResult result{};
    result.status = WipeStatus::InvalidExtent;
    const ExtentMappingResult mapping = mapVolumeExtentsToPhysical(target, extents);
    if (!mapping.success) {
        result.errorMessage = mapping.errorMessage;
        return result;
    }
    for (const auto& extent : mapping.extents) {
        if (extent.lengthBytes > std::numeric_limits<unsigned long long>::max() - result.totalBytes) {
            result.errorMessage = "Total extent size overflow";
            return result;
        }
        result.totalBytes += extent.lengthBytes;
    }
    int passCount = 1;
    if (method == WipeMethod::DOD_3PASS) passCount = 3;
    else if (method == WipeMethod::DOD_7PASS) passCount = 7;
    else if (method != WipeMethod::ZEROS && method != WipeMethod::RANDOM) {
        result.errorMessage = "Unsupported wipe method";
        return result;
    }

    std::lock_guard<std::mutex> operationLock(g_storageOperationMutex);
    if (!device.revalidate(target, result.errorMessage)) {
        if (result.errorMessage.empty()) result.errorMessage = "Selected storage target could not be revalidated";
        result.status = WipeStatus::OpenFailure;
        return result;
    }
    if (!device.lockVolume(result.errorMessage)) {
        if (result.errorMessage.empty()) result.errorMessage = "Could not lock the selected volume";
        result.status = WipeStatus::LockFailure;
        return result;
    }
    bool locked = true;
    const auto unlock = [&]() {
        if (!locked) return true;
        locked = false;
        std::string unlockError;
        const bool ok = device.unlockVolume(unlockError);
        if (!ok && result.errorMessage.empty())
            result.errorMessage = unlockError.empty() ? "Could not unlock the selected volume" : unlockError;
        return ok;
    };

    const std::uint64_t sector = target.logicalSectorSize;
    const std::uint64_t partitionStart = target.partitionOffsetBytes;
    const std::uint64_t partitionEnd = partitionStart + target.partitionLengthBytes;
    if (!device.revalidate(target, result.errorMessage)) {
        if (result.errorMessage.empty()) result.errorMessage = "Selected storage target changed after volume lock";
        result.status = WipeStatus::OpenFailure;
        unlock();
        return result;
    }

    std::random_device randomDevice;
    std::mt19937_64 randomEngine(randomDevice() | (static_cast<std::uint64_t>(randomDevice()) << 32));
    std::uniform_int_distribution<unsigned int> randomByte(0, 255);
    QCryptographicHash expectedHash(QCryptographicHash::Sha256);
    std::vector<unsigned char> sectorBuffer(static_cast<std::size_t>(sector));
    std::vector<unsigned char> logicalBuffer(static_cast<std::size_t>(sector));
    unsigned long long bytesWrittenThisPass = 0;

    for (int pass = 1; pass <= passCount; ++pass) {
        bytesWrittenThisPass = 0;
        for (const PhysicalExtent& extent : mapping.extents) {
            std::uint64_t cursor = extent.offsetBytes;
            const std::uint64_t end = cursor + extent.lengthBytes;
            while (cursor < end) {
                const std::uint64_t sectorStart = cursor - (cursor % sector);
                const std::uint64_t inSector = cursor - sectorStart;
                const std::uint64_t chunk = std::min<std::uint64_t>(end - cursor, sector - inSector);
                if (sectorStart < partitionStart || sectorStart > partitionEnd || sector > partitionEnd - sectorStart) {
                    result.errorMessage = "Aligned write would cross the selected partition boundary";
                    result.status = WipeStatus::InvalidExtent;
                    unlock();
                    return result;
                }
                if (!device.readAt(sectorStart, sectorBuffer.data(), static_cast<std::uint32_t>(sector), result.errorMessage)) {
                    if (result.errorMessage.empty()) result.errorMessage = "Could not read the destination sector before update";
                    result.status = WipeStatus::WriteFailure;
                    unlock();
                    return result;
                }
                bool useRandom = method == WipeMethod::RANDOM;
                unsigned char fill = 0;
                if (method == WipeMethod::DOD_3PASS) {
                    fill = pass == 2 ? 0xFF : 0;
                    useRandom = pass == 3;
                } else if (method == WipeMethod::DOD_7PASS) {
                    fill = pass == 2 ? 0xFF : (pass == 4 ? 0x96 : 0);
                    useRandom = pass == 3 || pass == 5 || pass == 6 || pass == 7;
                }
                for (std::uint64_t i = 0; i < chunk; ++i) {
                    logicalBuffer[static_cast<std::size_t>(i)] = useRandom
                        ? static_cast<unsigned char>(randomByte(randomEngine)) : fill;
                }
                std::copy_n(logicalBuffer.data(), static_cast<std::size_t>(chunk),
                            sectorBuffer.data() + static_cast<std::size_t>(inSector));
                if (!device.writeAt(sectorStart, sectorBuffer.data(), static_cast<std::uint32_t>(sector), result.errorMessage)) {
                    if (result.errorMessage.empty()) result.errorMessage = "Storage write failed";
                    result.status = WipeStatus::WriteFailure;
                    unlock();
                    return result;
                }
                if (pass == passCount)
                    expectedHash.addData(reinterpret_cast<const char*>(logicalBuffer.data()), static_cast<int>(chunk));
                cursor += chunk;
                bytesWrittenThisPass += chunk;
                if (progressCallback) {
                    const long double done = static_cast<long double>(pass - 1) * result.totalBytes + bytesWrittenThisPass;
                    const int percent = std::min(99, static_cast<int>(done * 100.0L / (passCount * static_cast<long double>(result.totalBytes))));
                    progressCallback(percent, pass, passCount);
                }
            }
        }
        if (!device.flush(result.errorMessage)) {
            if (result.errorMessage.empty()) result.errorMessage = "Could not flush storage writes";
            result.status = WipeStatus::WriteFailure;
            unlock();
            return result;
        }
        result.passesCompleted = pass;
    }

    result.finalDataSha256 = expectedHash.result().toHex().toStdString();
    result.verificationPerformed = true;
    QCryptographicHash actualHash(QCryptographicHash::Sha256);
    std::vector<unsigned char> readBuffer(65536);
    for (const PhysicalExtent& extent : mapping.extents) {
        std::uint64_t cursor = extent.offsetBytes;
        std::uint64_t remaining = extent.lengthBytes;
        while (remaining > 0) {
            const std::uint32_t chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, readBuffer.size()));
            if (!device.readAt(cursor, readBuffer.data(), chunk, result.errorMessage)) {
                if (result.errorMessage.empty()) result.errorMessage = "Read-back verification failed";
                result.status = WipeStatus::WriteFailure;
                unlock();
                return result;
            }
            actualHash.addData(reinterpret_cast<const char*>(readBuffer.data()), static_cast<int>(chunk));
            cursor += chunk;
            remaining -= chunk;
        }
    }
    result.verified = actualHash.result().toHex().toStdString() == result.finalDataSha256;
    const bool unlockOk = unlock();
    if (!result.verified) {
        result.status = WipeStatus::WriteFailure;
        result.errorMessage = "Read-back content does not match the final-pass digest";
        return result;
    }
    if (!unlockOk) {
        result.status = WipeStatus::LockFailure;
        result.success = false;
        return result;
    }
    result.success = true;
    result.bytesWiped = result.totalBytes;
    result.status = WipeStatus::Success;
    if (progressCallback) progressCallback(100, passCount, passCount);
    return result;
}


VerificationResult WipeEngine::verifyExtents(const std::string&,
    const std::vector<std::pair<unsigned long long, unsigned long long>>&,
    WipeMethod, const std::string&)
{
    return {VerificationStatus::Error, "Raw-path verification is disabled because the target coordinate system is ambiguous"};
}

ExtentReadResult WipeEngine::readExtents(const std::string&,
    const std::vector<std::pair<unsigned long long, unsigned long long>>&,
    unsigned long long)
{
    return {false, {}, "Raw-path reads are disabled because the target coordinate system is ambiguous"};
}

WipeResult WipeEngine::wipeFile(const QString&, WipeMethod)
{
    return disabledWipe("File wiping is disabled until handle-based target revalidation and read-back verification are implemented");
}

WipeResult WipeEngine::wipeFile(const QString&, WipeMethod,
                                std::function<void(int, int, int)>)
{
    return disabledWipe("File wiping is disabled until handle-based target revalidation and read-back verification are implemented");
}

WipeResult WipeEngine::wipeFolder(const QString&, WipeMethod)
{
    return disabledWipe("Folder wiping is disabled until each nested target can be safely revalidated");
}