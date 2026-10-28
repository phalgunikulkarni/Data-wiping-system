#pragma once

#include "DatabaseManager.h"
#include "AuthService.h"

#include <QMainWindow>

class QLineEdit;
class QLabel;
class QPushButton;
class QStackedWidget;

class AuthWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit AuthWindow(QWidget* parent = nullptr);
    bool isReady() const;
    QString startupError() const;
signals:
    void authenticated(const QString& username);
private:
    void buildUi();
    void showSignIn();
    void showCreateAccount();
    void submitSignIn();
    void submitRegistration();
    DatabaseManager m_database;
    AuthService* m_auth = nullptr;
    QString m_startupError;
    QStackedWidget* m_pages = nullptr;
    QLineEdit *m_identity = nullptr, *m_signInPassword = nullptr;
    QLineEdit *m_username = nullptr, *m_email = nullptr, *m_password = nullptr, *m_confirm = nullptr;
    QLabel *m_signInError = nullptr, *m_registerError = nullptr;
    QPushButton *m_signInButton = nullptr, *m_createButton = nullptr;
};
