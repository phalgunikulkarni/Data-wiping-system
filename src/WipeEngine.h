#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <QString>
#include <QFile>

enum class WipeMethod {
    ZEROS,
    DOD_3PASS,
    DOD_7PASS,
    RANDOM
};

struct WipeResult {
    bool success;
    unsigned long long bytesWiped;
    unsigned long long totalBytes;
    std::string errorMessage;
    int passesCompleted;
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

    WipeResult wipeFile(
    const QString& filePath,
    WipeMethod method
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
        unsigned long long startOffset = 0
    );

    bool verifyWipe(
        HANDLE hDrive,
        unsigned long long startOffset,
        unsigned long long totalBytes,
        unsigned char expectedByte
    );
};