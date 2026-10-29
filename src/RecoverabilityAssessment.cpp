#include "RecoverabilityAssessment.h"

namespace {
RecoverabilityAssessment incomplete(const char* why)
{
    return {AssessmentStatus::Incomplete,
            "Assessment Incomplete",
            why,
            "Reconnect the selected volume or retry a supported read-only scan."};
}
}

RecoverabilityAssessment assessRecoverability(const RecoverabilityEvidence& evidence)
{
    if (evidence.scanState == AssessmentScanState::Unsupported || !evidence.filesystemSupported)
        return incomplete("The selected filesystem is not supported by the current identification scanner.");
    if (evidence.scanState == AssessmentScanState::Cancelled)
        return incomplete("The scan was cancelled before directory metadata could be completely assessed.");
    if (evidence.scanState == AssessmentScanState::Failed || evidence.readErrors)
        return incomplete("The scan failed or encountered read errors. Its results cannot be treated as complete.");
    if (evidence.scanState == AssessmentScanState::Partial)
        return incomplete("Only part of the selected volume was assessed. A partial result cannot represent the whole volume.");
    if (evidence.scanState != AssessmentScanState::Complete || !evidence.filesystemValid || !evidence.volumeIdentityValidated)
        return incomplete("Filesystem validity or selected-volume identity was not fully established.");

    if (evidence.invalidEntryCount != 0 || evidence.ambiguousEntryCount != 0) {
        std::string explanation = "The directory scan completed, but it found invalid or ambiguous metadata that requires further analysis.";
        if (evidence.deletedEntryCount != 0)
            explanation += " Deleted-entry markers were found, but their content recoverability remains unknown.";
        return {AssessmentStatus::FurtherAnalysisRequired,
                "Further Analysis Required",
                explanation,
                "Review the scanner limitations and validate ambiguous metadata before considering any later workflow."};
    }

    if (evidence.deletedEntryCount != 0) {
        std::string explanation = "The completed scan identified deleted directory-entry markers. These are evidence of deleted entries only; they do not prove that file contents remain recoverable.";
        if (evidence.deletedEntriesWithUnknownName != 0)
            explanation += " Some deleted names could not be reconstructed from the remaining metadata.";
        return {AssessmentStatus::PotentialRecoveryEvidenceFound,
                "Potential Recovery Evidence Found",
                explanation,
                "A separate, validated recovery analysis would be required to assess file contents."};
    }

    return {AssessmentStatus::InsufficientEvidence,
            "Insufficient Evidence",
            "The supported metadata scan completed without identifying deleted entries. This does not prove that no deleted files exist or that data is unrecoverable.",
            "Use a future supported analysis workflow if additional evidence is required."};
}
