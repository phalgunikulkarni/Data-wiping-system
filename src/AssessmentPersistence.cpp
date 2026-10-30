#include "AssessmentPersistence.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QVariant>
#include <array>
#include <limits>
#include <set>
#include <utility>

namespace {
constexpr int kSchemaVersion = 3;
constexpr int kFileEntriesSchemaVersion = 2;
constexpr int kLegacySchemaVersion = 1;
constexpr std::size_t kMaximumStoredFileEntries = 100000;
constexpr qsizetype kMaximumStoredStringBytes = 32768;
constexpr qint64 kMaximumAssessmentFileBytes = 64LL * 1024 * 1024;

QString u64(quint64 value);
bool parseU64(const QJsonObject& object, const QString& key, quint64& value);

QString normalized(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())).trimmed().toLower();
}

bool isUsableTarget(const DriveInfo& device)
{
    return !normalized(device.volumeGuid).isEmpty() && !normalized(device.fileSystem).isEmpty() &&
        device.capacityAvailable && device.capacityBytes > 0 && device.logicalSectorSize > 0 &&
        !normalized(device.physicalDiskNumber).isEmpty() && device.partitionInfoAvailable &&
        !normalized(device.partitionNumber).isEmpty() && device.partitionLengthBytes > 0;
}

bool sameTarget(const DriveInfo& a, const DriveInfo& b)
{
    return isUsableTarget(a) && isUsableTarget(b) &&
        normalized(a.volumeGuid) == normalized(b.volumeGuid) &&
        normalized(a.fileSystem) == normalized(b.fileSystem) &&
        a.capacityBytes == b.capacityBytes && a.logicalSectorSize == b.logicalSectorSize &&
        normalized(a.physicalDiskNumber) == normalized(b.physicalDiskNumber) &&
        normalized(a.partitionNumber) == normalized(b.partitionNumber) &&
        a.partitionOffsetBytes == b.partitionOffsetBytes &&
        a.partitionLengthBytes == b.partitionLengthBytes;
}

bool countsMatchEvidence(const FileScanCounts& counts, const RecoverabilityEvidence& evidence)
{
    return counts.existingFiles == evidence.existingFileCount &&
        counts.deletedEntryCandidates == evidence.deletedEntryCount &&
        counts.invalidRecords == evidence.invalidEntryCount &&
        counts.ambiguousRecords == evidence.ambiguousEntryCount &&
        counts.deletedNamesUnknown == evidence.deletedEntriesWithUnknownName;
}

bool fileEntriesMatchCounts(const std::vector<FileEntry>& entries, const FileScanCounts& counts)
{
    FileScanCounts actual;
    for (const FileEntry& entry : entries) {
        if (entry.isDirectory) return false;
        if (entry.status == FileStatus::EXISTING) {
            ++actual.existingFiles;
        } else if (entry.status == FileStatus::DELETED) {
            ++actual.deletedEntryCandidates;
            if (!entry.nameKnown) ++actual.deletedNamesUnknown;
        } else if (entry.status == FileStatus::INVALID) {
            ++actual.invalidFileRecords;
            ++actual.invalidRecords;
            if (entry.deletedMarkerObserved && !entry.nameKnown) ++actual.deletedNamesUnknown;
        }
        if (entry.confidence == FileRecordConfidence::Ambiguous) ++actual.ambiguousRecords;
    }
    return actual.existingFiles == counts.existingFiles &&
        actual.deletedEntryCandidates == counts.deletedEntryCandidates &&
        actual.invalidFileRecords == counts.invalidFileRecords &&
        actual.invalidRecords <= counts.invalidRecords &&
        actual.ambiguousRecords <= counts.ambiguousRecords &&
        actual.deletedNamesUnknown == counts.deletedNamesUnknown;
}

bool fileEntryIdentitiesMatch(const std::vector<FileEntry>& entries, const DriveInfo& device)
{
    std::set<std::string> identities;
    const QString expectedVolume=normalized(device.volumeGuid);
    for (const FileEntry& entry : entries) {
        if (entry.isDirectory || !entry.entryIdentityAvailable || entry.identity.empty() ||
            normalized(entry.volumeIdentity)!=expectedVolume || !identities.insert(entry.identity).second)
            return false;
    }
    return true;
}

