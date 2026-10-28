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
#include <cassert>

class DashboardStateTests {
public:
    static void run()
    {
        MainWindow window;
        window.setSignedInUser(QStringLiteral("DashboardTester"));
        auto* states = window.findChild<QStackedWidget*>(QStringLiteral("dashboardStates"));
        assert(states);
        assert(states->currentIndex() == 0); // Initial connect state.

        DeviceEnumerationResult found;
        found.success = true;
        DriveInfo device;
        device.letter = 'E';
        device.mountPoint = "E:\\";
        device.volumeLabel = "TEST VOLUME";
        device.fileSystem = "exFAT";
        device.capacityBytes = 64000000000ULL;
        device.capacityAvailable = true;
        device.connected = true;
        found.devices.push_back(device);
        window.displayDeviceResults(found);
        assert(states->currentIndex() == 2); // Found state.
        auto* list = window.findChild<QListWidget*>(QStringLiteral("detectedDevicesList"));
        assert(list && list->count() == 1);
        assert(list->currentRow() == -1); // Selection remains explicit.
        window.selectDeviceRow(0);
        assert(!window.m_selectedDevicePanel->isHidden()); // Selected-device details revealed.

        DeviceEnumerationResult empty;
        empty.success = true;
        window.displayDeviceResults(empty);
        assert(states->currentIndex() == 3); // No-device state.

        DeviceEnumerationResult failure;
        failure.success = false;
        failure.errorMessage = "Test enumeration failure";
        window.displayDeviceResults(failure);
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
