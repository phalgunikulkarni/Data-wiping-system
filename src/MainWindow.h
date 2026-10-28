#pragma once

#include "DeviceManager.h"

#include <QMainWindow>
#include <QString>
#include <QVector>
#include <vector>

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
    QWidget* buildOperationsPage();
    QWidget* buildSettingsPage();
    QWidget* buildPlaceholderPage(const QString& heading, const QString& message, const QString& icon);
    void navigateTo(int pageIndex, const QString& title);
    void signOut();
    void startDeviceScan();
    void stopScanAnimation();
    void displayDeviceResults(const DeviceEnumerationResult& result);
    void selectDeviceRow(int row);

    QString m_signedInUser;
    QLabel* m_pageTitle = nullptr;
    QLabel* m_accountLabel = nullptr;
    QLabel* m_settingsUsername = nullptr;
    QLabel* m_selectionPrompt = nullptr;
    QLabel* m_scanStateIcon = nullptr;
    QLabel* m_scanStatusLabel = nullptr;
    QLabel* m_scanDetailLabel = nullptr;
    QLabel* m_selectedDeviceName = nullptr;
    QLabel* m_selectedDeviceDetails = nullptr;
    QListWidget* m_deviceList = nullptr;
    QPushButton* m_scanButton = nullptr;
    QFrame* m_selectedDevicePanel = nullptr;
    QStackedWidget* m_pages = nullptr;
    QStackedWidget* m_dashboardStates = nullptr;
    QPropertyAnimation* m_scanAnimation = nullptr;
    QThread* m_scanThread = nullptr;
    bool m_deviceScanInProgress = false;
    std::vector<DriveInfo> m_detectedDevices;
    QVector<QToolButton*> m_navigationButtons;
};
