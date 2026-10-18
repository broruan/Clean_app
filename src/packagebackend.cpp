#include "packagebackend.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

struct CommandResult {
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    QByteArray output;
    QByteArray error;
};

CommandResult command(const QString &program, const QStringList &arguments,
                      int timeoutMs = 15000)
{
    CommandResult result;
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    environment.insert(QStringLiteral("LANGUAGE"), QStringLiteral("C"));
    environment.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    process.setProcessEnvironment(environment);
    process.start(program, arguments, QIODevice::ReadOnly);
    result.started = process.waitForStarted(3000);
    if (!result.started) {
        result.error = process.errorString().toUtf8();
        return result;
    }
    if (!process.waitForFinished(timeoutMs)) {
        result.timedOut = true;
        process.kill();
        process.waitForFinished(1000);
    }
    if (process.exitStatus() == QProcess::NormalExit && !result.timedOut)
        result.exitCode = process.exitCode();
    result.output = process.readAllStandardOutput();
    result.error = process.readAllStandardError();
    return result;
}

QString commandFailure(const CommandResult &result)
{
    if (result.timedOut)
        return QStringLiteral("读取超时");
    QString error = QString::fromUtf8(result.error).simplified();
    if (error.size() > 280)
        error = error.left(280) + QChar(0x2026);
    return error.isEmpty() ? QStringLiteral("退出码 %1").arg(result.exitCode) : error;
}

QString singleLine(QString value)
{
    // Metadata is displayed as plain text. Remove control characters that can
    // otherwise create fake rows or visually overwrite a package name.
    for (int index = 0; index < value.size(); ++index) {
        if (value.at(index).category() == QChar::Other_Control)
            value[index] = QLatin1Char(' ');
    }
    return value.simplified();
}

QString desktopUnescape(const QString &value)
{
    QString result;
    result.reserve(value.size());
    for (int index = 0; index < value.size(); ++index) {
        const QChar character = value.at(index);
        if (character != QLatin1Char('\\') || index + 1 == value.size()) {
            result += character;
            continue;
        }
        const QChar escaped = value.at(++index);
        switch (escaped.unicode()) {
        case 's': result += QLatin1Char(' '); break;
        case 'n': result += QLatin1Char('\n'); break;
        case 't': result += QLatin1Char('\t'); break;
        case 'r': result += QLatin1Char('\r'); break;
        case '\\': result += QLatin1Char('\\'); break;
        default:
            // Unknown escapes are retained literally rather than interpreted
            // as a shell sequence, HTML, or a second level of escaping.
            result += QLatin1Char('\\');
            result += escaped;
            break;
        }
    }
    return singleLine(result);
}

QStringList desktopLocales()
{
    QStringList raw = QLocale::system().uiLanguages();
    raw << QLocale::system().name();
    // LANG remains useful when a desktop launches us with LC_ALL=C.UTF-8 but
    // still requests a localized UI, as several Ubuntu development terminals do.
    raw << qEnvironmentVariable("LANGUAGE").split(QLatin1Char(':'), Qt::SkipEmptyParts)
        << qEnvironmentVariable("LC_MESSAGES") << qEnvironmentVariable("LANG");
    QStringList locales;
    for (QString locale : raw) {
        locale.replace(QLatin1Char('-'), QLatin1Char('_'));
        locale = locale.section(QLatin1Char('.'), 0, 0);
        if (locale.isEmpty() || locale == QStringLiteral("C") || locale == QStringLiteral("POSIX"))
            continue;
        const QString modifier = locale.contains(QLatin1Char('@'))
            ? locale.section(QLatin1Char('@'), 1) : QString();
        const QString base = locale.section(QLatin1Char('@'), 0, 0);
        const QString language = base.section(QLatin1Char('_'), 0, 0);
        locales << locale << base;
        if (!modifier.isEmpty())
            locales << language + QLatin1Char('@') + modifier;
        locales << language;
    }
    locales.removeDuplicates();
    return locales;
}

struct DesktopEntry {
    QString path;
    QString name;
    QString comment;
    QString icon;
    QString snapName;
    QString flatpakId;
};

