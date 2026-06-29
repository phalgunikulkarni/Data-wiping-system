#pragma once

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDateTime>
#include <QDir>

enum class LogLevel {
    Info,
    Warning,
    Error,
    Success,
    Debug
};

class Logger {
public:
    static Logger& instance();

    void init(const QString& logDir = "logs");
    void log(LogLevel level, const QString& category, const QString& message);

    // Convenience methods
    void info   (const QString& category, const QString& msg) { log(LogLevel::Info,    category, msg); }
    void warning(const QString& category, const QString& msg) { log(LogLevel::Warning, category, msg); }
    void error  (const QString& category, const QString& msg) { log(LogLevel::Error,   category, msg); }
    void success(const QString& category, const QString& msg) { log(LogLevel::Success, category, msg); }
    void debug  (const QString& category, const QString& msg) { log(LogLevel::Debug,   category, msg); }

    // Wipe-specific helpers
    void logWipeStart (const QString& path, const QString& method, int passes);
    void logWipeEnd   (const QString& path, bool success, qint64 bytesWiped);
    void logFileWipe  (const QString& filePath, bool success);
    void logDeviceScan(const QString& devicePath, qint64 totalSize);
    void logSummary   (int total, int succeeded, int failed);

    QString currentLogFile() const { return m_logFilePath; }
    void setLogToConsole(bool enable) { m_logToConsole = enable; }

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    QString levelToString(LogLevel level) const;
    QString levelToTag   (LogLevel level) const;

    QFile        m_file;
    QTextStream  m_stream;
    QMutex       m_mutex;
    QString      m_logFilePath;
    bool         m_initialized   = false;
    bool         m_logToConsole  = true;
};

// Global convenience macros
#define LOG_INFO(cat, msg)    Logger::instance().info(cat, msg)
#define LOG_WARN(cat, msg)    Logger::instance().warning(cat, msg)
#define LOG_ERROR(cat, msg)   Logger::instance().error(cat, msg)
#define LOG_SUCCESS(cat, msg) Logger::instance().success(cat, msg)
#define LOG_DEBUG(cat, msg)   Logger::instance().debug(cat, msg)