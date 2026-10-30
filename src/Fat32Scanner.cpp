#include "FileScanner.h"

#include <windows.h>
#include <winioctl.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <deque>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr std::uint8_t kDeleted = 0xE5;
constexpr std::uint8_t kLongNameAttribute = 0x0F;
constexpr std::uint32_t kEndOfChain = 0x0FFFFFF8;
constexpr std::uint32_t kBadCluster = 0x0FFFFFF7;
constexpr std::uint32_t kReservedClusterStart = 0x0FFFFFF0;
constexpr std::uint64_t kMaximumClusterBytes = 1024 * 1024;
constexpr std::uint64_t kMaximumFatCacheBytes = 64 * 1024;
constexpr std::uint64_t kMaximumCoalescedDirectoryBytes = 1024 * 1024;

std::uint16_t u16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (std::uint16_t(p[1]) << 8)); }
std::uint32_t u32(const std::uint8_t* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }

bool checkedMul(std::uint64_t a, std::uint64_t b, std::uint64_t& out)
{
    if (a && b > std::numeric_limits<std::uint64_t>::max() / a) return false;
    out = a * b;
    return true;
}

bool checkedAdd(std::uint64_t a, std::uint64_t b, std::uint64_t& out)
{
    if (b > std::numeric_limits<std::uint64_t>::max() - a) return false;
    out = a + b;
    return true;
}

std::string utf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

std::string lowerAscii(std::string value)
{
    for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}

std::string volumeEntryIdentity(const std::string& volume, std::uint32_t parentCluster,
                                std::uint32_t containingCluster, std::uint32_t slot)
{
    return lowerAscii(volume) + "|FAT32|parent=" + std::to_string(parentCluster) +
           "|cluster=" + std::to_string(containingCluster) + "|slot=" + std::to_string(slot);
}

struct Fat32Geometry {
    std::uint32_t bytesPerSector = 0;
    std::uint32_t sectorsPerCluster = 0;
    std::uint32_t reservedSectors = 0;
    std::uint32_t fatCount = 0;
    std::uint32_t sectorsPerFat = 0;
    std::uint32_t rootCluster = 0;
    std::uint64_t totalSectors = 0;
    std::uint64_t clusterBytes = 0;
    std::uint64_t fatOffset = 0;
    std::uint64_t fatBytes = 0;
    std::uint64_t dataOffset = 0;
    std::uint64_t volumeBytes = 0;
    std::uint32_t maximumCluster = 0;
};

struct LfnPart {
    std::uint8_t order = 0;
    std::uint8_t checksum = 0;
    bool fieldsValid = false;
    std::array<std::uint16_t, 13> chars{};
};

std::uint8_t shortChecksum(const std::uint8_t* name)
{
    std::uint8_t checksum = 0;
    for (int i = 0; i < 11; ++i)
        checksum = static_cast<std::uint8_t>(((checksum & 1) ? 0x80 : 0) + (checksum >> 1) + name[i]);
    return checksum;
}

QString decodeLongName(const std::vector<LfnPart>& parts, bool requireChecksum,
                       std::uint8_t expectedChecksum, bool& structurallyValid)
{
    structurallyValid = false;
    if (parts.empty() || parts.size() > 20) return {};
    std::vector<LfnPart> ordered = parts;
    std::sort(ordered.begin(), ordered.end(), [](const LfnPart& a, const LfnPart& b) { return (a.order & 0x1F) < (b.order & 0x1F); });
    const std::size_t count = ordered.size();
    if ((ordered.back().order & 0x40) == 0 || (ordered.back().order & 0x1F) != count) return {};
    std::u16string text;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& part = ordered[i];
        if (!part.fieldsValid || (part.order & 0x1F) != i + 1 || part.checksum != ordered.front().checksum) return {};
        if (requireChecksum && part.checksum != expectedChecksum) return {};
        for (std::uint16_t ch : part.chars) {
            if (ch == 0x0000 || ch == 0xFFFF) goto done;
            text.push_back(static_cast<char16_t>(ch));
        }
    }
done:
    if (text.empty() || text.size() > 255) return {};
    for (char16_t ch : text) {
        if (ch < 0x20 || ch == u'/' || ch == u'\\' || ch == u':') return {};
    }
    structurallyValid = true;
    return QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
}

QString decodeShortName(const std::uint8_t* name, bool deleted)
{
    QString base;
    if (deleted) base.append(QLatin1Char('?'));
    const int firstKnown = deleted ? 1 : 0;
    bool hasSurvivingBase = false;
    for (int i = firstKnown; i < 8 && name[i] != ' '; ++i) {
        if (name[i] < 0x20 || name[i] > 0x7E) return {};
        base.append(QChar(name[i]));
        hasSurvivingBase = true;
    }
    QString extension;
    for (int i = 8; i < 11 && name[i] != ' '; ++i) {
        if (name[i] < 0x20 || name[i] > 0x7E) return {};
        extension.append(QChar(name[i]));
    }
    if (deleted && !hasSurvivingBase && extension.isEmpty()) return {};
    return extension.isEmpty() ? base : base + QLatin1Char('.') + extension;
}

