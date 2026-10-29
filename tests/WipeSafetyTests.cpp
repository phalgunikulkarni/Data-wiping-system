#include "../src/WipeEngine.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <iostream>

class MemoryStorage final : public IStorageDevice {
public:
    std::vector<unsigned char> bytes = std::vector<unsigned char>(8192, 0xA5);
    bool identityMatches = true;
    bool allowLock = true;
    bool corruptWrites = false;
    bool locked = false;
    int writes = 0;

    bool revalidate(const StorageTargetDescriptor&, std::string& error) override {
        if (!identityMatches) error = "stale target";
        return identityMatches;
    }
    bool lockVolume(std::string& error) override {
        if (!allowLock) error = "mock lock failure";
        locked = allowLock;
        return allowLock;
    }
    bool unlockVolume(std::string& error) noexcept override {
        (void)error;
        locked = false;
        return true;
    }
    bool readAt(std::uint64_t offset, unsigned char* data, std::uint32_t length, std::string& error) override {
        if (!locked || offset > bytes.size() || length > bytes.size() - offset) {
            error = "mock read outside device or unlocked";
            return false;
        }
        std::memcpy(data, bytes.data() + offset, length);
        return true;
    }
    bool writeAt(std::uint64_t offset, const unsigned char* data, std::uint32_t length, std::string& error) override {
        if (!locked || offset > bytes.size() || length > bytes.size() - offset) {
            error = "mock write outside device or unlocked";
            return false;
        }
        std::memcpy(bytes.data() + offset, data, length);
        if (corruptWrites && length) bytes[offset + length / 2] ^= 0x01;
        ++writes;
        return true;
    }
    bool flush(std::string&) override { return locked; }
};

static StorageTargetDescriptor target()
{
    return {"volume-guid:test", 3, 1024, 4096, 4096, 8192, 512};
}

int main()
{
    const auto mapped = mapVolumeExtentsToPhysical(target(), {{100, 700}});
    assert(mapped.success);
    assert(mapped.extents.size() == 1);
    assert(mapped.extents[0].offsetBytes == 1124);
    assert(mapped.extents[0].lengthBytes == 700);

    assert(!mapVolumeExtentsToPhysical(target(), {{4090, 16}}).success);
    assert(!mapVolumeExtentsToPhysical(target(), {{100, 0}}).success);
    auto incomplete = target(); incomplete.volumeId.clear();
    assert(!mapVolumeExtentsToPhysical(incomplete, {{1, 1}}).success);
    auto overflow = target(); overflow.partitionOffsetBytes = UINT64_MAX - 511; overflow.partitionLengthBytes = 1024;
    assert(!mapVolumeExtentsToPhysical(overflow, {{1, 1}}).success);
    auto unaligned = target(); unaligned.partitionOffsetBytes++;
    assert(!mapVolumeExtentsToPhysical(unaligned, {{1, 1}}).success);

    MemoryStorage storage;
    const auto original = storage.bytes;
    WipeEngine engine;
    const WipeResult result = engine.wipeVolumeExtents(target(), {{100, 700}}, WipeMethod::ZEROS, storage);
    assert(result.success && result.verified && result.verificationPerformed);
    assert(result.bytesWiped == 700 && result.totalBytes == 700);
    assert(!storage.locked);
    for (std::size_t i = 0; i < storage.bytes.size(); ++i) {
        if (i >= 1124 && i < 1824) assert(storage.bytes[i] == 0);
        else assert(storage.bytes[i] == original[i]);
    }

    MemoryStorage stale;
    stale.identityMatches = false;
    const auto staleResult = engine.wipeVolumeExtents(target(), {{100, 700}}, WipeMethod::ZEROS, stale);
    assert(!staleResult.success && stale.writes == 0 && !stale.locked);

    MemoryStorage lockFailure;
    lockFailure.allowLock = false;
    const auto lockResult = engine.wipeVolumeExtents(target(), {{100, 700}}, WipeMethod::ZEROS, lockFailure);
    assert(!lockResult.success && lockFailure.writes == 0);

    MemoryStorage corrupted;
    corrupted.corruptWrites = true;
    const auto verifyFailure = engine.wipeVolumeExtents(target(), {{100, 700}}, WipeMethod::ZEROS, corrupted);
    assert(!verifyFailure.success && verifyFailure.verificationPerformed && !verifyFailure.verified);
    assert(!corrupted.locked);

    const auto oldRawPath = engine.wipe("\\\\.\\PhysicalDrive0", 8192, WipeMethod::ZEROS, {});
    assert(!oldRawPath.success && oldRawPath.bytesWiped == 0);
    const auto oldExtents = engine.wipeExtents("\\\\.\\PhysicalDrive0", "E:", {{4096, 512}}, WipeMethod::ZEROS, {});
    assert(!oldExtents.success && oldExtents.bytesWiped == 0);
    assert(engine.wipeSectors("\\\\.\\PhysicalDrive0", 0, 512, WipeMethod::ZEROS, {}).status == WipeStatus::Unsupported);

    QTemporaryDir temporary(QDir::currentPath() + QStringLiteral("/wipe-safety-XXXXXX"));
    assert(temporary.isValid());
    const QString filePath = temporary.filePath(QStringLiteral("nested-target.txt"));
    QFile targetFile(filePath);
    if (!targetFile.open(QIODevice::WriteOnly)) { std::cerr << "temp file open failed: " << targetFile.errorString().toStdString() << " path=" << filePath.toStdString() << std::endl; return 2; }
    assert(targetFile.write("do not alter") == 12);
    targetFile.close();
    const WipeResult fileDisabled = engine.wipeFile(filePath, WipeMethod::ZEROS);
    assert(!fileDisabled.success && fileDisabled.status == WipeStatus::Unsupported);
    assert(targetFile.open(QIODevice::ReadOnly));
    assert(targetFile.readAll() == QByteArrayLiteral("do not alter"));
    targetFile.close();
    const WipeResult folderDisabled = engine.wipeFolder(temporary.path(), WipeMethod::ZEROS);
    assert(!folderDisabled.success && QFileInfo::exists(filePath));
    return 0;
}
