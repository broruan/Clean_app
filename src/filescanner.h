#pragma once

#include "types.h"

#include <atomic>
#include <functional>

// Runs synchronously; call from a worker thread. Files and directories are only
// inspected, never modified. The callback runs on the calling thread.
ScanResult scanLargeFiles(
    const QString &root,
    quint64 minimumBytes,
    int maxResults,
    std::atomic_bool *cancel,
    std::function<void(quint64, const QString &)> progress = {});
