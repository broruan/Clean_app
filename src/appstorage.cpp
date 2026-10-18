#include "localization.h"
#include "appstorage.h"
#include "spaceanalyzer.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

QVector<AppStorageLocation> discoverAppStorage(const AppEntry &app,
    const QString &home, const QString &cacheRoot, const QString &configRoot)
{
    QVector<AppStorageLocation> locations;
    const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9+_.-]*$"));
    const QString id = app.source == PackageSource::Deb ? app.id.section(':', 0, 0) : app.id;
    if (!identifier.match(id).hasMatch() || !QDir::isAbsolutePath(home)
        || !QDir::isAbsolutePath(cacheRoot) || !QDir::isAbsolutePath(configRoot)
        || home.contains(QChar::Null) || cacheRoot.contains(QChar::Null)
        || configRoot.contains(QChar::Null))
        return locations;

    QSet<QString> seen;
    auto add = [&](const QString &category, const QString &path, const QString &attribution) {
        const QString clean = QDir::cleanPath(path);
        const QFileInfo info(clean);
        if (seen.contains(clean) || !info.isDir() || info.isSymLink())
            return;
        seen.insert(clean);
        AppStorageLocation location;
        location.category = category;
        location.path = clean;
        location.attribution = attribution;
        locations.append(location);
    };
    if (app.source == PackageSource::Flatpak) {
        if (id.count('.') >= 2)
            add(dlTr("应用容器数据"), QDir(home).filePath(".var/app/" + id),
                dlTr("按 Flatpak 应用 ID 关联；包含配置、缓存和用户数据，不能整体视为缓存。"));
        return locations;
    }
    if (app.source == PackageSource::Snap) {
        add(dlTr("Snap 用户数据"), QDir(home).filePath("snap/" + id),
            dlTr("按 Snap 名称关联版本目录与 common；忽略 current 软链接，可能包含需要保留的用户数据。"));
        return locations;
    }

    QString storageName;
    if (id == "google-chrome-stable") storageName = "google-chrome";
    else if (id == "google-chrome-beta") storageName = "google-chrome-beta";
    else if (id == "google-chrome-unstable") storageName = "google-chrome-unstable";
    else if (id == "chromium" || id == "chromium-browser") storageName = "chromium";
    else if (id == "code" || id == "code-insiders") storageName = id == "code" ? "Code" : "Code - Insiders";
    else if (id == "vlc") storageName = "vlc";
    else if (id == "gimp") storageName = "GIMP";
    else if (id == "thunderbird") storageName = "thunderbird";

    const QString catalog = dlTr("按内置软件包与目录名称规则关联，请核对路径；不保证覆盖此应用的全部数据。");
    if (!storageName.isEmpty()) {
        add(dlTr("用户缓存"), QDir(cacheRoot).filePath(storageName), catalog);
        add(dlTr("配置与用户数据"), QDir(configRoot).filePath(storageName), catalog);
    }
    if (id == "firefox") {
        add(dlTr("浏览器缓存"), QDir(cacheRoot).filePath("mozilla/firefox"), catalog);
        add(dlTr("浏览器配置与用户数据"), QDir(home).filePath(".mozilla/firefox"), catalog);
    }
    if (id == "thunderbird")
        add(dlTr("邮件与用户数据"), QDir(home).filePath(".thunderbird"), catalog);
    if (id == "code" || id == "code-insiders")
        add(dlTr("编辑器扩展"), QDir(home).filePath(id == "code" ? ".vscode/extensions" : ".vscode-insiders/extensions"), catalog);
    return locations;
}

AppStorageResult measureAppStorage(QVector<AppStorageLocation> locations,
    std::atomic_bool *cancel, std::function<void(quint64, const QString &)> progress)
{
    AppStorageResult result;
    result.locations = std::move(locations);
    for (auto &location : result.locations) {
        if (cancel && cancel->load()) { result.canceled = true; break; }
        const auto scan = analyzeDirectory(location.path, cancel, progress);
        location.logicalBytes = scan.root.logicalBytes;
        location.allocatedBytes = scan.root.allocatedBytes;
        location.fileCount = scan.root.fileCount;
        location.skippedEntries = scan.skippedEntries;
        location.error = scan.error;
        location.measured = scan.error.isEmpty();
        location.partial = scan.canceled || scan.truncated || scan.skippedEntries > 0;
        if (scan.canceled) { result.canceled = true; break; }
    }
    return result;
}