bool fileEntriesFitStorageBounds(const std::vector<FileEntry>& entries)
{
    if (entries.size() > kMaximumStoredFileEntries) return false;
    quint64 estimatedBytes = 8192;
    const quint64 maximumBytes = static_cast<quint64>(kMaximumAssessmentFileBytes);
    for (const FileEntry& entry : entries) {
        if (entry.isDirectory) return false;
        const std::array<std::size_t, 7> lengths = {entry.name.size(), entry.fullPath.size(),
            entry.parentPath.size(), entry.shortNameFragment.size(), entry.identity.size(),
            entry.volumeIdentity.size(), entry.reason.size()};
        for (const std::size_t length : lengths) {
            if (length > static_cast<std::size_t>(kMaximumStoredStringBytes) ||
                length > maximumBytes / 6)
                return false;
            const quint64 escapedBound = static_cast<quint64>(length) * 6;
            if (escapedBound > maximumBytes - estimatedBytes) return false;
            estimatedBytes += escapedBound;
        }
        constexpr quint64 perEntryJsonOverhead = 512;
        if (perEntryJsonOverhead > maximumBytes - estimatedBytes) return false;
        estimatedBytes += perEntryJsonOverhead;
    }
    return true;
}

bool validDisplaySummary(const AssessmentDisplaySummary& summary)
{
    if (!summary.present || summary.totalRecords == 0 ||
        summary.existingFiles > std::numeric_limits<std::uint64_t>::max() - summary.deletedFileCandidates ||
        summary.existingFiles + summary.deletedFileCandidates > std::numeric_limits<std::uint64_t>::max() - summary.invalidRecords)
        return false;
    const std::uint64_t categorized = summary.existingFiles + summary.deletedFileCandidates + summary.invalidRecords;
    if (categorized != summary.totalRecords || summary.invalidRecords > summary.totalRecords ||
        summary.deletedFileCandidates > (std::numeric_limits<std::uint64_t>::max() - summary.totalRecords / 2) / 10000 ||
        (summary.totalRecords - summary.invalidRecords) > (std::numeric_limits<std::uint64_t>::max() - summary.totalRecords / 2) / 10000)
        return false;
    const auto roundedBasisPoints = [total = summary.totalRecords](std::uint64_t value) {
        return static_cast<std::uint32_t>((value * 10000 + total / 2) / total);
    };
    return summary.deletedCandidateProportionBasisPoints == roundedBasisPoints(summary.deletedFileCandidates) &&
        summary.recordValidityRateBasisPoints == roundedBasisPoints(summary.totalRecords - summary.invalidRecords) &&
        summary.deletedCandidateProportionBasisPoints <= 10000 && summary.recordValidityRateBasisPoints <= 10000;
}

QJsonObject encodeDisplaySummary(const AssessmentDisplaySummary& summary)
{
    return {{QStringLiteral("existingFiles"), u64(summary.existingFiles)},
            {QStringLiteral("deletedFileCandidates"), u64(summary.deletedFileCandidates)},
            {QStringLiteral("invalidRecords"), u64(summary.invalidRecords)},
            {QStringLiteral("totalRecords"), u64(summary.totalRecords)},
            {QStringLiteral("deletedCandidateProportionBasisPoints"), static_cast<int>(summary.deletedCandidateProportionBasisPoints)},
            {QStringLiteral("recordValidityRateBasisPoints"), static_cast<int>(summary.recordValidityRateBasisPoints)}};
}

bool decodeDisplaySummary(const QJsonObject& o, AssessmentDisplaySummary& summary)
{
    const auto rate = [&o](const QString& key, std::uint32_t& value) {
        const QJsonValue raw = o.value(key);
        if (!raw.isDouble() || raw.toInt(-1) < 0 || raw.toInt(-1) > 10000) return false;
        value = static_cast<std::uint32_t>(raw.toInt());
        return true;
    };
    if (!parseU64(o, QStringLiteral("existingFiles"), summary.existingFiles) ||
        !parseU64(o, QStringLiteral("deletedFileCandidates"), summary.deletedFileCandidates) ||
        !parseU64(o, QStringLiteral("invalidRecords"), summary.invalidRecords) ||
        !parseU64(o, QStringLiteral("totalRecords"), summary.totalRecords) ||
        !rate(QStringLiteral("deletedCandidateProportionBasisPoints"), summary.deletedCandidateProportionBasisPoints) ||
        !rate(QStringLiteral("recordValidityRateBasisPoints"), summary.recordValidityRateBasisPoints))
        return false;
    summary.present = true;
    return validDisplaySummary(summary);
}

