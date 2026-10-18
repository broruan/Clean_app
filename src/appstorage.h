#pragma once

#include "types.h"
#include <atomic>
#include <functional>

struct AppStorageLocation {
    QString category;
    QString path;
    QString attribution;
    quint64 logicalBytes = 0;
    quint64 allocatedBytes = 0;
    quint64 fileCount = 0;
    quint64 skippedEntries = 0;
    bool measured = false;
    bool partial = false;
    QString error;
};

struct AppStorageResult {
    QVector<AppStorageLocation> locations;
    bool canceled = false;
};

// Explicit application namespaces and a small package-name catalog only.
// A location is an association, not permission to clean it. No fuzzy matching,
// shell commands, package-size summation, or automatic deletion is performed.
QVector<AppStorageLocation> discoverAppStorage(const AppEntry &app,
    const QString &home, const QString &cacheRoot, const QString &configRoot);

// Synchronous metadata-only measurement, intended for a worker thread.
// Metrics are per location: different paths can share filesystem blocks and
// must not be advertised as a total or as reclaimable disk space.
AppStorageResult measureAppStorage(QVector<AppStorageLocation> locations,
    std::atomic_bool *cancel = nullptr,
    std::function<void(quint64, const QString &)> progress = {});
