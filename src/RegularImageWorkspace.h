#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <memory>

// File-only sandbox for future disposable disk-image workflows.
// This API never accepts drive letters as targets and is not an IStorageDevice.
class RegularImageWorkspace final
{
public:
    struct Impl; // Opaque implementation shared with scoped file handles.
    using CancellationCheck = std::function<bool()>;

    class File final
    {
    public:
        ~File();
        File(const File&) = delete;
        File& operator=(const File&) = delete;

        std::uint64_t sizeBytes() const;
        QString canonicalPath() const;
        bool readAt(std::uint64_t offset, std::uint32_t length,
                    QByteArray& output, QString& error) const;
        // Writes are restricted to a newly-created file owned by this object.
        bool writeAt(std::uint64_t offset, const QByteArray& bytes, QString& error);
        bool sha256(QByteArray& digest, QString& error) const;
        bool verifySha256(const QByteArray& expectedHex, QString& error) const;
        // Deletes only this object's newly-created file, after handle/path/id checks.
        bool removeOwnedFile(QString& error);

    private:
        friend class RegularImageWorkspace;
        struct Impl;
        explicit File(std::unique_ptr<Impl> impl);
        std::shared_ptr<Impl> m_impl;
    };

    // workspaceRoot must already exist and be a dedicated non-root directory.
    // maxFileBytes is mandatory and applies to every file opened or created.
    static std::unique_ptr<RegularImageWorkspace> open(
        const QString& workspaceRoot, const QString& workingDirectory,
        const QStringList& protectedDirectories, std::uint64_t maxFileBytes,
        QString& error);

    ~RegularImageWorkspace();
    RegularImageWorkspace(const RegularImageWorkspace&) = delete;
    RegularImageWorkspace& operator=(const RegularImageWorkspace&) = delete;

    // relativePath must be a safe relative path beneath the approved root.
    std::unique_ptr<File> openExisting(const QString& relativePath, QString& error) const;

    // Copies a verified input into a new uniquely named working file in the
    // requested existing subdirectory. The input is never modified.
    std::unique_ptr<File> copyToTemporary(const QString& inputRelativePath,
                                          const QString& outputDirectory,
                                          const QString& stem,
                                          const QByteArray& expectedSha256Hex,
                                          QString& error,
                                          CancellationCheck cancelled = {}) const;

    // Creates an empty, uniquely named file in an existing safe relative directory.
    // Uses CREATE_NEW and never truncates/replaces an existing path.
    std::unique_ptr<File> createTemporary(const QString& relativeDirectory,
                                          const QString& stem,
                                          QString& error) const;

    QString canonicalRoot() const;

private:
    explicit RegularImageWorkspace(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> m_impl;
};