class FatParser {
public:
    FatParser(std::shared_ptr<IReadOnlyVolumeReader> source, std::string expected,
              FileScanner::Limits limits, FileScanner::CancellationCheck cancel)
        : reader(std::move(source)), identity(std::move(expected)), limits(limits), cancelled(std::move(cancel)) {}

    FileScanResult run()
    {
        if (!reader || identity.empty()) return error("A selected volume identity and reader are required");
        if (!reader->revalidate(identity, errorText)) return stale(errorText);
        if (lowerAscii(reader->volumeIdentity()) != lowerAscii(identity)) return stale("Selected volume identity changed");
        identityValidated = true;
        if (lowerAscii(reader->fileSystem()) != "fat32") return unsupported("Selected volume is not FAT32");
        sectorSize = reader->logicalSectorSize();
        volumeSize = reader->sizeBytes();
        if (sectorSize < 512 || sectorSize > 4096 || (sectorSize & (sectorSize - 1)) != 0 || volumeSize < sectorSize)
            return error("Volume sector size or capacity is invalid");

        std::vector<std::uint8_t> boot;
        if (!read(0, sectorSize, boot)) return failRead();
        if (boot.size() < 512 || boot[510] != 0x55 || boot[511] != 0xAA)
            return error("FAT32 boot-sector signature is invalid");
        if (!parseGeometry(boot)) return error(errorText);

        Dir pending{geometry.rootCluster, geometry.rootCluster, QString(), 0, true};
        queue.push_back(pending);
        std::unordered_set<std::uint32_t> directoriesSeen;
        while (!queue.empty()) {
            if (isCancelled()) return cancelledResult();
            if (++directoryCount > limits.maximumDirectories) return error("Directory scan limit reached");
            Dir dir = queue.front();
            queue.pop_front();
            if (!directoriesSeen.insert(dir.startCluster).second) {
                return error("Directory cluster is referenced more than once; refusing ambiguous directory traversal");
            }
            parseDirectory(dir);
            if (!errorText.empty()) return failReadOrError();
        }
        if (!reader->revalidate(identity, errorText)) return stale(errorText);
        markReusedDeletedClusters();
        FileScanResult result;
        result.status = FileScanStatus::Success;
        result.entries = std::move(entries);
        result.volumeIdentityValidated = true;
        return result;
    }

private:
    struct Dir { std::uint32_t startCluster; std::uint32_t cluster; QString path; std::uint32_t depth; bool pathKnown; };

    FileScanResult error(const std::string& message) const { return {FileScanStatus::Error, {}, message, identityValidated, readError}; }
    FileScanResult unsupported(const std::string& message) const { return {FileScanStatus::Unsupported, {}, message, identityValidated, readError}; }
    FileScanResult stale(const std::string& message) const { return {FileScanStatus::StaleVolume, {}, message.empty() ? "Selected volume identity is stale" : message}; }
    FileScanResult cancelledResult() const { return {FileScanStatus::Cancelled, {}, "FAT32 identification was cancelled", identityValidated, readError}; }
    bool isCancelled() const { return cancelled && cancelled(); }

    bool parseGeometry(const std::vector<std::uint8_t>& boot)
    {
        const std::uint32_t bps = u16(boot.data() + 11);
        const std::uint32_t spc = boot[13];
        const std::uint32_t reserved = u16(boot.data() + 14);
        const std::uint32_t fats = boot[16];
        const std::uint32_t rootEntryCount = u16(boot.data() + 17);
        const std::uint64_t total = u16(boot.data() + 19) ? u16(boot.data() + 19) : u32(boot.data() + 32);
        const std::uint32_t fatSize16 = u16(boot.data() + 22);
        const std::uint32_t fatSize = fatSize16 ? fatSize16 : u32(boot.data() + 36);
        const std::uint32_t root = u32(boot.data() + 44) & 0x0FFFFFFF;
        const std::uint16_t fat32Version = u16(boot.data() + 42);
        if (bps != sectorSize || bps < 512 || bps > 4096 || (bps & (bps - 1)) != 0 ||
            spc == 0 || spc > 128 || (spc & (spc - 1)) != 0 || reserved == 0 ||
            (fats != 1 && fats != 2) || fatSize16 != 0 || rootEntryCount != 0 ||
            fat32Version != 0 || fatSize == 0 || total == 0 || root < 2) {
            errorText = "FAT32 geometry contains invalid or unsupported parameters";
            return false;
        }
        std::uint64_t totalBytes = 0, clusterBytes = 0, fatOffset = 0, fatBytes = 0;
        std::uint64_t fatSectors = 0, firstDataSector = 0, dataOffset = 0;
        if (!checkedMul(total, bps, totalBytes) || totalBytes > volumeSize ||
            !checkedMul(bps, spc, clusterBytes) || clusterBytes > kMaximumClusterBytes ||
            !checkedMul(reserved, bps, fatOffset) || !checkedMul(fatSize, bps, fatBytes) ||
            !checkedMul(fats, fatSize, fatSectors) || !checkedAdd(reserved, fatSectors, firstDataSector) ||
            firstDataSector >= total || !checkedMul(firstDataSector, bps, dataOffset)) {
            errorText = "FAT32 geometry arithmetic overflowed or BPB volume length (" +
                std::to_string(totalBytes) + " bytes) exceeds the mounted-volume reader length (" +
                std::to_string(volumeSize) + " bytes)";
            return false;
        }
        const std::uint64_t dataClusters = (total - firstDataSector) / spc;
        if (dataClusters < 65525 || dataClusters > 0x0FFFFFED || root > dataClusters + 1) {
            errorText = "FAT32 root cluster is outside the data region";
            return false;
        }
        std::uint64_t fatEntriesRequired = 0;
        if (!checkedMul(dataClusters + 2, 4, fatEntriesRequired) || fatEntriesRequired > fatBytes ||
            fatOffset > totalBytes || fatBytes > totalBytes - fatOffset) {
            errorText = "FAT32 allocation table is truncated or inconsistent";
            return false;
        }
        geometry = {bps, spc, reserved, fats, fatSize, root, total, clusterBytes,
                    fatOffset, fatBytes, dataOffset, totalBytes,
                    static_cast<std::uint32_t>(dataClusters + 1)};
        return true;
    }

