#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

enum class PackageSource { Deb, Snap, Flatpak };

struct AppEntry {
    QString id;
    QString name;
    QString version;
    QString description;
    QString iconName;
    PackageSource source = PackageSource::Deb;
    quint64 installedBytes = 0;
    bool desktopApp = false;
    bool removable = true;
    QString protectionReason;
    // Flatpak installation scope (user/system); empty for other backends.
    QString scope;
    QString targetRef;
};

struct InventoryResult {
    QVector<AppEntry> apps;
    QStringList warnings;
};

struct FileEntry {
    QString path;
    quint64 bytes = 0;
    quint64 allocatedBytes = 0;
    QDateTime modified;
};

struct ScanResult {
    QVector<FileEntry> files;
    quint64 scannedFiles = 0;
    quint64 skippedEntries = 0;
    quint64 matchingFiles = 0;
    quint64 totalMatchingBytes = 0;
    bool canceled = false;
    QString error;
};

inline QString sourceName(PackageSource source)
{
    switch (source) {
    case PackageSource::Deb: return QStringLiteral("DEB");
    case PackageSource::Snap: return QStringLiteral("Snap");
    case PackageSource::Flatpak: return QStringLiteral("Flatpak");
    }
    return {};
}
