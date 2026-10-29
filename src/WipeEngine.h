#pragma once
#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <QString>
#include "StorageSafety.h"

enum class WipeMethod {
    ZEROS,
    DOD_3PASS,
    DOD_7PASS,
    RANDOM
};

enum class WipeStatus {
    Success,
    PartialFailure,
    InvalidExtent,
    OpenFailure,
    LockFailure,
    WriteFailure,
    Unsupported
};

enum class VerificationStatus {
    Verified,
    Failed,
    Error
};

struct VerificationResult {
    VerificationStatus status = VerificationStatus::Error;
    std::string errorMessage;
};

struct ExtentReadResult {
    bool success = false;
    std::vector<unsigned char> data;
    std::string errorMessage;
};

struct WipeResult {
    bool success = false;
    unsigned long long bytesWiped = 0;
    unsigned long long totalBytes = 0;
    std::string errorMessage;
    std::string finalDataSha256;
    int passesCompleted = 0;
    WipeStatus status = WipeStatus::Success;
    bool verificationPerformed = false;
    bool verified = false;
};


class WipeEngine {
public:
    // Full drive wipe
    WipeResult wipe(
        const std::string& physicalPath,
        unsigned long long totalBytes,
        WipeMethod method,
        std::function<void(int, int, int)> progressCallback
    );

    WipeResult wipeSectors(
        const std::string& physicalPath,
        unsigned long long startSector,
        unsigned long long numSectors,
        WipeMethod method,
        std::function<void(int, int, int)> progressCallback
    );

    WipeResult wipeExtents(
    const std::string& physicalPath,
    const std::string& volumePath,
    const std::vector<std::pair<unsigned long long, unsigned long long>>& extents,
    WipeMethod method,
    std::function<void(int, int, int)> progressCallback
);

    VerificationResult verifyExtents(
        const std::string& physicalPath,
        const std::vector<std::pair<unsigned long long, unsigned long long>>& extents,
        WipeMethod method,
        const std::string& expectedFinalSha256
    );

    WipeResult wipeVolumeExtents(
        const StorageTargetDescriptor& target,
        const std::vector<VolumeRelativeExtent>& extents,
        WipeMethod method,
        IStorageDevice& device,
        std::function<void(int, int, int)> progressCallback = {});

    ExtentReadResult readExtents(
        const std::string& physicalPath,
        const std::vector<std::pair<unsigned long long, unsigned long long>>& extents,
        unsigned long long maximumBytes
    );

    WipeResult wipeFile(
    const QString& filePath,
    WipeMethod method
    );

    WipeResult wipeFile(
    const QString& filePath,
    WipeMethod method,
    std::function<void(int, int, int)> progressCallback
    );

    WipeResult wipeFolder(
    const QString& folderPath,
    WipeMethod method
    );

};