    bool read(std::uint64_t offset, std::uint32_t length, std::vector<std::uint8_t>& out)
    {
        if (isCancelled()) { errorText = "__CANCELLED__"; return false; }
        if (readBytes > limits.maximumReadBytes || length > limits.maximumReadBytes - readBytes) {
            errorText = "FAT32 scan read-byte limit reached";
            return false;
        }
        if (offset > geometry.volumeBytes && geometry.volumeBytes != 0) { errorText = "Volume-relative read begins outside the parsed volume"; return false; }
        const std::uint64_t limit = geometry.volumeBytes ? geometry.volumeBytes : volumeSize;
        if (!isValidVolumeReadRange(offset, length, limit)) { errorText = "Volume-relative read exceeds the selected volume"; return false; }
        if (!reader->revalidate(identity, errorText)) { staleVolume = true; return false; }
        if (!reader->readAt(offset, length, out, errorText) || out.size() != length) {
            std::string identityError;
            if (!reader->revalidate(identity, identityError)) {
                staleVolume = true;
                errorText = identityError;
            } else {
                readError = true;
                if (errorText.empty())
                    errorText = "Volume reader returned a short read";
            }
            return false;
        }
        readBytes += length;
        return true;
    }

    FileScanResult failReadOrError() const
    {
        if (errorText == "__CANCELLED__") return cancelledResult();
        if (staleVolume) return stale(errorText);
        return error(errorText.empty() ? "FAT32 metadata read failed" : errorText);
    }
    FileScanResult failRead() const { return failReadOrError(); }

    bool clusterOffset(std::uint32_t cluster, std::uint64_t& offset)
    {
        if (cluster < 2 || cluster > geometry.maximumCluster) return false;
        std::uint64_t relative = 0;
        if (!checkedMul(cluster - 2, geometry.clusterBytes, relative) ||
            !checkedAdd(geometry.dataOffset, relative, offset) ||
            offset > geometry.volumeBytes || geometry.clusterBytes > geometry.volumeBytes - offset)
            return false;
        return true;
    }

    bool nextCluster(std::uint32_t cluster, std::uint32_t& next)
    {
        std::uint64_t entryOffset = 0;
        if (!checkedMul(cluster, 4, entryOffset) || entryOffset > geometry.fatBytes || 4 > geometry.fatBytes - entryOffset) {
            errorText = "FAT32 cluster-chain entry exceeds the FAT";
            return false;
        }
        std::vector<std::uint8_t> raw;
        if (!readFatBytes(entryOffset, 4, raw)) return false;
        next = u32(raw.data()) & 0x0FFFFFFF;
        return true;
    }

