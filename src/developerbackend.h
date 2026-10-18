#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

enum class DeveloperCategory { RosBuild, RosInstall, RosLog, Rosbag, Data, PipCache, NpmCache, Ccache, Conda };

struct DeveloperEntry {
    DeveloperCategory category = DeveloperCategory::Data;
    QString name;
    QString path;
    QString evidence;
    QString purpose;
    QString rebuildability;
    QString suggestion;
    quint64 bytes = 0;
    quint64 allocatedBytes = 0;
    quint64 fileCount = 0;
    // Descendant groups are counted separately and excluded from these totals.
    bool excludesNestedGroups = false;
};

struct DeveloperScanOptions {
    QString root;
    // Empty values use the current home and absolute XDG_CACHE_HOME, respectively.
    QString homePath;
    QString cachePath;
    quint64 largeFileBytes = 256ULL * 1024 * 1024;
    quint64 maxEntries = 500000;
    int maxDirectories = 50000;
    int maxDepth = 32;
    int maxResults = 2000;
};

struct DeveloperScanResult {
    QString root;
    QVector<DeveloperEntry> entries;
    quint64 scannedFiles = 0;
    quint64 scannedDirectories = 0;
    quint64 skippedEntries = 0;
    quint64 skippedLinks = 0;
    quint64 totalAllocatedBytes = 0;
    quint64 identifiedAllocatedBytes = 0;
    quint64 hiddenAllocatedBytes = 0;
    int hiddenEntries = 0;
    bool canceled = false;
    bool limited = false;
    QString error;
    QStringList warnings;
};

using DeveloperProgress = std::function<void(quint64, const QString &)>;

// Read-only, synchronous descriptor-based traversal; call from a worker thread.
// Regular file blocks only, once per hard-linked inode. Never follows symlinks
// or enters another mount, including same-device bind mounts.
DeveloperScanResult scanDeveloperUsage(const DeveloperScanOptions &options,
                                      std::atomic_bool *cancel = nullptr,
                                      DeveloperProgress progress = {});
QString developerCategoryName(DeveloperCategory category);
// Atomically writes the displayed snapshot. Strings are quoted and spreadsheet
// formula prefixes are escaped. No scanned files are changed.
bool exportDeveloperCsv(const DeveloperScanResult &result, const QString &path, QString *error = nullptr);
