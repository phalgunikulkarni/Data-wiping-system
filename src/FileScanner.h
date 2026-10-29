#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <QString>
#include <functional>
#include <memory>
#include "StorageSafety.h"
#include "DeviceManager.h"

enum class FileStatus { EXISTING, DELETED, INVALID };
enum class FileScanStatus { Success, Unsupported, Error, Cancelled, StaleVolume };
enum class FileRecordConfidence { Validated, Candidate, Ambiguous, Invalid };

struct FileEntry {
    std::string name;
    std::string fullPath;
    std::string parentPath;
    bool parentPathKnown = false;
    unsigned long long sizeBytes = 0;
    std::string lastModified;
    FileStatus status = FileStatus::EXISTING;
    bool isDirectory = false;
    bool isValid = true;
    std::uint64_t directoryEntryOffset = 0;
    std::uint32_t parentDirectoryCluster = 0;
    bool entryIdentityAvailable = false;
    std::string volumeIdentity;
    std::string identity;
    bool sizeKnown = false;
    bool nameKnown = false;
    FileRecordConfidence confidence = FileRecordConfidence::Validated;
    bool operationEligible = false;
    std::string reason;
};

struct FileScanResult {
    FileScanStatus status = FileScanStatus::Error;
    std::vector<FileEntry> entries;
    std::string errorMessage;
};

class FileScanner {
public:
    std::vector<FileEntry> scanExisting(const std::string& driveLetter);
    FileScanResult scanExistingRoot(const QString& rootPath);
    // Deleted-entry parsing is quarantined until it has a bounded, mockable
    // volume reader and verified filesystem-specific identities.
    FileScanResult scanDeleted(const std::string& volumePath);
    FileScanResult scanAll(const std::string& driveLetter,
                           const std::string& volumePath);
    struct Limits {
        std::uint64_t maximumEntries = 100000;
        std::uint64_t maximumReadBytes = 128ULL * 1024 * 1024;
        std::uint32_t maximumDirectories = 10000;
        std::uint32_t maximumClusterVisitsPerChain = 100000;
        std::uint32_t maximumDepth = 64;
    };
    using CancellationCheck = std::function<bool()>;
    static FileScanResult scanFat32(
        const std::shared_ptr<class IReadOnlyVolumeReader>& reader,
        const std::string& expectedVolumeIdentity,
        CancellationCheck cancelled = {});
    static FileScanResult scanFat32(
        const std::shared_ptr<class IReadOnlyVolumeReader>& reader,
        const std::string& expectedVolumeIdentity,
        const Limits& limits,
        CancellationCheck cancelled = {});
    static std::shared_ptr<class IReadOnlyVolumeReader> openMountedVolumeReadOnly(
        const DriveInfo& device, std::string& error);
    std::string lastError() const { return m_lastError; }

private:
    void walkDirectory(const QString& rootPath, std::vector<FileEntry>& results);
    std::string m_lastError;
};

// All parser offsets are explicitly volume-relative. Implementations must bind
// reads to this volume identity and report true only for complete exact reads.
class IReadOnlyVolumeReader {
public:
    virtual ~IReadOnlyVolumeReader() = default;
    virtual std::string volumeIdentity() const = 0;
    virtual std::string fileSystem() const = 0;
    virtual std::uint64_t sizeBytes() const = 0;
    virtual std::uint32_t logicalSectorSize() const = 0;
    virtual bool revalidate(const std::string& expectedIdentity, std::string& error) = 0;
    virtual bool readAt(std::uint64_t volumeOffset, std::uint32_t length,
                        std::vector<std::uint8_t>& out, std::string& error) = 0;
};

// Overflow-safe volume-relative range check shared by readers and the parser.
bool isValidVolumeReadRange(std::uint64_t offsetBytes, std::uint64_t lengthBytes,
                            std::uint64_t volumeSizeBytes);

// Future operations must consume an explicit validated target and extents.
// FAT32 identification currently never creates an eligible operation target.
struct DeletedEntryOperationTarget {
    std::string volumeIdentity;
    std::string entryIdentity;
    std::vector<VolumeRelativeExtent> extents;
    bool validated = false;
    std::string disabledReason = "Deleted entry operations are not enabled";
};