    bool readFatBytes(std::uint64_t fatRelativeOffset, std::uint32_t length,
                      std::vector<std::uint8_t>& out)
    {
        if (!length || fatRelativeOffset > geometry.fatBytes || length > geometry.fatBytes - fatRelativeOffset) {
            errorText = "FAT32 cache read exceeds the validated allocation table";
            return false;
        }
        out.clear();
        out.reserve(length);
        const std::uint64_t sectorCount = std::max<std::uint64_t>(1, kMaximumFatCacheBytes / sectorSize);
        const std::uint64_t maxCacheBytes = sectorCount * sectorSize;
        std::uint64_t remaining = length;
        std::uint64_t current = fatRelativeOffset;
        while (remaining) {
            if (isCancelled()) { errorText = "__CANCELLED__"; return false; }
            const std::uint64_t sectorIndex = current / sectorSize;
            std::uint64_t sectorStart = 0;
            if (!checkedMul(sectorIndex,sectorSize,sectorStart)) {
                errorText = "FAT32 cache sector arithmetic overflow";
                return false;
            }
            if (sectorStart > geometry.fatBytes || sectorSize > geometry.fatBytes - sectorStart) {
                errorText = "FAT32 cache sector exceeds the validated allocation table";
                return false;
            }
            auto found = fatSectorCache.find(sectorIndex);
            if (found == fatSectorCache.end()) {
                std::uint64_t absoluteOffset = 0;
                if (!checkedAdd(geometry.fatOffset,sectorStart,absoluteOffset)) {
                    errorText = "FAT32 cache sector offset overflow";
                    return false;
                }
                std::vector<std::uint8_t> sectorBytes;
                if (!read(absoluteOffset,sectorSize,sectorBytes)) return false;
                if (sectorBytes.size()>maxCacheBytes) {
                    errorText="FAT32 sector exceeds the bounded cache capacity";
                    return false;
                }
                while (fatCacheBytes > maxCacheBytes - sectorBytes.size() && !fatSectorOrder.empty()) {
                    const std::uint64_t evict = fatSectorOrder.front();
                    fatSectorOrder.pop_front();
                    const auto old = fatSectorCache.find(evict);
                    if (old != fatSectorCache.end()) {
                        fatCacheBytes -= old->second.size();
                        fatSectorCache.erase(old);
                    }
                }
                if (!checkedAdd(fatCacheBytes,sectorBytes.size(),fatCacheBytes)) {
                    errorText="FAT32 cache size arithmetic overflow";
                    return false;
                }
                fatSectorOrder.push_back(sectorIndex);
                found = fatSectorCache.emplace(sectorIndex,std::move(sectorBytes)).first;
            } else if (!reader->revalidate(identity,errorText)) {
                staleVolume = true;
                return false;
            }
            const std::uint64_t within = current - sectorStart;
            const std::uint64_t amount = std::min<std::uint64_t>(remaining,sectorSize-within);
            if (within > found->second.size() || amount > found->second.size()-static_cast<std::size_t>(within)) {
                errorText = "FAT32 cached sector is shorter than the requested metadata";
                return false;
            }
            out.insert(out.end(),found->second.begin()+static_cast<std::ptrdiff_t>(within),
                       found->second.begin()+static_cast<std::ptrdiff_t>(within+amount));
            if (!checkedAdd(current,amount,current)) {
                errorText = "FAT32 cache read offset overflow";
                return false;
            }
            remaining -= amount;
        }
        return out.size()==length;
    }

    bool collectChain(std::uint32_t start, std::uint64_t bytesNeeded,
                      std::vector<std::uint32_t>& chain, bool requireEnough)
    {
        if (start < 2 || start > geometry.maximumCluster) return false;
        std::unordered_set<std::uint32_t> seen;
        std::uint32_t current = start;
        std::uint64_t collectedBytes = 0;
        for (std::uint32_t visits = 0; visits < limits.maximumClusterVisitsPerChain; ++visits) {
            if (!seen.insert(current).second) return false;
            chain.push_back(current);
            if (!checkedAdd(collectedBytes, geometry.clusterBytes, collectedBytes)) return false;
            if (!requireEnough && visits + 1 >= limits.maximumClusterVisitsPerChain) return false;
            if (requireEnough && collectedBytes >= bytesNeeded) return true;
            std::uint32_t next = 0;
            if (!nextCluster(current, next)) return false;
            if (next >= kEndOfChain) return !requireEnough || collectedBytes >= bytesNeeded;
            if (next == kBadCluster || next < 2 || next >= kReservedClusterStart || next > geometry.maximumCluster) return false;
            current = next;
        }
        return false;
    }

    std::vector<LfnPart> parseLfn(const std::uint8_t* raw)
    {
        LfnPart part;
        part.order = raw[0];
        part.checksum = raw[13];
        part.fieldsValid = raw[12] == 0 && u16(raw + 26) == 0 && (raw[0] & 0xA0) == 0;
        const std::uint8_t positions[] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
        for (std::size_t i = 0; i < 13; ++i) part.chars[i] = u16(raw + positions[i]);
        lfnParts.push_back(part);
        return lfnParts;
    }

    void appendInvalid(const Dir& dir, std::uint32_t containingCluster, std::uint32_t slot,
                       const std::string& reason, bool deletedMarkerObserved = false)
    {
        if (entries.size() >= limits.maximumEntries) { errorText = "FAT32 entry limit reached"; return; }
        FileEntry entry;
        entry.status = FileStatus::INVALID;
        entry.deletedMarkerObserved = deletedMarkerObserved;
        entry.isValid = false;
        entry.confidence = FileRecordConfidence::Invalid;
        entry.volumeIdentity = identity;
        entry.parentDirectoryCluster = dir.startCluster;
        if (dir.pathKnown) {
            entry.parentPath = utf8(dir.path);
            entry.parentPathKnown = true;
        }
        std::uint64_t clusterStart = 0;
        if (clusterOffset(containingCluster, clusterStart))
            entry.directoryEntryOffset = clusterStart + std::uint64_t(slot) * 32;
        entry.identity = volumeEntryIdentity(identity, dir.startCluster, containingCluster, slot);
        entry.entryIdentityAvailable = true;
        entry.reason = reason;
        entries.push_back(std::move(entry));
    }

