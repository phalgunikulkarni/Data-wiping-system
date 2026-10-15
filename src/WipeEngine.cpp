#include "WipeEngine.h"
#include "Logger.h"
#include <windows.h>
#include <winioctl.h>
#include <random>
#include <vector>
#include <cstring>
#include <cstdint>
#include <limits>
#include <memory>
#include <algorithm>
#include <QCryptographicHash>
#include <QFile>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>

namespace {

static const DWORD DEFAULT_BUFFER_SIZE = 65536;

static void alignedFree(void* ptr) {
    if (ptr) {
        _aligned_free(ptr);
    }
}

static std::unique_ptr<unsigned char, void(*)(void*)> makeAlignedBuffer(size_t size, size_t alignment) {
    void* raw = _aligned_malloc(size, alignment);
    if (!raw) {
        return {nullptr, alignedFree};
    }
    return {static_cast<unsigned char*>(raw), alignedFree};
}

static bool isValidSectorSize(DWORD sectorSize) {
    return sectorSize >= sizeof(void*) &&
           (sectorSize & (sectorSize - 1)) == 0;
}

static DWORD getDriveSectorSize(HANDLE hDrive) {
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageAccessAlignmentProperty;
    query.QueryType = PropertyStandardQuery;

    STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR alignment{};
    DWORD bytesReturned = 0;

    if (DeviceIoControl(hDrive,
                        IOCTL_STORAGE_QUERY_PROPERTY,
                        &query,
                        sizeof(query),
                        &alignment,
                        sizeof(alignment),
                        &bytesReturned,
                        NULL) &&
        bytesReturned >= static_cast<DWORD>(sizeof(alignment)) &&
        isValidSectorSize(alignment.BytesPerLogicalSector)) {
        return alignment.BytesPerLogicalSector;
    }

    DISK_GEOMETRY geometry{};

    if (DeviceIoControl(hDrive,
                        IOCTL_DISK_GET_DRIVE_GEOMETRY,
                        NULL,
                        0,
                        &geometry,
                        sizeof(geometry),
                        &bytesReturned,
                        NULL)) {
        if (isValidSectorSize(geometry.BytesPerSector)) {
            return geometry.BytesPerSector;
        }
    }

    DISK_GEOMETRY_EX geometryEx{};
    if (DeviceIoControl(hDrive,
                        IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                        NULL,
                        0,
                        &geometryEx,
                        sizeof(geometryEx),
                        &bytesReturned,
                        NULL)) {
        if (isValidSectorSize(geometryEx.Geometry.BytesPerSector)) {
            return geometryEx.Geometry.BytesPerSector;
        }
    }

    return 0;
}

static void updateHash(
    QCryptographicHash* hash,
    const unsigned char* data,
    unsigned long long length)
{
    if (!hash) {
        return;
    }

    while (length > 0) {
        const int chunkSize = static_cast<int>(std::min<unsigned long long>(
            length,
            static_cast<unsigned long long>(std::numeric_limits<int>::max())));
        hash->addData(reinterpret_cast<const char*>(data), chunkSize);
        data += chunkSize;
        length -= static_cast<unsigned long long>(chunkSize);
    }
}

static bool isRepresentableRawRange(
    unsigned long long offset,
    unsigned long long length);

static bool readLogicalExtent(
    HANDLE hRead,
    unsigned long long targetOffset,
    unsigned long long targetLength,
    unsigned long long deviceSize,
    DWORD sectorSize,
    const std::function<void(const unsigned char*, size_t)>& consume,
    std::string& errorMessage)
{
    if (targetLength == 0 || !isValidSectorSize(sectorSize) ||
        targetOffset > deviceSize || targetLength > deviceSize - targetOffset ||
        !isRepresentableRawRange(targetOffset, targetLength)) {
        errorMessage = "Invalid logical extent supplied for read-back";
        return false;
    }

    const size_t bufferSize = std::max<size_t>(DEFAULT_BUFFER_SIZE, sectorSize);
    auto buffer = makeAlignedBuffer(bufferSize, sectorSize);
    if (!buffer) {
        errorMessage = "Could not allocate an aligned verification buffer";
        return false;
    }

    unsigned long long currentOffset = targetOffset;
    unsigned long long remaining = targetLength;
    while (remaining > 0) {
        const unsigned long long physicalOffset =
            (currentOffset / sectorSize) * sectorSize;
        const unsigned long long inSector = currentOffset - physicalOffset;
        const unsigned long long logicalChunk = std::min<unsigned long long>(
            remaining,
            static_cast<unsigned long long>(bufferSize) - inSector);
        const unsigned long long unroundedSize = inSector + logicalChunk;
        const unsigned long long physicalReadSize =
            ((unroundedSize + sectorSize - 1) / sectorSize) * sectorSize;

        if (physicalOffset > deviceSize ||
            physicalReadSize > deviceSize - physicalOffset ||
            !isRepresentableRawRange(physicalOffset, physicalReadSize) ||
            physicalReadSize > bufferSize) {
            errorMessage = "Aligned verification read exceeds the device or buffer boundary";
            return false;
        }

        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(physicalOffset);
        if (!SetFilePointerEx(hRead, position, NULL, FILE_BEGIN)) {
            const DWORD seekError = GetLastError();
            errorMessage = "SetFilePointerEx failed at aligned read offset " +
                           std::to_string(physicalOffset) + "; Win32 error=" +
                           std::to_string(seekError);
            return false;
        }

        DWORD bytesRead = 0;
        const DWORD requested = static_cast<DWORD>(physicalReadSize);
        if (!ReadFile(hRead, buffer.get(), requested, &bytesRead, NULL)) {
            const DWORD readError = GetLastError();
            errorMessage = "ReadFile failed at aligned offset " +
                           std::to_string(physicalOffset) + "; requested=" +
                           std::to_string(requested) + ", bytesRead=" +
                           std::to_string(bytesRead) + ", Win32 error=" +
                           std::to_string(readError);
            return false;
        }
        if (bytesRead == 0) {
            errorMessage = "ReadFile returned success with zero bytes at aligned offset " +
                           std::to_string(physicalOffset) + "; requested=" +
                           std::to_string(requested);
            return false;
        }
        if (bytesRead != requested) {
            errorMessage = "Short aligned read at offset " +
                           std::to_string(physicalOffset) + "; requested=" +
                           std::to_string(requested) + ", bytesRead=" +
                           std::to_string(bytesRead);
            return false;
        }

        consume(buffer.get() + inSector, static_cast<size_t>(logicalChunk));
        currentOffset += logicalChunk;
        remaining -= logicalChunk;
    }

    return true;
}

static bool isRepresentableRawRange(
    unsigned long long offset,
    unsigned long long length)
{
    const unsigned long long maxOffset =
        static_cast<unsigned long long>(std::numeric_limits<LONGLONG>::max());
    return offset <= maxOffset && length <= maxOffset - offset;
}

static bool validateExtentList(
    const std::vector<std::pair<unsigned long long, unsigned long long>>& extents,
    unsigned long long& totalBytes,
    std::string& errorMessage)
{
    totalBytes = 0;
    if (extents.empty()) {
        errorMessage = "No physical extents provided";
        return false;
    }

    for (const auto& extent : extents) {
        if (extent.second == 0) {
            errorMessage = "Invalid extent: zero length";
            return false;
        }

        if (extent.first == 0) {
            errorMessage = "Invalid extent: zero offset";
            return false;
        }

        if (extent.first > std::numeric_limits<unsigned long long>::max() - extent.second) {
            errorMessage = "Invalid extent: offset/length overflow";
            return false;
        }

        if (!isRepresentableRawRange(extent.first, extent.second)) {
            errorMessage = "Invalid extent: range cannot be represented by raw I/O offsets";
            return false;
        }

        if (extent.second > std::numeric_limits<unsigned long long>::max() - totalBytes) {
            errorMessage = "Invalid extent list: total size overflow";
            totalBytes = 0;
            return false;
        }

        totalBytes += extent.second;
    }

    for (size_t i = 0; i < extents.size(); ++i) {
        const auto& a = extents[i];
        const unsigned long long aEnd = a.first + a.second;
        for (size_t j = i + 1; j < extents.size(); ++j) {
            const auto& b = extents[j];
            const unsigned long long bEnd = b.first + b.second;
            if (a.first < bEnd && b.first < aEnd) {
                errorMessage = "Invalid extent: overlapping extents";
                return false;
            }
        }
    }

    return true;
}

static bool writeSectorSafe(
    HANDLE hDrive,
    unsigned long long sectorOffset,
    unsigned long long targetOffset,
    unsigned long long targetEnd,
    DWORD sectorSize,
    unsigned char fillByte,
    bool useRandom,
    std::mt19937_64& rng,
    std::uniform_int_distribution<int>& dist,
    QCryptographicHash* finalPassHash)
{
    auto writeDebug = [&](const QString& message) {
        QFile debugFile("wipe_debug.txt");

        if (debugFile.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&debugFile);
            out << message << "\n";
            out << "-----------------------------\n";
            debugFile.close();
        }
    };

