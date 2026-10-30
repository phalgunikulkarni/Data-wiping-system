#include "RegularImageWorkspace.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QUuid>
#include <algorithm>
#include <limits>
#include <vector>

struct RegularImageWorkspace::Impl
{
    QString root;
    QString workingDirectory;
    QStringList protectedDirectories;
    std::uint64_t maxBytes = 0;
    HANDLE rootHandle = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION rootInfo{};
    ~Impl() { if (rootHandle != INVALID_HANDLE_VALUE) CloseHandle(rootHandle); }
};

namespace {
QString winError(const QString& action, DWORD code = GetLastError())
{
    return action + QStringLiteral(" (Windows error %1)").arg(code);
}

bool isDeviceOrNetworkPath(const QString& path)
{
    const QString p = QDir::toNativeSeparators(path);
    return p.startsWith(QStringLiteral("\\\\"), Qt::CaseSensitive) ||
           p.startsWith(QStringLiteral("\\?"), Qt::CaseInsensitive) ||
           p.startsWith(QStringLiteral("\\."), Qt::CaseInsensitive);
}

bool isDriveRoot(const QString& path)
{
    const QString p = QDir::toNativeSeparators(path);
    return p.size() == 3 && p[1] == QLatin1Char(':') && p[2] == QLatin1Char('\\');
}

QString normalizedHandlePath(HANDLE handle, QString& error)
{
    std::wstring buffer(32768, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                    FILE_NAME_OPENED | VOLUME_NAME_DOS);
    if (!written || written >= buffer.size()) {
        error = winError(QStringLiteral("Could not resolve the opened file path"));
        return {};
    }
    QString result = QDir::fromNativeSeparators(QString::fromWCharArray(buffer.data(), static_cast<int>(written)));
    if (result.startsWith(QStringLiteral("//?/"), Qt::CaseInsensitive))
        result.remove(0, 4);
    return QDir::cleanPath(result);
}

bool pathWithinRoot(const QString& root, const QString& candidate)
{
    QString boundary = root;
    while (boundary.size() > 3 && boundary.endsWith(QLatin1Char('/')))
        boundary.chop(1);
    if (candidate.compare(boundary, Qt::CaseInsensitive) == 0)
        return true;
    if (boundary.size() == 3 && boundary.endsWith(QLatin1Char('/')))
        return candidate.startsWith(boundary, Qt::CaseInsensitive);
    boundary += QLatin1Char('/');
    return candidate.startsWith(boundary, Qt::CaseInsensitive);
}

bool sameIdentity(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b)
{
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
           a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}

bool sameSnapshot(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b)
{
    return sameIdentity(a, b) && a.nFileSizeHigh == b.nFileSizeHigh &&
           a.nFileSizeLow == b.nFileSizeLow &&
           CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0;
}

std::uint64_t fileSize(const BY_HANDLE_FILE_INFORMATION& info)
{
    return (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}

bool getInfo(HANDLE handle, BY_HANDLE_FILE_INFORMATION& info, QString& error)
{
    if (!GetFileInformationByHandle(handle, &info)) {
        error = winError(QStringLiteral("Could not inspect opened file"));
        return false;
    }
    return true;
}

bool validateRegularHandle(HANDLE handle, const QString& root, std::uint64_t maxBytes,
                           BY_HANDLE_FILE_INFORMATION& info, QString& finalPath, QString& error)
{
    if (GetFileType(handle) != FILE_TYPE_DISK) {
        error = QStringLiteral("Only regular disk files are accepted");
        return false;
    }
    if (!getInfo(handle, info, error))
        return false;
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        info.nNumberOfLinks != 1) {
        error = QStringLiteral("Directories, reparse points, and multiply-linked files are rejected");
        return false;
    }
    if (fileSize(info) > maxBytes) {
        error = QStringLiteral("File exceeds the configured size limit");
        return false;
    }
    finalPath = normalizedHandlePath(handle, error);
    if (finalPath.isEmpty())
        return false;
    if (!pathWithinRoot(root, finalPath)) {
        error = QStringLiteral("Opened file resolved outside the approved workspace");
        return false;
    }
    return true;
}

bool relativeComponents(const QString& input, QStringList& components, QString& error)
{
    components.clear();
    if (input.isEmpty() || input.startsWith(QLatin1Char('/')) || input.startsWith(QLatin1Char('\\')) ||
        input.contains(QLatin1Char(':')) || input.contains(QChar::Null)) {
        error = QStringLiteral("A non-empty relative path is required");
        return false;
    }
    const QString normalized = input;
    const QStringList raw = normalized.split(QRegularExpression(QStringLiteral("[/\\\\]")), Qt::KeepEmptyParts);
    if (raw.isEmpty()) {
        error = QStringLiteral("Invalid relative path");
        return false;
    }
    for (const QString& part : raw) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..") ||
            part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')) ||
            part.contains(QRegularExpression(QStringLiteral("[<>\"|?*]")))) {
            error = QStringLiteral("Path contains an empty, traversal, or invalid Windows component");
            return false;
        }
        const QString base = part.section(QLatin1Char('.'), 0, 0).toUpper();
        if (base == QStringLiteral("CON") || base == QStringLiteral("PRN") ||
            base == QStringLiteral("AUX") || base == QStringLiteral("NUL") ||
            (base.size() == 4 && (base.startsWith(QStringLiteral("COM")) || base.startsWith(QStringLiteral("LPT"))) &&
             base[3] >= QLatin1Char('1') && base[3] <= QLatin1Char('9'))) {
            error = QStringLiteral("Reserved Windows device names are rejected");
            return false;
        }
        components.push_back(part);
    }
    return true;
}