    void parseDirectory(const Dir& dir)
    {
        if (dir.depth > limits.maximumDepth) { errorText = "FAT32 directory depth limit reached"; return; }
        std::vector<std::uint32_t> chain;
        if (!collectChain(dir.startCluster, 0, chain, false)) {
            if (errorText.empty()) errorText = "Directory cluster chain is invalid or cyclic";
            return;
        }
        allocatedClusters.insert(chain.begin(), chain.end());
        bool endOfDirectory = false;
        const std::uint64_t maxRunClusters = std::max<std::uint64_t>(1,kMaximumCoalescedDirectoryBytes / geometry.clusterBytes);
        for (std::size_t firstIndex=0; firstIndex<chain.size() && !endOfDirectory;) {
            if (isCancelled()) { errorText = "__CANCELLED__"; return; }
            std::uint64_t runCount=1;
            while (runCount<static_cast<std::uint64_t>(chain.size()-firstIndex) && runCount<maxRunClusters &&
                   std::uint64_t(chain[firstIndex+static_cast<std::size_t>(runCount-1)])+1==
                       chain[firstIndex+static_cast<std::size_t>(runCount)])
                ++runCount;
            std::uint64_t runBytes=0, offset=0;
            if (!checkedMul(runCount,geometry.clusterBytes,runBytes) ||
                runBytes>kMaximumCoalescedDirectoryBytes || runBytes>std::numeric_limits<std::uint32_t>::max() ||
                !clusterOffset(chain[firstIndex],offset) || !isValidVolumeReadRange(offset,runBytes,geometry.volumeBytes)) {
                errorText = "Coalesced directory read exceeds validated FAT32 bounds";
                return;
            }
            std::vector<std::uint8_t> bytes;
            if (!read(offset,static_cast<std::uint32_t>(runBytes),bytes)) return;
            for (std::uint64_t withinRun=0; withinRun<runCount && !endOfDirectory; ++withinRun) {
                if (isCancelled()) { errorText="__CANCELLED__"; return; }
                // Retain the previous per-cluster identity checks even when several
                // adjacent clusters were fetched by one bounded read.
                if (runCount>1 && !reader->revalidate(identity,errorText)) { staleVolume=true; return; }
                const std::uint32_t cluster=chain[firstIndex+static_cast<std::size_t>(withinRun)];
                std::uint64_t clusterOffsetInRun64=0;
                if (!checkedMul(withinRun,geometry.clusterBytes,clusterOffsetInRun64) ||
                    clusterOffsetInRun64>bytes.size() || geometry.clusterBytes>bytes.size()-clusterOffsetInRun64) {
                    errorText="Coalesced directory cluster offset overflow";
                    return;
                }
                const std::size_t clusterOffsetInRun=static_cast<std::size_t>(clusterOffsetInRun64);
                const std::uint8_t* clusterData=bytes.data()+clusterOffsetInRun;
                for (std::uint32_t slot=0; slot<geometry.clusterBytes/32; ++slot) {
                    const std::uint8_t* raw=clusterData+std::size_t(slot)*32;
                    const std::uint8_t first=raw[0];
                    if (first==0) { endOfDirectory=true; break; }
                    if (raw[11]==kLongNameAttribute) { parseLfn(raw); continue; }
                    const bool deleted=first==kDeleted;
                    if ((!deleted && first=='.') || (raw[11]&0x08)) { lfnParts.clear(); continue; }
                    if (raw[11]&0xC0) {
                        lfnParts.clear();
                        const std::uint32_t suspiciousCluster=((std::uint32_t(u16(raw+20))<<16)|u16(raw+26))&0x0FFFFFFF;
                        if (suspiciousCluster>=2 && suspiciousCluster<=geometry.maximumCluster) allocatedClusters.insert(suspiciousCluster);
                        appendInvalid(dir,cluster,slot,"Reserved FAT32 directory attribute bits are set.",deleted);
                        if (!errorText.empty()) return;
                        continue;
                    }
                    emitEntry(dir,cluster,slot,raw,deleted);
                    lfnParts.clear();
                    if (!errorText.empty()) return;
                }
            }
            firstIndex+=static_cast<std::size_t>(runCount);
        }
    }