    if (sectorSize == 0 ||
        sectorOffset % sectorSize != 0 ||
        sectorOffset < targetOffset ||
        sectorOffset > targetEnd ||
        sectorSize > targetEnd - sectorOffset) {

        writeDebug(
            QString("writeSectorSafe validation failed\n"
                    "sectorOffset = %1\n"
                    "targetOffset = %2\n"
                    "targetEnd = %3\n"
                    "sectorSize = %4")
                .arg(sectorOffset)
                .arg(targetOffset)
                .arg(targetEnd)
                .arg(sectorSize)
        );

        return false;
    }

    auto buffer = makeAlignedBuffer(sectorSize, sectorSize);

    if (!buffer) {
        writeDebug(
            QString("writeSectorSafe buffer allocation failed\n"
                    "sectorOffset = %1\n"
                    "sectorSize = %2")
                .arg(sectorOffset)
                .arg(sectorSize)
        );

        return false;
    }

    LARGE_INTEGER pos{};
    pos.QuadPart = static_cast<LONGLONG>(sectorOffset);

    if (!SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN)) {
        const DWORD errorCode = GetLastError();

        writeDebug(
            QString("writeSectorSafe FIRST SetFilePointerEx failed\n"
                    "sectorOffset = %1\n"
                    "error = %2")
                .arg(sectorOffset)
                .arg(errorCode)
        );

        return false;
    }

    DWORD bytesRead = 0;

    if (!ReadFile(
            hDrive,
            buffer.get(),
            sectorSize,
            &bytesRead,
            NULL)) {

        const DWORD errorCode = GetLastError();

        writeDebug(
            QString("writeSectorSafe ReadFile failed\n"
                    "sectorOffset = %1\n"
                    "sectorSize = %2\n"
                    "bytesRead = %3\n"
                    "error = %4")
                .arg(sectorOffset)
                .arg(sectorSize)
                .arg(bytesRead)
                .arg(errorCode)
        );

        return false;
    }

    if (bytesRead != sectorSize) {
        writeDebug(
            QString("writeSectorSafe ReadFile returned short read\n"
                    "sectorOffset = %1\n"
                    "sectorSize = %2\n"
                    "bytesRead = %3")
                .arg(sectorOffset)
                .arg(sectorSize)
                .arg(bytesRead)
        );

        return false;
    }

    for (DWORD i = 0; i < sectorSize; ++i) {
        buffer.get()[i] =
            useRandom
                ? static_cast<unsigned char>(dist(rng))
                : fillByte;
    }

    updateHash(
        finalPassHash,
        buffer.get(),
        sectorSize
    );

    pos.QuadPart = static_cast<LONGLONG>(sectorOffset);

    if (!SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN)) {
        const DWORD errorCode = GetLastError();

        writeDebug(
            QString("writeSectorSafe SECOND SetFilePointerEx failed\n"
                    "sectorOffset = %1\n"
                    "error = %2")
                .arg(sectorOffset)
                .arg(errorCode)
        );

        return false;
    }

    DWORD bytesWritten = 0;

    if (!WriteFile(
            hDrive,
            buffer.get(),
            sectorSize,
            &bytesWritten,
            NULL)) {

        const DWORD errorCode = GetLastError();

        writeDebug(
            QString("writeSectorSafe WriteFile failed\n"
                    "sectorOffset = %1\n"
                    "sectorSize = %2\n"
                    "bytesWritten = %3\n"
                    "error = %4")
                .arg(sectorOffset)
                .arg(sectorSize)
                .arg(bytesWritten)
                .arg(errorCode)
        );

        return false;
    }

    if (bytesWritten != sectorSize) {
        writeDebug(
            QString("writeSectorSafe WriteFile returned short write\n"
                    "sectorOffset = %1\n"
                    "sectorSize = %2\n"
                    "bytesWritten = %3")
                .arg(sectorOffset)
                .arg(sectorSize)
                .arg(bytesWritten)
        );

        return false;
    }

    return true;
}

