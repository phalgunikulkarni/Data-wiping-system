#pragma once

#include <QByteArray>
#include <QString>

class PasswordHasher {
public:
    static QString hash(const QString& password, QString* error = nullptr);
    static bool verify(const QString& password, const QString& encoded);
};