    void emitEntry(const Dir& dir, std::uint32_t containingCluster, std::uint32_t slot,
                   const std::uint8_t* raw, bool deleted)
    {
        if (entries.size() >= limits.maximumEntries) { errorText = "FAT32 entry limit reached"; return; }
        const bool directory = (raw[11] & 0x10) != 0;
        const std::uint32_t startCluster = ((std::uint32_t(u16(raw + 20)) << 16) | u16(raw + 26)) & 0x0FFFFFFF;
        const std::uint64_t size = u32(raw + 28);
        bool lfnIsStructurallyValid = false;
        (void)decodeLongName(lfnParts, false, 0, lfnIsStructurallyValid);
        bool lfnValid = false;
        QString name;
        bool nameKnown = false;
        QString shortNameFragment;
        if (deleted) {
            // Deletion destroys the first short-name byte and normally each
            // LFN order byte. The checksum cannot prove the missing byte, so
            // never reverse-engineer a deleted original filename.
            shortNameFragment = decodeShortName(raw,true);
        } else {
            name = decodeLongName(lfnParts,true,shortChecksum(raw),lfnValid);
            nameKnown = lfnValid;
            if (!nameKnown) {
                name = decodeShortName(raw,false);
                nameKnown = !name.isEmpty();
            }
        }
        FileEntry entry;
        entry.volumeIdentity = identity;
        entry.parentDirectoryCluster = dir.startCluster;
        std::uint64_t clusterStart = 0;
        if (!clusterOffset(containingCluster, clusterStart)) {
            errorText = "Directory entry offset exceeds volume bounds";
            return;
        }
        entry.directoryEntryOffset = clusterStart + std::uint64_t(slot) * 32;
        entry.identity = volumeEntryIdentity(identity, dir.startCluster, containingCluster, slot);
        entry.entryIdentityAvailable = true;
        entry.status = deleted ? FileStatus::DELETED : FileStatus::EXISTING;
        entry.deletedMarkerObserved = deleted;
        entry.isDirectory = directory;
        entry.name = nameKnown ? utf8(name) : std::string();
        entry.nameKnown = nameKnown;
        if (deleted && !shortNameFragment.isEmpty()) {
            entry.shortNameFragment = utf8(shortNameFragment);
            entry.shortNameFragmentAvailable = true;
        }
        if (dir.pathKnown) {
            entry.parentPath = utf8(dir.path);
            entry.parentPathKnown = true;
        }
        if (dir.pathKnown && nameKnown)
            entry.fullPath = utf8(dir.path.isEmpty() ? name : dir.path + QLatin1Char('\\') + name);
        entry.sizeBytes = size;
        entry.sizeKnown = true;
        entry.confidence = deleted ? FileRecordConfidence::Candidate
                                   : (nameKnown ? FileRecordConfidence::Validated : FileRecordConfidence::Ambiguous);
        entry.operationEligible = false;
        if (deleted) {
            entry.reason = lfnIsStructurallyValid
                ? "A deleted directory-entry marker was identified. Its filename metadata is only a candidate, and content recoverability is unknown; operations are disabled."
                : "A deleted directory-entry marker and its volume-relative slot were identified. The filename may be unavailable and content recoverability is unknown; operations are disabled.";
            if (directory) entry.reason += " Deleted directories are not traversed.";
            entry.sizeKnown = size <= geometry.volumeBytes && (size == 0 || (startCluster >= 2 && startCluster <= geometry.maximumCluster));
            if (size > 0 && (startCluster < 2 || startCluster > geometry.maximumCluster)) {
                entry.confidence = FileRecordConfidence::Invalid;
                entry.status = FileStatus::INVALID;
                entry.isValid = false;
                entry.reason = "Deleted entry has an invalid starting cluster; size and content location are not trusted.";
            } else if (size == 0 && startCluster != 0 && (startCluster < 2 || startCluster > geometry.maximumCluster)) {
                entry.confidence = FileRecordConfidence::Invalid;
                entry.status = FileStatus::INVALID;
                entry.isValid = false;
                entry.reason = "Zero-length deleted entry has an inconsistent cluster value.";
            }
            if (startCluster >= 2 && startCluster <= geometry.maximumCluster) deletedStartClusters[startCluster].push_back(entries.size());
        } else {
            entry.reason = entry.isDirectory ? "Existing directory entry; no operation is enabled." : "Existing file entry; this scan does not test deleted-data recoverability.";
            if (!nameKnown || (size > 0 && (startCluster < 2 || startCluster > geometry.maximumCluster))) {
                entry.status = FileStatus::INVALID;
                entry.confidence = FileRecordConfidence::Invalid;
                entry.isValid = false;
                entry.reason = !nameKnown ? "Existing entry has no validated filename." : "Existing entry has an invalid starting cluster.";
            }
        }
        if (entry.status == FileStatus::INVALID)
            entry.sizeKnown = false;
        entries.push_back(entry);

        if (!deleted) {
            if (directory) {
                if (startCluster < 2 || startCluster > geometry.maximumCluster) {
                    entries.back().status = FileStatus::INVALID;
                    entries.back().confidence = FileRecordConfidence::Invalid;
                    entries.back().isValid = false;
                    entries.back().sizeKnown = false;
                    entries.back().reason = "Existing directory has an invalid starting cluster.";
                } else if (dir.depth < limits.maximumDepth) {
                    std::vector<std::uint32_t> directoryChain;
                    if (!collectChain(startCluster, 0, directoryChain, false)) {
                        if (!errorText.empty()) return;
                        allocatedClusters.insert(directoryChain.begin(), directoryChain.end());
                        entries.back().status = FileStatus::INVALID;
                        entries.back().confidence = FileRecordConfidence::Invalid;
                        entries.back().isValid = false;
                        entries.back().sizeKnown = false;
                        entries.back().reason = "Existing directory cluster chain is malformed or truncated.";
                        return;
                    }
                    allocatedClusters.insert(directoryChain.begin(), directoryChain.end());
                    queue.push_back({startCluster, startCluster,
                                     nameKnown ? (dir.path.isEmpty() ? name : dir.path + QLatin1Char('\\') + name) : dir.path,
                                     dir.depth + 1, dir.pathKnown && nameKnown});
                } else {
                    errorText = "FAT32 directory depth limit reached";
                }
            } else if (size > 0 && startCluster >= 2 && startCluster <= geometry.maximumCluster) {
                std::vector<std::uint32_t> fileChain;
                if (collectChain(startCluster, size, fileChain, true))
                    allocatedClusters.insert(fileChain.begin(), fileChain.end());
                else {
                    if (!errorText.empty()) return;
                    allocatedClusters.insert(fileChain.begin(), fileChain.end());
                    entries.back().status = FileStatus::INVALID;
                    entries.back().confidence = FileRecordConfidence::Invalid;
                    entries.back().isValid = false;
                    entries.back().sizeKnown = false;
                    entries.back().reason = "Existing file cluster chain is malformed or truncated.";
                }
            } else if (!directory && startCluster >= 2 && startCluster <= geometry.maximumCluster) {
                // A nonzero cluster on a zero-length live entry is inconsistent,
                // but it still prevents treating that cluster as free.
                allocatedClusters.insert(startCluster);
            }
        }
    }

