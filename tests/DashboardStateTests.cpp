#include "../src/MainWindow.h"

#include <QApplication>
#include <QAbstractAnimation>
#include <QComboBox>
#include <QEventLoop>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTableView>
#include <QThread>
#include <cassert>
#include <algorithm>
#include <cstdio>

class DashboardStateTests {
public:
    static void run()
    {
        MainWindow window;
        QTemporaryDir assessmentStoreDir(QDir::current().filePath(QStringLiteral("dashboard-assessment-test-XXXXXX")));
        assert(assessmentStoreDir.isValid());
        window.m_assessmentPersistence = AssessmentPersistence(
            assessmentStoreDir.filePath(QStringLiteral("recoverability_assessment.json")));
        window.setSignedInUser(QStringLiteral("DashboardTester"));
        auto* states = window.findChild<QStackedWidget*>(QStringLiteral("dashboardStates"));
        assert(states);
        assert(states->currentIndex() == 0); // Initial connect state.
        assert(states->count() == 8); // Assessment, device information, and unavailable-operation states.

        auto completeControlledScan = [&window, states](const DeviceEnumerationResult& result, int expectedPage) {
            window.m_deviceScanInProgress = true;
            window.m_scanButton->setEnabled(false);
            states->setCurrentIndex(1);
            window.m_scanAnimation->start();
            window.completeDeviceScan(result);
            assert(!window.m_deviceScanInProgress);
            assert(window.m_scanButton->isEnabled());
            assert(window.m_scanAnimation->state() == QAbstractAnimation::Stopped);
            assert(states->currentIndex() == expectedPage);
        };

        DeviceEnumerationResult found;
        found.success = true;
        DriveInfo device;
        device.letter = 'E';
        device.mountPoint = "E:\\";
        device.volumeLabel = "TEST VOLUME";
        device.fileSystem = "exFAT";
        device.capacityBytes = 64000000000ULL;
        device.capacityAvailable = true;
        device.logicalSectorSize = 512;
        device.availableBytes = 12000000000ULL;
        device.availableBytesAvailable = true;
        device.volumeGuid = "\\\\?\\Volume{test-guid}\\";
        device.deviceType = "Removable volume";
        device.physicalDiskNumber = "2";
        device.deviceModel = "Test Storage";
        device.busType = "USB";
        device.partitionNumber = "1";
        device.partitionOffsetBytes = 1048576;
        device.partitionLengthBytes = 64000000000ULL;
        device.partitionInfoAvailable = true;
        device.physicalDiskCapacityBytes = 64000000000ULL;
        device.physicalDiskCapacityAvailable = true;
        device.connected = true;
        found.devices.push_back(device);
        DriveInfo otherDevice = device;
        otherDevice.volumeGuid = "\\\\?\\Volume{second-guid}\\";
        otherDevice.deviceModel = "Other Storage";
        otherDevice.letter = 'F';
        otherDevice.mountPoint = "F:\\";
        found.devices.push_back(otherDevice);
        completeControlledScan(found, 2);
        assert(states->currentIndex() == 2); // Found state.
        auto* list = window.findChild<QListWidget*>(QStringLiteral("detectedDevicesList"));
        assert(list && list->count() == 2);
        assert(list->currentRow() == -1); // Selection remains explicit.
        std::reverse(window.m_detectedDevices.begin(), window.m_detectedDevices.end());
        window.selectDeviceRow(0); // Selection resolves by volume GUID, not backing-vector row.
        assert(!window.m_selectedDevicePanel->isHidden()); // Selected-device details revealed.
        assert(window.m_hasSelectedDrive);
        assert(window.m_selectedDeviceName->text().contains(QStringLiteral("Test Storage")));
        assert(window.m_selectedDeviceDetails->text().contains(QStringLiteral("Available free space: 12.0 GB")));
        assert(window.m_technicalDeviceDetails->text().contains(QStringLiteral("Bus type: USB")));
        assert(window.m_technicalDeviceDetails->text().contains(QStringLiteral("Physical disk: 2")));
        assert(window.m_deviceDetailsToggle);
        window.m_deviceDetailsToggle->setChecked(true);
        assert(!window.m_technicalDeviceDetails->isHidden());
        window.navigateTo(2,QStringLiteral("Scan Results"));
        assert(window.m_pages->currentIndex()==2 && window.m_fileScanButton && window.m_fileResultsList);
        window.navigateTo(0,QStringLiteral("Dashboard"));

        DeviceEnumerationResult duplicateIdentity = found;
        duplicateIdentity.devices[1].volumeGuid = duplicateIdentity.devices[0].volumeGuid;
        window.displayDeviceResults(duplicateIdentity);
        assert(!(list->item(0)->flags() & Qt::ItemIsSelectable));
        window.selectDeviceRow(0);
        assert(window.m_selectedDevicePanel->isHidden());

        DeviceEnumerationResult unknownIdentity = found;
        unknownIdentity.devices[0].volumeGuid.clear();
        window.displayDeviceResults(unknownIdentity);
        assert(!(list->item(0)->flags() & Qt::ItemIsSelectable));
        window.selectDeviceRow(0);
        assert(window.m_selectedDevicePanel->isHidden());

        DeviceEnumerationResult empty;
        empty.success = true;
        completeControlledScan(empty, 3);
        assert(states->currentIndex() == 3); // No-device state.

        DeviceEnumerationResult failure;
        failure.success = false;
        failure.errorMessage = "Test enumeration failure";
        completeControlledScan(failure, 4);
        assert(states->currentIndex() == 4); // Error state, separate from empty.
        assert(window.m_scanDetailLabel->text().contains(QStringLiteral("Test enumeration failure")));

        window.displayDeviceResults(found);
        window.selectDeviceRow(1);
        assert(window.m_selectedDeviceIdentity==QStringLiteral("\\\\?\\volume{second-guid}\\"));
        assert(window.m_nextStepButton && window.m_nextStepButton->text()==QStringLiteral("Next Step"));
        DriveInfo assessmentDrive=window.m_selectedDriveInfo;
        assessmentDrive.fileSystem="FAT32";
        assessmentDrive.volumeLabel = "DD";
        assessmentDrive.capacityBytes = 7745871872ULL;
        window.m_selectedDriveInfo=assessmentDrive;
        FileScanResult firstAutoScan;
        firstAutoScan.status=FileScanStatus::Success;
        firstAutoScan.volumeIdentityValidated=true;
        FileEntry autoExisting; autoExisting.status=FileStatus::EXISTING; autoExisting.name="auto.txt";
        autoExisting.nameKnown=true; autoExisting.fullPath="Docs/auto.txt"; autoExisting.sizeKnown=true;
        autoExisting.sizeBytes=19; autoExisting.identity="auto-existing"; autoExisting.volumeIdentity=assessmentDrive.volumeGuid;
        autoExisting.entryIdentityAvailable=true;
        FileEntry autoDeleted; autoDeleted.status=FileStatus::DELETED; autoDeleted.confidence=FileRecordConfidence::Candidate;
        autoDeleted.deletedMarkerObserved=true; autoDeleted.parentPathKnown=true; autoDeleted.parentPath="Archive";
        autoDeleted.identity="auto-deleted"; autoDeleted.volumeIdentity=assessmentDrive.volumeGuid;
        autoDeleted.entryIdentityAvailable=true;
        firstAutoScan.entries={autoExisting,autoDeleted};
        int scanInvocationCount=0;
        window.m_fat32ScanLauncher=[&](const DriveInfo&, std::function<void(const FileScanResult&)> completion) {
            ++scanInvocationCount;
            completion(firstAutoScan);
        };
        window.m_nextStepButton->click();
        assert(states->currentIndex()==5); // Next Step performs and displays the initial assessment.
        assert(scanInvocationCount==1);
        assert(window.m_deviceInformationValues->text().contains(QStringLiteral("Filesystem: FAT32")));
        assert(window.m_deviceInformationValues->text().contains(QStringLiteral("Volume identity: ")));
        auto* autoTable=window.findChild<QTableView*>(QStringLiteral("dashboardFileTable"));
        assert(autoTable && autoTable->model()->rowCount()==2);
        assert(autoTable->model()->index(0,0).data().toString()==QStringLiteral("auto.txt"));
        assert(window.m_assessmentPersistence.loadFor(assessmentDrive).status==AssessmentLoadStatus::Match);
        window.m_assessmentBackButton->click();
        assert(states->currentIndex()==6); // Results provide a direct path back to verified details.
        window.m_deviceInformationContinueButton->click();
        assert(window.m_pages->currentIndex()==3 && window.m_navigationButtons[3]->isChecked());
        assert(window.m_operationTargetLabel->text().contains(QStringLiteral("Other Storage")));
        auto* wholeDrive=window.findChild<QPushButton*>(QStringLiteral("operationWholeDrive"));
        auto* recoverFiles=window.findChild<QPushButton*>(QStringLiteral("operationRecoverFiles"));
        auto* secureWipe=window.findChild<QPushButton*>(QStringLiteral("operationSecureWipe"));
        auto* viewAssessment=window.findChild<QPushButton*>(QStringLiteral("operationViewAssessment"));
        assert(wholeDrive && recoverFiles && secureWipe && viewAssessment);
        assert(wholeDrive->isEnabled() && recoverFiles->isEnabled() && secureWipe->isEnabled() && viewAssessment->isEnabled());
        wholeDrive->click();
        assert(states->currentIndex()==7 && window.m_pages->currentIndex()==0);
        window.findChild<QPushButton*>(QStringLiteral("backToOperationPanel"))->click();
        assert(window.m_pages->currentIndex()==3);
        recoverFiles->click(); // Operation screens reuse the completed Next Step scan.
        assert(states->currentIndex()==5 && window.m_pages->currentIndex()==0 && !window.m_fileScanThread);
        assert(scanInvocationCount==1);
        assert(autoTable->model()->rowCount()==1);
        assert(autoTable->model()->index(0,0).data().toString()==QStringLiteral("Unknown deleted entry"));
        window.m_assessmentBackButton->click();
        assert(window.m_pages->currentIndex()==3);
        secureWipe->click();
        assert(states->currentIndex()==5 && window.m_pages->currentIndex()==0 && !window.m_fileScanThread);
        assert(scanInvocationCount==1 && autoTable->model()->rowCount()==1);
        assert(!window.m_fileOperationUnavailableButton->isEnabled());
        window.m_assessmentBackButton->click();
        viewAssessment->click();
        assert(states->currentIndex()==5 && window.m_pages->currentIndex()==0 && !window.m_fileScanThread);
        assert(scanInvocationCount==1 && autoTable->model()->rowCount()==2);

        FileScanResult identified;
        identified.status=FileScanStatus::Success;
        identified.volumeIdentityValidated=true;
        FileEntry existingEntry; existingEntry.status=FileStatus::EXISTING; existingEntry.isDirectory=false;
        existingEntry.name="existing.txt"; existingEntry.nameKnown=true; existingEntry.fullPath="Docs\\existing.txt";
        existingEntry.sizeBytes=17; existingEntry.sizeKnown=true; existingEntry.entryIdentityAvailable=true;
        existingEntry.identity="existing-slot"; existingEntry.volumeIdentity=assessmentDrive.volumeGuid;
        FileEntry deletedEntry; deletedEntry.status=FileStatus::DELETED; deletedEntry.confidence=FileRecordConfidence::Candidate;
        deletedEntry.nameKnown=false; deletedEntry.deletedMarkerObserved=true;
        deletedEntry.parentPathKnown=true; deletedEntry.parentPath="Archive";
        deletedEntry.entryIdentityAvailable=true; deletedEntry.identity="deleted-slot"; deletedEntry.volumeIdentity=assessmentDrive.volumeGuid;
        FileEntry invalidDeletedEntry; invalidDeletedEntry.status=FileStatus::INVALID;
        invalidDeletedEntry.confidence=FileRecordConfidence::Invalid; invalidDeletedEntry.deletedMarkerObserved=true;
        invalidDeletedEntry.entryIdentityAvailable=true; invalidDeletedEntry.identity="invalid-slot"; invalidDeletedEntry.volumeIdentity=assessmentDrive.volumeGuid;
        identified.entries={existingEntry,deletedEntry,invalidDeletedEntry};
        const FileScanCounts dashboardCounts=aggregateFileEntries(identified.entries);
        assert(dashboardCounts.existingFiles==1 && dashboardCounts.deletedEntryCandidates==1);
        assert(dashboardCounts.invalidFileRecords==1 && dashboardCounts.invalidRecords==1);
        window.m_assessmentInProgress=true;
        window.m_fileScanInProgress=true;
        window.m_scanAnimation->start();
        window.m_nextStepButton->setEnabled(false);
        window.completeDriveAssessment(identified,assessmentDrive);
        assert(!window.m_assessmentInProgress && !window.m_fileScanInProgress);
        assert(window.m_scanAnimation->state()==QAbstractAnimation::Stopped);
        assert(states->currentIndex()==5);
        assert(window.m_assessmentStatus->text()==QStringLiteral("Further Analysis Required"));
        auto* fileTable=window.findChild<QTableView*>(QStringLiteral("dashboardFileTable"));
        auto* fileSearch=window.findChild<QLineEdit*>(QStringLiteral("dashboardFileSearch"));
        auto* fileFilter=window.findChild<QComboBox*>(QStringLiteral("dashboardFileFilter"));
        auto* fileSection=window.findChild<QFrame*>(QStringLiteral("dashboardFilesOnDrive"));
        assert(fileTable && fileSearch && fileFilter && fileSection);
        assert(!window.findChild<QFrame*>(QStringLiteral("assessmentScanSummary")));
        for (const QLabel* label : window.findChildren<QLabel*>())
            assert(label->text()!=QStringLiteral("Scan Summary"));
        assert(fileTable->model()->columnCount()==4 && fileTable->model()->rowCount()==3);
        assert(static_cast<std::uint64_t>(fileTable->model()->rowCount()) ==
               dashboardCounts.existingFiles+dashboardCounts.deletedEntryCandidates+dashboardCounts.invalidFileRecords);
        assert(fileTable->model()->index(0,0).data().toString()==QStringLiteral("existing.txt"));
        assert(fileTable->model()->index(0,1).data().toString()==QStringLiteral("Docs\\existing.txt"));
        assert(fileTable->model()->index(0,2).data().toString()==QStringLiteral("17 bytes"));
        assert(fileTable->model()->index(0,3).data().toString()==QStringLiteral("Existing"));
        assert(fileTable->model()->index(1,0).data().toString()==QStringLiteral("Unknown deleted entry"));
        assert(fileTable->model()->index(1,1).data().toString()==QStringLiteral("Archive"));
        assert(fileTable->model()->index(1,3).data().toString()==QStringLiteral("Deleted candidate"));
        assert(fileTable->model()->index(2,0).data().toString()==QStringLiteral("Unknown deleted entry"));
        assert(fileTable->model()->index(2,3).data().toString()==QStringLiteral("Invalid deleted entry"));
        fileFilter->setCurrentIndex(1); assert(fileTable->model()->rowCount()==1);
        fileFilter->setCurrentIndex(2); assert(fileTable->model()->rowCount()==1);
        fileFilter->setCurrentIndex(0); fileSearch->setText(QStringLiteral("not-present"));
        assert(fileTable->model()->rowCount()==0);
        fileSearch->setText(QStringLiteral("Archive")); assert(fileTable->model()->rowCount()==1);
        fileSearch->clear(); assert(fileTable->model()->rowCount()==3);

        // The saved scan can be reviewed through both file-operation choices.
        window.m_navigationButtons[3]->setChecked(true); window.navigateTo(3,QStringLiteral("Operations"));
        recoverFiles->click();
        assert(states->currentIndex()==5 && window.m_pages->currentIndex()==0 && fileTable->model()->rowCount()==1);
        QModelIndex deletedIndex=fileTable->model()->index(0,0);
        assert(deletedIndex.flags() & Qt::ItemIsUserCheckable);
        assert(fileTable->model()->setData(deletedIndex,Qt::Checked,Qt::CheckStateRole));
        assert(fileTable->model()->index(0,0).data(Qt::CheckStateRole).toInt()==Qt::Checked);
        assert(window.m_fileSelectionCount->text().contains(QStringLiteral("1 item(s)")));
        assert(window.m_fileSelectionReview->text().contains(QStringLiteral("Archive")));
        window.m_fileClearSelectionButton->click();
        assert(fileTable->model()->index(0,0).data(Qt::CheckStateRole).toInt()==Qt::Unchecked);
        window.m_assessmentBackButton->click();
        secureWipe->click();
        assert(states->currentIndex()==5 && window.m_pages->currentIndex()==0 && fileTable->model()->rowCount()==1);
        assert(fileTable->model()->index(0,0).flags().testFlag(Qt::ItemIsUserCheckable));
        fileFilter->setCurrentIndex(0);
        assert(fileTable->model()->rowCount()==3);
        assert(!fileTable->model()->index(1,0).flags().testFlag(Qt::ItemIsUserCheckable)); // Deleted candidate is not a wipe target.
        assert(!fileTable->model()->index(2,0).flags().testFlag(Qt::ItemIsUserCheckable)); // Invalid record is never selectable.
        window.m_fileSelectAllButton->click();
        assert(fileTable->model()->index(0,0).data(Qt::CheckStateRole).toInt()==Qt::Checked);
        assert(!window.m_fileOperationUnavailableButton->isEnabled());
        window.m_assessmentBackButton->click();
        viewAssessment->click();
        assert(states->currentIndex()==5 && window.m_pages->currentIndex()==0 && !window.m_fileScanThread);
        assert(window.m_assessmentSummary->text().contains(QStringLiteral("Existing files: 483")));

        FileScanResult noDeleted; noDeleted.status=FileScanStatus::Success; noDeleted.volumeIdentityValidated=true; noDeleted.entries={existingEntry};
        window.m_assessmentInProgress=true; window.m_fileScanInProgress=true;
        window.m_scanAnimation->start();
        window.completeDriveAssessment(noDeleted,assessmentDrive);
        assert(window.m_assessmentStatus->text()==QStringLiteral("Insufficient Evidence"));
        assert(window.m_scanAnimation->state()==QAbstractAnimation::Stopped);
        assert(fileTable->model()->rowCount()==1);
        FileScanResult emptyResult; emptyResult.status=FileScanStatus::Success; emptyResult.volumeIdentityValidated=true;
        window.m_assessmentInProgress=true; window.m_fileScanInProgress=true;
        window.completeDriveAssessment(emptyResult,assessmentDrive);
        assert(fileTable->model()->rowCount()==0);
        assert(window.m_dashboardFileStatus->text().contains(QStringLiteral("No file records")));
        FileScanResult partial; partial.status=FileScanStatus::Partial;
        window.m_assessmentInProgress=true; window.m_fileScanInProgress=true;
        window.m_scanAnimation->start();
        window.completeDriveAssessment(partial,assessmentDrive);
        assert(window.m_assessmentStatus->text()==QStringLiteral("Assessment Incomplete"));
        assert(window.m_dashboardFileStatus->text().contains(QStringLiteral("Partial")));
        assert(window.m_scanAnimation->state()==QAbstractAnimation::Stopped);
        FileScanResult cancelled; cancelled.status=FileScanStatus::Cancelled;
        window.m_assessmentInProgress=true; window.m_fileScanInProgress=true;
        window.m_scanAnimation->start();
        window.completeDriveAssessment(cancelled,assessmentDrive);
        assert(window.m_assessmentStatus->text()==QStringLiteral("Assessment Incomplete"));
        assert(window.m_scanAnimation->state()==QAbstractAnimation::Stopped);
        FileScanResult failed; failed.status=FileScanStatus::Error; failed.errorMessage="synthetic read error";
        window.m_assessmentInProgress=true; window.m_fileScanInProgress=true;
        window.m_scanAnimation->start();
        window.completeDriveAssessment(failed,assessmentDrive);
        assert(window.m_assessmentStatus->text()==QStringLiteral("Assessment Incomplete"));
        assert(fileTable->model()->rowCount()==0);
        assert(window.m_dashboardFileStatus->text().contains(QStringLiteral("synthetic read error")));
        assert(window.m_scanAnimation->state()==QAbstractAnimation::Stopped);
        DriveInfo unsupportedDrive=assessmentDrive; unsupportedDrive.fileSystem="exFAT";
        FileScanResult unsupported; unsupported.status=FileScanStatus::Unsupported;
        window.m_assessmentInProgress=true; window.m_fileScanInProgress=true;
        window.m_scanAnimation->start();
        window.completeDriveAssessment(unsupported,unsupportedDrive);
        assert(window.m_assessmentStatus->text()==QStringLiteral("Assessment Incomplete"));
        assert(window.m_scanAnimation->state()==QAbstractAnimation::Stopped);
        FileScanResult ambiguous; ambiguous.status=FileScanStatus::Success;
        ambiguous.volumeIdentityValidated=true;
        FileEntry ambiguousEntry; ambiguousEntry.status=FileStatus::DELETED; ambiguousEntry.confidence=FileRecordConfidence::Ambiguous;
        ambiguous.entries.push_back(ambiguousEntry);
        window.m_assessmentInProgress=true; window.m_fileScanInProgress=true;
        window.m_scanAnimation->start();
        window.completeDriveAssessment(ambiguous,assessmentDrive);
        assert(window.m_assessmentStatus->text()==QStringLiteral("Further Analysis Required"));
        assert(window.m_scanAnimation->state()==QAbstractAnimation::Stopped);
        window.m_assessmentBackButton->click();
        assert(states->currentIndex()==2);
        window.navigateTo(0,QStringLiteral("Dashboard"));
        window.m_assessmentInProgress=true; window.m_fileScanInProgress=true;
        window.startDriveAssessment(); // Duplicate requests do not create another worker.
        assert(!window.m_fileScanThread);
        window.m_assessmentInProgress=false; window.m_fileScanInProgress=false;
        window.m_selectedDeviceIdentity=QStringLiteral("stale-volume-id");
        window.m_assessmentInProgress=true; window.m_fileScanInProgress=true;
        window.completeDriveAssessment(identified,assessmentDrive);
        assert(window.m_assessmentStatus->text()==QStringLiteral("Assessment unavailable"));

        window.m_selectedDriveInfo = assessmentDrive;
        window.m_selectedDeviceIdentity = QStringLiteral("\\\\?\\volume{second-guid}\\");
        window.m_hasSelectedDrive = true;
        StoredRecoverabilityAssessment savedAssessment;
        savedAssessment.device = assessmentDrive;
        savedAssessment.evidence.scanState = AssessmentScanState::Complete;
        savedAssessment.evidence.filesystemSupported = true;
        savedAssessment.evidence.filesystemValid = true;
        savedAssessment.evidence.volumeIdentityValidated = true;
        savedAssessment.evidence.existingFileCount = 1;
        savedAssessment.evidence.deletedEntryCount = 1;
        savedAssessment.evidence.deletedEntriesWithUnknownName = 1;
        savedAssessment.assessment = assessRecoverability(savedAssessment.evidence);
        savedAssessment.counts.existingFiles = 1;
        savedAssessment.counts.deletedEntryCandidates = 1;
        savedAssessment.counts.deletedNamesUnknown = 1;
        savedAssessment.displaySummary.existingFiles = 483;
        savedAssessment.displaySummary.deletedFileCandidates = 1621;
        savedAssessment.displaySummary.invalidRecords = 3;
        savedAssessment.displaySummary.totalRecords = 2107;
        savedAssessment.displaySummary.deletedCandidateProportionBasisPoints = 7693;
        savedAssessment.displaySummary.recordValidityRateBasisPoints = 9986;
        savedAssessment.displaySummary.present = true;
        FileEntry persistedExisting;
        persistedExisting.name = "cached.txt";
        persistedExisting.fullPath = "Folder/cached.txt";
        persistedExisting.parentPath = "Folder";
        persistedExisting.parentPathKnown = true;
        persistedExisting.nameKnown = true;
        persistedExisting.sizeBytes = 23;
        persistedExisting.sizeKnown = true;
        persistedExisting.status = FileStatus::EXISTING;
        persistedExisting.identity = "saved-existing-slot";
        persistedExisting.volumeIdentity = assessmentDrive.volumeGuid;
        persistedExisting.entryIdentityAvailable = true;
        FileEntry persistedDeleted;
        persistedDeleted.status = FileStatus::DELETED;
        persistedDeleted.confidence = FileRecordConfidence::Candidate;
        persistedDeleted.deletedMarkerObserved = true;
        persistedDeleted.parentPathKnown = true;
        persistedDeleted.parentPath = "Archive";
        persistedDeleted.identity = "saved-deleted-slot";
        persistedDeleted.volumeIdentity = assessmentDrive.volumeGuid;
        persistedDeleted.entryIdentityAvailable = true;
        savedAssessment.fileEntries = {persistedExisting,persistedDeleted};
        savedAssessment.fileEntriesStored = true;
        savedAssessment.assessedAtUtc = QStringLiteral("2026-10-29T12:34:56.000Z");
        QString persistenceError;
        const bool saved = window.m_assessmentPersistence.save(savedAssessment,&persistenceError);
        if (!saved) std::fprintf(stderr, "assessment save failed: %s\n", persistenceError.toUtf8().constData());
        assert(saved);
        window.m_assessmentInProgress=false;
        window.m_fileScanInProgress=false;
        window.m_dashboardStates->setCurrentIndex(2);
        window.startDriveAssessment();
        assert(!window.m_fileScanThread);
        assert(states->currentIndex()==5);
        assert(window.m_assessmentStatus->isHidden());
        assert(window.m_assessmentStatus->text()==QString::fromStdString(savedAssessment.assessment.title));
        assert(window.m_assessmentSummary->text().contains(QStringLiteral("Existing files: 483")));
        assert(window.m_assessmentSummary->text().contains(QStringLiteral("Deleted-file candidates: 1,621")));
        assert(window.m_assessmentSummary->text().contains(QStringLiteral("Invalid records: 3")));
        assert(window.m_assessmentSummary->text().contains(QStringLiteral("Total records: 2,107")));
        assert(window.m_assessmentSummary->text().contains(QStringLiteral("Deleted-candidate proportion: 76.93% (1,621 / 2,107)")));
        assert(window.m_assessmentSummary->text().contains(QStringLiteral("Record validity rate: 99.86% (2,104 / 2,107)")));
        assert(window.m_dashboardFileStatus->text().contains(QStringLiteral("2 file entries")));
        auto* persistedTable=window.findChild<QTableView*>(QStringLiteral("dashboardFileTable"));
        assert(persistedTable && persistedTable->model()->rowCount()==2);
        assert(persistedTable->model()->index(0,0).data().toString()==QStringLiteral("cached.txt"));
        assert(persistedTable->model()->index(0,1).data().toString()==QStringLiteral("Folder/cached.txt"));
        assert(persistedTable->model()->index(0,2).data().toString()==QStringLiteral("23 bytes"));
        assert(persistedTable->model()->index(0,3).data().toString()==QStringLiteral("Existing"));
        assert(persistedTable->model()->index(1,3).data().toString()==QStringLiteral("Deleted candidate"));

        StoredRecoverabilityAssessment legacySaved = savedAssessment;
        legacySaved.fileEntries.clear();
        legacySaved.fileEntriesStored = false;
        legacySaved.displaySummary = {};
        legacySaved.assessment.status = AssessmentStatus::FurtherAnalysisRequired;
        legacySaved.assessment.title = "Further Analysis Required";
        window.displaySavedAssessment(legacySaved,assessmentDrive);
        assert(persistedTable->model()->rowCount()==0);
        assert(window.m_dashboardFileStatus->text().contains(QStringLiteral("Legacy assessment")));
        assert(window.m_dashboardFileStatus->text().contains(QStringLiteral("Legacy assessment")));
        auto* fileListingAssessmentButton=window.findChild<QPushButton*>(QStringLiteral("fileListingRunAssessment"));
        assert(fileListingAssessmentButton && !fileListingAssessmentButton->isHidden());
        assert(window.m_assessmentSummary->text().contains(QStringLiteral("76.93%")));
        assert(window.m_assessmentSummary->text().contains(QStringLiteral("99.86%")));
        window.displaySavedAssessment(savedAssessment,assessmentDrive);
        assert(!fileListingAssessmentButton->isHidden()); // Refresh remains available after loading saved rows.
        window.displaySavedAssessment(legacySaved,assessmentDrive);
        assert(!fileListingAssessmentButton->isHidden());

        // A different selected volume cannot inherit another volume's saved
        // rows; it receives its own safe scan result instead.
        window.m_selectedDriveInfo = found.devices[0];
        window.m_selectedDeviceIdentity = QStringLiteral("\\\\?\\volume{test-guid}\\");
        window.m_dashboardStates->setCurrentIndex(2);
        window.m_fat32ScanLauncher=[&](const DriveInfo& drive, std::function<void(const FileScanResult&)> completion) {
            ++scanInvocationCount;
            FileScanResult result;
            result.status=QString::fromStdString(drive.fileSystem).compare(QStringLiteral("FAT32"),Qt::CaseInsensitive)==0
                ? FileScanStatus::Success : FileScanStatus::Unsupported;
            result.volumeIdentityValidated=true;
            completion(result);
        };
        window.startDriveAssessment();
        assert(!window.m_fileScanThread);
        assert(scanInvocationCount==2);
        assert(window.m_assessmentStatus->text()==QStringLiteral("Assessment Incomplete"));
        assert(persistedTable->model()->rowCount()==0);

        // A new application window loads the same target's saved rows without
        // invoking its injected scanner.
        MainWindow reopened;
        reopened.m_assessmentPersistence=AssessmentPersistence(window.m_assessmentPersistence.path());
        reopened.m_selectedDriveInfo=assessmentDrive;
        reopened.m_selectedDeviceIdentity=QStringLiteral("\\\\?\\volume{second-guid}\\");
        reopened.m_hasSelectedDrive=true;
        int reopenScanCount=0;
        reopened.m_fat32ScanLauncher=[&](const DriveInfo&, std::function<void(const FileScanResult&)>) { ++reopenScanCount; };
        reopened.startDriveAssessment();
        auto* reopenedTable=reopened.findChild<QTableView*>(QStringLiteral("dashboardFileTable"));
        assert(reopenScanCount==0 && reopenedTable && reopenedTable->model()->rowCount()==3);

        // A schema-1 record has no listing. Entering its workflow starts one
        // read-only scan, replaces it with a complete current record, and
        // subsequent panels reuse that result.
        const QString legacyPath=assessmentStoreDir.filePath(QStringLiteral("legacy-auto.json"));
        QFile currentRecord(window.m_assessmentPersistence.path());
        assert(currentRecord.open(QIODevice::ReadOnly));
        QJsonObject legacyObject=QJsonDocument::fromJson(currentRecord.readAll()).object();
        currentRecord.close();
        legacyObject.insert(QStringLiteral("schemaVersion"),1);
        legacyObject.remove(QStringLiteral("fileEntries"));
        QSaveFile legacyOutput(legacyPath);
        assert(legacyOutput.open(QIODevice::WriteOnly));
        const QByteArray legacyBytes=QJsonDocument(legacyObject).toJson();
        assert(legacyOutput.write(legacyBytes)==legacyBytes.size() && legacyOutput.commit());
        MainWindow legacyWindow;
        legacyWindow.m_assessmentPersistence=AssessmentPersistence(legacyPath);
        legacyWindow.m_selectedDriveInfo=assessmentDrive;
        legacyWindow.m_selectedDeviceIdentity=QStringLiteral("\\\\?\\volume{second-guid}\\");
        legacyWindow.m_hasSelectedDrive=true;
        int legacyScanCount=0;
        legacyWindow.m_fat32ScanLauncher=[&](const DriveInfo&, std::function<void(const FileScanResult&)> completion) {
            ++legacyScanCount;
            completion(firstAutoScan);
        };
        legacyWindow.startDriveAssessment();
        auto* legacyTable=legacyWindow.findChild<QTableView*>(QStringLiteral("dashboardFileTable"));
        assert(legacyScanCount==1 && legacyTable && legacyTable->model()->rowCount()==2);
        assert(legacyWindow.m_assessmentPersistence.loadFor(assessmentDrive).stored.fileEntriesStored);
        legacyWindow.openFileSelection(MainWindow::FileSelectionMode::SecureWipeReview);
        assert(legacyScanCount==1 && legacyTable->model()->rowCount()==2);

        // A real scan error is shown and cached for this visit; it does not
        // fabricate entries or cause repeated scans while moving between panels.
        MainWindow failedWindow;
        failedWindow.m_assessmentPersistence=AssessmentPersistence(assessmentStoreDir.filePath(QStringLiteral("failed-auto.json")));
        failedWindow.m_selectedDriveInfo=assessmentDrive;
        failedWindow.m_selectedDeviceIdentity=QStringLiteral("\\\\?\\volume{second-guid}\\");
        failedWindow.m_hasSelectedDrive=true;
        int failedScanCount=0;
        failedWindow.m_fat32ScanLauncher=[&](const DriveInfo&, std::function<void(const FileScanResult&)> completion) {
            ++failedScanCount;
            FileScanResult errorResult; errorResult.status=FileScanStatus::Error; errorResult.errorMessage="synthetic I/O failure";
            completion(errorResult);
        };
        failedWindow.startDriveAssessment();
        auto* failedTable=failedWindow.findChild<QTableView*>(QStringLiteral("dashboardFileTable"));
        assert(failedScanCount==1 && failedTable && failedTable->model()->rowCount()==0);
        assert(failedWindow.m_dashboardFileStatus->text().contains(QStringLiteral("synthetic I/O failure")));
        failedWindow.openFileSelection(MainWindow::FileSelectionMode::RecoverCandidates);
        assert(failedScanCount==1 && failedTable->model()->rowCount()==0);

        window.resize(1240, 820);
        window.show();
        QApplication::processEvents();
        assert(window.width() >= 960 && window.height() >= 640);
        auto* overviewCard=window.findChild<QFrame*>(QStringLiteral("assessmentDriveOverview"));
        auto* summaryCard=window.findChild<QFrame*>(QStringLiteral("assessmentSummary"));
        assert(overviewCard && summaryCard && fileSection);
        assert(overviewCard->y()<fileSection->y() && fileSection->y()<summaryCard->y());
        assert(!window.findChild<QFrame*>(QStringLiteral("recoverabilityIndicators")));
        assert(!window.findChild<QFrame*>(QStringLiteral("assessmentResult")));
        window.resize(960, 640);
        QApplication::processEvents();
        assert(states->width() > 0 && states->height() > 0);

        if (qEnvironmentVariable("DATAGUARD_ALLOW_REAL_DEVICES") == QStringLiteral("1")) {
            window.startDeviceScan();
            assert(states->currentIndex() == 1); // Scanning state appears while enumeration runs.
            assert(window.m_scanAnimation->state() == QAbstractAnimation::Running);
            assert(!window.m_scanButton->isEnabled());
            QThread* firstScan = window.m_scanThread;
            window.startDeviceScan();
            assert(window.m_scanThread == firstScan); // Repeat clicks cannot create another worker.

            QEventLoop waitForScan;
            QTimer poll;
            poll.setInterval(10);
            QObject::connect(&poll, &QTimer::timeout, &waitForScan, [&window,&waitForScan]() {
                if (!window.m_deviceScanInProgress)
                    waitForScan.quit();
            });
            QTimer::singleShot(5000, &waitForScan, &QEventLoop::quit);
            poll.start();
            waitForScan.exec();
            poll.stop();
            assert(!window.m_deviceScanInProgress);
            assert(states->currentIndex() == 2 || states->currentIndex() == 3 || states->currentIndex() == 4);
            assert(window.m_scanAnimation->state() == QAbstractAnimation::Stopped);
            assert(window.m_scanButton->isEnabled());
        }
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    DashboardStateTests::run();
    return 0;
}
