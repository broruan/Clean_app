#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

struct CacheFileSnapshot {
    // Relative to CacheEntry::rootPath, including the first-level app group.
    QString relativePath;
    quint64 device = 0;
    quint64 inode = 0;
    quint64 logicalBytes = 0;
    quint64 allocatedBytes = 0;
    qint64 modifiedNs = 0;
    qint64 changedNs = 0;
    quint64 ownerUid = 0;
};

struct CacheDirectorySnapshot {
    QString relativePath;
    quint64 device = 0;
    quint64 inode = 0;
};

struct CacheEntry {
    QString id;
    QString name;
    QString path;
    QVector<CacheFileSnapshot> files;
    quint64 bytes = 0;
    quint64 allocatedBytes = 0;
    quint64 fileCount = 0;
    quint64 logCount = 0;
    quint64 logAllocatedBytes = 0;
    quint64 skippedEntries = 0;
    bool logOnly = false;

    // Provenance is checked against independently supplied trusted roots when
    // cleaning. Callers must retain these fields from scanCaches unchanged.
    QString rootPath;
    QString relativePath;
    quint64 rootDevice = 0;
    quint64 rootInode = 0;
    quint64 rootMountId = 0;
    QVector<CacheDirectorySnapshot> directories;
    // Process-local signature: edited or fabricated manifests are rejected.
    QByteArray manifestToken;
};

struct CacheScanResult {
    QVector<CacheEntry> entries;
    quint64 scannedFiles = 0;
    quint64 skippedEntries = 0;
    bool canceled = false;
    QStringList warnings;
    QString error;
};

struct CacheCleanResult {
    quint64 removedFiles = 0;
    // Actual allocated bytes of successfully unlinked, singly linked files.
    quint64 removedBytes = 0;
    quint64 skippedFiles = 0;
    quint64 failedFiles = 0;
    bool canceled = false;
    QStringList errors;
};

using CacheProgress = std::function<void(quint64, const QString &)>;

// Synchronous, read-only scanning. cacheRoot groups all ordinary cache files;
// stateRoot groups only *.log / *.log.* or files beneath log/logs directories.
// stateRoot may be empty. Roots must be supplied by the application (normally
// ~/.cache and ~/.local/state, or explicit QTemporaryDir roots in tests).
CacheScanResult scanCaches(const QString &cacheRoot, const QString &stateRoot,
                           std::atomic_bool *cancel = nullptr,
                           CacheProgress progress = {});

// Deletes only unchanged file snapshots. Trusted roots must be supplied again
// by the application, never copied from an arbitrary entry. Directories, new
// files, symlinks, hardlinks, foreign-owned and changed files are preserved.
CacheCleanResult cleanCaches(const QVector<CacheEntry> &selected,
                             const QString &cacheRoot, const QString &stateRoot,
                             std::atomic_bool *cancel = nullptr,
                             CacheProgress progress = {});