    void markReusedDeletedClusters()
    {
        for (const auto& [cluster, indexes] : deletedStartClusters) {
            if (allocatedClusters.count(cluster) || indexes.size() > 1) {
                for (std::size_t index : indexes) {
                    FileEntry& entry = entries[index];
                    entry.confidence = FileRecordConfidence::Ambiguous;
                    entry.isValid = false;
                    entry.sizeKnown = false;
                    entry.reason = allocatedClusters.count(cluster)
                        ? "Starting cluster is allocated to an existing entry; deleted metadata may have been reused. Operations are disabled."
                        : "Multiple deleted entries reference the same starting cluster; identity is ambiguous. Operations are disabled.";
                }
            }
        }
    }

    std::shared_ptr<IReadOnlyVolumeReader> reader;
    std::string identity, errorText;
    FileScanner::Limits limits;
    FileScanner::CancellationCheck cancelled;
    Fat32Geometry geometry;
    std::uint32_t sectorSize = 0, directoryCount = 0;
    std::uint64_t volumeSize = 0, readBytes = 0;
    bool staleVolume = false;
    bool identityValidated = false;
    bool readError = false;
    std::vector<FileEntry> entries;
    std::vector<LfnPart> lfnParts;
    std::deque<Dir> queue;
    std::unordered_set<std::uint32_t> allocatedClusters;
    std::unordered_map<std::uint32_t, std::vector<std::size_t>> deletedStartClusters;
    std::unordered_map<std::uint64_t,std::vector<std::uint8_t>> fatSectorCache;
    std::deque<std::uint64_t> fatSectorOrder;
    std::uint64_t fatCacheBytes = 0;
};

