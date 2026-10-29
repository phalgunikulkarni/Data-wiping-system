#include "../src/RecoverabilityAssessment.h"

#include <cassert>

int main()
{
    RecoverabilityEvidence evidence;
    evidence.scanState=AssessmentScanState::Complete;
    evidence.filesystemSupported=true;
    evidence.filesystemValid=true;
    evidence.volumeIdentityValidated=true;
    auto result=assessRecoverability(evidence);
    assert(result.status==AssessmentStatus::InsufficientEvidence);
    assert(result.explanation.find("does not prove")!=std::string::npos);

    evidence.deletedEntryCount=2;
    evidence.deletedEntriesWithUnknownName=1;
    result=assessRecoverability(evidence);
    assert(result.status==AssessmentStatus::PotentialRecoveryEvidenceFound);
    assert(result.explanation.find("do not prove")!=std::string::npos);
    assert(result.explanation.find("names could not")!=std::string::npos);

    evidence.invalidEntryCount=1;
    result=assessRecoverability(evidence);
    assert(result.status==AssessmentStatus::FurtherAnalysisRequired);
    evidence.invalidEntryCount=0;
    evidence.ambiguousEntryCount=2;
    result=assessRecoverability(evidence);
    assert(result.status==AssessmentStatus::FurtherAnalysisRequired);
    assert(result.explanation.find("recoverability remains unknown")!=std::string::npos);

    evidence.ambiguousEntryCount=0;
    evidence.scanState=AssessmentScanState::Partial;
    assert(assessRecoverability(evidence).status==AssessmentStatus::Incomplete);
    evidence.scanState=AssessmentScanState::Cancelled;
    assert(assessRecoverability(evidence).status==AssessmentStatus::Incomplete);
    evidence.scanState=AssessmentScanState::Failed;
    assert(assessRecoverability(evidence).status==AssessmentStatus::Incomplete);
    evidence.scanState=AssessmentScanState::Complete;
    evidence.readErrors=true;
    assert(assessRecoverability(evidence).status==AssessmentStatus::Incomplete);
    evidence.readErrors=false;
    evidence.filesystemSupported=false;
    assert(assessRecoverability(evidence).status==AssessmentStatus::Incomplete);
    evidence.filesystemSupported=true;
    evidence.filesystemValid=false;
    assert(assessRecoverability(evidence).status==AssessmentStatus::Incomplete);
    evidence.filesystemValid=true;
    evidence.volumeIdentityValidated=false;
    assert(assessRecoverability(evidence).status==AssessmentStatus::Incomplete);
    evidence.volumeIdentityValidated=true;
    evidence.scanState=AssessmentScanState::Unsupported;
    assert(assessRecoverability(evidence).status==AssessmentStatus::Incomplete);
    return 0;
}
