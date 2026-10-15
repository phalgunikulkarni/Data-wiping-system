#include "FileScanner.h"
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <unordered_set>
// ─── Helpers ───────────────────────────────────────────────

static std::string filetimeToString(FILETIME ft) {
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

// ─── Existing file walk ────────────────────────────────────

void FileScanner::walkDirectory(const std::string& path,
                                 std::vector<FileEntry>& results) {
    std::string searchPath = path + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;

        FileEntry entry;
        entry.name = name;
        entry.fullPath = path + "\\" + name;
        entry.status = FileStatus::EXISTING;
        entry.isDirectory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);

        ULARGE_INTEGER size;
        size.LowPart  = fd.nFileSizeLow;
        size.HighPart = fd.nFileSizeHigh;
        entry.sizeBytes = size.QuadPart;
        entry.lastModified = filetimeToString(fd.ftLastWriteTime);

        results.push_back(entry);

        if (entry.isDirectory) {
            walkDirectory(entry.fullPath, results);
        }

    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

std::vector<FileEntry> FileScanner::scanExisting(
    const std::string& driveLetter)
{
    std::vector<FileEntry> results;
    std::string root = driveLetter + ":";
    walkDirectory(root, results);
    return results;
}

// ─── Filesystem detection ──────────────────────────────────

uint8_t FileScanner::detectFilesystem(HANDLE hDrive) {
    // Read boot sector (first 512 bytes)
    uint8_t sector[512] = {};
    DWORD br = 0;
    LARGE_INTEGER pos; pos.QuadPart = 0;
    SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN);
    ReadFile(hDrive, sector, 512, &br, NULL);

    // FAT32: bytes 82-89 = "FAT32   "
    if (memcmp(sector + 82, "FAT32   ", 8) == 0) return 32;

    // exFAT: bytes 3-10 = "EXFAT   "
    if (memcmp(sector + 3, "EXFAT   ", 8) == 0) return 64;

    return 0; // unknown
}

// ─── FAT32 deleted file parser ─────────────────────────────

#pragma pack(push, 1)
struct FAT32DirEntry {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  createTimeTenth;
    uint16_t createTime;
    uint16_t createDate;
    uint16_t accessDate;
    uint16_t clusterHigh;
    uint16_t writeTime;
    uint16_t writeDate;
    uint16_t clusterLow;
    uint32_t fileSize;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct FAT32LFNEntry
{
    uint8_t order;
    uint16_t name1[5];
    uint8_t attr;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t firstCluster;
    uint16_t name3[2];
};
#pragma pack(pop)

static std::string decodeLFNCharacters(const FAT32LFNEntry& lfn)
{
    std::string result;

    uint16_t chars[13];

    for (int i = 0; i < 5; ++i)
        chars[i] = lfn.name1[i];

    for (int i = 0; i < 6; ++i)
        chars[5 + i] = lfn.name2[i];

    for (int i = 0; i < 2; ++i)
        chars[11 + i] = lfn.name3[i];

    for (int i = 0; i < 13; ++i)
    {
        uint16_t c = chars[i];

        if (c == 0x0000 || c == 0xFFFF)
            break;

        if (c < 128)
            result += static_cast<char>(c);
        else
            result += '?';
    }

    return result;
}

static std::string buildDeletedShortName(const FAT32DirEntry& entry)
{
    char fname[12] = {};
    fname[0] = '?';

    for (int j = 1; j < 8; ++j)
    {
        if (entry.name[j] == 0x20)
            break;

        fname[j] = static_cast<char>(entry.name[j]);
    }

    bool hasExt = false;
    char ext[4] = {};

    for (int j = 0; j < 3; ++j)
    {
        if (entry.name[8 + j] != 0x20)
        {
            ext[j] = static_cast<char>(entry.name[8 + j]);
            hasExt = true;
        }
    }

    std::string result = fname;

    if (hasExt)
        result += "." + std::string(ext);

    return result;
}

std::vector<FileEntry> FileScanner::parseFAT32Deleted(HANDLE hDrive)
{
    std::vector<FileEntry> results;

    uint8_t boot[512] = {};
    DWORD br = 0;

    LARGE_INTEGER pos;
    pos.QuadPart = 0;

    SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN);

    if (!ReadFile(hDrive, boot, 512, &br, NULL) || br != 512)
        return results;

    uint16_t bytesPerSector =
        *(uint16_t*)(boot + 11);

    uint8_t sectorsPerCluster =
        *(uint8_t*)(boot + 13);

    uint16_t reservedSectors =
        *(uint16_t*)(boot + 14);

    uint8_t numFATs =
        *(uint8_t*)(boot + 16);

    uint32_t sectorsPerFAT =
        *(uint32_t*)(boot + 36);

    uint32_t rootCluster =
        *(uint32_t*)(boot + 44);

