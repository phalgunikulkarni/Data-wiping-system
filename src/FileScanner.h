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
enum class FileScanStatus { Success, Partial, Unsupported, Error, Cancelled, StaleVolume };
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
    // Deleted FAT short names lose their first byte when marked 0xE5. Any
    // surviving suffix is presentation-only and is never a validated name.
    std::string shortNameFragment;
    bool shortNameFragmentAvailable = false;
    bool sizeKnown = false;
    bool nameKnown = false;
    bool deletedMarkerObserved = false;
    FileRecordConfidence confidence = FileRecordConfidence::Validated;
    bool operationEligible = false;
    std::string reason;
};

struct FileScanCounts {
    std::uint64_t existingFiles = 0;
    std::uint64_t deletedEntryCandidates = 0;
    std::uint64_t invalidFileRecords = 0;
    std::uint64_t invalidRecords = 0;
    std::uint64_t ambiguousRecords = 0; // Diagnostic subset; may overlap deleted candidates.
    std::uint64_t deletedNamesUnknown = 0;
};

// Shared UI/reporting policy: every non-directory parser record is a table row,
// including invalid records. Existing/deleted totals count only those statuses;
// invalid/ambiguous totals remain diagnostics and ambiguous may overlap. Counts
// are entry-record counts, not unique-name/path counts; distinct directory slots
// remain distinct even when names or paths collide.
bool isListedFileRecord(const FileEntry& entry);
FileScanCounts aggregateFileEntries(const std::vector<FileEntry>& entries);

struct FileScanResult {
    FileScanStatus status = FileScanStatus::Error;
    std::vector<FileEntry> entries;
    std::string errorMessage;
    bool volumeIdentityValidated = false;
    bool readError = false;
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
