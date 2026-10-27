#include "../src/AuthService.h"
#include "../src/DatabaseManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("auth-test-XXXXXX")));
    assert(temporary.isValid());

    QString error;
    DatabaseManager manager(temporary.filePath(QStringLiteral("auth-test.sqlite3")));
    const bool initialized = manager.initialize(&error);
    if (!initialized) std::cerr << "Database initialization failed: " << error.toStdString() << std::endl;
    assert(initialized);
    AuthService auth(manager.database());

    assert(auth.registerUser(QStringLiteral("Test_User"), QStringLiteral("Person@Example.com"),
                             QStringLiteral("correct horse battery staple"), &error));
    QString signedIn;
    assert(auth.signIn(QStringLiteral("person@example.com"), QStringLiteral("correct horse battery staple"), &signedIn, &error));
    assert(signedIn == QStringLiteral("Test_User"));
    assert(!auth.signIn(QStringLiteral("Test_User"), QStringLiteral("incorrect password"), nullptr, &error));
    assert(!auth.registerUser(QStringLiteral("test_user"), QStringLiteral("other@example.com"),
                              QStringLiteral("correct horse battery staple"), &error));
    assert(!auth.registerUser(QStringLiteral("other_user"), QStringLiteral("person@example.com"),
                              QStringLiteral("correct horse battery staple"), &error));

    QSqlQuery stored(manager.database());
    assert(stored.exec(QStringLiteral("SELECT password_hash FROM users LIMIT 1")));
    assert(stored.next());
    const QString encoded = stored.value(0).toString();
    assert(encoded.startsWith(QStringLiteral("pbkdf2-sha256$")));
    assert(!encoded.contains(QStringLiteral("correct horse battery staple")));

    assert(!auth.registerUser(QStringLiteral("bad name"), QStringLiteral("bad-email"),
                              QStringLiteral("short"), &error));
    return 0;
}