bool relativeWithin(const QStringList& base, const QStringList& candidate)
{
    if (candidate.size() < base.size()) return false;
    for (qsizetype i = 0; i < base.size(); ++i)
        if (base[i].compare(candidate[i], Qt::CaseInsensitive) != 0) return false;
    return true;
}

HANDLE openCheckedDirectory(const QString& path, const QString& allowedRoot, QString& error)
{
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
                                FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = winError(QStringLiteral("Could not open workspace directory"));
        return INVALID_HANDLE_VALUE;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!getInfo(handle, info, error) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        if (error.isEmpty()) error = QStringLiteral("Workspace path contains a non-directory or reparse point");
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    const QString checkedPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (!pathWithinRoot(QDir::fromNativeSeparators(allowedRoot), checkedPath)) {
        error = QStringLiteral("Directory path is outside approved workspace");
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

bool openDirectoryChain(const std::shared_ptr<RegularImageWorkspace::Impl>& workspace,
                       const QStringList& parts, std::vector<HANDLE>& handles,
                       QString& finalPath, QString& error)
{
    QString currentPath = workspace->root;
    HANDLE current = CreateFileW(reinterpret_cast<LPCWSTR>(currentPath.utf16()), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (current == INVALID_HANDLE_VALUE) {
        error = winError(QStringLiteral("Could not reopen workspace root"));
        return false;
    }
    BY_HANDLE_FILE_INFORMATION rootInfo{};
    if (!getInfo(current, rootInfo, error) || !sameIdentity(rootInfo, workspace->rootInfo)) {
        if (error.isEmpty()) error = QStringLiteral("Workspace root identity changed");
        CloseHandle(current);
        return false;
    }
    handles.push_back(current);
    for (const QString& part : parts) {
        currentPath = QDir(currentPath).filePath(part);
        current = openCheckedDirectory(currentPath, workspace->root, error);
        if (current == INVALID_HANDLE_VALUE) return false;
        handles.push_back(current);
    }
    finalPath = currentPath;
    return true;
}

void closeHandles(std::vector<HANDLE>& handles)
{
    for (HANDLE handle : handles)
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    handles.clear();
}
}

struct RegularImageWorkspace::File::Impl
{
    std::shared_ptr<RegularImageWorkspace::Impl> workspace;
    HANDLE handle = INVALID_HANDLE_VALUE;
    QString finalPath;
    BY_HANDLE_FILE_INFORMATION snapshot{};
    bool owned = false;
    bool removed = false;
    ~Impl() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }

    bool current(BY_HANDLE_FILE_INFORMATION& info, QString& error) const
    {
        if (removed || handle == INVALID_HANDLE_VALUE) {
            error = QStringLiteral("File handle is no longer available");
            return false;
        }
        QString checkedPath;
        if (!validateRegularHandle(handle, workspace->root, workspace->maxBytes, info, checkedPath, error))
            return false;
        if (!sameSnapshot(snapshot, info) || checkedPath.compare(finalPath, Qt::CaseInsensitive) != 0) {
            error = QStringLiteral("File identity, path, size, or modification time changed");
            return false;
        }
        return true;
    }
};

RegularImageWorkspace::File::File(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
RegularImageWorkspace::File::~File() = default;

std::uint64_t RegularImageWorkspace::File::sizeBytes() const
{
    if (!m_impl || m_impl->removed) return 0;
    BY_HANDLE_FILE_INFORMATION info{};
    QString error;
    if (!m_impl->current(info, error)) return 0;
    return fileSize(info);
}

QString RegularImageWorkspace::File::canonicalPath() const
{
    if (!m_impl || m_impl->removed) return {};
    QString error;
    return normalizedHandlePath(m_impl->handle, error);
}

bool RegularImageWorkspace::File::readAt(std::uint64_t offset, std::uint32_t length,
                                         QByteArray& output, QString& error) const
{
    output.clear();
    if (!m_impl) { error = QStringLiteral("Invalid file handle"); return false; }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!m_impl->current(info, error)) return false;
    const std::uint64_t size = fileSize(info);
    if (offset > size || static_cast<std::uint64_t>(length) > size - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max())) {
        error = QStringLiteral("Read range is outside the file bounds");
        return false;
    }
    if (length == 0) return true;
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(m_impl->handle, position, nullptr, FILE_BEGIN)) {
        error = winError(QStringLiteral("Could not seek in regular image file"));
        return false;
    }
    output.resize(static_cast<qsizetype>(length));
    DWORD received = 0;
    if (!ReadFile(m_impl->handle, output.data(), length, &received, nullptr) || received != length) {
        output.clear();
        error = winError(QStringLiteral("Short or failed regular-file read"));
        return false;
    }
    return m_impl->current(info, error);
}

