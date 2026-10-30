#pragma once

#include "DeviceManager.h"
#include "FileScanner.h"
#include "RecoverabilityAssessment.h"

#include <QString>
#include <cstdint>
#include <vector>

struct AssessmentDisplaySummary {
    std::uint64_t existingFiles = 0;
    std::uint64_t deletedFileCandidates = 0;
    std::uint64_t invalidRecords = 0;
    std::uint64_t totalRecords = 0;
    std::uint32_t deletedCandidateProportionBasisPoints = 0;
    std::uint32_t recordValidityRateBasisPoints = 0;
    bool present = false;
};

struct StoredRecoverabilityAssessment {
    DriveInfo device;
    RecoverabilityEvidence evidence;
    RecoverabilityAssessment assessment;
    FileScanCounts counts;
    std::vector<FileEntry> fileEntries;
    bool fileEntriesStored = false;
    AssessmentDisplaySummary displaySummary;
    QString assessedAtUtc;
};

enum class AssessmentLoadStatus { Missing, Match, DifferentTarget, Invalid };

struct AssessmentLoadResult {
    AssessmentLoadStatus status = AssessmentLoadStatus::Missing;
    StoredRecoverabilityAssessment stored;
    QString error;
};

// Stores completed assessments and directory-entry metadata needed to
// reproduce each target's table. Additional verified volumes use identity-
// keyed sidecar records; file contents are never persisted.
class AssessmentPersistence {
public:
    explicit AssessmentPersistence(QString path = {});

    static QString defaultPath();
    const QString& path() const { return m_path; }

    AssessmentLoadResult loadFor(const DriveInfo& currentDevice) const;
    bool save(const StoredRecoverabilityAssessment& assessment, QString* error = nullptr) const;
    bool saveDisplaySummaryFor(const DriveInfo& target, const AssessmentDisplaySummary& summary,
                               QString* error = nullptr) const;

private:
    QString m_path;
};
