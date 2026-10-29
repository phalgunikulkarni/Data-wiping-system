#pragma once

#include <cstdint>
#include <string>

// These states describe the evidence gathering run, not file recoverability.
enum class AssessmentScanState { Complete, Partial, Cancelled, Failed, Unsupported };
enum class AssessmentStatus {
    Incomplete,
    PotentialRecoveryEvidenceFound,
    InsufficientEvidence,
    FurtherAnalysisRequired
};

struct RecoverabilityEvidence {
    AssessmentScanState scanState = AssessmentScanState::Failed;
    bool filesystemSupported = false;
    bool filesystemValid = false;
    bool volumeIdentityValidated = false;
    bool readErrors = false;
    std::uint64_t existingFileCount = 0;
    std::uint64_t deletedEntryCount = 0;
    std::uint64_t invalidEntryCount = 0;
    std::uint64_t ambiguousEntryCount = 0;
    std::uint64_t deletedEntriesWithUnknownName = 0;
};

struct RecoverabilityAssessment {
    AssessmentStatus status = AssessmentStatus::Incomplete;
    std::string title;
    std::string explanation;
    std::string recommendedNextStep;
};

// Deterministic conservative rules. This does not estimate probability or
// establish that data can be recovered.
RecoverabilityAssessment assessRecoverability(const RecoverabilityEvidence& evidence);
