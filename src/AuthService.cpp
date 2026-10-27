#include "AuthService.h"
#include "PasswordHasher.h"

#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

AuthService::AuthService(const QSqlDatabase& database) : m_database(database) {}

bool AuthService::registerUser(const QString& usernameInput, const QString& emailInput, const QString& password, QString* error)
{
    const QString username = usernameInput.trimmed();
    const QString email = emailInput.trimmed().toLower();
    static const QRegularExpression usernamePattern(QStringLiteral("^[A-Za-z0-9_]{3,32}$"));
    static const QRegularExpression emailPattern(QStringLiteral("^[^\\s@]+@[^\\s@]+\\.[^\\s@]{2,}$"));
    if (!usernamePattern.match(username).hasMatch()) { if (error) *error = QStringLiteral("Username must be 3–32 characters using letters, numbers, or underscore."); return false; }
    if (!emailPattern.match(email).hasMatch()) { if (error) *error = QStringLiteral("Enter a valid email address."); return false; }
    if (password.size() < 12 || password.size() > 256) { if (error) *error = QStringLiteral("Password must be 12–256 characters."); return false; }

    QString hashError;
    const QString passwordHash = PasswordHasher::hash(password, &hashError);
    if (passwordHash.isEmpty()) { if (error) *error = hashError; return false; }
    if (!m_database.transaction()) { if (error) *error = QStringLiteral("Could not start account creation."); return false; }

    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral("INSERT INTO users(username,email,password_hash) VALUES(:username,:email,:hash)"));
    insert.bindValue(QStringLiteral(":username"), username);
    insert.bindValue(QStringLiteral(":email"), email);
    insert.bindValue(QStringLiteral(":hash"), passwordHash);
    if (!insert.exec()) {
        m_database.rollback();
        if (error) *error = insert.lastError().nativeErrorCode() == QStringLiteral("19")
            ? QStringLiteral("That username or email is already in use.")
            : QStringLiteral("Could not create the local account.");
        return false;
    }
    QSqlQuery prefs(m_database);
    prefs.prepare(QStringLiteral("INSERT INTO user_preferences(user_id) VALUES(:id)"));
    prefs.bindValue(QStringLiteral(":id"), insert.lastInsertId());
    if (!prefs.exec() || !m_database.commit()) {
        if (error) *error = QStringLiteral("Could not save account preferences.");
        m_database.rollback();
        return false;
    }
    return true;
}

bool AuthService::signIn(const QString& identityInput, const QString& password, QString* username, QString* error)
{
    const QString identity = identityInput.trimmed();
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT username,password_hash FROM users WHERE (email=:identity OR username=:identity) AND account_status='active' LIMIT 1"));
    query.bindValue(QStringLiteral(":identity"), identity);
    if (!query.exec()) { if (error) *error = QStringLiteral("Sign in is temporarily unavailable."); return false; }
    if (!query.next() || !PasswordHasher::verify(password, query.value(1).toString())) {
        if (error) *error = QStringLiteral("The username/email or password is incorrect.");
        return false;
    }
    if (username) *username = query.value(0).toString();
    return true;
}