    uint16_t totalSectors16 =
        *(uint16_t*)(boot + 19);

    uint32_t totalSectors32 =
        *(uint32_t*)(boot + 32);

    if (bytesPerSector == 0 ||
        sectorsPerCluster == 0 ||
        numFATs == 0 ||
        sectorsPerFAT == 0 ||
        rootCluster < 2 ||
        (bytesPerSector & (bytesPerSector - 1)) != 0 ||
        bytesPerSector < sizeof(uint32_t))
    {
        return results;
    }

    const uint64_t totalSectors =
        totalSectors16 != 0 ? totalSectors16 : totalSectors32;
    const uint64_t firstDataSector =
        (uint64_t)reservedSectors + (uint64_t)numFATs * sectorsPerFAT;
    if (totalSectors <= firstDataSector) {
        return results;
    }

    uint64_t fatStart =
        (uint64_t)reservedSectors * bytesPerSector;

    uint64_t dataStart =
        fatStart +
        (uint64_t)numFATs *
        sectorsPerFAT *
        bytesPerSector;

    uint64_t clusterSize =
        (uint64_t)sectorsPerCluster *
        bytesPerSector;

    const uint64_t fatSizeBytes =
        (uint64_t)sectorsPerFAT * bytesPerSector;
    const uint64_t fatEnd = fatStart + fatSizeBytes;
    const uint64_t dataClusterCount =
        (totalSectors - firstDataSector) / sectorsPerCluster;
    const uint64_t maxDataCluster = dataClusterCount + 1;

    uint32_t cluster = rootCluster;

    uint64_t clusterOffset =
        dataStart +
        (uint64_t)(cluster - 2) *
        clusterSize;

    std::vector<uint8_t> clusterData(clusterSize);

    LARGE_INTEGER seekPos;
    seekPos.QuadPart = (LONGLONG)clusterOffset;

    SetFilePointerEx(
        hDrive,
        seekPos,
        NULL,
        FILE_BEGIN
    );

    if (!ReadFile(
            hDrive,
            clusterData.data(),
            (DWORD)clusterSize,
            &br,
            NULL))
    {
        return results;
    }

    if (br != clusterSize)
        return results;

    size_t entryCount =
        clusterSize / sizeof(FAT32DirEntry);

    FAT32DirEntry* entries =
        reinterpret_cast<FAT32DirEntry*>(
            clusterData.data()
        );