QString u64(quint64 value) { return QString::number(value); }

bool parseU64(const QJsonObject& object, const QString& key, quint64& value)
{
    const QJsonValue raw = object.value(key);
    if (!raw.isString()) return false;
    bool ok = false;
    value = raw.toString().toULongLong(&ok, 10);
    return ok;
}

QJsonObject encodeDrive(const DriveInfo& d)
{
    return {{QStringLiteral("mountPoint"), QString::fromStdString(d.mountPoint)},
            {QStringLiteral("volumeLabel"), QString::fromStdString(d.volumeLabel)},
            {QStringLiteral("fileSystem"), QString::fromStdString(d.fileSystem)},
            {QStringLiteral("volumeGuid"), QString::fromStdString(d.volumeGuid)},
            {QStringLiteral("capacityBytes"), u64(d.capacityBytes)},
            {QStringLiteral("logicalSectorSize"), static_cast<int>(d.logicalSectorSize)},
            {QStringLiteral("deviceType"), QString::fromStdString(d.deviceType)},
            {QStringLiteral("physicalDiskNumber"), QString::fromStdString(d.physicalDiskNumber)},
            {QStringLiteral("deviceModel"), QString::fromStdString(d.deviceModel)},
            {QStringLiteral("busType"), QString::fromStdString(d.busType)},
            {QStringLiteral("partitionNumber"), QString::fromStdString(d.partitionNumber)},
            {QStringLiteral("partitionOffsetBytes"), u64(d.partitionOffsetBytes)},
            {QStringLiteral("partitionLengthBytes"), u64(d.partitionLengthBytes)}};
}

bool decodeDrive(const QJsonObject& o, DriveInfo& d)
{
    const auto string = [&o](const QString& key, std::string& out) {
        if (!o.value(key).isString()) return false;
        const QByteArray bytes = o.value(key).toString().toUtf8();
        out.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        return true;
    };
    quint64 capacity = 0, partitionOffset = 0, partitionLength = 0;
    const int sector = o.value(QStringLiteral("logicalSectorSize")).toInt(-1);
    if (!string(QStringLiteral("mountPoint"), d.mountPoint) ||
        !string(QStringLiteral("volumeLabel"), d.volumeLabel) ||
        !string(QStringLiteral("fileSystem"), d.fileSystem) ||
        !string(QStringLiteral("volumeGuid"), d.volumeGuid) ||
        !string(QStringLiteral("deviceType"), d.deviceType) ||
        !string(QStringLiteral("physicalDiskNumber"), d.physicalDiskNumber) ||
        !string(QStringLiteral("deviceModel"), d.deviceModel) ||
        !string(QStringLiteral("busType"), d.busType) ||
        !string(QStringLiteral("partitionNumber"), d.partitionNumber) ||
        !parseU64(o, QStringLiteral("capacityBytes"), capacity) ||
        !parseU64(o, QStringLiteral("partitionOffsetBytes"), partitionOffset) ||
        !parseU64(o, QStringLiteral("partitionLengthBytes"), partitionLength) || sector <= 0)
        return false;
    d.capacityBytes = capacity;
    d.capacityAvailable = true;
    d.logicalSectorSize = static_cast<unsigned int>(sector);
    d.partitionOffsetBytes = partitionOffset;
    d.partitionLengthBytes = partitionLength;
    d.partitionInfoAvailable = true;
    d.connected = true;
    return isUsableTarget(d);
}

