#include "../src/AssessmentPersistence.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cassert>
#include <cstdio>

namespace {
DriveInfo target()
{
    DriveInfo d;
    d.mountPoint = "D:\\";
    d.volumeLabel = "DD";
    d.fileSystem = "FAT32";
    d.volumeGuid = "\\\\?\\Volume{test-target}\\";
    d.capacityBytes = 7745871872ULL;
    d.capacityAvailable = true;
    d.logicalSectorSize = 512;
    d.deviceType = "Removable volume";
    d.physicalDiskNumber = "1";
    d.partitionNumber = "1";
    d.partitionOffsetBytes = 16384;
    d.partitionLengthBytes = 7761018880ULL;
    d.partitionInfoAvailable = true;
    d.connected = true;
    return d;
}

StoredRecoverabilityAssessment sample()
{
    StoredRecoverabilityAssessment s;
    s.device = target();
    s.evidence.scanState = AssessmentScanState::Complete;
    s.evidence.filesystemSupported = true;
    s.evidence.filesystemValid = true;
    s.evidence.volumeIdentityValidated = true;
    s.evidence.existingFileCount = 1;
    s.evidence.deletedEntryCount = 1;
    s.evidence.invalidEntryCount = 1;
    s.evidence.ambiguousEntryCount = 1;
    s.evidence.deletedEntriesWithUnknownName = 2;
    s.assessment = assessRecoverability(s.evidence);
    s.counts.existingFiles = 1;
    s.counts.deletedEntryCandidates = 1;
    s.counts.invalidFileRecords = 1;
    s.counts.invalidRecords = 1;
    s.counts.ambiguousRecords = 1;
    s.counts.deletedNamesUnknown = 2;
    FileEntry existing;
    existing.name = "résumé.txt";
    existing.fullPath = "docs/nested/résumé.txt";
    existing.parentPath = "docs/nested";
    existing.parentPathKnown = true;
    existing.nameKnown = true;
    existing.sizeBytes = 4097;
    existing.sizeKnown = true;
    existing.status = FileStatus::EXISTING;
    existing.reason = "validated filesystem record";
    existing.identity = "FAT32|slot=1";
    existing.volumeIdentity = s.device.volumeGuid;
    existing.entryIdentityAvailable = true;
    FileEntry deleted;
    deleted.nameKnown = false;
    deleted.parentPathKnown = true;
    deleted.parentPath = "archive";
    deleted.status = FileStatus::DELETED;
    deleted.confidence = FileRecordConfidence::Ambiguous;
    deleted.deletedMarkerObserved = true;
    deleted.shortNameFragmentAvailable = true;
    deleted.shortNameFragment = "REPORT~1.DOC";
    deleted.reason = "ambiguous entry";
    deleted.identity = "FAT32|slot=2";
    deleted.volumeIdentity = s.device.volumeGuid;
    deleted.entryIdentityAvailable = true;
    FileEntry invalidDeleted;
    invalidDeleted.status = FileStatus::INVALID;
    invalidDeleted.confidence = FileRecordConfidence::Invalid;
    invalidDeleted.deletedMarkerObserved = true;
    invalidDeleted.identity = "FAT32|slot=3";
    invalidDeleted.volumeIdentity = s.device.volumeGuid;
    invalidDeleted.entryIdentityAvailable = true;
    s.fileEntries = {existing, deleted, invalidDeleted};
    s.fileEntriesStored = true;
    s.assessedAtUtc = QStringLiteral("2026-10-29T12:34:56.000Z");
    return s;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir(QDir::current().filePath(QStringLiteral("assessment-test-XXXXXX")));
    assert(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("recoverability_assessment.json"));
    AssessmentPersistence store(filePath);
    const StoredRecoverabilityAssessment original = sample();
    QString error;

    assert(store.loadFor(original.device).status == AssessmentLoadStatus::Missing);
    const bool saved = store.save(original, &error);
    if (!saved) std::fprintf(stderr, "save failed: %s\n", error.toUtf8().constData());
    assert(saved);
    QFile encodedFile(filePath);
    assert(encodedFile.open(QIODevice::ReadOnly));
    const QJsonObject encoded = QJsonDocument::fromJson(encodedFile.readAll()).object();
    encodedFile.close();
    assert(encoded.value(QStringLiteral("fileEntries")).isArray());
    assert(encoded.value(QStringLiteral("schemaVersion")).toInt()==3);
    const QJsonObject firstEncodedEntry=encoded.value(QStringLiteral("fileEntries")).toArray().first().toObject();
    assert(firstEncodedEntry.value(QStringLiteral("identity")).toString()==QStringLiteral("FAT32|slot=1"));
    assert(firstEncodedEntry.value(QStringLiteral("entryIdentityAvailable")).toBool());
    assert(!encoded.contains(QStringLiteral("fileContents")));
    assert(!encoded.value(QStringLiteral("fileEntries")).toArray().first().toObject().contains(QStringLiteral("content")));

    const auto firstVisit = store.loadFor(target());
    assert(firstVisit.status == AssessmentLoadStatus::Match);
    assert(firstVisit.stored.assessment.title == original.assessment.title);
    assert(firstVisit.stored.assessment.explanation == original.assessment.explanation);
    assert(firstVisit.stored.assessment.recommendedNextStep == original.assessment.recommendedNextStep);
    assert(firstVisit.stored.assessedAtUtc == original.assessedAtUtc);
    assert(firstVisit.stored.evidence.deletedEntryCount == 1);
    assert(firstVisit.stored.counts.existingFiles == 1);
    assert(firstVisit.stored.fileEntriesStored);
    assert(firstVisit.stored.fileEntries.size() == 3);
    assert(firstVisit.stored.fileEntries[0].name == "résumé.txt");
    assert(firstVisit.stored.fileEntries[0].fullPath == "docs/nested/résumé.txt");
    assert(firstVisit.stored.fileEntries[0].sizeKnown && firstVisit.stored.fileEntries[0].sizeBytes == 4097);
    assert(firstVisit.stored.fileEntries[0].status == FileStatus::EXISTING);
    assert(firstVisit.stored.fileEntries[0].entryIdentityAvailable);
    assert(firstVisit.stored.fileEntries[0].identity=="FAT32|slot=1");
    assert(firstVisit.stored.fileEntries[0].volumeIdentity==target().volumeGuid);
    assert(firstVisit.stored.fileEntries[1].status == FileStatus::DELETED);
    assert(firstVisit.stored.fileEntries[1].confidence == FileRecordConfidence::Ambiguous);
    assert(firstVisit.stored.fileEntries[2].status == FileStatus::INVALID);

    // A second visit reads the same serialized result; the assessment function
    // is not needed to reconstruct or alter its presentation values.
    const auto secondVisit = store.loadFor(target());
    assert(secondVisit.status == AssessmentLoadStatus::Match);
    assert(secondVisit.stored.assessment.title == firstVisit.stored.assessment.title);
    assert(secondVisit.stored.assessment.explanation == firstVisit.stored.assessment.explanation);
    assert(secondVisit.stored.assessedAtUtc == firstVisit.stored.assessedAtUtc);
    assert(secondVisit.stored.fileEntries.size() == firstVisit.stored.fileEntries.size());
    assert(secondVisit.stored.fileEntries[0].fullPath == firstVisit.stored.fileEntries[0].fullPath);

    // Schema 2 rows remain readable, but are not selectable because those
    // records did not persist validated per-entry identities.
    QJsonObject schema2=encoded;
    schema2.insert(QStringLiteral("schemaVersion"),2);
    QJsonArray schema2Entries=schema2.value(QStringLiteral("fileEntries")).toArray();
    for (int i=0;i<schema2Entries.size();++i) {
        QJsonObject entry=schema2Entries[i].toObject();
        for (const QString& key : {QStringLiteral("identity"),QStringLiteral("volumeIdentity"),QStringLiteral("entryIdentityAvailable"),
                                   QStringLiteral("isDirectory"),QStringLiteral("isValid"),QStringLiteral("reason")})
            entry.remove(key);
        schema2Entries[i]=entry;
    }
    schema2.insert(QStringLiteral("fileEntries"),schema2Entries);
    const QString schema2Path=dir.filePath(QStringLiteral("schema2.json"));
    QFile schema2File(schema2Path); assert(schema2File.open(QIODevice::WriteOnly));
    assert(schema2File.write(QJsonDocument(schema2).toJson())>0); schema2File.close();
    AssessmentPersistence schema2Store(schema2Path);
    const auto schema2Loaded=schema2Store.loadFor(target());
    assert(schema2Loaded.status==AssessmentLoadStatus::Match && schema2Loaded.stored.fileEntriesStored);
    assert(schema2Loaded.stored.fileEntries.size()==3);
    assert(!schema2Loaded.stored.fileEntries[0].entryIdentityAvailable);
    assert(schema2Loaded.stored.fileEntries[0].identity.empty());

    StoredRecoverabilityAssessment wrongEntryIdentity=original;
    wrongEntryIdentity.fileEntries[0].volumeIdentity="other-volume";
    const QString mismatchPath=dir.filePath(QStringLiteral("entry-identity-mismatch.json"));
    AssessmentPersistence mismatchStore(mismatchPath);
    assert(!mismatchStore.save(wrongEntryIdentity,&error));

    const QString summaryPath = dir.filePath(QStringLiteral("assessment_with_display_summary.json"));
    AssessmentPersistence summaryStore(summaryPath);
    assert(summaryStore.save(original, &error));
    AssessmentDisplaySummary displaySummary;
    displaySummary.existingFiles = 483;
    displaySummary.deletedFileCandidates = 1621;
    displaySummary.invalidRecords = 3;
    displaySummary.totalRecords = 2107;
    displaySummary.deletedCandidateProportionBasisPoints = 7693;
    displaySummary.recordValidityRateBasisPoints = 9986;
    displaySummary.present = true;
    assert(summaryStore.saveDisplaySummaryFor(target(), displaySummary, &error));
    const auto summaryReloaded = summaryStore.loadFor(target());
    assert(summaryReloaded.status == AssessmentLoadStatus::Match);
    assert(summaryReloaded.stored.displaySummary.present);
    assert(summaryReloaded.stored.displaySummary.existingFiles == 483);
    assert(summaryReloaded.stored.displaySummary.deletedFileCandidates == 1621);
    assert(summaryReloaded.stored.displaySummary.invalidRecords == 3);
    assert(summaryReloaded.stored.displaySummary.totalRecords == 2107);
    assert(summaryReloaded.stored.displaySummary.deletedCandidateProportionBasisPoints == 7693);
    assert(summaryReloaded.stored.displaySummary.recordValidityRateBasisPoints == 9986);
    assert(summaryReloaded.stored.counts.existingFiles == original.counts.existingFiles);
    assert(summaryReloaded.stored.fileEntries.size() == original.fileEntries.size());
    DriveInfo mismatchedSummaryTarget = target();
    mismatchedSummaryTarget.volumeGuid = "\\\\?\\Volume{different-summary-target}\\";
    assert(!summaryStore.saveDisplaySummaryFor(mismatchedSummaryTarget, displaySummary, &error));
    auto invalidSummary = displaySummary;
    invalidSummary.totalRecords = 2106;
    assert(!summaryStore.saveDisplaySummaryFor(target(), invalidSummary, &error));
    assert(summaryStore.loadFor(target()).stored.displaySummary.totalRecords == 2107);

    // Schema-1 saved assessments remain readable, but have no retained listing.
    const QString legacyPath = dir.filePath(QStringLiteral("legacy_assessment.json"));
    AssessmentPersistence legacyStore(legacyPath);
    assert(legacyStore.save(original, &error));
    QFile legacyFile(legacyPath);
    assert(legacyFile.open(QIODevice::ReadOnly));
    QJsonObject legacyObject = QJsonDocument::fromJson(legacyFile.readAll()).object();
    legacyFile.close();
    legacyObject.insert(QStringLiteral("schemaVersion"), 1);
    legacyObject.remove(QStringLiteral("fileEntries"));
    QSaveFile legacyOutput(legacyPath);
    assert(legacyOutput.open(QIODevice::WriteOnly));
    const QByteArray legacyBytes = QJsonDocument(legacyObject).toJson();
    assert(legacyOutput.write(legacyBytes) == legacyBytes.size());
    assert(legacyOutput.commit());
    const auto legacyLoaded = legacyStore.loadFor(target());
    assert(legacyLoaded.status == AssessmentLoadStatus::Match);
    assert(!legacyLoaded.stored.fileEntriesStored);
    assert(legacyLoaded.stored.fileEntries.empty());
    assert(legacyStore.saveDisplaySummaryFor(target(), displaySummary, &error));
    const auto legacyWithSummary = legacyStore.loadFor(target());
    assert(legacyWithSummary.status == AssessmentLoadStatus::Match);
    assert(!legacyWithSummary.stored.fileEntriesStored);
    assert(legacyWithSummary.stored.displaySummary.present);
    assert(legacyWithSummary.stored.displaySummary.totalRecords == 2107);

    DriveInfo otherVolume = target();
    otherVolume.volumeGuid = "\\\\?\\Volume{other-target}\\";
    assert(store.loadFor(otherVolume).status == AssessmentLoadStatus::DifferentTarget);
    DriveInfo changedMapping = target();
    changedMapping.physicalDiskNumber = "2";
    assert(store.loadFor(changedMapping).status == AssessmentLoadStatus::DifferentTarget);
    assert(store.save(sample(), &error)); // Same target may be deliberately refreshed.
    assert(store.loadFor(target()).status == AssessmentLoadStatus::Match);
    auto incomplete = sample();
    incomplete.evidence.scanState = AssessmentScanState::Failed;
    assert(!store.save(incomplete, &error));
    assert(store.loadFor(target()).stored.assessedAtUtc == original.assessedAtUtc);
    auto otherAssessment = sample();
    otherAssessment.device = otherVolume;
    for (FileEntry& entry : otherAssessment.fileEntries)
        entry.volumeIdentity = otherVolume.volumeGuid;
    assert(store.save(otherAssessment, &error));
    assert(store.loadFor(target()).status == AssessmentLoadStatus::Match);
    assert(store.loadFor(otherVolume).status == AssessmentLoadStatus::Match);
    assert(store.loadFor(otherVolume).stored.device.volumeGuid == otherVolume.volumeGuid);

    return 0;
}
