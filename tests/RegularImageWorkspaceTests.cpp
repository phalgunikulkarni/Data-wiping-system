#include "../src/RegularImageWorkspace.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>
#include <limits>

static bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
}

static QStringList regularFiles(const QString& path)
{
    return QDir(path).entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
}

int main()
{
    QTemporaryDir temporary(QDir::currentPath() + QStringLiteral("/regular-image-workspace-XXXXXX"));
    assert(temporary.isValid());
    const QString root = temporary.path();
    assert(QDir(root).mkpath(QStringLiteral("baseline")));
    assert(QDir(root).mkpath(QStringLiteral("references")));
    assert(QDir(root).mkpath(QStringLiteral("working")));
    const QByteArray original("abcdefgh");
    const QString sourcePath = QDir(root).filePath(QStringLiteral("baseline/source.img"));
    assert(writeFile(sourcePath, original));

    QString error;
    auto workspace = RegularImageWorkspace::open(root, QStringLiteral("working"),
        {QStringLiteral("baseline"), QStringLiteral("references")}, 2 * 1024 * 1024, error);
    if (!workspace) std::cerr << "workspace open failed: " << error.toStdString() << '\n';
    assert(workspace && error.isEmpty());
    assert(!RegularImageWorkspace::open(root, QStringLiteral("baseline"),
        {QStringLiteral("baseline")}, 1024, error)); // Work and protected roots cannot overlap.
    assert(!RegularImageWorkspace::open(QStringLiteral("C:/"), QStringLiteral("working"),
        {}, 1024, error));
    assert(!RegularImageWorkspace::open(QStringLiteral("\\\\.\\C:"), QStringLiteral("working"),
        {}, 1024, error));

    auto source = workspace->openExisting(QStringLiteral("baseline/source.img"), error);
    if (!source) std::cerr << "open source failed: " << error.toStdString() << '\n';
    assert(source && source->sizeBytes() == static_cast<std::uint64_t>(original.size()));
    QByteArray bytes;
    assert(source->readAt(0, static_cast<std::uint32_t>(original.size()), bytes, error));
    assert(bytes == original);
    assert(!source->readAt(std::numeric_limits<std::uint64_t>::max(), 1, bytes, error));
    assert(!source->readAt(4, 8, bytes, error)); // Truncated/out-of-bounds read is rejected.

    const QByteArray digest = QCryptographicHash::hash(original, QCryptographicHash::Sha256);
    assert(source->verifySha256(digest.toHex(), error));
    assert(!source->verifySha256(QByteArray(64, '0'), error));
    assert(!source->verifySha256(QByteArray("not-a-digest"), error));
    assert(!workspace->openExisting(QStringLiteral("../outside.txt"), error));
    assert(!workspace->openExisting(QStringLiteral("C:/Windows/win.ini"), error));
    assert(!workspace->openExisting(QStringLiteral("\\\\.\\PhysicalDrive0"), error));
    assert(!workspace->openExisting(QStringLiteral("\\\\?\\GLOBALROOT\\Device\\Harddisk0"), error));
    assert(!workspace->openExisting(QStringLiteral("baseline:stream"), error));
    assert(!workspace->createTemporary(QStringLiteral("baseline"), QStringLiteral("bad"), error));
    assert(!source->removeOwnedFile(error));

    const QByteArray beforeMismatch = regularFiles(QDir(root).filePath(QStringLiteral("working"))).join(',').toUtf8();
    assert(!workspace->copyToTemporary(QStringLiteral("baseline/source.img"), QStringLiteral("working"),
        QStringLiteral("mismatch"), QByteArray(64, '0'), error));
    assert(regularFiles(QDir(root).filePath(QStringLiteral("working"))).join(',').toUtf8() == beforeMismatch);

    auto copy = workspace->copyToTemporary(QStringLiteral("baseline/source.img"), QStringLiteral("working"),
        QStringLiteral("baseline-copy"), digest.toHex(), error);
    assert(copy && copy->sizeBytes() == original.size());
    assert(copy->verifySha256(digest.toHex(), error));
    QByteArray copied;
    assert(copy->readAt(0, static_cast<std::uint32_t>(original.size()), copied, error));
    assert(copied == original);
    const QString copyPath = copy->canonicalPath();
    assert(!copyPath.isEmpty() && copyPath.startsWith(workspace->canonicalRoot(), Qt::CaseInsensitive));

    const QByteArray seed("01234567");
    auto modified = workspace->createTemporary(QStringLiteral("working"), QStringLiteral("write-test"), error);
    assert(modified);
    assert(modified->writeAt(0, seed, error));
    assert(!modified->writeAt(std::numeric_limits<std::uint64_t>::max(), QByteArray("x"), error));
    assert(!modified->writeAt(9, QByteArray("x"), error));
    assert(modified->writeAt(2, QByteArray("XY"), error));
    QByteArray afterWrite;
    assert(modified->readAt(0, 8, afterWrite, error));
    assert(afterWrite == QByteArray("01XY4567"));
    assert(modified->removeOwnedFile(error));
    modified.reset();
    assert(QFileInfo::exists(copyPath)); // Copy remains until its owner removes it.

    const QStringList beforeCancel = regularFiles(QDir(root).filePath(QStringLiteral("working")));
    QByteArray large(1024 * 1024 + 17, 'q');
    const QString largePath = QDir(root).filePath(QStringLiteral("references/large.bin"));
    assert(writeFile(largePath, large));
    const QByteArray largeHash = QCryptographicHash::hash(large, QCryptographicHash::Sha256).toHex();
    int cancelChecks = 0;
    auto cancelled = workspace->copyToTemporary(QStringLiteral("references/large.bin"),
        QStringLiteral("working"), QStringLiteral("cancelled"), largeHash, error,
        [&cancelChecks]() { return ++cancelChecks >= 4; });
    assert(!cancelled && error.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
    assert(regularFiles(QDir(root).filePath(QStringLiteral("working"))).size() == beforeCancel.size());
    assert(QFileInfo(sourcePath).size() == original.size());

    const QString tooLargeRoot = QDir(root).filePath(QStringLiteral("small-limit"));
    assert(QDir().mkpath(QDir(tooLargeRoot).filePath(QStringLiteral("working"))));
    assert(QDir().mkpath(QDir(tooLargeRoot).filePath(QStringLiteral("baseline"))));
    assert(writeFile(QDir(tooLargeRoot).filePath(QStringLiteral("baseline/source.img")), original));
    auto smallLimit = RegularImageWorkspace::open(tooLargeRoot, QStringLiteral("working"),
        {QStringLiteral("baseline")}, 4, error);
    assert(smallLimit && !smallLimit->openExisting(QStringLiteral("baseline/source.img"), error));

    const QString outside = QDir(root).filePath(QStringLiteral("outside"));
    assert(QDir().mkpath(outside));
    assert(writeFile(QDir(outside).filePath(QStringLiteral("secret.txt")), QByteArray("secret")));
    const QString linkPath = QDir(root).filePath(QStringLiteral("working/link"));
    const DWORD linkFlags = SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2; // ALLOW_UNPRIVILEGED_CREATE when supported.
    if (CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(linkPath.utf16()),
                            reinterpret_cast<LPCWSTR>(outside.utf16()), linkFlags)) {
        assert(!workspace->openExisting(QStringLiteral("working/link/secret.txt"), error));
    } else {
        std::cout << "SKIP_SYMLINK_FIXTURE error=" << GetLastError() << '\n';
    }
    assert(QFileInfo(sourcePath).size() == original.size());
    assert(copy->removeOwnedFile(error));
    copy.reset();
    std::cout << "RegularImageWorkspace synthetic safety checks completed.\n";
}