QJsonObject encodeAssessment(const StoredRecoverabilityAssessment& s)
{
    const auto& e = s.evidence;
    const auto& a = s.assessment;
    QJsonArray fileEntries;
    for (const FileEntry& entry : s.fileEntries) {
        fileEntries.push_back(QJsonObject{
            {QStringLiteral("name"), QString::fromUtf8(entry.name.data(), static_cast<qsizetype>(entry.name.size()))},
            {QStringLiteral("fullPath"), QString::fromUtf8(entry.fullPath.data(), static_cast<qsizetype>(entry.fullPath.size()))},
            {QStringLiteral("parentPath"), QString::fromUtf8(entry.parentPath.data(), static_cast<qsizetype>(entry.parentPath.size()))},
            {QStringLiteral("parentPathKnown"), entry.parentPathKnown},
            {QStringLiteral("nameKnown"), entry.nameKnown},
            {QStringLiteral("sizeBytes"), u64(entry.sizeBytes)},
            {QStringLiteral("sizeKnown"), entry.sizeKnown},
            {QStringLiteral("status"), static_cast<int>(entry.status)},
            {QStringLiteral("confidence"), static_cast<int>(entry.confidence)},
            {QStringLiteral("deletedMarkerObserved"), entry.deletedMarkerObserved},
            {QStringLiteral("shortNameFragment"), QString::fromUtf8(entry.shortNameFragment.data(), static_cast<qsizetype>(entry.shortNameFragment.size()))},
            {QStringLiteral("shortNameFragmentAvailable"), entry.shortNameFragmentAvailable},
            {QStringLiteral("identity"), QString::fromUtf8(entry.identity.data(), static_cast<qsizetype>(entry.identity.size()))},
            {QStringLiteral("volumeIdentity"), QString::fromUtf8(entry.volumeIdentity.data(), static_cast<qsizetype>(entry.volumeIdentity.size()))},
            {QStringLiteral("entryIdentityAvailable"), entry.entryIdentityAvailable},
            {QStringLiteral("isDirectory"), entry.isDirectory},
            {QStringLiteral("isValid"), entry.isValid},
            {QStringLiteral("reason"), QString::fromUtf8(entry.reason.data(), static_cast<qsizetype>(entry.reason.size()))}});
    }
    QJsonObject root{{QStringLiteral("schemaVersion"), kSchemaVersion},
            {QStringLiteral("target"), encodeDrive(s.device)},
            {QStringLiteral("assessedAtUtc"), s.assessedAtUtc},
            {QStringLiteral("scanStatus"), QStringLiteral("Success")},
            {QStringLiteral("evidence"), QJsonObject{
                {QStringLiteral("scanState"), static_cast<int>(e.scanState)},
                {QStringLiteral("filesystemSupported"), e.filesystemSupported},
                {QStringLiteral("filesystemValid"), e.filesystemValid},
                {QStringLiteral("volumeIdentityValidated"), e.volumeIdentityValidated},
                {QStringLiteral("readErrors"), e.readErrors},
                {QStringLiteral("existingFileCount"), u64(e.existingFileCount)},
                {QStringLiteral("deletedEntryCount"), u64(e.deletedEntryCount)},
                {QStringLiteral("invalidEntryCount"), u64(e.invalidEntryCount)},
                {QStringLiteral("ambiguousEntryCount"), u64(e.ambiguousEntryCount)},
                {QStringLiteral("deletedEntriesWithUnknownName"), u64(e.deletedEntriesWithUnknownName)}}},
            {QStringLiteral("assessment"), QJsonObject{
                {QStringLiteral("status"), static_cast<int>(a.status)},
                {QStringLiteral("title"), QString::fromStdString(a.title)},
                {QStringLiteral("explanation"), QString::fromStdString(a.explanation)},
                {QStringLiteral("recommendedNextStep"), QString::fromStdString(a.recommendedNextStep)}}},
            {QStringLiteral("counts"), QJsonObject{
                {QStringLiteral("existingFiles"), u64(s.counts.existingFiles)},
                {QStringLiteral("deletedEntryCandidates"), u64(s.counts.deletedEntryCandidates)},
                {QStringLiteral("invalidFileRecords"), u64(s.counts.invalidFileRecords)},
                {QStringLiteral("invalidRecords"), u64(s.counts.invalidRecords)},
                {QStringLiteral("ambiguousRecords"), u64(s.counts.ambiguousRecords)},
                {QStringLiteral("deletedNamesUnknown"), u64(s.counts.deletedNamesUnknown)}}},
            {QStringLiteral("fileEntries"), fileEntries}};
    if (s.displaySummary.present)
        root.insert(QStringLiteral("displaySummary"), encodeDisplaySummary(s.displaySummary));
    return root;
}

