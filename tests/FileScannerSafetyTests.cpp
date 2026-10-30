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
    bool failRead = false;
    unsigned int validations = 0;
    unsigned int staleOnValidation = 0;
    std::uint64_t declaredSize = 0;
    std::uint64_t readAtCalls = 0;
    std::uint64_t bytesRead = 0;
    std::uint32_t maximumReadRequest = 0;
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
        ++readAtCalls;
        maximumReadRequest=std::max(maximumReadRequest,length);
        if (failRead) { error = "synthetic reader error"; return false; }
        if (offset > image.size() || length > image.size() - offset) { error = "out of bounds"; return false; }
        const std::size_t amount = shortRead && length ? length - 1 : length;
        bytesRead+=amount;
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
    for (std::uint32_t c=2;c<=7;++c)setFat(*v,c,0x0FFFFFFF);
    auto root=clusterOffset(2);
    writeShort(b,root,"DELETED TXT",0x20,3,500,true);
    writeShort(b,root+32,"SAME    TXT",0x20,4,3);
    writeShort(b,root+64,"SUBDIR     ",0x10,5,0);
    b[root+96]=0;
    auto sub=clusterOffset(5);
    writeShort(b,sub,"SAME    TXT",0x20,7,11,true);
    writeShort(b,sub+32,"SAME    TXT",0x20,6,11);
    b[sub+64]=0;
    return v;
}
static std::shared_ptr<FakeVolume> makeManyDirectoryImage(std::uint32_t directoryCount,std::uint32_t clusterSpacing=1)
{
    auto v=makeImage();
    const std::uint32_t rootClusters=(directoryCount+15)/16+1;
    for (std::uint32_t c=2;c<2+rootClusters;++c) {
        const std::uint32_t next=c+1<2+rootClusters ? c+1 : 0x0FFFFFFF;
        setFat(*v,c,next);
        std::fill(v->image.begin()+clusterOffset(c),v->image.begin()+clusterOffset(c)+BPS,0);
    }
    for (std::uint32_t i=0;i<directoryCount;++i) {
        const std::uint32_t directoryCluster=1024+i*clusterSpacing;
        setFat(*v,directoryCluster,0x0FFFFFFF);
        std::fill(v->image.begin()+clusterOffset(directoryCluster),v->image.begin()+clusterOffset(directoryCluster)+BPS,0);
        const std::uint32_t rootSlot=i;
        const std::uint32_t rootCluster=2+rootSlot/16;
        const std::uint32_t slot=rootSlot%16;
        const std::string name="DIR"+std::to_string(100000+i);
        writeShort(v->image,clusterOffset(rootCluster)+slot*32,name,0x10,directoryCluster,0);
    }
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
    const FileScanResult missingRoot = scanner.scanExistingRoot(
        QDir(root).filePath(QStringLiteral("missing-root")));
    assert(missingRoot.status == FileScanStatus::Error && missingRoot.entries.empty());
    assert(scanner.scanDeleted("\\\\.\\PhysicalDrive0").status==FileScanStatus::Unsupported);

    auto image=makeImage();
    auto parsed=scan(image);
    assert(parsed.status==FileScanStatus::Success && parsed.entries.size()==5);
    assert(parsed.volumeIdentityValidated);
    int deletedCount=0, existingCount=0; std::set<std::string> identities; bool nested=false;
    for(const auto& entry:parsed.entries) {
        assert(entry.entryIdentityAvailable && !entry.identity.empty() && !entry.operationEligible);
        identities.insert(entry.identity);
        if(entry.status==FileStatus::DELETED) {
            ++deletedCount; assert(!entry.nameKnown); assert(entry.confidence==FileRecordConfidence::Candidate);
            assert(entry.shortNameFragmentAvailable && !entry.shortNameFragment.empty() && entry.shortNameFragment[0]=='?');
        }
        if(entry.status==FileStatus::EXISTING) ++existingCount;
        if(entry.status==FileStatus::DELETED && entry.parentDirectoryCluster==5) { nested=true; assert(entry.parentPath=="SUBDIR"); }
    }
    assert(identities.size()==parsed.entries.size() && deletedCount==2 && existingCount==3 && nested);
    const FileScanCounts fileCounts=aggregateFileEntries(parsed.entries);
    assert(fileCounts.existingFiles==2 && fileCounts.deletedEntryCandidates==2);
    assert(fileCounts.invalidFileRecords==0 && fileCounts.invalidRecords==0 && fileCounts.ambiguousRecords==0);
    const std::uint64_t legacyAllExistingRecords=static_cast<std::uint64_t>(std::count_if(parsed.entries.begin(),parsed.entries.end(),[](const FileEntry& entry) {
        return entry.status==FileStatus::EXISTING;
    }));
    assert(legacyAllExistingRecords==3 && legacyAllExistingRecords!=fileCounts.existingFiles); // One of three is a directory.
    std::vector<FileEntry> samePathRecords;
    FileEntry samePathA; samePathA.status=FileStatus::EXISTING; samePathA.fullPath="same.txt";
    FileEntry samePathB=samePathA; samePathB.identity="different-directory-slot";
    samePathRecords={samePathA,samePathB};
    assert(aggregateFileEntries(samePathRecords).existingFiles==2); // Distinct records are counted, not deduplicated by path/name.
    const DeletedEntryOperationTarget notReady{};
    assert(!notReady.validated && notReady.extents.empty());
    int namedSame=0;
    std::set<std::string> samePaths;
    for(const auto& entry:parsed.entries) if(entry.status==FileStatus::EXISTING && entry.name=="SAME.TXT") { ++namedSame; samePaths.insert(entry.fullPath); }
    assert(namedSame==2 && samePaths.size()==2);

    // One FAT32 long-name slot at the last slot of one directory cluster and
    // its short entry at the first slot of the next cluster remain one record.
    auto boundaryLfn=makeImage();
    auto& boundaryBytes=boundaryLfn->image;
    setFat(*boundaryLfn,2,3); setFat(*boundaryLfn,3,0x0FFFFFFF);
    std::fill(boundaryBytes.begin()+clusterOffset(2),boundaryBytes.begin()+clusterOffset(2)+BPS,0);
    std::fill(boundaryBytes.begin()+clusterOffset(3),boundaryBytes.begin()+clusterOffset(3)+BPS,0);
    for (std::uint32_t slot=0;slot<15;++slot)
        writeShort(boundaryBytes,clusterOffset(2)+slot*32,"DEL0000000",0x20,0,0,true);
    const std::size_t lfnOffset=clusterOffset(2)+15*32;
    const std::string boundaryShortName="ACROSS~1TXT";
    const std::string boundaryLongName="Across.txt";
    boundaryBytes[lfnOffset]=0x41; boundaryBytes[lfnOffset+11]=0x0F; boundaryBytes[lfnOffset+12]=0;
    boundaryBytes[lfnOffset+13]=shortChecksum(reinterpret_cast<const std::uint8_t*>(boundaryShortName.data()));
    const int lfnPositions[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    for (int i=0;i<13;++i) put16(boundaryBytes,lfnOffset+lfnPositions[i],
        i<static_cast<int>(boundaryLongName.size()) ? static_cast<unsigned char>(boundaryLongName[static_cast<std::size_t>(i)])
        : (i==static_cast<int>(boundaryLongName.size()) ? 0 : 0xFFFF));
    writeShort(boundaryBytes,clusterOffset(3),boundaryShortName,0x20,4,5);
    boundaryBytes[clusterOffset(3)+32]=0;
    const auto boundaryResult=scan(boundaryLfn);
    assert(boundaryResult.status==FileScanStatus::Success && boundaryResult.entries.size()==16);
    bool foundBoundaryName=false;
    for (const auto& entry:boundaryResult.entries)
        if (entry.name==boundaryLongName && entry.status==FileStatus::EXISTING) foundBoundaryName=true;
    assert(foundBoundaryName);
    assert(boundaryLfn->readAtCalls==3); // Boot, one FAT sector, and one coalesced two-cluster directory read.

    // The valid BPB uses sector-aligned 32-bit FAT entries, so a FAT entry
    // cannot straddle a 512-byte boundary. Adjacent entries on opposite sides
    // of that boundary still cause separate bounded cache misses.
    auto fatBoundary=makeImage();
    put32(fatBoundary->image,44,127);
    setFat(*fatBoundary,127,128); setFat(*fatBoundary,128,0x0FFFFFFF);
    std::fill(fatBoundary->image.begin()+clusterOffset(127),fatBoundary->image.begin()+clusterOffset(127)+BPS,0);
    std::fill(fatBoundary->image.begin()+clusterOffset(128),fatBoundary->image.begin()+clusterOffset(128)+BPS,0);
    for (std::uint32_t slot=0;slot<16;++slot)
        writeShort(fatBoundary->image,clusterOffset(127)+slot*32,"DEL0000000",0x20,0,0,true);
    const auto fatBoundaryResult=scan(fatBoundary);
    assert(fatBoundaryResult.status==FileScanStatus::Success && fatBoundaryResult.entries.size()==16);
    assert(fatBoundary->readAtCalls==4); // Boot, two adjacent FAT sectors, one coalesced directory range.

    // Counting-reader benchmark: many directory chains revisit nearby FAT
    // entries, while adjacent root clusters can be read in one bounded request.
    auto benchmarkImage=makeManyDirectoryImage(250);
    const auto benchmarkResult=scan(benchmarkImage);
    assert(benchmarkResult.status==FileScanStatus::Success && benchmarkResult.entries.size()==250);
    const std::uint64_t benchmarkRootClusters=(250+15)/16+1;
    const std::uint64_t baselineFatReads=benchmarkRootClusters+500; // Root chain and two walks per child directory.
    const std::uint64_t baselineDirectoryReads=benchmarkRootClusters+250;
    const std::uint64_t estimatedUncachedCalls=1+baselineFatReads+baselineDirectoryReads;
    const std::uint64_t estimatedUncachedBytes=BPS+baselineFatReads*4+baselineDirectoryReads*BPS;
    std::cout << "Synthetic FAT32 I/O benchmark: readAt calls optimized=" << benchmarkImage->readAtCalls
              << ", estimated uncached=" << estimatedUncachedCalls
              << "; bytes optimized=" << benchmarkImage->bytesRead
              << ", estimated uncached=" << estimatedUncachedBytes << '\n';
    assert(benchmarkImage->readAtCalls<estimatedUncachedCalls);
    assert(benchmarkImage->bytesRead<estimatedUncachedBytes);
    assert(benchmarkImage->maximumReadRequest<=1024*1024);
    const std::uint64_t firstScanCalls=benchmarkImage->readAtCalls;
    const std::uint64_t firstScanBytes=benchmarkImage->bytesRead;
    const auto resetResult=scan(benchmarkImage);
    assert(resetResult.status==FileScanStatus::Success && resetResult.entries.size()==250);
    assert(benchmarkImage->readAtCalls-firstScanCalls==firstScanCalls);
    assert(benchmarkImage->bytesRead-firstScanBytes==firstScanBytes);

    // A wider working set crosses the 64 KiB FAT-sector cache capacity. Scan
    // remains bounded, succeeds, and batches no directory read above 1 MiB.
    auto boundedCacheImage=makeManyDirectoryImage(300,64);
    const auto boundedCacheResult=scan(boundedCacheImage);
    assert(boundedCacheResult.status==FileScanStatus::Success && boundedCacheResult.entries.size()==300);
    assert(boundedCacheImage->maximumReadRequest<=1024*1024);
    // The directory workload touches more than 128 distinct 512-byte FAT
    // sectors; a 64 KiB cache therefore evicts and later rereads some sectors.
    assert(boundedCacheImage->readAtCalls>453);

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
    auto malformedExistingLfn=std::make_shared<FakeVolume>(*longNameImage);
    malformedExistingLfn->image[rootOffset+13]^=0x01;
    const auto malformedExistingLfnResult=scan(malformedExistingLfn);
    assert(malformedExistingLfnResult.status==FileScanStatus::Success);
    assert(malformedExistingLfnResult.entries.front().nameKnown);
    assert(malformedExistingLfnResult.entries.front().name=="LONGNA~1.TXT"); // Invalid LFN falls back to the validated short name.

    auto deletedLongNameImage=std::make_shared<FakeVolume>(*longNameImage);
    deletedLongNameImage->image[rootOffset+32]=0xE5; // Short-name first byte is lost even when the LFN slot survives.
    auto deletedLongNameResult=scan(deletedLongNameImage);
    assert(deletedLongNameResult.status==FileScanStatus::Success && deletedLongNameResult.entries.size()==1);
    assert(deletedLongNameResult.entries.front().status==FileStatus::DELETED);
    assert(!deletedLongNameResult.entries.front().nameKnown && deletedLongNameResult.entries.front().name.empty());
    assert(deletedLongNameResult.entries.front().shortNameFragmentAvailable);

    auto removedLfnMarkers=std::make_shared<FakeVolume>(*deletedLongNameImage);
    removedLfnMarkers->image[rootOffset]=0xE5; // FAT deletion destroys the LFN sequence-order byte.
    auto removedLfnResult=scan(removedLfnMarkers);
    assert(removedLfnResult.status==FileScanStatus::Success && removedLfnResult.entries.size()==1);
    assert(!removedLfnResult.entries.front().nameKnown);
    assert(removedLfnResult.entries.front().shortNameFragmentAvailable);

    auto misorderedDeletedLfn=std::make_shared<FakeVolume>(*deletedLongNameImage);
    misorderedDeletedLfn->image[rootOffset]=0x42; // Says there are two LFN slots, but only one is present.
    auto misorderedResult=scan(misorderedDeletedLfn);
    assert(misorderedResult.status==FileScanStatus::Success && !misorderedResult.entries.front().nameKnown);
    assert(misorderedResult.entries.front().shortNameFragmentAvailable);

    auto badDeletedLfnChecksum=std::make_shared<FakeVolume>(*deletedLongNameImage);
    badDeletedLfnChecksum->image[rootOffset+13]^=0x01;
    auto badChecksumResult=scan(badDeletedLfnChecksum);
    assert(badChecksumResult.status==FileScanStatus::Success && !badChecksumResult.entries.front().nameKnown);
    assert(badChecksumResult.entries.front().shortNameFragmentAvailable);

    auto malformed=makeImage();
    writeShort(malformed->image,clusterOffset(2),"BADATTR TXT",0xC0,6,12);
    malformed->image[clusterOffset(2)+32]=0;
    auto malformedResult=scan(malformed);
    assert(malformedResult.status==FileScanStatus::Success && malformedResult.entries.size()==1);
    assert(malformedResult.entries.front().status==FileStatus::INVALID);

    auto stale=makeImage(); stale->stale=true; auto staleResult=scan(stale);
    assert(staleResult.status==FileScanStatus::StaleVolume && !staleResult.volumeIdentityValidated);
    auto disconnectDuringRead=makeImage(); disconnectDuringRead->staleOnValidation=3;
    auto disconnectedResult=scan(disconnectDuringRead);
    assert(disconnectedResult.status==FileScanStatus::StaleVolume && !disconnectedResult.volumeIdentityValidated);
    auto shortRead=makeImage(); shortRead->shortRead=true; auto shortReadResult=scan(shortRead);
    assert(shortReadResult.status==FileScanStatus::Error && shortReadResult.readError);
    auto cancelled=makeImage(); assert(scan(cancelled,[](){return true;}).status==FileScanStatus::Cancelled);
    auto midCancel=makeImage(); int cancelChecks=0;
    assert(scan(midCancel,[&cancelChecks](){return ++cancelChecks>=3;}).status==FileScanStatus::Cancelled);
    auto outOfBounds=makeImage(); outOfBounds->declaredSize=BPS; assert(scan(outOfBounds).status==FileScanStatus::Error);
    auto declaredTooSmall=makeImage(); declaredTooSmall->declaredSize=declaredTooSmall->image.size()-BPS;
    const auto sizeMismatch=scan(declaredTooSmall);
    assert(sizeMismatch.status==FileScanStatus::Error && sizeMismatch.errorMessage.find("BPB volume length")!=std::string::npos);
    auto badBpbSector=makeImage(); put16(badBpbSector->image,11,1024); assert(scan(badBpbSector).status==FileScanStatus::Error);
    auto badClusterSize=makeImage(); badClusterSize->image[13]=3; assert(scan(badClusterSize).status==FileScanStatus::Error);
    auto fatBeyondData=makeImage(); put32(fatBeyondData->image,36,TOTAL_SECTORS); assert(scan(fatBeyondData).status==FileScanStatus::Error);
    auto invalidRoot=makeImage(); put32(invalidRoot->image,44,0x0FFFFFFF); assert(scan(invalidRoot).status==FileScanStatus::Error);
    auto truncatedBoot=makeImage(); truncatedBoot->image.resize(128); assert(scan(truncatedBoot).status==FileScanStatus::Error);
    auto readerError=makeImage(); readerError->failRead=true;
    const auto readerErrorResult=scan(readerError);
    assert(readerErrorResult.status==FileScanStatus::Error && readerErrorResult.readError);
    auto overflow=makeImage(); put32(overflow->image,32,0xFFFFFFFFU); assert(scan(overflow).status==FileScanStatus::Error);
    auto wrongFilesystem=makeImage(); wrongFilesystem->fs="exFAT"; assert(scan(wrongFilesystem).status==FileScanStatus::Unsupported);
    auto badSector=makeImage(); badSector->sector=0; assert(scan(badSector).status==FileScanStatus::Error);
    auto limited=makeImage(); FileScanner::Limits limits; limits.maximumReadBytes=512;
    assert(FileScanner::scanFat32(limited,limited->id,limits).status==FileScanStatus::Error);
    auto invalidCluster=makeImage(); writeShort(invalidCluster->image,clusterOffset(2),"BROKEN  TXT",0x20,0x0FFFFFFF,4096,true);
    auto invalidResult=scan(invalidCluster); assert(invalidResult.status==FileScanStatus::Success && !invalidResult.entries.empty());
    assert(invalidResult.entries.front().status==FileStatus::INVALID);
    assert(!invalidResult.entries.front().sizeKnown && invalidResult.entries.front().shortNameFragmentAvailable);
    const FileScanCounts invalidCounts=aggregateFileEntries(invalidResult.entries);
    assert(invalidCounts.invalidFileRecords==1 && invalidCounts.deletedEntryCandidates==1);
    auto reused=makeImage(); writeShort(reused->image,clusterOffset(2)+32,"LIVE    TXT",0x20,3,500);
    auto reusedResult=scan(reused); assert(reusedResult.status==FileScanStatus::Success);
    bool markedReused=false;
    for(const auto& entry:reusedResult.entries) if(entry.status==FileStatus::DELETED && entry.parentDirectoryCluster==2)
        markedReused=entry.confidence==FileRecordConfidence::Ambiguous && !entry.sizeKnown && entry.reason.find("allocated")!=std::string::npos;
    assert(markedReused);
    const FileScanCounts reusedCounts=aggregateFileEntries(reusedResult.entries);
    assert(reusedCounts.existingFiles==2 && reusedCounts.deletedEntryCandidates==2);
    assert(reusedCounts.ambiguousRecords==1); // Ambiguity is diagnostic and overlaps the candidate category.
    const std::uint64_t legacyNonAmbiguousDeleted=static_cast<std::uint64_t>(std::count_if(reusedResult.entries.begin(),reusedResult.entries.end(),[](const FileEntry& entry) {
        return entry.status==FileStatus::DELETED && entry.confidence!=FileRecordConfidence::Ambiguous;
    }));
    assert(legacyNonAmbiguousDeleted==1 && reusedCounts.deletedEntryCandidates==legacyNonAmbiguousDeleted+reusedCounts.ambiguousRecords);
    return 0;
}
