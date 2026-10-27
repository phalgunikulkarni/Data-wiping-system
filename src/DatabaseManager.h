#pragma once

#include <QSqlDatabase>
#include <QString>

class DatabaseManager {
public:
    explicit DatabaseManager(const QString& databasePath = QString());
    ~DatabaseManager();
    bool initialize(QString* error = nullptr);
    QSqlDatabase database() const;
    QString databasePath() const;

private:
    QSqlDatabase m_database;
    QString m_connectionName;
    QString m_databasePath;
};