static bool writePartialSectorSafe(
    HANDLE hDrive,
    unsigned long long logicalOffset,
    unsigned long long logicalLength,
    unsigned long long targetOffset,
    unsigned long long targetEnd,
    unsigned char fillByte,
    bool useRandom,
    DWORD sectorSize,
    std::mt19937_64& rng,
    std::uniform_int_distribution<int>& dist,
    QCryptographicHash* finalPassHash)
{
    if (logicalLength == 0) {
        return true;
    }

    if (sectorSize < sizeof(void*) ||
        (sectorSize & (sectorSize - 1)) != 0 ||
        logicalLength > sectorSize ||
        logicalOffset < targetOffset ||
        logicalOffset >= targetEnd ||
        logicalLength > targetEnd - logicalOffset ||
        logicalOffset > std::numeric_limits<unsigned long long>::max() - logicalLength) {
        return false;
    }

    const unsigned long long sectorStart = (logicalOffset / sectorSize) * sectorSize;
    if (sectorStart < targetOffset ||
        !isRepresentableRawRange(sectorStart, sectorSize)) {
        return false;
    }

    const unsigned long long sectorEnd = sectorStart + sectorSize;
    const unsigned long long spanEnd = logicalOffset + logicalLength;

    if (logicalOffset < sectorStart || spanEnd > sectorEnd) {
        return false;
    }

    auto buffer = makeAlignedBuffer(sectorSize, sectorSize);
    if (!buffer) {
        return false;
    }

    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)sectorStart;
    if (!SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN)) {
        return false;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(hDrive, buffer.get(), sectorSize, &bytesRead, NULL) || bytesRead != sectorSize) {
        return false;
    }

    const unsigned long long localStart = logicalOffset - sectorStart;
    const unsigned long long localEnd = localStart + logicalLength;

    for (unsigned long long i = localStart; i < localEnd; ++i) {
        buffer.get()[i] = useRandom ? static_cast<unsigned char>(dist(rng)) : fillByte;
    }

    updateHash(finalPassHash,
               buffer.get() + localStart,
               logicalLength);

    pos.QuadPart = (LONGLONG)sectorStart;
    if (!SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN)) {
        return false;
    }

    DWORD bytesWritten = 0;

if (!WriteFile(hDrive, buffer.get(), sectorSize, &bytesWritten, NULL) ||
    bytesWritten != sectorSize) {

    const DWORD errorCode = GetLastError();

    QFile debugFile("wipe_debug.txt");

    if (debugFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&debugFile);

        out << "writePartialSectorSafe WriteFile failed\n";
        out << "logicalOffset = " << logicalOffset << "\n";
        out << "sectorStart = " << sectorStart << "\n";
        out << "sectorSize = " << sectorSize << "\n";
        out << "bytesWritten = " << bytesWritten << "\n";
        out << "error = " << errorCode << "\n";
        out << "-----------------------------\n";

        debugFile.close();
    }

    return false;
}

    return true;
}

