#include "FileScanner.h"

#include <windows.h>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <deque>
#include <limits>

namespace {
constexpr std::size_t kMaximumEntries = 250000;

std::string utf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString longPath(QString path)
{
    path = QDir::toNativeSeparators(path);
    if (path.startsWith(QStringLiteral("\\\\?\\"))) return path;
    if (path.startsWith(QStringLiteral("\\\\")))
        return QStringLiteral("\\\\?\\UNC\\") + path.mid(2);
    return QStringLiteral("\\\\?\\") + path;
}
}

std::vector<FileEntry> FileScanner::scanExisting(const std::string& driveLetter)
{
    QString drive = QString::fromUtf8(driveLetter.data(), static_cast<qsizetype>(driveLetter.size())).trimmed();
    if (drive.size() == 2 && drive[1] == QLatin1Char(':')) drive.chop(1);
    if (drive.size() != 1 || !drive[0].isLetter() || drive[0].unicode() > 127) {
        m_lastError = "Expected a single Windows drive letter";
        return {};
    }
    return scanExistingRoot(drive.toUpper() + QStringLiteral(":\\")).entries;
}

FileScanResult FileScanner::scanExistingRoot(const QString& rootPath)
{
    FileScanResult result;
    m_lastError.clear();
    const QFileInfo rootInfo(rootPath);
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty() || !rootInfo.isDir()) {
        result.status = FileScanStatus::Error;
        result.errorMessage = "The scan root does not resolve to an existing directory";
        m_lastError = result.errorMessage;
        return result;
    }

    walkDirectory(canonicalRoot, result.entries);
    result.status = m_lastError.empty() ? FileScanStatus::Success : FileScanStatus::Error;
    result.errorMessage = m_lastError;
    return result;
}

void FileScanner::walkDirectory(const QString& rootPath, std::vector<FileEntry>& results)
{
    std::deque<QString> pending;
    pending.push_back(rootPath);
    while (!pending.empty() && results.size() < kMaximumEntries) {
        const QString directory = pending.front();
        pending.pop_front();
        const QString nativeDirectory = longPath(directory);
        const std::wstring pattern = (nativeDirectory + QStringLiteral("\\*")).toStdWString();
        WIN32_FIND_DATAW data{};
        HANDLE search = FindFirstFileW(pattern.c_str(), &data);
        if (search == INVALID_HANDLE_VALUE) {
            if (m_lastError.empty())
                m_lastError = "Windows could not enumerate one or more directories; error=" + std::to_string(GetLastError());
            continue;
        }
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
                continue;
            const QString child = directory + QLatin1Char('\\') + QString::fromWCharArray(data.cFileName);
            const bool isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool isReparsePoint = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            FileEntry entry;
            entry.name = utf8(QString::fromWCharArray(data.cFileName));
            entry.fullPath = utf8(child);
            entry.status = FileStatus::EXISTING;
            entry.isDirectory = isDirectory;
            entry.sizeBytes = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            SYSTEMTIME modified{};
            if (FileTimeToSystemTime(&data.ftLastWriteTime, &modified)) {
                entry.lastModified = std::to_string(modified.wYear) + "-" +
                    (modified.wMonth < 10 ? "0" : "") + std::to_string(modified.wMonth) + "-" +
                    (modified.wDay < 10 ? "0" : "") + std::to_string(modified.wDay) + " " +
                    (modified.wHour < 10 ? "0" : "") + std::to_string(modified.wHour) + ":" +
                    (modified.wMinute < 10 ? "0" : "") + std::to_string(modified.wMinute) + ":" +
                    (modified.wSecond < 10 ? "0" : "") + std::to_string(modified.wSecond);
            } else {
                entry.lastModified = "Unknown";
            }
            results.push_back(std::move(entry));
            // Never traverse junctions or symbolic links; they can escape the selected root
            // or lead back to an ancestor. Listing their name does not dereference them.
            if (isDirectory && !isReparsePoint)
                pending.push_back(child);
            if (results.size() >= kMaximumEntries) {
                m_lastError = "Directory enumeration stopped at the configured entry safety limit";
                break;
            }
        } while (FindNextFileW(search, &data));
        const DWORD enumerationError = GetLastError();
        FindClose(search);
        if (enumerationError != ERROR_NO_MORE_FILES && m_lastError.empty())
            m_lastError = "Windows directory enumeration failed; error=" + std::to_string(enumerationError);
    }
}

FileScanResult FileScanner::scanDeleted(const std::string& volumePath)
{
    (void)volumePath;
    FileScanResult result;
    result.status = FileScanStatus::Unsupported;
    result.errorMessage = "Deleted-file parsing is disabled until FAT/exFAT metadata reads are bounded and tested through a mockable volume reader";
    m_lastError = result.errorMessage;
    return result;
}

FileScanResult FileScanner::scanAll(const std::string& driveLetter, const std::string& volumePath)
{
    FileScanResult result;
    result.entries = scanExisting(driveLetter);
    const FileScanResult deleted = scanDeleted(volumePath);
    result.status = deleted.status;
    result.errorMessage = deleted.errorMessage;
    return result;
}