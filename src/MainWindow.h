#pragma once

#include "DeviceManager.h"
#include "FileScanner.h"

#include <QMainWindow>
#include <QString>
#include <QVector>
#include <vector>
#include <atomic>

class QLabel;
class QListWidget;
class QPropertyAnimation;
class QPushButton;
class QStackedWidget;
class QThread;
class QToolButton;
class QFrame;

class MainWindow : public QMainWindow {
    Q_OBJECT
    friend class DashboardStateTests;
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void setSignedInUser(const QString& username);

signals:
    void signOutRequested();

private:
    QWidget* buildDashboardPage();
    QWidget* buildDevicesPage();
    QWidget* buildScanResultsPage();
    QWidget* buildOperationsPage();
    QWidget* buildSettingsPage();
    QWidget* buildPlaceholderPage(const QString& heading, const QString& message, const QString& icon);
    void navigateTo(int pageIndex, const QString& title);
    void signOut();
    void startDeviceScan();
    void completeDeviceScan(const DeviceEnumerationResult& result);
    void stopScanAnimation();
    void displayDeviceResults(const DeviceEnumerationResult& result);
    void selectDeviceRow(int row);
    void startDeletedEntryScan();
    void completeDeletedEntryScan(const FileScanResult& result, const QString& sourceVolume);

    QString m_signedInUser;
    QString m_selectedDeviceIdentity;
    DriveInfo m_selectedDriveInfo;
    bool m_hasSelectedDrive = false;
    QLabel* m_pageTitle = nullptr;
    QLabel* m_accountLabel = nullptr;
    QLabel* m_settingsUsername = nullptr;
    QLabel* m_selectionPrompt = nullptr;
    QLabel* m_scanStateIcon = nullptr;
    QLabel* m_scanStatusLabel = nullptr;
    QLabel* m_scanDetailLabel = nullptr;
    QLabel* m_selectedDeviceName = nullptr;
    QLabel* m_selectedDeviceDetails = nullptr;
    QLabel* m_technicalDeviceDetails = nullptr;
    QToolButton* m_deviceDetailsToggle = nullptr;
    QListWidget* m_deviceList = nullptr;
    QListWidget* m_fileResultsList = nullptr;
    QPushButton* m_scanButton = nullptr;
    QPushButton* m_fileScanButton = nullptr;
    QPushButton* m_fileScanCancelButton = nullptr;
    QLabel* m_fileScanStatus = nullptr;
    QLabel* m_fileScanSource = nullptr;
    QFrame* m_selectedDevicePanel = nullptr;
    QStackedWidget* m_pages = nullptr;
    QStackedWidget* m_dashboardStates = nullptr;
    QPropertyAnimation* m_scanAnimation = nullptr;
    QThread* m_scanThread = nullptr;
    QThread* m_fileScanThread = nullptr;
    std::shared_ptr<std::atomic_bool> m_fileScanCancelled;
    bool m_fileScanInProgress = false;
    bool m_deviceScanInProgress = false;
    std::vector<DriveInfo> m_detectedDevices;
    QVector<QToolButton*> m_navigationButtons;
};