    for (size_t i = 0; i < entryCount; ++i)
    {
        uint8_t first =
            entries[i].name[0];

        if (first == 0x00)
            break;

        /*
         * Deleted LFN entries are kept so that
         * they can be associated with the
         * deleted short-name entry that follows.
         */
        if (first == 0xE5 &&
            entries[i].attr == 0x0F)
        {
            continue;
        }

        if (first != 0xE5)
            continue;

        if (entries[i].attr == 0x0F)
            continue;

        if (entries[i].attr & 0x10)
            continue;

        if (entries[i].attr & 0x08)
            continue;

        if (entries[i].fileSize == 0 &&
            entries[i].clusterLow == 0 &&
            entries[i].clusterHigh == 0)
        {
            continue;
        }

        /*
         * Look backwards for consecutive deleted
         * LFN entries belonging to this file.
         */
        std::vector<std::pair<uint8_t, std::string>> lfnParts;

        size_t j = i;

        while (j > 0)
        {
            --j;

            FAT32LFNEntry* lfn =
                reinterpret_cast<FAT32LFNEntry*>(
                    &entries[j]
                );

            if (entries[j].name[0] != 0xE5)
                break;

            if (entries[j].attr != 0x0F)
                break;

            std::string part =
                decodeLFNCharacters(*lfn);

            lfnParts.push_back(
                {
                    static_cast<uint8_t>(
                        lfn->order & 0x1F
                    ),
                    part
                }
            );

            /*
             * The LAST_LONG_ENTRY flag is 0x40.
             * Once found, the LFN sequence is complete.
             */
        }

        std::string fullName;

        if (!lfnParts.empty())
        {
            std::sort(
                lfnParts.begin(),
                lfnParts.end(),
                [](const auto& a, const auto& b)
                {
                    return a.first < b.first;
                }
            );

            for (const auto& part : lfnParts)
                fullName += part.second;
        }

        if (fullName.empty())
            fullName =
                buildDeletedShortName(entries[i]);

        FileEntry entry;

        entry.name =
            fullName;

        entry.fullPath =
            "[DELETED] " + fullName;

        entry.sizeBytes =
            entries[i].fileSize;

        entry.lastModified =
            "Unknown";

        entry.status =
            FileStatus::DELETED;

        entry.isDirectory = false;

uint32_t startCluster =
    ((uint32_t)entries[i].clusterHigh << 16) |
    entries[i].clusterLow;

entry.startCluster = startCluster;

if (startCluster >= 2) {
    entry.dataOffset =
        dataStart +
        (uint64_t)(startCluster - 2) * clusterSize;
}

entry.clusterSize = clusterSize;
entry.dataRegionStart = dataStart;

if (startCluster >= 2) {
    entry.dataOffset =
        dataStart + (uint64_t)(startCluster - 2) * clusterSize;
} else {
    entry.dataOffset = 0;
}

bool chainValid = true;
uint64_t remainingBytes = entry.sizeBytes;
uint32_t currentCluster = startCluster;
size_t iterations = 0;
const size_t maxClusterVisits = 0x100000;
const uint64_t expectedClusterCount = clusterSize == 0
    ? 0
    : entry.sizeBytes / clusterSize + (entry.sizeBytes % clusterSize != 0);
std::unordered_set<uint32_t> visitedClusters;

if (startCluster < 2 || startCluster > maxDataCluster ||
    (entry.sizeBytes > 0 && startCluster == 0) ||
    expectedClusterCount > maxClusterVisits) {
    chainValid = false;
}

while (chainValid &&
       currentCluster >= 2 &&
       currentCluster < 0x0FFFFFF8 &&
       remainingBytes > 0 &&
       iterations < maxClusterVisits) {
    if (currentCluster > maxDataCluster ||
        !visitedClusters.insert(currentCluster).second) {
        chainValid = false;
        break;
    }

    entry.clusterChain.push_back(currentCluster);

    uint64_t extentOffset =
        dataStart + (uint64_t)(currentCluster - 2) * clusterSize;
    uint64_t extentLength = std::min<uint64_t>(clusterSize, remainingBytes);
    entry.extents.push_back({extentOffset, extentLength});
    remainingBytes -= extentLength;

    if (remainingBytes == 0) {
        break;
    }

    uint64_t fatEntryOffset = fatStart + (uint64_t)currentCluster * 4;
    const uint64_t fatEntryWithinFat = fatEntryOffset - fatStart;
    const uint64_t fatSectorOffset =
        (fatEntryOffset / bytesPerSector) * bytesPerSector;
    const uint64_t entryOffsetInSector = fatEntryOffset - fatSectorOffset;

    if (fatEntryWithinFat > fatSizeBytes - sizeof(uint32_t) ||
        fatSectorOffset < fatStart ||
        fatSectorOffset > fatEnd - bytesPerSector ||
        entryOffsetInSector > bytesPerSector - sizeof(uint32_t)) {
        chainValid = false;
        break;
    }

    LARGE_INTEGER fatPos;
    fatPos.QuadPart = (LONGLONG)fatSectorOffset;

    if (!SetFilePointerEx(hDrive, fatPos, NULL, FILE_BEGIN)) {
        chainValid = false;
        break;
    }

    std::vector<uint8_t> fatSector(bytesPerSector);
    DWORD fatBytesRead = 0;

    if (!ReadFile(hDrive,
                  fatSector.data(),
                  bytesPerSector,
                  &fatBytesRead,
                  NULL) ||
        fatBytesRead != bytesPerSector) {
        chainValid = false;
        break;
    }

    uint32_t nextCluster = 0;
    memcpy(&nextCluster,
           fatSector.data() + entryOffsetInSector,
           sizeof(nextCluster));
    nextCluster &= 0x0FFFFFFF;

    if (nextCluster >= 0x0FFFFFF8 && nextCluster <= 0x0FFFFFFF) {
        if (remainingBytes > 0) {
            chainValid = false;
        }
        break;
    }

    if (nextCluster < 2 ||
        nextCluster == 0x0FFFFFF7 ||
        (nextCluster >= 0x0FFFFFF0 && nextCluster <= 0x0FFFFFF6) ||
        nextCluster == currentCluster ||
        visitedClusters.find(nextCluster) != visitedClusters.end() ||
        nextCluster > maxDataCluster) {
        chainValid = false;
        break;
    }

    currentCluster = nextCluster;
    ++iterations;
}

if (entry.sizeBytes == 0) {
    if (!entry.clusterChain.empty()) {
        chainValid = false;
    }
} else {
    if (remainingBytes > 0) {
        chainValid = false;
    }
}

if (!chainValid) {
    entry.isValid = false;
    entry.clusterChain.clear();
    entry.extents.clear();
    entry.dataOffset = 0;
}

results.push_back(entry);
    }

    return results;
}

// ─── exFAT deleted file parser ─────────────────────────────

