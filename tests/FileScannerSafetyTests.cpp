#include "../src/FileScanner.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <vector>

static void makeFile(const QString& path, const QByteArray& body)
{
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write(body) == body.size());
}

class FakeVolume final : public IReadOnlyVolumeReader {
public:
    std::string id = "volume:test-guid";
    std::string fs = "FAT32";
    std::uint32_t sector = 512;
    std::vector<std::uint8_t> image;
    bool stale = false;
    bool shortRead = false;
    unsigned int validations = 0;
    unsigned int staleOnValidation = 0;
    std::uint64_t declaredSize = 0;
    std::string volumeIdentity() const override { return id; }
    std::string fileSystem() const override { return fs; }
    std::uint64_t sizeBytes() const override { return declaredSize ? declaredSize : image.size(); }
    std::uint32_t logicalSectorSize() const override { return sector; }
    bool revalidate(const std::string& expected, std::string& error) override {
        ++validations;
        if (stale || expected != id || (staleOnValidation && validations >= staleOnValidation)) { error = "stale synthetic volume"; return false; }
        return true;
    }
    bool readAt(std::uint64_t offset, std::uint32_t length, std::vector<std::uint8_t>& out, std::string& error) override {
        if (offset > image.size() || length > image.size() - offset) { error = "out of bounds"; return false; }
        const std::size_t amount = shortRead && length ? length - 1 : length;
        out.assign(image.begin() + static_cast<std::ptrdiff_t>(offset), image.begin() + static_cast<std::ptrdiff_t>(offset + amount));
        return true;
    }
};

static void put16(std::vector<std::uint8_t>& b, std::size_t p, std::uint16_t v) { b[p]=v&255; b[p+1]=(v>>8)&255; }
static void put32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t v) { for(int i=0;i<4;++i)b[p+i]=(v>>(8*i))&255; }
static constexpr std::uint32_t BPS=512, FAT_START_SECTOR=32, FAT_SECTORS=512, DATA_START_SECTOR=32+2*FAT_SECTORS;
static constexpr std::uint32_t DATA_CLUSTERS=65525, TOTAL_SECTORS=DATA_START_SECTOR+DATA_CLUSTERS;
static constexpr std::size_t DATA_OFFSET=std::size_t(DATA_START_SECTOR)*BPS;
static std::size_t clusterOffset(std::uint32_t cluster) { return DATA_OFFSET + std::size_t(cluster-2)*BPS; }
static void setFat(FakeVolume& v, std::uint32_t cluster, std::uint32_t next) { put32(v.image, FAT_START_SECTOR*BPS + cluster*4, next); }
static void writeShort(std::vector<std::uint8_t>& b, std::size_t p, const std::string& name, std::uint8_t attr, std::uint32_t cluster, std::uint32_t size, bool deleted=false) {
    std::fill(b.begin()+p,b.begin()+p+32,0);
    for (std::size_t i=0;i<11;++i)b[p+i]=i<name.size()?static_cast<std::uint8_t>(name[i]):' ';
    if(deleted)b[p]=0xE5;
    b[p+11]=attr; put16(b,p+20,(cluster>>16)&0xFFFF); put16(b,p+26,cluster&0xFFFF); put32(b,p+28,size);
}
static std::uint8_t shortChecksum(const std::uint8_t* name) {
    std::uint8_t sum=0;
    for(int i=0;i<11;++i)sum=static_cast<std::uint8_t>(((sum&1)?0x80:0)+(sum>>1)+name[i]);
    return sum;
}
static std::shared_ptr<FakeVolume> makeImage() {
    auto v=std::make_shared<FakeVolume>();
    v->image.resize(std::size_t(TOTAL_SECTORS)*BPS);
    auto& b=v->image;
    put16(b,11,BPS); b[13]=1; put16(b,14,FAT_START_SECTOR); b[16]=2;
    put16(b,17,0); put16(b,19,0); put16(b,22,0); put32(b,32,TOTAL_SECTORS); put32(b,36,FAT_SECTORS); put16(b,42,0); put32(b,44,2); b[510]=0x55; b[511]=0xAA;
    for (std::uint32_t c=2;c<=6;++c)setFat(*v,c,0x0FFFFFFF);
    auto root=clusterOffset(2);
    writeShort(b,root,"DELETED TXT",0x20,3,500,true);
    writeShort(b,root+32,"SAME    TXT",0x20,4,3);
    writeShort(b,root+64,"SUBDIR     ",0x10,5,0);
    b[root+96]=0;
    auto sub=clusterOffset(5);
    writeShort(b,sub,"SAME    TXT",0x20,3,11,true);
    writeShort(b,sub+32,"SAME    TXT",0x20,6,11);
    b[sub+64]=0;
    return v;
}
static FileScanResult scan(const std::shared_ptr<FakeVolume>& image, FileScanner::CancellationCheck cancel = {}) {
    return FileScanner::scanFat32(image,image->id,FileScanner::Limits{},std::move(cancel));
}