bool decodeFileEntries(const QJsonArray& array, std::vector<FileEntry>& entries, bool hasIdentityFields)
{
    if (static_cast<std::size_t>(array.size()) > kMaximumStoredFileEntries)
        return false;
    entries.clear();
    entries.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue& value : array) {
        if (!value.isObject()) return false;
        const QJsonObject o = value.toObject();
        const auto readString = [&o](const QString& key, std::string& output) {
            const QJsonValue raw = o.value(key);
            if (!raw.isString()) return false;
            const QByteArray bytes = raw.toString().toUtf8();
            if (bytes.size() > kMaximumStoredStringBytes) return false;
            output.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
            return true;
        };
        FileEntry entry;
        quint64 size = 0;
        const int status = o.value(QStringLiteral("status")).toInt(-1);
        const int confidence = o.value(QStringLiteral("confidence")).toInt(-1);
        if (!readString(QStringLiteral("name"), entry.name) ||
            !readString(QStringLiteral("fullPath"), entry.fullPath) ||
            !readString(QStringLiteral("parentPath"), entry.parentPath) ||
            !readString(QStringLiteral("shortNameFragment"), entry.shortNameFragment) ||
            !parseU64(o, QStringLiteral("sizeBytes"), size) ||
            status < static_cast<int>(FileStatus::EXISTING) || status > static_cast<int>(FileStatus::INVALID) ||
            confidence < static_cast<int>(FileRecordConfidence::Validated) || confidence > static_cast<int>(FileRecordConfidence::Invalid) ||
            !o.value(QStringLiteral("parentPathKnown")).isBool() ||
            !o.value(QStringLiteral("nameKnown")).isBool() ||
            !o.value(QStringLiteral("sizeKnown")).isBool() ||
            !o.value(QStringLiteral("deletedMarkerObserved")).isBool() ||
            !o.value(QStringLiteral("shortNameFragmentAvailable")).isBool())
            return false;
        entry.status = static_cast<FileStatus>(status);
        entry.confidence = static_cast<FileRecordConfidence>(confidence);
        entry.sizeBytes = size;
        entry.parentPathKnown = o.value(QStringLiteral("parentPathKnown")).toBool();
        entry.nameKnown = o.value(QStringLiteral("nameKnown")).toBool();
        entry.sizeKnown = o.value(QStringLiteral("sizeKnown")).toBool();
        entry.deletedMarkerObserved = o.value(QStringLiteral("deletedMarkerObserved")).toBool();
        entry.shortNameFragmentAvailable = o.value(QStringLiteral("shortNameFragmentAvailable")).toBool();
        if (hasIdentityFields) {
            if (!readString(QStringLiteral("identity"),entry.identity) ||
                !readString(QStringLiteral("volumeIdentity"),entry.volumeIdentity) ||
                !readString(QStringLiteral("reason"),entry.reason) ||
                !o.value(QStringLiteral("entryIdentityAvailable")).isBool() ||
                !o.value(QStringLiteral("isDirectory")).isBool() ||
                !o.value(QStringLiteral("isValid")).isBool())
                return false;
            entry.entryIdentityAvailable=o.value(QStringLiteral("entryIdentityAvailable")).toBool();
            entry.isDirectory=o.value(QStringLiteral("isDirectory")).toBool();
            entry.isValid=o.value(QStringLiteral("isValid")).toBool();
        } else {
            // Schema 2 kept the rows but not their per-entry volume/slot IDs.
            // Continue to render them, while leaving them unselectable.
            entry.isDirectory = false;
            entry.isValid = entry.status != FileStatus::INVALID;
            entry.entryIdentityAvailable = false;
        }
        entry.operationEligible = false;
        entries.push_back(std::move(entry));
    }
    return true;
}

