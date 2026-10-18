#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

// All scans are read-only, stay on the root mount, and reject symbolic links
// even in the selected root's ancestors. Hard links contribute bytes once.
struct SpaceNode {
    QString name;
    QString path;
    bool directory = false;
    quint64 logicalBytes = 0;
    quint64 allocatedBytes = 0;
    quint64 fileCount = 0;
    QVector<SpaceNode> children;
};

struct DirectoryScanResult {
    SpaceNode root;
    quint64 scannedFiles = 0;
    quint64 skippedEntries = 0;
    bool canceled = false;
    bool truncated = false;
    QString error;
};

// maxNodes includes the root and file nodes (accepted range: 1..1,000,000).
// Results are partial when canceled,
// truncated, or skippedEntries is nonzero. Tree depth is bounded to 128 levels.
DirectoryScanResult analyzeDirectory(
    const QString &root, std::atomic_bool *cancel = nullptr,
    std::function<void(quint64, const QString &)> progress = {},
    int maxNodes = 100000);

struct DuplicateGroup {
    quint64 bytes = 0;
    QByteArray sha256;
    QStringList paths;
};

struct DuplicateScanResult {
    QVector<DuplicateGroup> groups;
    quint64 scannedFiles = 0;
    quint64 hashedFiles = 0;
    quint64 skippedEntries = 0;
    quint64 redundantBytes = 0;
    bool canceled = false;
    bool truncated = false;
    QString error;
};

// Size -> head/tail sample -> complete SHA-256. Each content read verifies the
// original inode and metadata before/after; changing files are excluded.
// redundantBytes is logical redundancy, never a promise of reclaimable space.
// maxFiles bounds unique regular files and, separately, traversed directories;
// its accepted range is 1..1,000,000. Empty files are always excluded.
DuplicateScanResult findDuplicates(
    const QString &root, quint64 minimumBytes = 1024 * 1024,
    std::atomic_bool *cancel = nullptr,
    std::function<void(quint64, const QString &)> progress = {},
    int maxFiles = 100000);
