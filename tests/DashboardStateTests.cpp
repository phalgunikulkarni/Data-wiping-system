#include "../src/MainWindow.h"

#include <QApplication>
#include <QAbstractAnimation>
#include <QEventLoop>
#include <QLabel>
#include <QListWidget>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
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

        window.resize(1240, 820);
        window.show();
        QApplication::processEvents();
        assert(window.width() >= 960 && window.height() >= 640);
        window.resize(960, 640);
        QApplication::processEvents();
        assert(states->width() > 0 && states->height() > 0);

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
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    DashboardStateTests::run();
    return 0;
}