static bool writeLogicalSpanSafe(
    HANDLE hDrive,
    unsigned long long startOffset,
    unsigned long long totalBytes,
    unsigned long long targetOffset,
    unsigned long long targetEnd,
    unsigned char fillByte,
    bool useRandom,
    DWORD sectorSize,
    std::mt19937_64& rng,
    std::uniform_int_distribution<int>& dist,
    QCryptographicHash* finalPassHash)
{
    if (startOffset < targetOffset ||
        startOffset > targetEnd ||
        totalBytes > targetEnd - startOffset) {
        return false;
    }

    unsigned long long remaining = totalBytes;
    unsigned long long currentOffset = startOffset;

    while (remaining > 0) {
        const unsigned long long sectorStart = (currentOffset / sectorSize) * sectorSize;
        const unsigned long long inSector = currentOffset - sectorStart;
        const unsigned long long available = std::min<unsigned long long>(sectorSize - inSector, remaining);

        if (inSector == 0 && available == sectorSize) {
            if (!writeSectorSafe(hDrive, sectorStart, targetOffset, targetEnd,
                                 sectorSize, fillByte, useRandom, rng, dist,
                                 finalPassHash)) {
                return false;
            }
        } else {
            if (!writePartialSectorSafe(hDrive, currentOffset, available,
                                        targetOffset, targetEnd, fillByte,
                                        useRandom, sectorSize, rng, dist,
                                        finalPassHash)) {
                return false;
            }
        }

        remaining -= available;
        currentOffset += available;
    }

    return true;
}

} // namespace