QString localized(const QHash<QString, QString> &fields, const QString &key,
                  const QStringList &locales)
{
    for (const QString &locale : locales) {
        const QString candidate = fields.value(key + QLatin1Char('[') + locale + QLatin1Char(']'));
        if (!candidate.isEmpty())
            return desktopUnescape(candidate);
    }
    return desktopUnescape(fields.value(key));
}

QVector<DesktopEntry> desktopEntries(const QString &directory,
                                    const QStringList &locales)
{
    QVector<DesktopEntry> entries;
    QDirIterator iterator(directory, {QStringLiteral("*.desktop")},
                          QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024)
            continue;
        const QString text = QString::fromUtf8(file.readAll());
        QHash<QString, QString> fields;
        bool inDesktopEntry = false;
        for (QString line : text.split(QLatin1Char('\n'))) {
            if (line.startsWith(QChar(0xfeff)))
                line.remove(0, 1);
            line = line.trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            if (line.startsWith(QLatin1Char('['))) {
                inDesktopEntry = line == QStringLiteral("[Desktop Entry]");
                continue;
            }
            if (!inDesktopEntry)
                continue;
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals > 0)
                fields.insert(line.left(equals).trimmed(), line.mid(equals + 1));
        }
        if (fields.value(QStringLiteral("Type")) != QStringLiteral("Application")
            || fields.value(QStringLiteral("Hidden")).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
            || fields.value(QStringLiteral("NoDisplay")).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0)
            continue;
        DesktopEntry entry;
        entry.path = path;
        entry.name = localized(fields, QStringLiteral("Name"), locales);
        entry.comment = localized(fields, QStringLiteral("Comment"), locales);
        if (entry.comment.isEmpty())
            entry.comment = localized(fields, QStringLiteral("GenericName"), locales);
        entry.icon = desktopUnescape(fields.value(QStringLiteral("Icon")));
        entry.snapName = fields.value(QStringLiteral("X-SnapInstanceName"));
        entry.flatpakId = fields.value(QStringLiteral("X-Flatpak"));
        if (!entry.name.isEmpty())
            entries.append(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const DesktopEntry &left, const DesktopEntry &right) {
        return left.path < right.path;
    });
    return entries;
}

int desktopScore(const DesktopEntry &entry, const QString &id)
{
    const QString file = QFileInfo(entry.path).completeBaseName();
    const QString unqualified = id.section(QLatin1Char(':'), 0, 0);
    if (file == id || file == unqualified)
        return 100;
    if (file.startsWith(id + QLatin1Char('_')) || file.startsWith(unqualified + QLatin1Char('-')))
        return 50;
    return 1;
}

void applyDesktop(AppEntry &app, const DesktopEntry &desktop)
{
    app.desktopApp = true;
    app.name = desktop.name;
    if (!desktop.comment.isEmpty())
        app.description = desktop.comment;
    if (!desktop.icon.isEmpty())
        app.iconName = desktop.icon;
}

bool protectedDeb(const QString &id, const QString &essential, const QString &protectedFlag,
                  const QString &priority, QString *reason)
{
    if (essential == QStringLiteral("yes") || protectedFlag == QStringLiteral("yes")) {
        *reason = QStringLiteral("包管理器标记为 Essential / Protected 的系统组件");
        return true;
    }
    if (priority == QStringLiteral("required") || priority == QStringLiteral("important")) {
        *reason = QStringLiteral("优先级为 required / important 的系统组件");
        return true;
    }
    const QString package = id.section(QLatin1Char(':'), 0, 0);
    static const QRegularExpression systemPackages(QStringLiteral(
        "^(?:apt(?:-.*)?|dpkg(?:-.*)?|sudo|login|passwd|systemd(?:-.*)?|udev|"
        "dbus(?:-.*)?|polkitd|policykit-1(?:-.*)?|network-manager(?:-.*)?|netplan.io|"
        "gdm3|lightdm(?:-.*)?|sddm(?:-.*)?|xorg|xserver-xorg(?:-.*)?|xwayland|"
        "gnome-shell(?:-.*)?|gnome-session(?:-.*)?|gnome-settings-daemon(?:-.*)?|"
        "gnome-control-center(?:-.*)?|mutter(?:-.*)?|ubuntu-desktop(?:-.*)?|"
        "ubuntu-session|plasma-desktop|plasma-workspace(?:-.*)?|"
        "xfce4-session|mate-session-manager|cinnamon(?:-.*)?|budgie-desktop(?:-.*)?|"
        "snapd|flatpak|packagekit(?:-.*)?|linux-image(?:-.*)?|linux-modules(?:-.*)?|"
        "linux-generic(?:-.*)?|linux-base|init|init-system-helpers|grub(?:-.*)?|"
        "shim(?:-.*)?|cryptsetup(?:-.*)?)$"));
    if (systemPackages.match(package).hasMatch()) {
        *reason = QStringLiteral("桌面、启动、网络或软件包管理所需的系统组件");
        return true;
    }
    return false;
}