bool RegularImageWorkspace::File::writeAt(std::uint64_t offset, const QByteArray& bytes, QString& error)
{
    if (!m_impl || !m_impl->owned) { error = QStringLiteral("Writes are allowed only to a newly-created owned working file"); return false; }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!m_impl->current(info, error)) return false;
    const std::uint64_t size = fileSize(info);
    const std::uint64_t length = static_cast<std::uint64_t>(bytes.size());
    if (offset > size || offset > m_impl->workspace->maxBytes ||
        length > m_impl->workspace->maxBytes - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max())) {
        error = QStringLiteral("Write range is outside the current file or configured size limit");
        return false;
    }
    if (bytes.isEmpty()) return true;
    if (static_cast<std::uint64_t>(bytes.size()) > std::numeric_limits<DWORD>::max()) {
        error = QStringLiteral("Write request exceeds the bounded Windows transfer size");
        return false;
    }
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(m_impl->handle, position, nullptr, FILE_BEGIN)) {
        error = winError(QStringLiteral("Could not seek in working file"));
        return false;
    }
    DWORD written = 0;
    if (!WriteFile(m_impl->handle, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
        written != static_cast<DWORD>(bytes.size())) {
        error = winError(QStringLiteral("Short or failed working-file write"));
        return false;
    }
    if (!FlushFileBuffers(m_impl->handle)) {
        error = winError(QStringLiteral("Could not flush working file"));
        return false;
    }
    if (!getInfo(m_impl->handle, m_impl->snapshot, error)) return false;
    return true;
}

bool RegularImageWorkspace::File::sha256(QByteArray& digest, QString& error) const
{
    digest.clear();
    if (!m_impl) { error = QStringLiteral("Invalid file handle"); return false; }
    BY_HANDLE_FILE_INFORMATION before{};
    if (!m_impl->current(before, error)) return false;
    const std::uint64_t size = fileSize(before);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    constexpr std::uint32_t chunkSize = 1024 * 1024;
    std::uint64_t offset = 0;
    while (offset < size) {
        const std::uint64_t remain = size - offset;
        const auto amount = static_cast<std::uint32_t>(std::min<std::uint64_t>(remain, chunkSize));
        QByteArray chunk;
        if (!readAt(offset, amount, chunk, error)) return false;
        hash.addData(chunk);
        offset += amount; // amount is bounded by size - offset.
    }
    BY_HANDLE_FILE_INFORMATION after{};
    if (!m_impl->current(after, error) || !sameSnapshot(before, after)) {
        if (error.isEmpty()) error = QStringLiteral("File changed during SHA-256 calculation");
        return false;
    }
    digest = hash.result();
    return true;
}

