#include "DatabaseManager.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

DatabaseManager::DatabaseManager(const QString& databasePath)
    : m_connectionName(QStringLiteral("dataguard_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
    if (databasePath.isEmpty()) {
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        m_databasePath = QDir(appData).filePath(QStringLiteral("dataguard.sqlite3"));
    } else {
        m_databasePath = databasePath;
    }
}

DatabaseManager::~DatabaseManager()
{
    if (m_database.isValid()) {
        const QString connection = m_connectionName;
        m_database.close();
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection);
    }
}

bool DatabaseManager::initialize(QString* error)
{
    const QFileInfo dbInfo(m_databasePath);
    if (!QDir().mkpath(dbInfo.absolutePath())) {
        if (error) *error = QStringLiteral("Could not create the application data directory.");
        return false;
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_databasePath);
    if (!m_database.open()) {
        if (error) *error = m_database.lastError().text();
        return false;
    }

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        if (error) *error = query.lastError().text();
        return false;
    }
    if (!query.exec(QStringLiteral("PRAGMA user_version"))) {
        if (error) *error = query.lastError().text();
        return false;
    }
    int version = 0;
    if (query.next()) version = query.value(0).toInt();
    if (version > 1) {
        if (error) *error = QStringLiteral("The local database was created by a newer application version.");
        return false;
    }
    if (version == 1) return true;

    if (!m_database.transaction()) {
        if (error) *error = m_database.lastError().text();
        return false;
    }
    const QStringList statements = {
        QStringLiteral("CREATE TABLE users (user_id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL COLLATE NOCASE UNIQUE, email TEXT NOT NULL COLLATE NOCASE UNIQUE, password_hash TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, account_status TEXT NOT NULL DEFAULT 'active' CHECK(account_status IN ('active','disabled')))"),
        QStringLiteral("CREATE TABLE user_preferences (user_id INTEGER PRIMARY KEY, remember_username INTEGER NOT NULL DEFAULT 0 CHECK(remember_username IN (0,1)), theme TEXT NOT NULL DEFAULT 'system', updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(user_id) REFERENCES users(user_id) ON DELETE CASCADE)"),
        QStringLiteral("CREATE INDEX idx_users_email ON users(email COLLATE NOCASE)")
    };
    for (const QString& sql : statements) {
        if (!query.exec(sql)) {
            if (error) *error = query.lastError().text();
            m_database.rollback();
            return false;
        }
    }
    if (!query.exec(QStringLiteral("PRAGMA user_version = 1")) || !m_database.commit()) {
        if (error) *error = m_database.lastError().text().isEmpty() ? query.lastError().text() : m_database.lastError().text();
        m_database.rollback();
        return false;
    }
    return true;
}

QSqlDatabase DatabaseManager::database() const { return m_database; }
QString DatabaseManager::databasePath() const { return m_databasePath; }