void readDebPackages(InventoryResult &result, const QStringList &locales)
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("dpkg-query"));
    if (executable.isEmpty()) {
        result.warnings << QStringLiteral("未找到 dpkg-query，无法读取 DEB 软件。");
        return;
    }
    const QString format = QStringLiteral("${binary:Package}\t${Status}\t${Installed-Size}\t${Version}\t${Essential}\t${Protected}\t${Priority}\t${binary:Summary}\n");
    const CommandResult packages = command(executable, {QStringLiteral("-W"), QStringLiteral("-f=") + format});
    if (packages.exitCode != 0) {
        result.warnings << QStringLiteral("DEB 软件读取失败：%1").arg(commandFailure(packages));
        return;
    }
    QHash<QString, int> indices;
    QStringList invalidSizePackages;
    for (const QByteArray &line : packages.output.split('\n')) {
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 8 || fields.at(1) != "install ok installed")
            continue;
        AppEntry app;
        app.id = QString::fromUtf8(fields.at(0));
        app.targetRef = app.id;
        app.name = app.id;
        app.version = singleLine(QString::fromUtf8(fields.at(3)));
        app.description = singleLine(QString::fromUtf8(fields.at(7)));
        bool sizeOk = false;
        const quint64 kibibytes = fields.at(2).toULongLong(&sizeOk);
        if (sizeOk && kibibytes <= std::numeric_limits<quint64>::max() / 1024)
            app.installedBytes = kibibytes * 1024;
        else
            invalidSizePackages.append(app.id);
        app.removable = !protectedDeb(app.id, QString::fromUtf8(fields.at(4)),
                                     QString::fromUtf8(fields.at(5)), QString::fromUtf8(fields.at(6)),
                                     &app.protectionReason);
        indices.insert(app.id, result.apps.size());
        result.apps.append(app);
    }
    if (!invalidSizePackages.isEmpty()) {
        result.warnings << QStringLiteral("%1 个 DEB 包未提供有效 Installed-Size，大小记为未知（%2%3）。")
                               .arg(invalidSizePackages.size())
                               .arg(invalidSizePackages.mid(0, 5).join(QStringLiteral("、")))
                               .arg(invalidSizePackages.size() > 5 ? QStringLiteral(" 等") : QString());
    }
    QVector<DesktopEntry> desktops = desktopEntries(QStringLiteral("/usr/share/applications"), locales);
    desktops += desktopEntries(QStringLiteral("/usr/local/share/applications"), locales);
    QHash<QString, DesktopEntry> entriesByPath;
    QStringList paths;
    for (const DesktopEntry &entry : desktops) {
        entriesByPath.insert(entry.path, entry);
        paths.append(entry.path);
    }
    QHash<int, int> chosenScores;
    // Keep argument lists bounded; each path remains a separate QProcess
    // argument, so spaces and shell metacharacters are never executed.
    bool queryFailed = false;
    for (int offset = 0; offset < paths.size(); offset += 100) {
        QStringList arguments{QStringLiteral("-S"), QStringLiteral("--")};
        arguments += paths.mid(offset, 100);
        const CommandResult owners = command(executable, arguments);
        if (!owners.started || owners.timedOut || owners.exitCode > 1 || owners.exitCode < 0) {
            queryFailed = true;
            continue;
        }
        // Exit 1 is normal when a hand-installed .desktop file has no DEB owner.
        for (const QByteArray &lineBytes : owners.output.split('\n')) {
            const QString line = QString::fromUtf8(lineBytes);
            const int delimiter = line.indexOf(QStringLiteral(": "));
            if (delimiter < 1)
                continue;
            const QString path = line.mid(delimiter + 2);
            const auto desktop = entriesByPath.constFind(path);
            if (desktop == entriesByPath.constEnd())
                continue;
            const QStringList ownersList = line.left(delimiter).split(QStringLiteral(", "), Qt::SkipEmptyParts);
            for (const QString &owner : ownersList) {
                int index = indices.value(owner, -1);
                if (index < 0) {
                    // dpkg-query -S may qualify an otherwise unqualified native
                    // package with its architecture.
                    index = indices.value(owner.section(QLatin1Char(':'), 0, 0), -1);
                }
                if (index < 0)
                    continue;
                const int score = desktopScore(*desktop, result.apps.at(index).id);
                if (score > chosenScores.value(index, 0)) {
                    applyDesktop(result.apps[index], *desktop);
                    chosenScores.insert(index, score);
                }
            }
        }
    }
    if (queryFailed)
        result.warnings << QStringLiteral("部分桌面入口所属 DEB 包读取失败，相关软件仍可在“全部包”中找到。");
}