bool WipeEngine::writePass(
    HANDLE hDrive,
    unsigned long long totalBytes,
    unsigned char fillByte,
    bool useRandom,
    int passNumber,
    int totalPasses,
    std::function<void(int, int, int)> progressCallback,
    unsigned long long startOffset,
    QCryptographicHash* finalPassHash)
{
    if (totalBytes == 0) {
        return true;
    }

    DWORD sectorSize = getDriveSectorSize(hDrive);
    if (!isValidSectorSize(sectorSize)) {
        return false;
    }

    if (startOffset > std::numeric_limits<unsigned long long>::max() - totalBytes) {
        return false;
    }

    if (!isRepresentableRawRange(startOffset, totalBytes)) {
        return false;
    }

    const unsigned long long targetEnd = startOffset + totalBytes;

    if (startOffset != 0 && (startOffset % sectorSize) != 0) {
        return false;
    }

    std::random_device rd;
    std::mt19937_64 rng(rd() | ((std::uint64_t)rd() << 32));
    std::uniform_int_distribution<int> dist(0, 255);

    unsigned long long processed = 0;
    int lastPercent = -1;

    while (processed < totalBytes) {
        const unsigned long long remaining = totalBytes - processed;
        const unsigned long long currentOffset = startOffset + processed;
        const unsigned long long sectorBase = (currentOffset / sectorSize) * sectorSize;
        const unsigned long long inSector = currentOffset - sectorBase;
        const unsigned long long chunkSize = std::min<unsigned long long>(remaining, sectorSize - inSector);

        if (!writeLogicalSpanSafe(hDrive, currentOffset, chunkSize,
                      startOffset, targetEnd, fillByte,
                      useRandom, sectorSize, rng, dist,
                      finalPassHash)) {
            return false;
        }

        processed += chunkSize;

        if (progressCallback) {
            const int percent = std::min(
                99,
                static_cast<int>((double)processed / totalBytes * 100.0));
            if (percent != lastPercent) {
                lastPercent = percent;
                progressCallback(percent, passNumber, totalPasses);
            }
        }
    }

    if (!FlushFileBuffers(hDrive)) {
        return false;
    }

    if (progressCallback) {
        progressCallback(100, passNumber, totalPasses);
    }

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

    const DWORD sectorSize = getDriveSectorSize(hDrive);
    std::vector<unsigned char> buffer(sectorSize > 0 ? sectorSize : DEFAULT_BUFFER_SIZE);
    unsigned long long bytesRead = 0;

    while (bytesRead < totalBytes) {
        unsigned long long remaining = totalBytes - bytesRead;
        DWORD toRead = (remaining >= buffer.size())
                       ? static_cast<DWORD>(buffer.size())
                       : static_cast<DWORD>(remaining);

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
        CloseHandle(hDrive);
        result.status = WipeStatus::LockFailure;
        result.errorMessage = "Failed to lock volume before wipe";
        return result;
    }

    bool ok = false;
    const DWORD sectorSize = getDriveSectorSize(hDrive);
    if (!isValidSectorSize(sectorSize)) {
        DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL);
CloseHandle(hDrive);
result.status = WipeStatus::WriteFailure;
result.errorMessage = "Unable to determine the device logical sector size";
        return result;
    }

    const unsigned long long effectiveSectorSize = sectorSize;
    if (startSector > std::numeric_limits<unsigned long long>::max() / effectiveSectorSize) {
        DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL);
        CloseHandle(hDrive);
        result.status = WipeStatus::InvalidExtent;
        result.errorMessage = "Start sector offset is not representable";
        return result;
    }

    unsigned long long startOffset = startSector * effectiveSectorSize;

    auto passProgressCallback = [&](int percent, int pass, int passes) {
        if (!progressCallback || passes <= 0) {
            return;
        }

        const int passPercent = std::max(0, std::min(100, percent));
        int operationPercent = static_cast<int>(
            ((static_cast<double>(pass - 1) * 100.0) + passPercent) / passes);
        operationPercent = std::min(operationPercent, 99);
        progressCallback(operationPercent, pass, passes);
    };

    GET_LENGTH_INFORMATION deviceLength{};
    DWORD lengthBytesReturned = 0;
    if (!DeviceIoControl(hDrive,
                         IOCTL_DISK_GET_LENGTH_INFO,
                         NULL,
                         0,
                         &deviceLength,
                         sizeof(deviceLength),
                         &lengthBytesReturned,
                         NULL) ||
        lengthBytesReturned < sizeof(deviceLength) ||
        deviceLength.Length.QuadPart <= 0) {
        DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL);
        CloseHandle(hDrive);
        result.status = WipeStatus::WriteFailure;
        result.errorMessage = "Failed to revalidate target device length";
        return result;
    }

    const unsigned long long deviceSize =
        static_cast<unsigned long long>(deviceLength.Length.QuadPart);
    if (startOffset > deviceSize || numSectors > deviceSize - startOffset) {
        DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL);
        CloseHandle(hDrive);
        result.status = WipeStatus::InvalidExtent;
        result.errorMessage = "Requested range exceeds target device boundary";
        return result;
    }

    if (numSectors > 0) {
        const unsigned long long targetEnd = startOffset + numSectors;
        const unsigned long long finalSectorStart =
            ((targetEnd - 1) / effectiveSectorSize) * effectiveSectorSize;
        if (effectiveSectorSize > deviceSize - finalSectorStart) {
            DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL);
            CloseHandle(hDrive);
            result.status = WipeStatus::InvalidExtent;
            result.errorMessage = "Final physical sector exceeds target device boundary";
            return result;
        }
    }

    switch (method) {
        case WipeMethod::ZEROS:
            ok = writePass(hDrive, numSectors, 0x00,
                          false, 1, 1, passProgressCallback, startOffset);
            result.passesCompleted = ok ? 1 : 0;
            break;

        case WipeMethod::RANDOM:
            ok = writePass(hDrive, numSectors, 0x00,
                          true, 1, 1, passProgressCallback, startOffset);
            result.passesCompleted = ok ? 1 : 0;
            break;

        case WipeMethod::DOD_3PASS:
            ok = writePass(hDrive, numSectors, 0x00,
                          false, 1, 3, passProgressCallback, startOffset); // pass 1: zeros
            result.passesCompleted = ok ? 1 : 0;
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0xFF,
                              false, 2, 3, passProgressCallback, startOffset); // pass 2: ones
                result.passesCompleted = ok ? 2 : 1;
            }
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0x00,
                              true,  3, 3, passProgressCallback, startOffset); // pass 3: random
                result.passesCompleted = ok ? 3 : 2;
            }
            break;

        case WipeMethod::DOD_7PASS:
            ok = writePass(hDrive, numSectors, 0x00, false, 1, 7, passProgressCallback, startOffset);
            result.passesCompleted = ok ? 1 : 0;
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0xFF, false, 2, 7, passProgressCallback, startOffset);
                result.passesCompleted = ok ? 2 : 1;
            }
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0x00, true,  3, 7, passProgressCallback, startOffset);
                result.passesCompleted = ok ? 3 : 2;
            }
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0x96, false, 4, 7, passProgressCallback, startOffset);
                result.passesCompleted = ok ? 4 : 3;
            }
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0x00, true,  5, 7, passProgressCallback, startOffset);
                result.passesCompleted = ok ? 5 : 4;
            }
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0xFF, true,  6, 7, passProgressCallback, startOffset);
                result.passesCompleted = ok ? 6 : 5;
            }
            
            if (ok) {
                ok = writePass(hDrive, numSectors, 0x00, true,  7, 7, passProgressCallback, startOffset);
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

    if (ok && progressCallback) {
        progressCallback(100, result.passesCompleted, result.passesCompleted);
    }

    return result;
}