bool decodeAssessment(const QJsonObject& root, StoredRecoverabilityAssessment& s)
{
    const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if ((schemaVersion != kSchemaVersion && schemaVersion != kFileEntriesSchemaVersion && schemaVersion != kLegacySchemaVersion) ||
        root.value(QStringLiteral("scanStatus")).toString() != QStringLiteral("Success") ||
        !root.value(QStringLiteral("assessedAtUtc")).isString() ||
        root.value(QStringLiteral("assessedAtUtc")).toString().isEmpty() ||
        !root.value(QStringLiteral("target")).isObject() ||
        !decodeDrive(root.value(QStringLiteral("target")).toObject(), s.device))
        return false;

    const QJsonObject e = root.value(QStringLiteral("evidence")).toObject();
    const QJsonObject a = root.value(QStringLiteral("assessment")).toObject();
    const QJsonObject c = root.value(QStringLiteral("counts")).toObject();
    if (e.isEmpty() || a.isEmpty() || c.isEmpty()) return false;
    const int scanState = e.value(QStringLiteral("scanState")).toInt(-1);
    const int assessmentStatus = a.value(QStringLiteral("status")).toInt(-1);
    if (scanState != static_cast<int>(AssessmentScanState::Complete) ||
        assessmentStatus < static_cast<int>(AssessmentStatus::PotentialRecoveryEvidenceFound) ||
        assessmentStatus > static_cast<int>(AssessmentStatus::FurtherAnalysisRequired))
        return false;
    s.evidence.scanState = static_cast<AssessmentScanState>(scanState);
    s.evidence.filesystemSupported = e.value(QStringLiteral("filesystemSupported")).toBool(false);
    s.evidence.filesystemValid = e.value(QStringLiteral("filesystemValid")).toBool(false);
    s.evidence.volumeIdentityValidated = e.value(QStringLiteral("volumeIdentityValidated")).toBool(false);
    s.evidence.readErrors = e.value(QStringLiteral("readErrors")).toBool(true);
    if (!s.evidence.filesystemSupported || !s.evidence.filesystemValid ||
        !s.evidence.volumeIdentityValidated || s.evidence.readErrors)
        return false;

    if (!parseU64(e, QStringLiteral("existingFileCount"), s.evidence.existingFileCount) ||
        !parseU64(e, QStringLiteral("deletedEntryCount"), s.evidence.deletedEntryCount) ||
        !parseU64(e, QStringLiteral("invalidEntryCount"), s.evidence.invalidEntryCount) ||
        !parseU64(e, QStringLiteral("ambiguousEntryCount"), s.evidence.ambiguousEntryCount) ||
        !parseU64(e, QStringLiteral("deletedEntriesWithUnknownName"), s.evidence.deletedEntriesWithUnknownName) ||
        !parseU64(c, QStringLiteral("existingFiles"), s.counts.existingFiles) ||
        !parseU64(c, QStringLiteral("deletedEntryCandidates"), s.counts.deletedEntryCandidates) ||
        !parseU64(c, QStringLiteral("invalidFileRecords"), s.counts.invalidFileRecords) ||
        !parseU64(c, QStringLiteral("invalidRecords"), s.counts.invalidRecords) ||
        !parseU64(c, QStringLiteral("ambiguousRecords"), s.counts.ambiguousRecords) ||
        !parseU64(c, QStringLiteral("deletedNamesUnknown"), s.counts.deletedNamesUnknown))
        return false;

    s.assessment.status = static_cast<AssessmentStatus>(assessmentStatus);
    const auto readText = [](const QJsonObject& o, const QString& key, std::string& out) {
        if (!o.value(key).isString()) return false;
        const QByteArray bytes = o.value(key).toString().toUtf8();
        out.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        return true;
    };
    if (!readText(a, QStringLiteral("title"), s.assessment.title) ||
        !readText(a, QStringLiteral("explanation"), s.assessment.explanation) ||
        !readText(a, QStringLiteral("recommendedNextStep"), s.assessment.recommendedNextStep))
        return false;
    if (s.assessment.title.empty() || s.assessment.explanation.empty() ||
        s.assessment.recommendedNextStep.empty() || !countsMatchEvidence(s.counts, s.evidence))
        return false;
    if (root.contains(QStringLiteral("displaySummary")) &&
        (!root.value(QStringLiteral("displaySummary")).isObject() ||
         !decodeDisplaySummary(root.value(QStringLiteral("displaySummary")).toObject(), s.displaySummary)))
        return false;
    s.assessedAtUtc = root.value(QStringLiteral("assessedAtUtc")).toString();
    if (schemaVersion == kLegacySchemaVersion) {
        s.fileEntries.clear();
        s.fileEntriesStored = false;
    } else {
        if (!root.value(QStringLiteral("fileEntries")).isArray() ||
            !decodeFileEntries(root.value(QStringLiteral("fileEntries")).toArray(), s.fileEntries,
                               schemaVersion >= kSchemaVersion))
            return false;
        s.fileEntriesStored = true;
        if (schemaVersion >= kSchemaVersion && !fileEntryIdentitiesMatch(s.fileEntries,s.device))
            return false;
        if (!fileEntriesMatchCounts(s.fileEntries, s.counts))
            return false;
    }
    return true;
}

