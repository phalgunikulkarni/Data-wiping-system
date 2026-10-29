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
#include <QToolButton>
#include <QTableView>
#include <QThread>
#include <cassert>
#include <algorithm>

class DashboardStateTests {
public:
    static void run()
    {
        MainWindow window;
        window.setSignedInUser(QStringLiteral("DashboardTester"));
        auto* states = window.findChild<QStackedWidget*>(QStringLiteral("dashboardStates"));
        assert(states);
        assert(states->currentIndex() == 0); // Initial connect state.
        assert(states->count() == 6); // The last dashboard state is the drive-level assessment.

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
        FileScanResult identified;
        identified.status=FileScanStatus::Success;
        identified.volumeIdentityValidated=true;
        FileEntry existingEntry; existingEntry.status=FileStatus::EXISTING; existingEntry.isDirectory=false;
        existingEntry.name="existing.txt"; existingEntry.nameKnown=true; existingEntry.fullPath="Docs\\existing.txt";
        existingEntry.sizeBytes=17; existingEntry.sizeKnown=true;
        FileEntry deletedEntry; deletedEntry.status=FileStatus::DELETED; deletedEntry.confidence=FileRecordConfidence::Candidate;
        deletedEntry.nameKnown=false; deletedEntry.deletedMarkerObserved=true;
        deletedEntry.parentPathKnown=true; deletedEntry.parentPath="Archive";
        FileEntry invalidDeletedEntry; invalidDeletedEntry.status=FileStatus::INVALID;
        invalidDeletedEntry.confidence=FileRecordConfidence::Invalid; invalidDeletedEntry.deletedMarkerObserved=true;
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
        assert(window.m_assessmentStatus->text()==QStringLiteral("Assessment Incomplete"));

        window.resize(1240, 820);
        window.show();
        QApplication::processEvents();
        assert(window.width() >= 960 && window.height() >= 640);
        auto* overviewCard=window.findChild<QFrame*>(QStringLiteral("assessmentDriveOverview"));
        auto* indicatorsCard=window.findChild<QFrame*>(QStringLiteral("recoverabilityIndicators"));
        auto* resultCard=window.findChild<QFrame*>(QStringLiteral("assessmentResult"));
        assert(overviewCard && indicatorsCard && resultCard && fileSection);
        assert(overviewCard->y()<fileSection->y() && fileSection->y()<indicatorsCard->y());
        assert(indicatorsCard->x()<resultCard->x());
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