bool RegularImageWorkspace::File::verifySha256(const QByteArray& expectedHex, QString& error) const
{
    if (expectedHex.size() != 64) { error = QStringLiteral("Expected SHA-256 must contain 64 hexadecimal characters"); return false; }
    for (char c : expectedHex) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            error = QStringLiteral("Expected SHA-256 contains a non-hexadecimal character");
            return false;
        }
    }
    QByteArray actual;
    if (!sha256(actual, error)) return false;
    if (actual.toHex().compare(expectedHex, Qt::CaseInsensitive) != 0) {
        error = QStringLiteral("SHA-256 mismatch");
        return false;
    }
    return true;
}

bool RegularImageWorkspace::File::removeOwnedFile(QString& error)
{
    if (!m_impl || !m_impl->owned) { error = QStringLiteral("Only an owned temporary file can be removed"); return false; }
    BY_HANDLE_FILE_INFORMATION info{};
    QString path;
    if (!validateRegularHandle(m_impl->handle, m_impl->workspace->root,
                               m_impl->workspace->maxBytes, info, path, error) ||
        !sameIdentity(info, m_impl->snapshot) ||
        path.compare(m_impl->finalPath, Qt::CaseInsensitive) != 0) {
        if (error.isEmpty()) error = QStringLiteral("Owned temporary file identity or path changed");
        return false;
    }
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    if (!SetFileInformationByHandle(m_impl->handle, FileDispositionInfo, &disposition, sizeof(disposition))) {
        error = winError(QStringLiteral("Could not remove owned temporary file"));
        return false;
    }
    m_impl->removed = true;
    return true;
}

RegularImageWorkspace::RegularImageWorkspace(std::shared_ptr<Impl> impl) : m_impl(std::move(impl)) {}
RegularImageWorkspace::~RegularImageWorkspace() = default;