QString yamlScalar(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))) {
        value = value.mid(1, value.size() - 2);
        value.replace(QStringLiteral("''"), QStringLiteral("'"));
    } else if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) {
        const QJsonDocument json = QJsonDocument::fromJson((QLatin1Char('[') + value + QLatin1Char(']')).toUtf8());
        if (json.isArray() && !json.array().isEmpty())
            value = json.array().at(0).toString();
    }
    if (value == QStringLiteral("|") || value == QStringLiteral(">"))
        return {};
    return singleLine(value);
}

void snapMetadata(AppEntry &app, const QString &mountPath)
{
    QFile metadata(mountPath + QStringLiteral("/meta/snap.yaml"));
    if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 1024 * 1024)
        return;
    for (const QByteArray &raw : metadata.readAll().split('\n')) {
        if (raw.startsWith(' ') || raw.startsWith('\t') || raw.startsWith('#'))
            continue;
        const int separator = raw.indexOf(':');
        if (separator < 1)
            continue;
        const QByteArray key = raw.left(separator);
        const QString value = yamlScalar(QString::fromUtf8(raw.mid(separator + 1)));
        if (key == "version")
            app.version = value;
        else if (key == "summary" && !value.isEmpty())
            app.description = value;
        else if (key == "type" && (value == QStringLiteral("base") || value == QStringLiteral("os")
                                  || value == QStringLiteral("snapd") || value == QStringLiteral("kernel")
                                  || value == QStringLiteral("gadget"))) {
            app.removable = false;
            app.protectionReason = QStringLiteral("其他 Snap 软件依赖的系统运行环境");
        }
    }
}

