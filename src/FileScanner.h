#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

enum class FileStatus {
    EXISTING,
    DELETED
};

struct FileEntry {
    std::string name;
    std::string fullPath;
    unsigned long long sizeBytes;
    std::string lastModified;
    FileStatus status;
    bool isDirectory;
};

class FileScanner {
public:
    std::vector<FileEntry> scanExisting(const std::string& driveLetter);
    std::vector<FileEntry> scanDeleted(const std::string& physicalPath);
    std::vector<FileEntry> scanAll(const std::string& driveLetter,
                                   const std::string& physicalPath);

private:
    void walkDirectory(const std::string& path,
                       std::vector<FileEntry>& results);
    std::vector<FileEntry> parseFAT32Deleted(HANDLE hDrive);
    std::vector<FileEntry> parseExFATDeleted(HANDLE hDrive);
    uint8_t detectFilesystem(HANDLE hDrive);
};