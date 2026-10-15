#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <QString>
#include <QFile>

class QCryptographicHash;

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
    WriteFailure
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
    bool success;
    unsigned long long bytesWiped;
    unsigned long long totalBytes;
    std::string errorMessage;
    std::string finalDataSha256;
    int passesCompleted;
    WipeStatus status = WipeStatus::Success;
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

private:
    bool verifyZeroPass(
    const QString& filePath
    );

    bool writePass(
        HANDLE hDrive,
        unsigned long long totalBytes,
        unsigned char fillByte,
        bool useRandom,
        int passNumber,
        int totalPasses,
        std::function<void(int, int, int)> progressCallback,
        unsigned long long startOffset = 0,
        QCryptographicHash* finalPassHash = nullptr
    );

    bool verifyWipe(
        HANDLE hDrive,
        unsigned long long startOffset,
        unsigned long long totalBytes,
        unsigned char expectedByte
    );
};