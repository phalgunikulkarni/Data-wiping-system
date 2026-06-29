#include "FileScanner.h"
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <ctime>
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

std::vector<FileEntry> FileScanner::parseFAT32Deleted(HANDLE hDrive) {
    std::vector<FileEntry> results;

    // Read BPB (BIOS Parameter Block) from boot sector
    uint8_t boot[512] = {};
    DWORD br = 0;
    LARGE_INTEGER pos; pos.QuadPart = 0;
    SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN);
    ReadFile(hDrive, boot, 512, &br, NULL);

    uint16_t bytesPerSector    = *(uint16_t*)(boot + 11);
    uint8_t  sectorsPerCluster = *(uint8_t* )(boot + 13);
    uint16_t reservedSectors   = *(uint16_t*)(boot + 14);
    uint8_t  numFATs           = *(uint8_t* )(boot + 16);
    uint32_t sectorsPerFAT     = *(uint32_t*)(boot + 36);
    uint32_t rootCluster       = *(uint32_t*)(boot + 44);

    if (bytesPerSector == 0) return results; // safety

    uint64_t fatStart  = (uint64_t)reservedSectors * bytesPerSector;
    uint64_t dataStart = fatStart +
                         (uint64_t)numFATs * sectorsPerFAT * bytesPerSector;
    uint64_t clusterSize = (uint64_t)sectorsPerCluster * bytesPerSector;

    // Scan root directory cluster
    uint32_t cluster = rootCluster;
    uint64_t clusterOffset = dataStart +
                             (uint64_t)(cluster - 2) * clusterSize;

    // Read one cluster of directory entries
    std::vector<uint8_t> clusterData(clusterSize);
    LARGE_INTEGER seekPos;
    seekPos.QuadPart = (LONGLONG)clusterOffset;
    SetFilePointerEx(hDrive, seekPos, NULL, FILE_BEGIN);
    ReadFile(hDrive, clusterData.data(), (DWORD)clusterSize, &br, NULL);

    size_t entryCount = clusterSize / sizeof(FAT32DirEntry);
    FAT32DirEntry* entries = (FAT32DirEntry*)clusterData.data();

    for (size_t i = 0; i < entryCount; i++) {
        uint8_t first = entries[i].name[0];

        // 0xE5 = deleted entry marker in FAT32
        if (first != 0xE5) continue;

        // Skip LFN (long filename) entries
        if (entries[i].attr == 0x0F) continue;

        // Skip completely empty
        if (entries[i].fileSize == 0 &&
            entries[i].clusterLow == 0 &&
            entries[i].clusterHigh == 0) continue;

        // Reconstruct filename (first char was 0xE5, replace with '?')
        char fname[12] = {};
        fname[0] = '?';
        for (int j = 1; j < 8; j++) {
            if (entries[i].name[j] == 0x20) break;
            fname[j] = (char)entries[i].name[j];
        }
        // Extension
        bool hasExt = false;
        char ext[5] = {};
        for (int j = 0; j < 3; j++) {
            if (entries[i].name[8 + j] != 0x20) {
                ext[j] = (char)entries[i].name[8 + j];
                hasExt = true;
            }
        }

        std::string fullName = std::string(fname);
        if (hasExt) fullName += "." + std::string(ext);

        FileEntry entry;
        entry.name        = fullName;
        entry.fullPath    = "[DELETED] " + fullName;
        entry.sizeBytes   = entries[i].fileSize;
        entry.lastModified = "Unknown";
        entry.status      = FileStatus::DELETED;
        entry.isDirectory = false;

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
    MessageBoxA(
    NULL,
    std::to_string(fs).c_str(),
    "Filesystem",
    MB_OK
    );

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