std::vector<FileEntry> FileScanner::parseExFATDeleted(HANDLE hDrive) {
    std::vector<FileEntry> results;

    // Read exFAT boot sector
    uint8_t boot[512] = {};
    DWORD br = 0;
    LARGE_INTEGER pos; pos.QuadPart = 0;
    SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN);
    ReadFile(hDrive, boot, 512, &br, NULL);

    uint32_t clusterHeapOffset = *(uint32_t*)(boot + 88);
    uint32_t rootCluster       = *(uint32_t*)(boot + 96);
    uint8_t  sectorShift       = *(uint8_t* )(boot + 108);
    uint8_t  clusterShift      = *(uint8_t* )(boot + 109);

    uint64_t bytesPerSector  = 1ULL << sectorShift;
    uint64_t bytesPerCluster = bytesPerSector << clusterShift;
    uint64_t dataRegionStart = (uint64_t)clusterHeapOffset * bytesPerSector;

    uint64_t rootOffset = dataRegionStart +
                          (uint64_t)(rootCluster - 2) * bytesPerCluster;

    std::vector<uint8_t> clusterData(bytesPerCluster);
    LARGE_INTEGER seekPos;
    seekPos.QuadPart = (LONGLONG)rootOffset;
    SetFilePointerEx(hDrive, seekPos, NULL, FILE_BEGIN);
    ReadFile(hDrive, clusterData.data(), (DWORD)bytesPerCluster, &br, NULL);

    // In exFAT, entry type 0x00 or types < 0x80 = inactive (deleted)
    size_t offset = 0;
    while (offset + 32 <= clusterData.size()) {
        uint8_t entryType = clusterData[offset];

        // Active file entry = 0x85, inactive = 0x05
        if (entryType == 0x05) {
            // This is a deleted file directory entry
            // Next entry (offset+32) should be stream extension (0x40/0x00)
            if (offset + 64 <= clusterData.size()) {
                uint8_t streamType = clusterData[offset + 32];

                // Get file size from stream extension
                uint64_t fileSize = 0;
                if (streamType == 0x00 || streamType == 0x40) {
                    fileSize = *(uint64_t*)(clusterData.data() + offset + 32 + 8);
                }

                // Get filename from file name entry (offset+64), type 0x41/0x01
                std::string fname = "[recovered]";
                if (offset + 96 <= clusterData.size()) {
                    uint8_t nameType = clusterData[offset + 64];
                    if (nameType == 0x01 || nameType == 0x41) {
                        // Unicode filename, up to 15 chars per entry
                        wchar_t wname[16] = {};
                        memcpy(wname, clusterData.data() + offset + 66, 30);
                        char narrow[32] = {};
                        WideCharToMultiByte(CP_ACP, 0, wname, -1,
                                           narrow, sizeof(narrow),
                                           NULL, NULL);
                        fname = std::string(narrow);
                    }
                }

                FileEntry entry;
                entry.name        = fname;
                entry.fullPath    = "[DELETED] " + fname;
                entry.sizeBytes   = fileSize;
                entry.lastModified = "Unknown";
                entry.status      = FileStatus::DELETED;
                entry.isDirectory = false;
                results.push_back(entry);
            }
        }
        offset += 32;
    }

    return results;
}

std::vector<FileEntry> FileScanner::scanDeleted(const std::string& physicalPath)
{
    std::vector<FileEntry> results;

    // IMPORTANT: Open WITHOUT FILE_FLAG_NO_BUFFERING to avoid corruption
    // NO_BUFFERING requires sector-aligned memory which std::vector doesn't guarantee
    HANDLE hDrive = CreateFileA(
        physicalPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING,
        0, NULL);  // NO FILE_FLAG_NO_BUFFERING - this prevents corruption!

    if (hDrive == INVALID_HANDLE_VALUE) return results;

    uint8_t fs = detectFilesystem(hDrive);
    if (fs == 32)      results = parseFAT32Deleted(hDrive);
    else if (fs == 64) results = parseExFATDeleted(hDrive);

    CloseHandle(hDrive);
    return results;
}
// ─── Combined scan ─────────────────────────────────────────

std::vector<FileEntry> FileScanner::scanAll(
    const std::string& driveLetter,
    const std::string& physicalPath)
{
    // Existing files
    auto results = scanExisting(driveLetter);

    // Open physical drive for raw read - WITHOUT FILE_FLAG_NO_BUFFERING
    HANDLE hDrive = CreateFileA(
        physicalPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING,
        0, NULL);  // NO FILE_FLAG_NO_BUFFERING - prevents corruption!

    if (hDrive != INVALID_HANDLE_VALUE) {
        uint8_t fs = detectFilesystem(hDrive);

        std::vector<FileEntry> deleted;
        if (fs == 32)      deleted = parseFAT32Deleted(hDrive);
        else if (fs == 64) deleted = parseExFATDeleted(hDrive);

        for (auto& d : deleted) results.push_back(d);
        CloseHandle(hDrive);
    }

    return results;
}