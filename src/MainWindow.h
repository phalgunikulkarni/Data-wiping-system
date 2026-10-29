#pragma once

#include "DeviceManager.h"
#include "FileScanner.h"
#include "RecoverabilityAssessment.h"

#include <QMainWindow>
#include <QString>
#include <QVector>
#include <array>
#include <vector>
#include <atomic>
#include <functional>

class QLabel;
class QListWidget;
class QPropertyAnimation;
class QPushButton;
class QStackedWidget;
class QThread;
class QToolButton;
class QFrame;
class EvidenceBreakdownWidget;
class QLineEdit;
class QComboBox;
class QTableView;
class DashboardFileFilterProxy;
class DashboardFileTableModel;

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
    QWidget* buildDashboardAssessmentPage();
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
    void startDriveAssessment();
    void completeDriveAssessment(const FileScanResult& result, const DriveInfo& drive);
    void launchFat32Scan(const DriveInfo& drive, std::function<void(const FileScanResult&)> completion);

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
    QLabel* m_progressStatusLabel = nullptr;
    QLabel* m_progressDetailLabel = nullptr;
    QLabel* m_selectedDeviceName = nullptr;
    QLabel* m_selectedDeviceDetails = nullptr;
    QLabel* m_technicalDeviceDetails = nullptr;
    QToolButton* m_deviceDetailsToggle = nullptr;
    QListWidget* m_deviceList = nullptr;
    QListWidget* m_fileResultsList = nullptr;
    QPushButton* m_scanButton = nullptr;
    QPushButton* m_fileScanButton = nullptr;
    QPushButton* m_fileScanCancelButton = nullptr;
    QPushButton* m_nextStepButton = nullptr;
    QPushButton* m_assessmentRetryButton = nullptr;
    QPushButton* m_assessmentBackButton = nullptr;
    QPushButton* m_assessmentCancelButton = nullptr;
    QLabel* m_fileScanStatus = nullptr;
    QLabel* m_fileScanSource = nullptr;
    QLabel* m_assessmentSummary = nullptr;
    QLabel* m_assessmentIndicators = nullptr;
    QLabel* m_assessmentStatus = nullptr;
    QLabel* m_assessmentExplanation = nullptr;
    QLabel* m_assessmentNextSteps = nullptr;
    QLabel* m_assessmentCompletionBadge = nullptr;
    std::array<QLabel*, 8> m_driveOverviewValues{};
    std::array<QLabel*, 2> m_assessmentMetricValues{};
    EvidenceBreakdownWidget* m_evidenceBreakdown = nullptr;
    QLineEdit* m_fileSearch = nullptr;
    QComboBox* m_fileFilter = nullptr;
    QTableView* m_dashboardFileTable = nullptr;
    QLabel* m_dashboardFileStatus = nullptr;
    DashboardFileTableModel* m_dashboardFileModel = nullptr;
    DashboardFileFilterProxy* m_dashboardFileProxy = nullptr;
    QFrame* m_selectedDevicePanel = nullptr;
    QStackedWidget* m_pages = nullptr;
    QStackedWidget* m_dashboardStates = nullptr;
    QPropertyAnimation* m_scanAnimation = nullptr;
    QThread* m_scanThread = nullptr;
    QThread* m_fileScanThread = nullptr;
    std::shared_ptr<std::atomic_bool> m_fileScanCancelled;
    bool m_fileScanInProgress = false;
    bool m_assessmentInProgress = false;
    bool m_deviceScanInProgress = false;
    std::vector<DriveInfo> m_detectedDevices;
    QVector<QToolButton*> m_navigationButtons;
};
