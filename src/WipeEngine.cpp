#include "WipeEngine.h"
#include "Logger.h"
#include <windows.h>
#include <random>
#include <vector>
#include <cstring>
#include <cstdint>
#include <QCryptographicHash>
#include <QFile>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
static const DWORD SECTOR_SIZE = 4096; // Use 4K sectors for modern drives
static const DWORD BUFFER_SECTORS = 256; // 1MB buffer
static const DWORD BUFFER_SIZE = SECTOR_SIZE * BUFFER_SECTORS;

bool WipeEngine::writePass(
    HANDLE hDrive,
    unsigned long long totalBytes,
    unsigned char fillByte,
    bool useRandom,
    int passNumber,
    int totalPasses,
    std::function<void(int, int, int)> progressCallback,
    unsigned long long startOffset)
{
    // Seek to start position
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)startOffset;
    if (!SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN)) {
        return false;
    }

    // Allocate aligned buffer
    std::vector<unsigned char> buffer(BUFFER_SIZE);

    std::random_device rd;
    std::mt19937_64 rng(rd() | ((std::uint64_t)rd() << 32));
    std::uniform_int_distribution<int> dist(0, 255);

    unsigned long long bytesWritten = 0;
    int lastPercent = -1;

    while (bytesWritten < totalBytes) {
        // Fill buffer
        if (useRandom) {
            for (auto& b : buffer) b = (unsigned char)dist(rng);
        } else {
            std::fill(buffer.begin(), buffer.end(), fillByte);
        }

        // How much to write this iteration
        unsigned long long remaining = totalBytes - bytesWritten;
        DWORD toWrite = (remaining >= BUFFER_SIZE)
                        ? BUFFER_SIZE
                        : (DWORD)(remaining);

        // Align to sector size
        toWrite = (toWrite / SECTOR_SIZE) * SECTOR_SIZE;
        if (toWrite == 0) {
            // Handle remainder
            if (remaining > 0 && remaining < SECTOR_SIZE) {
                toWrite = (DWORD)remaining;
                toWrite = ((toWrite + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE;
            }
        }
        if (toWrite == 0) break;

        DWORD written = 0;
        BOOL ok = WriteFile(hDrive, buffer.data(), toWrite, &written, NULL);
        if (!ok) {
            return false;
        }
        if (written == 0) break;

        bytesWritten += written;

        // Report progress
        int percent = (int)((double)bytesWritten / totalBytes * 100);
        if (percent != lastPercent) {
            lastPercent = percent;
            progressCallback(percent, passNumber, totalPasses);
        }
    }

    // Flush to disk
    FlushFileBuffers(hDrive);
    return true;
}

bool WipeEngine::verifyWipe(
    HANDLE hDrive,
    unsigned long long startOffset,
    unsigned long long totalBytes,
    unsigned char expectedByte)
{
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)startOffset;
    SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN);

    std::vector<unsigned char> buffer(BUFFER_SIZE);
    unsigned long long bytesRead = 0;

    while (bytesRead < totalBytes) {
        unsigned long long remaining = totalBytes - bytesRead;
        DWORD toRead = (remaining >= BUFFER_SIZE)
                       ? BUFFER_SIZE
                       : (DWORD)(remaining);

        DWORD read = 0;
        if (!ReadFile(hDrive, buffer.data(), toRead, &read, NULL)) {
            return false;
        }
        if (read == 0) break;

        // Check if all bytes match expected value
        for (DWORD i = 0; i < read; i++) {
            if (buffer[i] != expectedByte) {
                return false;
            }
        }

        bytesRead += read;
    }

    return true;
}

WipeResult WipeEngine::wipe(
    const std::string& physicalPath,
    unsigned long long totalBytes,
    WipeMethod method,
    std::function<void(int, int, int)> progressCallback)
{
    return wipeSectors(physicalPath, 0, totalBytes, method, progressCallback);
}