std::unique_ptr<RegularImageWorkspace> RegularImageWorkspace::open(
    const QString& workspaceRoot, const QString& workingDirectory,
    const QStringList& protectedDirectories, std::uint64_t maxFileBytes, QString& error)
{
    if (maxFileBytes == 0 || workspaceRoot.isEmpty() || isDeviceOrNetworkPath(workspaceRoot) ||
        workspaceRoot.contains(QStringLiteral(".."))) {
        error = QStringLiteral("A local dedicated workspace and nonzero file-size limit are required");
        return {};
    }
    const QString absolute = QDir::toNativeSeparators(QFileInfo(workspaceRoot).absoluteFilePath());
    if (isDriveRoot(absolute)) { error = QStringLiteral("A drive root cannot be an approved workspace"); return {}; }
    if (absolute.size() < 3 || absolute[1] != QLatin1Char(':') || absolute.count(QLatin1Char(':')) != 1) {
        error = QStringLiteral("Workspace must use a local drive path without alternate streams");
        return {};
    }
    wchar_t volumeRoot[MAX_PATH + 1] = {};
    if (!GetVolumePathNameW(reinterpret_cast<LPCWSTR>(absolute.utf16()), volumeRoot, MAX_PATH + 1)) {
        error = winError(QStringLiteral("Could not determine workspace volume"));
        return {};
    }
    QString volume = QDir::toNativeSeparators(QString::fromWCharArray(volumeRoot));
    QString relative = QDir::fromNativeSeparators(QDir(volume).relativeFilePath(absolute));
    QStringList rootParts;
    if (!relativeComponents(relative, rootParts, error)) return {};
    std::vector<HANDLE> rootChain;
    HANDLE current = CreateFileW(reinterpret_cast<LPCWSTR>(volume.utf16()), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (current == INVALID_HANDLE_VALUE) { error = winError(QStringLiteral("Could not open workspace volume directory")); return {}; }
    rootChain.push_back(current);
    QString currentPath = volume;
    for (const QString& part : rootParts) {
        currentPath = QDir(currentPath).filePath(part);
        current = openCheckedDirectory(currentPath, volume, error);
        if (current == INVALID_HANDLE_VALUE) { closeHandles(rootChain); return {}; }
        rootChain.push_back(current);
    }
    HANDLE handle = rootChain.back();
    auto impl = std::make_shared<Impl>();
    impl->rootHandle = handle;
    impl->maxBytes = maxFileBytes;
    if (!getInfo(handle, impl->rootInfo, error) ||
        !(impl->rootInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (impl->rootInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        if (error.isEmpty()) error = QStringLiteral("Workspace root must be a non-reparse directory");
        rootChain.pop_back();
        closeHandles(rootChain);
        return {};
    }
    impl->root = QDir::cleanPath(QDir::fromNativeSeparators(absolute));
    if (impl->root.isEmpty() || isDeviceOrNetworkPath(impl->root) || isDriveRoot(QDir::toNativeSeparators(impl->root))) {
        if (error.isEmpty()) error = QStringLiteral("Workspace root is not an allowed local directory");
        rootChain.pop_back();
        closeHandles(rootChain);
        impl->rootHandle = INVALID_HANDLE_VALUE;
        CloseHandle(handle);
        return {};
    }
    const QString volumeCanonical = QDir::fromNativeSeparators(volume);
    if (impl->root.compare(QDir::cleanPath(volumeCanonical), Qt::CaseInsensitive) == 0) {
        error = QStringLiteral("A volume root cannot be an approved workspace");
        rootChain.pop_back();
        closeHandles(rootChain);
        impl->rootHandle = INVALID_HANDLE_VALUE;
        CloseHandle(handle);
        return {};
    }
    rootChain.pop_back();
    closeHandles(rootChain);
    QStringList workingParts;
    if (!relativeComponents(workingDirectory, workingParts, error)) return {};
    impl->workingDirectory = workingParts.join(QLatin1Char('/'));
    std::vector<HANDLE> directoryHandles;
    QString checkedDirectory;
    if (!openDirectoryChain(impl, workingParts, directoryHandles, checkedDirectory, error)) {
        closeHandles(directoryHandles);
        return {};
    }
    closeHandles(directoryHandles);
    for (const QString& protectedDirectory : protectedDirectories) {
        QStringList protectedParts;
        if (!relativeComponents(protectedDirectory, protectedParts, error)) return {};
        if (relativeWithin(protectedParts, workingParts) || relativeWithin(workingParts, protectedParts)) {
            error = QStringLiteral("Working directory must be separate from protected directories");
            return {};
        }
        directoryHandles.clear();
        if (!openDirectoryChain(impl, protectedParts, directoryHandles, checkedDirectory, error)) {
            closeHandles(directoryHandles);
            return {};
        }
        closeHandles(directoryHandles);
        const QString normalized = protectedParts.join(QLatin1Char('/'));
        for (const QString& existing : impl->protectedDirectories) {
            QStringList existingParts;
            QString ignored;
            if (relativeComponents(existing, existingParts, ignored) &&
                (relativeWithin(existingParts, protectedParts) || relativeWithin(protectedParts, existingParts))) {
                error = QStringLiteral("Protected directory definitions overlap");
                return {};
            }
        }
        impl->protectedDirectories.push_back(normalized);
    }
    return std::unique_ptr<RegularImageWorkspace>(new RegularImageWorkspace(std::move(impl)));
}

QString RegularImageWorkspace::canonicalRoot() const { return m_impl ? m_impl->root : QString(); }

std::unique_ptr<RegularImageWorkspace::File> RegularImageWorkspace::openExisting(
    const QString& relativePath, QString& error) const
{
    QStringList parts;
    if (!m_impl || !relativeComponents(relativePath, parts, error)) return {};
    QString path;
    QStringList parentParts = parts;
    const QString leaf = parentParts.takeLast();
    std::vector<HANDLE> dirs;
    if (!openDirectoryChain(m_impl, parentParts, dirs, path, error)) {
        closeHandles(dirs);
        return {};
    }
    path = QDir(path).filePath(leaf);
    HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_RANDOM_ACCESS, nullptr);
    closeHandles(dirs);
    if (file == INVALID_HANDLE_VALUE) { error = winError(QStringLiteral("Could not open regular workspace file")); return {}; }
    auto f = std::make_unique<File::Impl>();
    f->workspace = m_impl;
    f->handle = file;
    if (!validateRegularHandle(file, m_impl->root, m_impl->maxBytes, f->snapshot, f->finalPath, error)) return {};
    return std::unique_ptr<File>(new File(std::move(f)));
}

std::unique_ptr<RegularImageWorkspace::File> RegularImageWorkspace::createTemporary(
    const QString& relativeDirectory, const QString& stem, QString& error) const
{
    QStringList dirs;
    if (!m_impl || !relativeComponents(relativeDirectory, dirs, error) || stem.isEmpty()) {
        if (error.isEmpty()) error = QStringLiteral("A safe existing directory and non-empty stem are required");
        return {};
    }
    QStringList workingParts;
    QString ignored;
    if (!relativeComponents(m_impl->workingDirectory, workingParts, ignored) ||
        !relativeWithin(workingParts, dirs)) {
        error = QStringLiteral("Temporary files may be created only beneath the approved working directory");
        return {};
    }
    for (const QString& protectedDirectory : m_impl->protectedDirectories) {
        QStringList protectedParts;
        if (!relativeComponents(protectedDirectory, protectedParts, ignored) ||
            relativeWithin(protectedParts, dirs) || relativeWithin(dirs, protectedParts)) {
            error = QStringLiteral("Temporary path overlaps a protected directory");
            return {};
        }
    }
    for (const QChar c : stem) {
        if (!(c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))) {
            error = QStringLiteral("Temporary-file stem contains unsupported characters");
            return {};
        }
    }
    std::vector<HANDLE> parentHandles;
    QString parentPath;
    if (!openDirectoryChain(m_impl, dirs, parentHandles, parentPath, error)) {
        closeHandles(parentHandles);
        return {};
    }
    for (int attempt = 0; attempt < 16; ++attempt) {
        const QString name = stem + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".tmp");
        const QString path = QDir(parentPath).filePath(name);
        HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE | DELETE | FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_RANDOM_ACCESS,
                                  nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) continue;
            closeHandles(parentHandles);
            error = winError(QStringLiteral("Could not create unique temporary working file"), code);
            return {};
        }
        auto f = std::make_unique<File::Impl>();
        f->workspace = m_impl;
        f->handle = file;
        f->owned = true;
        if (!validateRegularHandle(file, m_impl->root, m_impl->maxBytes, f->snapshot, f->finalPath, error)) {
            FILE_DISPOSITION_INFO disposition{}; disposition.DeleteFile = TRUE;
            SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
            closeHandles(parentHandles);
            return {};
        }
        closeHandles(parentHandles);
        return std::unique_ptr<File>(new File(std::move(f)));
    }
    closeHandles(parentHandles);
    error = QStringLiteral("Could not allocate a unique temporary filename");
    return {};
}