bool readStored(const QString& path, StoredRecoverabilityAssessment& s, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.size() < 0 || file.size() > kMaximumAssessmentFileBytes) {
        if (error) *error = QStringLiteral("Saved assessment file exceeds the allowed size");
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        !decodeAssessment(document.object(), s)) {
        if (error) *error = QStringLiteral("Saved assessment is malformed or unsupported");
        return false;
    }
    return true;
}

QString pathForTarget(const QString& basePath, const DriveInfo& target)
{
    QByteArray key;
    const auto add = [&key](const std::string& value) {
        key += QString::fromUtf8(value.data(),static_cast<qsizetype>(value.size())).trimmed().toLower().toUtf8();
        key += '\n';
    };
    add(target.volumeGuid);
    add(target.fileSystem);
    add(target.physicalDiskNumber);
    add(target.partitionNumber);
    key += QByteArray::number(target.capacityBytes) + '\n' +
           QByteArray::number(target.logicalSectorSize) + '\n' +
           QByteArray::number(target.partitionOffsetBytes) + '\n' +
           QByteArray::number(target.partitionLengthBytes);
    const QByteArray digest=QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex();
    return basePath + QStringLiteral(".") + QString::fromLatin1(digest) + QStringLiteral(".json");
}

QString writableRecordPath(const QString& basePath, const DriveInfo& target, QString* error)
{
    if (!QFileInfo::exists(basePath)) return basePath;
    StoredRecoverabilityAssessment baseRecord;
    QString baseError;
    if (readStored(basePath,baseRecord,&baseError) && sameTarget(baseRecord.device,target))
        return basePath;
    const QString targetPath=pathForTarget(basePath,target);
    if (QFileInfo::exists(targetPath)) {
        StoredRecoverabilityAssessment targetRecord;
        QString targetError;
        if (!readStored(targetPath,targetRecord,&targetError) || !sameTarget(targetRecord.device,target)) {
            if (error) *error = QStringLiteral("Target-specific assessment record is invalid or has a different verified identity");
            return {};
        }
    }
    return targetPath;
}
}

AssessmentPersistence::AssessmentPersistence(QString path)
    : m_path(path.isEmpty() ? defaultPath() : std::move(path))
{}

QString AssessmentPersistence::defaultPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/recoverability_assessment.json"));
}

AssessmentLoadResult AssessmentPersistence::loadFor(const DriveInfo& currentDevice) const
{
    AssessmentLoadResult result;
    if (!isUsableTarget(currentDevice)) {
        result.status = AssessmentLoadStatus::Invalid;
        result.error = QStringLiteral("Current device identity or physical mapping is incomplete");
        return result;
    }
    if (!QFileInfo::exists(m_path)) {
        const QString targetPath=pathForTarget(m_path,currentDevice);
        if (!QFileInfo::exists(targetPath)) return result;
        if (!readStored(targetPath,result.stored,&result.error)) {
            result.status=AssessmentLoadStatus::Invalid;
            return result;
        }
        result.status=sameTarget(result.stored.device,currentDevice)
            ? AssessmentLoadStatus::Match : AssessmentLoadStatus::Invalid;
        return result;
    }
    if (!readStored(m_path, result.stored, &result.error)) {
        const QString targetPath=pathForTarget(m_path,currentDevice);
        if (!QFileInfo::exists(targetPath) || !readStored(targetPath,result.stored,&result.error)) {
            result.status = AssessmentLoadStatus::Invalid;
            return result;
        }
        result.status = sameTarget(result.stored.device,currentDevice)
            ? AssessmentLoadStatus::Match : AssessmentLoadStatus::Invalid;
        return result;
    }
    if (sameTarget(result.stored.device,currentDevice)) {
        result.status=AssessmentLoadStatus::Match;
        return result;
    }
    const QString targetPath=pathForTarget(m_path,currentDevice);
    if (QFileInfo::exists(targetPath)) {
        if (!readStored(targetPath,result.stored,&result.error)) {
            result.status=AssessmentLoadStatus::Invalid;
            return result;
        }
        result.status=sameTarget(result.stored.device,currentDevice)
            ? AssessmentLoadStatus::Match : AssessmentLoadStatus::Invalid;
        return result;
    }
    result.status=AssessmentLoadStatus::DifferentTarget;
    result.stored={};
    result.error=QStringLiteral("No saved assessment matches this verified volume");
    return result;
}

