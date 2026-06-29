#include "Logger.h"
#include <QDebug>
#include <QCoreApplication>

// ── Singleton ────────────────────────────────────────────────────────────────

Logger& Logger::instance() {
    static Logger s_instance;
    return s_instance;
}

Logger::~Logger() {
    if (m_file.isOpen()) {
        m_stream << "\n[" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                 << "] ===== SESSION ENDED =====\n";
        m_file.close();
    }
}

// ── Init ─────────────────────────────────────────────────────────────────────

void Logger::init(const QString& logDir) {
    QMutexLocker lock(&m_mutex);
    if (m_initialized) return;

    // Create logs/ folder next to the executable
    QString appPath = QCoreApplication::applicationDirPath();
    QDir dir(appPath);
    dir.mkpath(logDir);

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    m_logFilePath = dir.filePath(logDir + "/wipe_" + timestamp + ".log");

    m_file.setFileName(m_logFilePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Logger: could not open log file:" << m_logFilePath;
        return;
    }

    m_stream.setDevice(&m_file);
    m_initialized = true;

    m_stream << "===== WipeEngine Session Started: "
             << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
             << " =====\n\n";
    m_stream.flush();
}

// ── Core log ─────────────────────────────────────────────────────────────────

void Logger::log(LogLevel level, const QString& category, const QString& message) {
    QMutexLocker lock(&m_mutex);

    if (!m_initialized) return;          // call init() first

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString tag       = levelToTag(level);
    QString line      = QString("[%1] [%2] [%3] %4\n")
                            .arg(timestamp, tag, category, message);

    m_stream << line;
    m_stream.flush();

    if (m_logToConsole)
        qDebug().noquote() << line.trimmed();
}

// ── Wipe helpers ─────────────────────────────────────────────────────────────

void Logger::logWipeStart(const QString& path, const QString& method, int passes) {
    log(LogLevel::Info, "WIPE",
        QString("START  | Path: %1 | Method: %2 | Passes: %3")
            .arg(path, method).arg(passes));
}

void Logger::logWipeEnd(const QString& path, bool success, qint64 bytesWiped) {
    QString size = QString::number(bytesWiped / 1024.0 / 1024.0, 'f', 2) + " MB";
    if (success)
        log(LogLevel::Success, "WIPE",
            QString("DONE   | Path: %1 | Wiped: %2").arg(path, size));
    else
        log(LogLevel::Error, "WIPE",
            QString("FAILED | Path: %1 | Attempted: %2").arg(path, size));
}

void Logger::logFileWipe(const QString& filePath, bool success) {
    if (success)
        log(LogLevel::Success, "FILE",
            QString("Wiped OK  → %1").arg(filePath));
    else
        log(LogLevel::Error, "FILE",
            QString("Wipe FAIL → %1").arg(filePath));
}

void Logger::logDeviceScan(const QString& devicePath, qint64 totalSize) {
    QString size = QString::number(totalSize / 1024.0 / 1024.0 / 1024.0, 'f', 2) + " GB";
    log(LogLevel::Info, "SCAN",
        QString("Device: %1 | Total size: %2").arg(devicePath, size));
}

void Logger::logSummary(int total, int succeeded, int failed) {
    log(LogLevel::Info, "SUMMARY",
        QString("Total: %1 | Success: %2 | Failed: %3")
            .arg(total).arg(succeeded).arg(failed));

    if (failed == 0)
        log(LogLevel::Success, "SUMMARY", "All operations completed successfully.");
    else
        log(LogLevel::Warning, "SUMMARY",
            QString("%1 operation(s) failed — review log for details.").arg(failed));
}

// ── Helpers ──────────────────────────────────────────────────────────────────

QString Logger::levelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Success: return "SUCCESS";
        case LogLevel::Debug:   return "DEBUG";
    }
    return "UNKNOWN";
}

QString Logger::levelToTag(LogLevel level) const {
    switch (level) {
        case LogLevel::Info:    return "INFO   ";
        case LogLevel::Warning: return "WARN   ";
        case LogLevel::Error:   return "ERROR  ";
        case LogLevel::Success: return "SUCCESS";
        case LogLevel::Debug:   return "DEBUG  ";
    }
    return "UNKNOWN";
}