class WindowsMountedVolumeReader final : public IReadOnlyVolumeReader {
public:
    explicit WindowsMountedVolumeReader(const DriveInfo& device)
        : root(std::wstring(1, static_cast<wchar_t>(device.letter)) + L":\\"),
          identity(device.volumeGuid), fs(device.fileSystem), bytes(0), sector(device.logicalSectorSize),
          handle(CreateFileW((std::wstring(L"\\\\.\\") + std::wstring(1, static_cast<wchar_t>(device.letter)) + L":").c_str(),
                             GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr))
    {
        if (handle == INVALID_HANDLE_VALUE) return;
        // Display capacity comes from GetDiskFreeSpaceEx. Parser bounds must
        // instead use the addressable length of this exact mounted-volume
        // handle, queried without translating to physical-disk offsets.
        GET_LENGTH_INFORMATION length{};
        DWORD returned = 0;
        if (DeviceIoControl(handle, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
                            &length, sizeof(length), &returned, nullptr) &&
            returned >= sizeof(length) && length.Length.QuadPart > 0)
            bytes = static_cast<std::uint64_t>(length.Length.QuadPart);
    }
    ~WindowsMountedVolumeReader() override { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    bool isOpen() const { return handle != INVALID_HANDLE_VALUE && bytes != 0; }
    std::string volumeIdentity() const override { return identity; }
    std::string fileSystem() const override { return fs; }
    std::uint64_t sizeBytes() const override { return bytes; }
    std::uint32_t logicalSectorSize() const override { return sector; }
    bool revalidate(const std::string& expectedIdentity, std::string& error) override
    {
        if (handle == INVALID_HANDLE_VALUE || lowerAscii(expectedIdentity) != lowerAscii(identity)) {
            error = "The selected volume handle or identity is stale";
            return false;
        }
        wchar_t current[MAX_PATH + 1] = {};
        if (!GetVolumeNameForVolumeMountPointW(root.c_str(), current, MAX_PATH + 1)) {
            error = "The selected mounted volume is no longer available";
            return false;
        }
        if (lowerAscii(utf8(QString::fromWCharArray(current))) != lowerAscii(identity)) {
            error = "The mount point now refers to a different volume";
            return false;
        }
        wchar_t currentFs[64] = {};
        if (!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr, currentFs, 64) ||
            lowerAscii(utf8(QString::fromWCharArray(currentFs))) != lowerAscii(fs)) {
            error = "The selected volume filesystem changed or could not be revalidated";
            return false;
        }
        return true;
    }
    bool readAt(std::uint64_t volumeOffset, std::uint32_t length,
                std::vector<std::uint8_t>& out, std::string& error) override
    {
        out.clear();
        if (!revalidate(identity, error)) return false;
        if (!isValidVolumeReadRange(volumeOffset, length, bytes) || volumeOffset > static_cast<std::uint64_t>(LLONG_MAX)) {
            error = "Volume-relative read exceeds the mounted volume boundary";
            return false;
        }
        const std::uint64_t alignedStart = volumeOffset - (volumeOffset % sector);
        std::uint64_t requestedEnd = 0;
        if (!checkedAdd(volumeOffset, length, requestedEnd)) {
            error = "Volume-relative read arithmetic overflow";
            return false;
        }
        std::uint64_t alignedEnd = requestedEnd;
        const std::uint64_t remainder = alignedEnd % sector;
        if (remainder && !checkedAdd(alignedEnd, sector - remainder, alignedEnd)) {
            error = "Sector-aligned read arithmetic overflow";
            return false;
        }
        if (alignedEnd > bytes || alignedEnd - alignedStart > std::numeric_limits<DWORD>::max()) {
            error = "Sector-aligned read exceeds the mounted-volume boundary";
            return false;
        }
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(alignedStart);
        if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) {
            error = "Could not seek within the selected volume";
            return false;
        }
        std::vector<std::uint8_t> aligned(static_cast<std::size_t>(alignedEnd - alignedStart));
        DWORD received = 0;
        if (!ReadFile(handle, aligned.data(), static_cast<DWORD>(aligned.size()), &received, nullptr) || received != aligned.size()) {
            error = "The selected volume returned a short or failed read";
            return false;
        }
        const std::size_t crop = static_cast<std::size_t>(volumeOffset - alignedStart);
        out.assign(aligned.begin() + static_cast<std::ptrdiff_t>(crop),
                   aligned.begin() + static_cast<std::ptrdiff_t>(crop + length));
        return revalidate(identity, error);
    }
private:
    std::wstring root;
    std::string identity, fs;
    std::uint64_t bytes;
    std::uint32_t sector;
    HANDLE handle;
};
}

bool isListedFileRecord(const FileEntry& entry)
{
    return !entry.isDirectory;
}

FileScanCounts aggregateFileEntries(const std::vector<FileEntry>& entries)
{
    FileScanCounts counts;
    for (const FileEntry& entry : entries) {
        if (entry.status == FileStatus::INVALID) ++counts.invalidRecords;
        if (entry.confidence == FileRecordConfidence::Ambiguous) ++counts.ambiguousRecords;
        if (entry.isDirectory) continue;
        if (entry.status == FileStatus::EXISTING) {
            ++counts.existingFiles;
        } else if (entry.status == FileStatus::DELETED) {
            ++counts.deletedEntryCandidates;
            if (!entry.nameKnown) ++counts.deletedNamesUnknown;
        } else if (entry.status == FileStatus::INVALID) {
            ++counts.invalidFileRecords;
            if (entry.deletedMarkerObserved && !entry.nameKnown) ++counts.deletedNamesUnknown;
        }
    }
    return counts;
}

FileScanResult FileScanner::scanFat32(const std::shared_ptr<IReadOnlyVolumeReader>& reader,
                                    const std::string& expectedVolumeIdentity,
                                    CancellationCheck cancelled)
{
    return scanFat32(reader, expectedVolumeIdentity, Limits{}, std::move(cancelled));
}

FileScanResult FileScanner::scanFat32(const std::shared_ptr<IReadOnlyVolumeReader>& reader,
                                    const std::string& expectedVolumeIdentity,
                                    const Limits& limits,
                                    CancellationCheck cancelled)
{
    return FatParser(reader, expectedVolumeIdentity, limits, std::move(cancelled)).run();
}

std::shared_ptr<IReadOnlyVolumeReader> FileScanner::openMountedVolumeReadOnly(
    const DriveInfo& device, std::string& error)
{
    if (!device.connected || !device.letter || device.volumeGuid.empty() ||
        device.capacityBytes == 0 || !device.capacityAvailable || device.logicalSectorSize == 0) {
        error = "Selected device metadata is incomplete; volume reader was not opened";
        return {};
    }
    auto reader = std::make_shared<WindowsMountedVolumeReader>(device);
    if (!reader->isOpen() || !reader->revalidate(device.volumeGuid, error)) {
        if (error.empty()) error = "Could not open the selected mounted volume read-only or query its accessible length";
        return {};
    }
    return reader;
}

bool isValidVolumeReadRange(std::uint64_t offsetBytes, std::uint64_t lengthBytes,
                            std::uint64_t volumeSizeBytes)
{
    return offsetBytes <= volumeSizeBytes && lengthBytes <= volumeSizeBytes - offsetBytes;
}