int main()
{
    assert(isValidVolumeReadRange(0,512,512));
    assert(!isValidVolumeReadRange(UINT64_MAX-3,8,UINT64_MAX));
    assert(!isValidVolumeReadRange(UINT64_MAX,1,UINT64_MAX));
    QTemporaryDir temporary(QDir::currentPath() + QStringLiteral("/file-scan-safety-XXXXXX"));
    assert(temporary.isValid());
    const QString root = temporary.path();
    assert(QDir(root).mkpath(QStringLiteral("one")));
    assert(QDir(root).mkpath(QStringLiteral("two/nested")));
    makeFile(root + QStringLiteral("/one/same.txt"), "one");
    makeFile(root + QStringLiteral("/two/nested/same.txt"), "two");
    makeFile(root + QStringLiteral("/two/nested/файл-☂.txt"), "unicode");
    FileScanner scanner;
    const FileScanResult existing = scanner.scanExistingRoot(root);
    assert(existing.status == FileScanStatus::Success && existing.entries.size() == 6);
    std::set<std::string> paths;
    int duplicateNames=0; bool unicodePath=false;
    for(const auto& entry:existing.entries) { paths.insert(entry.fullPath); if(entry.name=="same.txt")++duplicateNames; if(entry.name.find("файл")!=std::string::npos)unicodePath=true; }
    assert(paths.size()==existing.entries.size() && duplicateNames==2 && unicodePath);
    assert(scanner.scanExisting("C:\\").empty());
    assert(scanner.scanDeleted("\\\\.\\PhysicalDrive0").status==FileScanStatus::Unsupported);

    auto image=makeImage();
    auto parsed=scan(image);
    assert(parsed.status==FileScanStatus::Success && parsed.entries.size()==5);
    int deletedCount=0, existingCount=0; std::set<std::string> identities; bool nested=false;
    for(const auto& entry:parsed.entries) {
        assert(entry.entryIdentityAvailable && !entry.identity.empty() && !entry.operationEligible);
        identities.insert(entry.identity);
        if(entry.status==FileStatus::DELETED) { ++deletedCount; assert(!entry.nameKnown); assert(entry.confidence==FileRecordConfidence::Ambiguous); }
        if(entry.status==FileStatus::EXISTING) ++existingCount;
        if(entry.status==FileStatus::DELETED && entry.parentDirectoryCluster==5) { nested=true; assert(entry.parentPath=="SUBDIR"); }
    }
    assert(identities.size()==parsed.entries.size() && deletedCount==2 && existingCount==3 && nested);
    const DeletedEntryOperationTarget notReady{};
    assert(!notReady.validated && notReady.extents.empty());
    int namedSame=0;
    std::set<std::string> samePaths;
    for(const auto& entry:parsed.entries) if(entry.status==FileStatus::EXISTING && entry.name=="SAME.TXT") { ++namedSame; samePaths.insert(entry.fullPath); }
    assert(namedSame==2 && samePaths.size()==2);

    auto longNameImage=makeImage();
    const std::size_t rootOffset=clusterOffset(2);
    std::fill(longNameImage->image.begin()+rootOffset,longNameImage->image.begin()+rootOffset+BPS,0);
    const std::string shortName="LONGNA~1TXT";
    const std::string longName="LongName.txt";
    auto& bytes=longNameImage->image;
    bytes[rootOffset]=0x41; bytes[rootOffset+11]=0x0F; bytes[rootOffset+12]=0;
    bytes[rootOffset+13]=shortChecksum(reinterpret_cast<const std::uint8_t*>(shortName.data()));
    const int positions[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    for(int i=0;i<13;++i) put16(bytes,rootOffset+positions[i],i<static_cast<int>(longName.size())?static_cast<unsigned char>(longName[i]):(i==static_cast<int>(longName.size())?0:0xFFFF));
    std::copy(shortName.begin(),shortName.end(),bytes.begin()+rootOffset+32);
    bytes[rootOffset+43]=0x20; put16(bytes,rootOffset+52,0); put16(bytes,rootOffset+58,6); put32(bytes,rootOffset+60,10); bytes[rootOffset+64]=0;
    auto longNameResult=scan(longNameImage);
    assert(longNameResult.status==FileScanStatus::Success && longNameResult.entries.size()==1);
    assert(longNameResult.entries.front().status==FileStatus::EXISTING);
    assert(longNameResult.entries.front().nameKnown && longNameResult.entries.front().name==longName);

    auto malformed=makeImage();
    writeShort(malformed->image,clusterOffset(2),"BADATTR TXT",0xC0,6,12);
    malformed->image[clusterOffset(2)+32]=0;
    auto malformedResult=scan(malformed);
    assert(malformedResult.status==FileScanStatus::Success && malformedResult.entries.size()==1);
    assert(malformedResult.entries.front().status==FileStatus::INVALID);

    auto stale=makeImage(); stale->stale=true; assert(scan(stale).status==FileScanStatus::StaleVolume);
    auto disconnectDuringRead=makeImage(); disconnectDuringRead->staleOnValidation=3;
    assert(scan(disconnectDuringRead).status==FileScanStatus::StaleVolume);
    auto shortRead=makeImage(); shortRead->shortRead=true; assert(scan(shortRead).status==FileScanStatus::Error);
    auto cancelled=makeImage(); assert(scan(cancelled,[](){return true;}).status==FileScanStatus::Cancelled);
    auto midCancel=makeImage(); int cancelChecks=0;
    assert(scan(midCancel,[&cancelChecks](){return ++cancelChecks>=3;}).status==FileScanStatus::Cancelled);
    auto outOfBounds=makeImage(); outOfBounds->declaredSize=BPS; assert(scan(outOfBounds).status==FileScanStatus::Error);
    auto overflow=makeImage(); put32(overflow->image,32,0xFFFFFFFFU); assert(scan(overflow).status==FileScanStatus::Error);
    auto wrongFilesystem=makeImage(); wrongFilesystem->fs="exFAT"; assert(scan(wrongFilesystem).status==FileScanStatus::Unsupported);
    auto badSector=makeImage(); badSector->sector=0; assert(scan(badSector).status==FileScanStatus::Error);
    auto limited=makeImage(); FileScanner::Limits limits; limits.maximumReadBytes=512;
    assert(FileScanner::scanFat32(limited,limited->id,limits).status==FileScanStatus::Error);
    auto invalidCluster=makeImage(); writeShort(invalidCluster->image,clusterOffset(2),"BROKEN  TXT",0x20,0x0FFFFFFF,4096,true);
    auto invalidResult=scan(invalidCluster); assert(invalidResult.status==FileScanStatus::Success && !invalidResult.entries.empty());
    assert(invalidResult.entries.front().status==FileStatus::INVALID);
    auto reused=makeImage(); writeShort(reused->image,clusterOffset(2)+32,"LIVE    TXT",0x20,3,500);
    auto reusedResult=scan(reused); assert(reusedResult.status==FileScanStatus::Success);
    bool markedReused=false;
    for(const auto& entry:reusedResult.entries) if(entry.status==FileStatus::DELETED && entry.parentDirectoryCluster==2)
        markedReused=entry.confidence==FileRecordConfidence::Ambiguous && entry.reason.find("allocated")!=std::string::npos;
    assert(markedReused);
    return 0;
}