bool AssessmentPersistence::save(const StoredRecoverabilityAssessment& assessment, QString* error) const
{
    QString validationError;
    if (!isUsableTarget(assessment.device)) validationError=QStringLiteral("Selected device identity or mapping is incomplete");
    else if (assessment.evidence.scanState != AssessmentScanState::Complete ||
             !assessment.evidence.filesystemSupported || !assessment.evidence.filesystemValid ||
             !assessment.evidence.volumeIdentityValidated || assessment.evidence.readErrors ||
             assessment.assessment.status == AssessmentStatus::Incomplete || assessment.assessedAtUtc.isEmpty())
        validationError=QStringLiteral("Only a complete assessment with validated volume identity can be saved");
    else if (!countsMatchEvidence(assessment.counts, assessment.evidence))
        validationError=QStringLiteral("Assessment counts do not match its evidence");
    else if (assessment.displaySummary.present && !validDisplaySummary(assessment.displaySummary))
        validationError=QStringLiteral("Configured display summary is invalid");
    else if (!assessment.fileEntriesStored || !fileEntriesFitStorageBounds(assessment.fileEntries))
        validationError=QStringLiteral("File listing is absent or exceeds its storage bounds");
    else if (!fileEntryIdentitiesMatch(assessment.fileEntries,assessment.device))
        validationError=QStringLiteral("A file entry has a missing, duplicate, or different-volume identity");
    else if (!fileEntriesMatchCounts(assessment.fileEntries, assessment.counts))
        validationError=QStringLiteral("Stored file entries do not match the assessment counts");
    if (!validationError.isEmpty()) {
        if (error) *error = validationError;
        return false;
    }

    QString recordPathError;
    const QString recordPath=writableRecordPath(m_path,assessment.device,&recordPathError);
    if (recordPath.isEmpty()) {
        if (error) *error=recordPathError;
        return false;
    }
    AssessmentDisplaySummary priorDisplaySummary;
    if (QFileInfo::exists(recordPath)) {
        StoredRecoverabilityAssessment prior;
        QString readError;
        if (!readStored(recordPath, prior, &readError)) {
            if (error) *error = readError;
            return false;
        }
        if (!sameTarget(prior.device, assessment.device)) {
            if (error) *error = QStringLiteral("The target-specific assessment belongs to a different verified volume and was preserved");
            return false;
        }
        priorDisplaySummary = prior.displaySummary;
    }

    const QFileInfo destination(recordPath);
    if (!QDir().mkpath(destination.absolutePath())) {
        if (error) *error = QStringLiteral("Could not create the local assessment data directory");
        return false;
    }
    QSaveFile file(recordPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonObject encoded = encodeAssessment(assessment);
    if (!assessment.displaySummary.present && priorDisplaySummary.present)
        encoded.insert(QStringLiteral("displaySummary"), encodeDisplaySummary(priorDisplaySummary));
    const QByteArray bytes = QJsonDocument(encoded).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    return true;
}

bool AssessmentPersistence::saveDisplaySummaryFor(const DriveInfo& target,
                                                  const AssessmentDisplaySummary& summary,
                                                  QString* error) const
{
    if (!isUsableTarget(target) || !validDisplaySummary(summary)) {
        if (error) *error = QStringLiteral("Display summary or selected target is invalid");
        return false;
    }
    QString recordPathError;
    const QString recordPath=writableRecordPath(m_path,target,&recordPathError);
    if (recordPath.isEmpty()) {
        if (error) *error=recordPathError;
        return false;
    }
    QFile input(recordPath);
    if (!input.open(QIODevice::ReadOnly) || input.size() < 0 || input.size() > kMaximumAssessmentFileBytes) {
        if (error) *error = input.errorString().isEmpty()
            ? QStringLiteral("Saved assessment file exceeds the allowed size") : input.errorString();
        return false;
    }
    QJsonParseError parseError{};
    QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &parseError);
    input.close();
    StoredRecoverabilityAssessment verified;
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        !decodeAssessment(document.object(), verified)) {
        if (error) *error = QStringLiteral("Saved assessment changed or became malformed");
        return false;
    }
    if (!sameTarget(verified.device, target)) {
        if (error) *error = QStringLiteral("The saved assessment belongs to a different verified volume");
        return false;
    }
    QJsonObject root = document.object();
    root.insert(QStringLiteral("displaySummary"), encodeDisplaySummary(summary));
    QSaveFile output(recordPath);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) *error = output.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        if (error) *error = output.errorString();
        output.cancelWriting();
        return false;
    }
    return true;
}
