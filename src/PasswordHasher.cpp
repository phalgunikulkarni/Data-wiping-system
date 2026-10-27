#include "PasswordHasher.h"

#include <windows.h>
#include <bcrypt.h>
#include <QStringList>

namespace {
constexpr ULONG kIterations = 600000;
constexpr ULONG kSaltBytes = 16;
constexpr ULONG kHashBytes = 32;

QByteArray derive(const QString& password, const QByteArray& salt, ULONG iterations)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0)
        return {};
    const QByteArray utf8 = password.toUtf8();
    QByteArray output(kHashBytes, Qt::Uninitialized);
    const NTSTATUS status = BCryptDeriveKeyPBKDF2(
        algorithm,
        reinterpret_cast<PUCHAR>(const_cast<char*>(utf8.constData())), static_cast<ULONG>(utf8.size()),
        reinterpret_cast<PUCHAR>(const_cast<char*>(salt.constData())), static_cast<ULONG>(salt.size()),
        iterations, reinterpret_cast<PUCHAR>(output.data()), static_cast<ULONG>(output.size()), 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return {};
    return output;
}

bool constantTimeEqual(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size()) return false;
    unsigned char difference = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        difference |= static_cast<unsigned char>(a.at(i) ^ b.at(i));
    return difference == 0;
}
}

QString PasswordHasher::hash(const QString& password, QString* error)
{
    QByteArray salt(kSaltBytes, Qt::Uninitialized);
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(salt.data()), kSaltBytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        if (error) *error = QStringLiteral("Secure random salt generation failed.");
        return {};
    }
    const QByteArray key = derive(password, salt, kIterations);
    if (key.isEmpty()) {
        if (error) *error = QStringLiteral("Password hashing failed.");
        return {};
    }
    return QStringLiteral("pbkdf2-sha256$%1$%2$%3")
        .arg(kIterations).arg(QString::fromLatin1(salt.toHex()), QString::fromLatin1(key.toHex()));
}

bool PasswordHasher::verify(const QString& password, const QString& encoded)
{
    const QStringList parts = encoded.split(QLatin1Char('$'));
    if (parts.size() != 4 || parts[0] != QStringLiteral("pbkdf2-sha256")) return false;
    bool ok = false;
    const uint iterations = parts[1].toUInt(&ok);
    if (!ok || iterations < 100000 || iterations > 2000000) return false;
    const QByteArray salt = QByteArray::fromHex(parts[2].toLatin1());
    const QByteArray expected = QByteArray::fromHex(parts[3].toLatin1());
    if (salt.size() != kSaltBytes || expected.size() != kHashBytes ||
        salt.toHex() != parts[2].toLatin1().toLower() || expected.toHex() != parts[3].toLatin1().toLower()) return false;
    return constantTimeEqual(derive(password, salt, iterations), expected);
}