std::unique_ptr<RegularImageWorkspace::File> RegularImageWorkspace::copyToTemporary(
    const QString& inputRelativePath, const QString& outputDirectory, const QString& stem,
    const QByteArray& expectedSha256Hex, QString& error, CancellationCheck cancelled) const
{
    if (cancelled && cancelled()) { error = QStringLiteral("Copy cancelled before starting"); return {}; }
    auto input = openExisting(inputRelativePath, error);
    if (!input || !input->verifySha256(expectedSha256Hex, error)) return {};
    auto output = createTemporary(outputDirectory, stem, error);
    if (!output) return {};
    constexpr std::uint32_t chunkSize = 1024 * 1024;
    const std::uint64_t size = input->sizeBytes();
    std::uint64_t offset = 0;
    while (offset < size) {
        if (cancelled && cancelled()) {
            error = QStringLiteral("Copy cancelled; owned working copy removed");
            QString cleanupError;
            output->removeOwnedFile(cleanupError);
            return {};
        }
        const std::uint64_t remaining = size - offset;
        const auto amount = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, chunkSize));
        QByteArray chunk;
        if (!input->readAt(offset, amount, chunk, error) || !output->writeAt(offset, chunk, error)) {
            QString cleanupError;
            output->removeOwnedFile(cleanupError);
            return {};
        }
        offset += amount;
    }
    if (cancelled && cancelled()) {
        error = QStringLiteral("Copy cancelled; owned working copy removed");
        QString cleanupError;
        output->removeOwnedFile(cleanupError);
        return {};
    }
    if (!input->verifySha256(expectedSha256Hex, error) ||
        !output->verifySha256(expectedSha256Hex, error)) {
        QString cleanupError;
        output->removeOwnedFile(cleanupError);
        return {};
    }
    return output;
}