WipeResult WipeEngine::wipeSectors(
    const std::string& physicalPath,
    unsigned long long startSector,
    unsigned long long numSectors,
    WipeMethod method,
    std::function<void(int, int, int)> progressCallback)
{
    WipeResult result;
    result.success = false;
    result.bytesWiped = 0;
    result.totalBytes = numSectors;
    result.passesCompleted = 0;

    // Open drive for raw write
    HANDLE hDrive = CreateFileA(
        physicalPath.c_str(),
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
        NULL
    );

    if (hDrive == INVALID_HANDLE_VALUE) {
        result.errorMessage = "Failed to open drive. Run as Administrator. Error: " +
                             std::to_string(GetLastError());
        return result;
    }

    // Lock the volume
    DWORD br;
    BOOL lockOk = DeviceIoControl(hDrive, FSCTL_LOCK_VOLUME,
                                  NULL, 0, NULL, 0, &br, NULL);
    
    if (!lockOk) {
        // Try to continue anyway (may fail on system drive)
        result.errorMessage = "Warning: Could not lock volume. ";
    }

    bool ok = false;
    unsigned long long startOffset = startSector * SECTOR_SIZE;

    switch (method) {
        case WipeMethod::ZEROS:
            ok = writePass(hDrive, numSectors, 0x00,
                          false, 1, 1, progressCallback, startOffset);
            result.passesCompleted = ok ? 1 : 0;
            break;

        case WipeMethod::RANDOM:
            ok = writePass(hDrive, numSectors, 0x00,
                          true, 1, 1, progressCallback, startOffset);
            result.passesCompleted = ok ? 1 : 0;
            break;

        case WipeMethod::DOD_3PASS:
            ok = writePass(hDrive, numSectors, 0x00,
                          false, 1, 3, progressCallback, startOffset); // pass 1: zeros
            result.passesCompleted = ok ? 1 : 0;
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0xFF,
                              false, 2, 3, progressCallback, startOffset); // pass 2: ones
                result.passesCompleted = ok ? 2 : 1;
            }
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0x00,
                              true,  3, 3, progressCallback, startOffset); // pass 3: random
                result.passesCompleted = ok ? 3 : 2;
            }
            break;

        case WipeMethod::DOD_7PASS:
            ok = writePass(hDrive, numSectors, 0x00, false, 1, 7, progressCallback, startOffset);
            result.passesCompleted = ok ? 1 : 0;
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0xFF, false, 2, 7, progressCallback, startOffset);
                result.passesCompleted = ok ? 2 : 1;
            }
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0x00, true,  3, 7, progressCallback, startOffset);
                result.passesCompleted = ok ? 3 : 2;
            }
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0x96, false, 4, 7, progressCallback, startOffset);
                result.passesCompleted = ok ? 4 : 3;
            }
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0x00, true,  5, 7, progressCallback, startOffset);
                result.passesCompleted = ok ? 5 : 4;
            }
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0xFF, true,  6, 7, progressCallback, startOffset);
                result.passesCompleted = ok ? 6 : 5;
            }
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0x00, true,  7, 7, progressCallback, startOffset);
                result.passesCompleted = ok ? 7 : 6;
            }
            break;
    }

    // Try to unlock volume
    DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME,
                    NULL, 0, NULL, 0, &br, NULL);

    // Verification (optional - for full wipe only)
    if (ok && startSector == 0) {
        // Only verify on full wipes
        // if (!verifyWipe(hDrive, startOffset, numSectors, 0x00)) {
        //     result.errorMessage = "Verification failed";
        //     ok = false;
        // }
    }

    CloseHandle(hDrive);

    result.success = ok;
    result.bytesWiped = ok ? numSectors : 0;
    
    if (!ok && result.errorMessage.empty()) {
        result.errorMessage = "Write operation failed. Error code: " +
                             std::to_string(GetLastError());
    }

    return result;
}

WipeResult WipeEngine::wipeFile(
    const QString& filePath,
    WipeMethod method)
{
    WipeResult result{};
    result.success = false;

    QFile file(filePath);

    if (!file.exists()) {
        result.errorMessage = "File does not exist";
        return result;
    }

    qint64 fileSize = file.size();

    if (!file.open(QIODevice::ReadWrite)) {
        result.errorMessage = "Unable to open file";
        return result;
    }

    int passes = 1;

    if (method == WipeMethod::DOD_3PASS)
        passes = 3;
    else if (method == WipeMethod::DOD_7PASS)
        passes = 7;

    QByteArray buffer(8192, 0);

    std::random_device rd;
    std::mt19937 gen(rd());

    for (int pass = 0; pass < passes; pass++) {

        file.seek(0);

        qint64 remaining = fileSize;

        while (remaining > 0) {

            int chunk =
                (remaining > buffer.size())
                ? buffer.size()
                : (int)remaining;

            if (method == WipeMethod::RANDOM ||
                pass == passes - 1)
            {
                for (int i = 0; i < chunk; i++)
                    buffer[i] = static_cast<char>(gen() % 256);
            }
            else if (pass % 2 == 0)
            {
                memset(buffer.data(), 0x00, chunk);
            }
            else
            {
                memset(buffer.data(), 0xFF, chunk);
            }

            file.write(buffer.constData(), chunk);

            remaining -= chunk;
        }

        file.flush();
    }

    file.close();

// VERIFY BEFORE DELETE
if (method == WipeMethod::ZEROS)
{
    if (!verifyZeroPass(filePath))
    {
        result.errorMessage =
            "Verification Failed";

        return result;
    }
}

bool deleted = QFile::remove(filePath);

    result.success = deleted;
    result.bytesWiped = fileSize;
    result.totalBytes = fileSize;
    result.passesCompleted = passes;

    if (!deleted)
        result.errorMessage = "File overwrite succeeded but deletion failed";

    return result;
}

WipeResult WipeEngine::wipeFolder(
    const QString& folderPath,
    WipeMethod method)
{
    WipeResult result{};

    QDir dir(folderPath);

    if (!dir.exists()) {
        result.errorMessage = "Folder not found";
        return result;
    }

    QDirIterator it(
        folderPath,
        QDir::Files,
        QDirIterator::Subdirectories
    );

    unsigned long long totalBytes = 0;
    bool allSuccess = true;

    while (it.hasNext()) {

        QString file = it.next();

        auto r = wipeFile(file, method);

        totalBytes += r.bytesWiped;

        if (!r.success)
            allSuccess = false;
    }

    dir.removeRecursively();

    result.success = allSuccess;
    result.bytesWiped = totalBytes;
    result.totalBytes = totalBytes;

    return result;
}

bool WipeEngine::verifyZeroPass(
    const QString& filePath)
{
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    while (!file.atEnd())
    {
        QByteArray chunk = file.read(8192);

        for (char c : chunk)
        {
            if ((unsigned char)c != 0x00)
                return false;
        }
    }

    return true;
}