WipeResult WipeEngine::wipeExtents(
    const std::string& physicalPath,
    const std::string& volumePath,
    const std::vector<std::pair<unsigned long long, unsigned long long>>& extents,
    WipeMethod method,
    std::function<void(int, int, int)> progressCallback)
{
    WipeResult result{};
    result.success = false;
    result.bytesWiped = 0;
    result.totalBytes = 0;
    result.passesCompleted = 0;
    result.status = WipeStatus::InvalidExtent;

    std::string errorMessage;
    if (!validateExtentList(extents, result.totalBytes, errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

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
    const DWORD errorCode = GetLastError();

    result.status = WipeStatus::OpenFailure;
    result.errorMessage =
        "Failed to open physical drive.\n"
        "Path: " + physicalPath +
        "\nWindows error: " + std::to_string(errorCode);

    return result;
}

    GET_LENGTH_INFORMATION deviceLength{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDrive,
                         IOCTL_DISK_GET_LENGTH_INFO,
                         NULL,
                         0,
                         &deviceLength,
                         sizeof(deviceLength),
                         &bytesReturned,
                         NULL) ||
        bytesReturned < sizeof(deviceLength) ||
        deviceLength.Length.QuadPart <= 0) {
        CloseHandle(hDrive);
        result.status = WipeStatus::WriteFailure;
        result.errorMessage = "Failed to revalidate target device length";
        return result;
    }

    const unsigned long long deviceSize =
        static_cast<unsigned long long>(deviceLength.Length.QuadPart);
    for (const auto& extent : extents) {
        if (!isRepresentableRawRange(extent.first, extent.second) ||
            extent.first > deviceSize ||
            extent.second > deviceSize - extent.first) {
            CloseHandle(hDrive);
            result.status = WipeStatus::InvalidExtent;
            result.errorMessage = "Invalid extent: range exceeds target device boundary";
            return result;
        }
    }

    DWORD br = 0;

HANDLE hVolume = CreateFileA(
    volumePath.c_str(),
    GENERIC_READ | GENERIC_WRITE,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    NULL,
    OPEN_EXISTING,
    0,
    NULL
);

if (hVolume == INVALID_HANDLE_VALUE) {
    const DWORD errorCode = GetLastError();

    CloseHandle(hDrive);

    result.status = WipeStatus::LockFailure;
    result.errorMessage =
        "Failed to open volume for locking. Windows error: " +
        std::to_string(errorCode);

    return result;
}

if (!DeviceIoControl(
        hVolume,
        FSCTL_LOCK_VOLUME,
        NULL,
        0,
        NULL,
        0,
        &br,
        NULL)) {

    const DWORD errorCode = GetLastError();

    CloseHandle(hVolume);
    CloseHandle(hDrive);

    result.status = WipeStatus::LockFailure;
    result.errorMessage =
        "Failed to lock volume before wiping. Windows error: " +
        std::to_string(errorCode);

    return result;
}

    const DWORD driveSectorSize = getDriveSectorSize(hDrive);
    if (!isValidSectorSize(driveSectorSize)) {
        DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL);
        CloseHandle(hDrive);
        result.status = WipeStatus::WriteFailure;
        result.errorMessage = "Unable to determine the device logical sector size";
        return result;
    }

    int totalPasses = 1;
    if (method == WipeMethod::DOD_3PASS)
        totalPasses = 3;
    else if (method == WipeMethod::DOD_7PASS)
        totalPasses = 7;

    for (const auto& extent : extents) {
        if (extent.first % driveSectorSize != 0) {
            result.status = WipeStatus::InvalidExtent;
            result.errorMessage = "Invalid extent: offset is not sector aligned";
            DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL);
            CloseHandle(hDrive);
            return result;
        }

        const unsigned long long extentEnd = extent.first + extent.second;
        const unsigned long long finalSectorStart =
            ((extentEnd - 1) / driveSectorSize) * driveSectorSize;
        if (driveSectorSize > deviceSize - finalSectorStart) {
            result.status = WipeStatus::InvalidExtent;
            result.errorMessage = "Invalid extent: final physical sector exceeds target device boundary";
            DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL);
            CloseHandle(hDrive);
            return result;
        }
    }

    bool allOk = true;
    int completedPasses = 0;
    unsigned long long passLogicalBytesDone = 0;

    for (int pass = 1; pass <= totalPasses && allOk; ++pass) {
        passLogicalBytesDone = 0;
        QCryptographicHash finalPassHash(QCryptographicHash::Sha256);

        for (const auto& extent : extents) {
            const unsigned long long offset = extent.first;
            const unsigned long long size = extent.second;

            unsigned char fillByte = 0x00;
            bool useRandom = false;

            if (method == WipeMethod::DOD_3PASS) {
                if (pass == 1)
                    fillByte = 0x00;
                else if (pass == 2)
                    fillByte = 0xFF;
                else
                    useRandom = true;
            }
            else if (method == WipeMethod::DOD_7PASS) {
                if (pass == 1)
                    fillByte = 0x00;
                else if (pass == 2)
                    fillByte = 0xFF;
                else if (pass == 4)
                    fillByte = 0x96;
                else
                    useRandom = true;
            }
            else if (method == WipeMethod::RANDOM) {
                useRandom = true;
            }

            auto extentProgressCallback = [&](int extentPercent, int, int) {
                if (!progressCallback) {
                    return;
                }

                const int boundedExtentPercent =
                    std::max(0, std::min(100, extentPercent));
                const long double completedBytes =
                    static_cast<long double>(pass - 1) * result.totalBytes +
                    passLogicalBytesDone +
                    static_cast<long double>(size) * boundedExtentPercent / 100.0L;
                const long double operationBytes =
                    static_cast<long double>(totalPasses) * result.totalBytes;
                const int operationPercent = std::min(
                    99,
                    static_cast<int>(completedBytes * 100.0L / operationBytes));
                progressCallback(operationPercent, pass, totalPasses);
            };

            allOk = writePass(
                hDrive,
                size,
                fillByte,
                useRandom,
                pass,
                totalPasses,
                extentProgressCallback,
                offset,
                pass == totalPasses ? &finalPassHash : nullptr
            );

            if (!allOk) {
                result.status = WipeStatus::WriteFailure;
                result.errorMessage = "Failed while wiping one or more physical extents at offset " + std::to_string(offset);
                break;
            }

            passLogicalBytesDone += size;
        }

        if (allOk) {
            completedPasses = pass;
            if (pass == totalPasses) {
                result.finalDataSha256 = finalPassHash.result().toHex().toStdString();
            }
        }
    }

    DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL);
    CloseHandle(hVolume);
    CloseHandle(hDrive);

    result.success = allOk;
    result.passesCompleted = completedPasses;
    result.bytesWiped = allOk ? result.totalBytes : 0;

    if (!allOk && result.errorMessage.empty()) {
        result.errorMessage = "Failed while wiping one or more physical extents";
    }

    if (allOk) {
        result.status = WipeStatus::Success;
        if (progressCallback) {
            progressCallback(100, totalPasses, totalPasses);
        }
    }

    return result;
}