void finalizeSnap(AppEntry &app, const QString &revision, const QStringList &locales,
                  const QVector<DesktopEntry> &desktops)
{
    app.source = PackageSource::Snap;
    app.targetRef = app.id;
    const QString mountPath = QStringLiteral("/snap/") + app.id + QLatin1Char('/') + revision;
    snapMetadata(app, mountPath);
    const QFileInfo snapFile(QStringLiteral("/var/lib/snapd/snaps/") + app.id + QLatin1Char('_')
                             + revision + QStringLiteral(".snap"));
    if (snapFile.isFile() && snapFile.size() >= 0)
        app.installedBytes = static_cast<quint64>(snapFile.size());
    int chosen = 0;
    for (const DesktopEntry &desktop : desktops) {
        const QString instance = desktop.snapName.isEmpty()
            ? QFileInfo(desktop.path).fileName().section(QLatin1Char('_'), 0, 0) : desktop.snapName;
        if (instance != app.id)
            continue;
        const int score = desktopScore(desktop, app.id);
        if (score > chosen) {
            applyDesktop(app, desktop);
            chosen = score;
        }
    }
    if (!app.desktopApp) {
        const QVector<DesktopEntry> mountedDesktops = desktopEntries(mountPath + QStringLiteral("/meta/gui"), locales);
        if (!mountedDesktops.isEmpty())
            applyDesktop(app, mountedDesktops.first());
    }
    static const QRegularExpression sharedRuntime(QStringLiteral(
        "^(?:snapd|snapd-desktop-integration|bare|core[0-9]*|gnome-[0-9].*|gtk-common-themes|mesa-[0-9].*)$"));
    if (sharedRuntime.match(app.id).hasMatch()) {
        app.removable = false;
        app.protectionReason = QStringLiteral("Snap 服务或其他软件共享的桌面运行环境");
    }
}

void readSnapPackages(InventoryResult &result, const QStringList &locales)
{
    // Mounted current revisions are stronger evidence than cached .snap files
    // or sequence records, both of which may survive an uninstall.
    const QVector<DesktopEntry> desktops = desktopEntries(QStringLiteral("/var/lib/snapd/desktop/applications"), locales);
    QSet<QString> discovered;
    const QDir mountRoot(QStringLiteral("/snap"));
    for (const QFileInfo &directory : mountRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString id = directory.fileName();
        const QFileInfo current(directory.filePath() + QStringLiteral("/current"));
        if (!current.isSymLink() || !current.exists())
            continue;
        const QString revision = QFileInfo(current.symLinkTarget()).fileName();
        if (!QRegularExpression(QStringLiteral("^(?:[0-9]+|x[0-9]+)$")).match(revision).hasMatch())
            continue;
        AppEntry app;
        app.id = id;
        app.name = id;
        finalizeSnap(app, revision, locales, desktops);
        discovered.insert(id);
        result.apps.append(app);
    }
    if (!discovered.isEmpty())
        return;
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("snap"));
    if (executable.isEmpty())
        return;
    const CommandResult packages = command(executable, {QStringLiteral("list"), QStringLiteral("--unicode=never")}, 6000);
    if (packages.exitCode != 0) {
        result.warnings << QStringLiteral("Snap 软件读取失败：%1").arg(commandFailure(packages));
        return;
    }
    const QList<QByteArray> lines = packages.output.split('\n');
    for (int index = 1; index < lines.size(); ++index) {
        const QStringList fields = QString::fromUtf8(lines.at(index)).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (fields.size() < 3)
            continue;
        AppEntry app;
        app.id = fields.at(0);
        app.name = app.id;
        app.version = singleLine(fields.at(1));
        finalizeSnap(app, fields.at(2), locales, desktops);
        result.apps.append(app);
    }
}

