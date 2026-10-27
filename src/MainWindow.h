#pragma once

#include <QMainWindow>
#include <QString>
#include <QVector>

class QLabel;
class QStackedWidget;
class QToolButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
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

    QString m_signedInUser;
    QLabel* m_pageTitle = nullptr;
    QLabel* m_accountLabel = nullptr;
    QLabel* m_welcomeLabel = nullptr;
    QLabel* m_settingsUsername = nullptr;
    QStackedWidget* m_pages = nullptr;
    QVector<QToolButton*> m_navigationButtons;
};