VerificationResult WipeEngine::verifyExtents(
    const std::string& physicalPath,
    const std::vector<std::pair<unsigned long long, unsigned long long>>& extents,
    WipeMethod method,
    const std::string& expectedFinalSha256)
{
    VerificationResult verification{};

    unsigned long long totalBytes = 0;
    std::string extentError;
    if (!validateExtentList(extents, totalBytes, extentError)) {
        verification.status = VerificationStatus::Error;
        verification.errorMessage = extentError;
        return verification;
    }

    if (expectedFinalSha256.size() != 64 ||
        !std::all_of(expectedFinalSha256.begin(), expectedFinalSha256.end(),
                     [](unsigned char character) {
                         return (character >= '0' && character <= '9') ||
                                (character >= 'a' && character <= 'f');
                     })) {
        verification.status = VerificationStatus::Error;
        verification.errorMessage = "No valid final-pass digest is available for verification";
        return verification;
    }

    HANDLE hRead = CreateFileA(
        physicalPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hRead == INVALID_HANDLE_VALUE) {
        verification.status = VerificationStatus::Error;
        verification.errorMessage = "Failed to open target for read-only verification. Error: " +
                                    std::to_string(GetLastError());
        return verification;
    }

    GET_LENGTH_INFORMATION deviceLength{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hRead,
                         IOCTL_DISK_GET_LENGTH_INFO,
                         NULL,
                         0,
                         &deviceLength,
                         sizeof(deviceLength),
                         &bytesReturned,
                         NULL) ||
        bytesReturned < sizeof(deviceLength) ||
        deviceLength.Length.QuadPart <= 0) {
        CloseHandle(hRead);
        verification.status = VerificationStatus::Error;
        verification.errorMessage = "Failed to revalidate target length for verification";
        return verification;
    }

    const unsigned long long deviceSize =
        static_cast<unsigned long long>(deviceLength.Length.QuadPart);
    for (const auto& extent : extents) {
        if (extent.first > deviceSize || extent.second > deviceSize - extent.first) {
            CloseHandle(hRead);
            verification.status = VerificationStatus::Error;
            verification.errorMessage = "Verification extent exceeds target device boundary";
            return verification;
        }
    }

    const DWORD sectorSize = getDriveSectorSize(hRead);
    if (!isValidSectorSize(sectorSize)) {
        CloseHandle(hRead);
        verification.status = VerificationStatus::Error;
        verification.errorMessage = "Could not determine the logical sector size for verification";
        return verification;
    }

    QCryptographicHash actualHash(QCryptographicHash::Sha256);
    bool allZero = true;

    for (const auto& extent : extents) {
        std::string readError;
        if (!readLogicalExtent(
                hRead,
                extent.first,
                extent.second,
                deviceSize,
                sectorSize,
                [&](const unsigned char* bytes, size_t length) {
                    updateHash(&actualHash, bytes, length);
            if (method == WipeMethod::ZEROS) {
                        for (size_t i = 0; i < length; ++i) {
                            if (bytes[i] != 0) {
                                allZero = false;
                            }
                        }
                    }
                },
                readError)) {
            CloseHandle(hRead);
            verification.status = VerificationStatus::Error;
            verification.errorMessage = readError;
            return verification;
        }
    }

    CloseHandle(hRead);

    const bool digestMatches =
        actualHash.result().toHex().toStdString() == expectedFinalSha256;
    if (digestMatches && (method != WipeMethod::ZEROS || allZero)) {
        verification.status = VerificationStatus::Verified;
    } else {
        verification.status = VerificationStatus::Failed;
        verification.errorMessage = method == WipeMethod::ZEROS && !allZero
            ? "Read-back contains non-zero bytes"
            : "Read-back does not match the final-pass data digest";
    }

    return verification;
}