quint64 flatpakSize(QString text)
{
    text = text.trimmed();
    static const QRegularExpression sizePattern(QStringLiteral("^([0-9]+(?:\\.[0-9]+)?)\\s*(bytes?|[KMGTPE]i?B|B)$"),
                                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = sizePattern.match(text);
    if (!match.hasMatch())
        return 0;
    bool ok = false;
    const double amount = match.captured(1).toDouble(&ok);
    const QString unit = match.captured(2).toUpper();
    const int power = QStringLiteral("BKMGTPE").indexOf(unit.at(0));
    const double multiplier = power <= 0 ? 1.0 : std::pow(unit.contains(QLatin1Char('I')) ? 1024.0 : 1000.0, power);
    const long double bytes = static_cast<long double>(amount) * multiplier;
    if (!ok || !std::isfinite(amount) || bytes < 0 || bytes >= std::numeric_limits<quint64>::max())
        return 0;
    return static_cast<quint64>(bytes + 0.5L);
}

void readFlatpakPackages(InventoryResult &result, const QStringList &locales)
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("flatpak"));
    if (executable.isEmpty())
        return;
    const QString columns = QStringLiteral("--columns=application:f,name:f,version:f,size:f,installation:f,ref:f,description:f");
    const CommandResult packages = command(executable, {QStringLiteral("list"), QStringLiteral("--app"), columns});
    if (packages.exitCode != 0) {
        result.warnings << QStringLiteral("Flatpak 软件读取失败：%1").arg(commandFailure(packages));
        return;
    }
    QHash<QString, QVector<DesktopEntry>> desktopCache;
    int invalidRows = 0;
    bool hasApps = false;
    for (const QByteArray &line : packages.output.split('\n')) {
        if (line.trimmed().isEmpty())
            continue;
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 7) {
            ++invalidRows;
            continue;
        }
        AppEntry app;
        app.source = PackageSource::Flatpak;
        app.id = QString::fromUtf8(fields.at(0)).trimmed();
        app.name = singleLine(QString::fromUtf8(fields.at(1)));
        app.version = singleLine(QString::fromUtf8(fields.at(2)));
        app.installedBytes = flatpakSize(QString::fromUtf8(fields.at(3)));
        app.scope = QString::fromUtf8(fields.at(4)).trimmed();
        app.targetRef = QString::fromUtf8(fields.at(5)).trimmed();
        app.description = singleLine(QString::fromUtf8(fields.at(6)));
        // Validate identifiers before they become uninstall arguments. Display
        // fields are deliberately never used as an operation target.
        static const QRegularExpression appId(QStringLiteral("^[A-Za-z0-9_-]+(?:\\.[A-Za-z0-9_-]+){2,}$"));
        // Flatpak versions differ in whether list's ref column includes the
        // leading app/ component. Normalize both spellings for uninstall.
        if (app.targetRef.startsWith(app.id + QLatin1Char('/')))
            app.targetRef.prepend(QStringLiteral("app/"));
        const QRegularExpression refPattern(QStringLiteral("^app/") + QRegularExpression::escape(app.id)
                                           + QStringLiteral("/[A-Za-z0-9_-]+/[A-Za-z0-9_.-]+$"));
        if (!appId.match(app.id).hasMatch() || app.scope.isEmpty()
            || !refPattern.match(app.targetRef).hasMatch()) {
            ++invalidRows;
            continue;
        }
        if (app.name.isEmpty())
            app.name = app.id;
        app.iconName = app.id;
        app.desktopApp = true;
        if (app.scope != QStringLiteral("user") && app.scope != QStringLiteral("system")) {
            app.removable = false;
            app.protectionReason = QStringLiteral("此 Flatpak 位于命名安装位置，请使用对应的系统管理工具卸载。");
        }
        const QString directory = app.scope == QStringLiteral("user")
            ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                  + QStringLiteral("/flatpak/exports/share/applications")
            : QStringLiteral("/var/lib/flatpak/exports/share/applications");
        if (!desktopCache.contains(directory))
            desktopCache.insert(directory, desktopEntries(directory, locales));
        for (const DesktopEntry &desktop : desktopCache.value(directory)) {
            if (desktop.flatpakId == app.id || QFileInfo(desktop.path).completeBaseName() == app.id) {
                applyDesktop(app, desktop);
                break;
            }
        }
        result.apps.append(app);
        hasApps = true;
    }
    if (invalidRows)
        result.warnings << QStringLiteral("有 %1 条 Flatpak 记录格式不受支持，已跳过。").arg(invalidRows);
    if (hasApps)
        result.warnings << QStringLiteral("Flatpak 大小来自包管理器显示值，可能经过四舍五入；共享运行库与用户数据未计入。");
}

} // namespace

InventoryResult readInstalledSoftware()
{
    InventoryResult result;
    const QStringList locales = desktopLocales();
    readDebPackages(result, locales);
    readSnapPackages(result, locales);
    readFlatpakPackages(result, locales);
    std::sort(result.apps.begin(), result.apps.end(), [](const AppEntry &left, const AppEntry &right) {
        if (left.installedBytes != right.installedBytes)
            return left.installedBytes > right.installedBytes;
        return left.id < right.id;
    });
    return result;
}
