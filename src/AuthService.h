#pragma once

#include <QSqlDatabase>
#include <QString>

class AuthService {
public:
    explicit AuthService(const QSqlDatabase& database);
    bool registerUser(const QString& username, const QString& email, const QString& password, QString* error);
    bool signIn(const QString& identity, const QString& password, QString* username, QString* error);
private:
    QSqlDatabase m_database;
};