ExtentReadResult WipeEngine::readExtents(
    const std::string& physicalPath,
    const std::vector<std::pair<unsigned long long, unsigned long long>>& extents,
    unsigned long long maximumBytes)
{
    ExtentReadResult readResult{};

    unsigned long long totalBytes = 0;
    if (!validateExtentList(extents, totalBytes, readResult.errorMessage)) {
        return readResult;
    }

    if (totalBytes > maximumBytes ||
        totalBytes > std::numeric_limits<size_t>::max()) {
        readResult.errorMessage = "Extent data exceeds the controlled recovery read limit";
        return readResult;
    }

    HANDLE hRead = CreateFileA(
        physicalPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hRead == INVALID_HANDLE_VALUE) {
        readResult.errorMessage = "Failed to open target for recovery read. Error: " +
                                  std::to_string(GetLastError());
        return readResult;
    }

    GET_LENGTH_INFORMATION deviceLength{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hRead,
                         IOCTL_DISK_GET_LENGTH_INFO,
                         NULL,
                         0,
                         &deviceLength,
                         sizeof(deviceLength),
                         &bytesReturned,
                         NULL) ||
        bytesReturned < sizeof(deviceLength) ||
        deviceLength.Length.QuadPart <= 0) {
        CloseHandle(hRead);
        readResult.errorMessage = "Failed to revalidate target length for recovery read";
        return readResult;
    }

    const unsigned long long deviceSize =
        static_cast<unsigned long long>(deviceLength.Length.QuadPart);
    for (const auto& extent : extents) {
        if (extent.first > deviceSize || extent.second > deviceSize - extent.first) {
            CloseHandle(hRead);
            readResult.errorMessage = "Recovery extent exceeds target device boundary";
            return readResult;
        }
    }

    const DWORD sectorSize = getDriveSectorSize(hRead);
    if (!isValidSectorSize(sectorSize)) {
        CloseHandle(hRead);
        readResult.errorMessage = "Could not determine the logical sector size for recovery read";
        return readResult;
    }

    readResult.data.reserve(static_cast<size_t>(totalBytes));
    for (const auto& extent : extents) {
        std::string readError;
        if (!readLogicalExtent(
                hRead,
                extent.first,
                extent.second,
                deviceSize,
                sectorSize,
                [&](const unsigned char* bytes, size_t length) {
                    readResult.data.insert(readResult.data.end(), bytes, bytes + length);
                },
                readError)) {
            CloseHandle(hRead);
            readResult.data.clear();
            readResult.errorMessage = readError;
            return readResult;
        }
    }

    CloseHandle(hRead);
    readResult.success = readResult.data.size() == totalBytes;
    if (!readResult.success) {
        readResult.data.clear();
        readResult.errorMessage = "Recovery read returned an incomplete target range";
    }
    return readResult;
}

WipeResult WipeEngine::wipeFile(
    const QString& filePath,
    WipeMethod method)
{
    return wipeFile(filePath, method, {});
}

WipeResult WipeEngine::wipeFile(
    const QString& filePath,
    WipeMethod method,
    std::function<void(int, int, int)> progressCallback)
{
    WipeResult result{};
    result.success = false;

    QFile file(filePath);

    if (!file.exists()) {
        result.errorMessage = "File does not exist";
        return result;
    }

    const qint64 fileSize = file.size();
    if (fileSize < 0) {
        result.errorMessage = "Unable to determine file size";
        return result;
    }
    result.totalBytes = static_cast<unsigned long long>(fileSize);

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
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);

    for (int pass = 0; pass < passes; ++pass) {
        if (!file.seek(0)) {
            result.status = WipeStatus::WriteFailure;
            result.errorMessage = "Failed to seek to the start of the file";
            file.close();
            return result;
        }

        qint64 remaining = fileSize;
        qint64 processed = 0;

        while (remaining > 0) {
            const int chunk = static_cast<int>(std::min<qint64>(remaining, buffer.size()));
            unsigned char fillByte = 0x00;
            bool useRandom = method == WipeMethod::RANDOM;

            if (method == WipeMethod::DOD_3PASS) {
                if (pass == 1)
                    fillByte = 0xFF;
                else if (pass == 2)
                    useRandom = true;
            } else if (method == WipeMethod::DOD_7PASS) {
                if (pass == 1)
                    fillByte = 0xFF;
                else if (pass == 3)
                    fillByte = 0x96;
                else if (pass != 0)
                    useRandom = true;
            }

            if (useRandom) {
                for (int i = 0; i < chunk; ++i) {
                    buffer[i] = static_cast<char>(dist(gen));
                }
            } else {
                memset(buffer.data(), fillByte, chunk);
            }

            if (file.write(buffer.constData(), chunk) != chunk) {
                result.status = WipeStatus::WriteFailure;
                result.errorMessage = "Failed to write the complete file chunk";
                file.close();
                return result;
            }

            remaining -= chunk;
            processed += chunk;

            if (progressCallback && fileSize > 0) {
                const long double passPercent =
                    static_cast<long double>(processed) * 100.0L / fileSize;
                const long double operationPercent =
                    (static_cast<long double>(pass) * 100.0L + passPercent) / passes;
                const int percent = std::min(99, static_cast<int>(operationPercent));
                progressCallback(percent, pass + 1, passes);
            }
        }

        if (!file.flush()) {
            result.status = WipeStatus::WriteFailure;
            result.errorMessage = "Failed to flush file data";
            file.close();
            return result;
        }
    }

    file.close();

    if (method == WipeMethod::ZEROS) {
        if (!verifyZeroPass(filePath)) {
        result.status = WipeStatus::WriteFailure;
            result.errorMessage = "Verification Failed";
            return result;
        }
    }

    const bool deleted = QFile::remove(filePath);

    result.success = deleted;
    result.bytesWiped = deleted ? static_cast<unsigned long long>(fileSize) : 0;
    result.passesCompleted = passes;

    if (!deleted) {
        result.status = WipeStatus::WriteFailure;
        result.errorMessage = "File overwrite succeeded but deletion failed";
    } else {
        result.status = WipeStatus::Success;
        if (progressCallback) {
            progressCallback(100, passes, passes);
        }
    }

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