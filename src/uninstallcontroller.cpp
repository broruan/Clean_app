#include "localization.h"
#include "uninstallcontroller.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {
constexpr int ReadTimeoutMs = 45000;
constexpr int RemovalReminderMs = 120000;
constexpr int MaxOutputCharacters = 4 * 1024 * 1024;

bool isDebId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^[a-z0-9][a-z0-9+.-]*(?::[a-z0-9][a-z0-9-]*)?$"));
    return id.size() <= 256 && pattern.match(id).hasMatch();
}

QString debBaseName(const QString &id)
{
    return id.section(QLatin1Char(':'), 0, 0);
}

bool validFlatpakRef(const QString &ref, const QString &id)
{
    const QStringList parts = ref.split(QLatin1Char('/'));
    static const QRegularExpression component(QStringLiteral("^[A-Za-z0-9._-]+$"));
    return parts.size() == 4 && parts[0] == QStringLiteral("app")
        && parts[1] == id && component.match(parts[2]).hasMatch()
        && component.match(parts[3]).hasMatch();
}

QString errorDetail(const QString &output)
{
    QString detail = output.trimmed();
    if (detail.size() > 1800)
        detail = detail.right(1800);
    return detail;
}

QStringList aptSafetyArguments()
{
    // Override permissive global/local configuration as well as choosing the
    // conservative command. Apply the same settings to simulation and removal.
    return {QStringLiteral("-o"), QStringLiteral("APT::Get::AutomaticRemove=false"),
            QStringLiteral("-o"), QStringLiteral("APT::Get::Purge=false"),
            QStringLiteral("-o"), QStringLiteral("APT::Get::force-yes=false"),
            QStringLiteral("-o"), QStringLiteral("APT::Get::allow-remove-essential=false"),
            QStringLiteral("-o"), QStringLiteral("APT::Get::allow-change-held-packages=false")};
}

bool verifySnapMetadata(const QString &id, const QString &revision, const QStringList &notes,
                        QString *metadataState, QString *reason)
{
    static const QRegularExpression validRevision(QStringLiteral("^(?:[0-9]+|x[0-9]+)$"));
    if (!validRevision.match(revision).hasMatch()) {
        *reason = dlTr("无法验证 Snap 的已安装修订版本。");
        return false;
    }
    const QSet<QString> protectedNotes{QStringLiteral("required"), QStringLiteral("base"),
        QStringLiteral("core"), QStringLiteral("snapd"), QStringLiteral("kernel"), QStringLiteral("gadget")};
    for (const QString &note : notes) {
        if (protectedNotes.contains(note)) {
            *reason = dlTr("Snap 将此软件标记为必需组件或系统运行环境，不能卸载。");
            return false;
        }
    }
    QFile file(QStringLiteral("/snap/") + id + QLatin1Char('/') + revision
               + QStringLiteral("/meta/snap.yaml"));
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 1024 * 1024) {
        *reason = dlTr("无法读取 Snap 当前修订版本的本地元数据，不能可靠验证系统组件保护属性。");
        return false;
    }
    const QByteArray metadata = file.readAll();
    // snap.yaml defaults to type: app. Only accept the simple top-level scalar
    // used by snapd; unknown/complex type values fail closed.
    QString type = QStringLiteral("app");
    bool typeFound = false;
    for (QString line : QString::fromUtf8(metadata).split(QLatin1Char('\n'))) {
        if (line.startsWith(QChar(0xfeff)))
            line.remove(0, 1);
        if (!line.startsWith(QStringLiteral("type:")))
            continue;
        if (typeFound) {
            *reason = dlTr("Snap 元数据包含重复的组件类型，已停止操作。");
            return false;
        }
        typeFound = true;
        type = line.mid(5).trimmed();
        type.remove(QRegularExpression(QStringLiteral("\\s+#.*$")));
        if ((type.startsWith(QLatin1Char('\'')) && type.endsWith(QLatin1Char('\'')))
            || (type.startsWith(QLatin1Char('"')) && type.endsWith(QLatin1Char('"'))))
            type = type.mid(1, type.size() - 2);
    }
    if (type != QStringLiteral("app")) {
        *reason = dlTr("此 Snap 的类型为 %1，属于系统组件或无法验证的组件，不能卸载。").arg(type);
        return false;
    }
    *metadataState = QString::fromLatin1(QCryptographicHash::hash(metadata, QCryptographicHash::Sha256).toHex());
    return true;
}
}

UninstallController::UninstallController(QObject *parent)
    : QObject(parent), m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    environment.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    m_process->setProcessEnvironment(environment);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &UninstallController::consumeOutput);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &UninstallController::processFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(dlTr("无法启动 %1：%2").arg(m_process->program(), m_process->errorString()));
    });
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &UninstallController::timeout);
}

UninstallController::~UninstallController()
{
    if (m_executing && m_process->state() != QProcess::NotRunning) {
        // Defensive fallback if an owner violates the close guard. Never let the
        // QProcess destructor kill a package manager that has started changing state.
        // The object is deliberately detached; application exit will close its pipes.
        m_process->disconnect(this);
        m_process->setParent(nullptr);
    } else if (m_process->state() != QProcess::NotRunning) {
        m_process->kill(); // Only a read-only query or simulation may be stopped.
        m_process->waitForFinished(1000);
    }
}

QString UninstallController::metadataFormat()
{
    return QStringLiteral("${binary:Package}\t${db:Status-Status}\t${Essential}\t"
                          "${Protected}\t${Priority}\t${Architecture}\t${Version}\n");
}

QString UninstallController::criticalDebReason(const QString &name)
{
    static const QRegularExpression critical(QStringLiteral(
        "^(?:apt(?:-.*)?|dpkg(?:-.*)?|systemd(?:-.*)?|libsystemd.*|"
        "linux(?:-.*)?|grub(?:-.*)?|shim(?:-.*)?|init(?:-.*)?|"
        "ubuntu-(?:minimal|standard|desktop)(?:-.*)?|kubuntu-desktop|"
        "xubuntu-desktop|lubuntu-desktop|debian-archive-keyring|ubuntu-keyring|"
        "gnome-shell(?:-.*)?|gnome-session(?:-.*)?|gnome-settings-daemon(?:-.*)?|"
        "gnome-control-center(?:-.*)?|mutter(?:-.*)?|ubuntu-session|"
        "gdm3?|sddm(?:-.*)?|lightdm(?:-.*)?|"
        "plasma-desktop|plasma-workspace(?:-.*)?|xfce4-session|mate-session-manager|"
        "cinnamon(?:-.*)?|budgie-desktop(?:-.*)?|xorg|xwayland|xserver-xorg(?:-.*)?|"
        "network-manager(?:-.*)?|networkd-dispatcher|netplan.io|"
        "dbus(?:-.*)?|udev|sudo|polkitd|policykit-1(?:-.*)?|snapd|flatpak|packagekit(?:-.*)?|"
        "libc6(?:-.*)?|libgcc-s1|libstdc\\+\\+6|"
        "bash|dash|coreutils|util-linux|mount|login|passwd|"
        "openssh-server|cryptsetup(?:-.*)?|lvm2|zfs(?:-.*)?)$"));
    if (critical.match(debBaseName(name)).hasMatch())
        return dlTr("关键系统组件");
    return {};
}

bool UninstallController::validTarget(QString *reason) const
{
    if (!m_app.removable) {
        *reason = m_app.protectionReason.isEmpty() ? dlTr("此软件被标记为受保护")
                                                : m_app.protectionReason;
        return false;
    }
    switch (m_app.source) {
    case PackageSource::Deb:
        if (!isDebId(m_app.id)) {
            *reason = dlTr("DEB 软件包标识不合法");
            return false;
        }
        if (!criticalDebReason(m_app.id).isEmpty()) {
            *reason = dlTr("为保护系统，不能卸载关键组件：%1").arg(m_app.id);
            return false;
        }
        break;
    case PackageSource::Snap: {
        static const QRegularExpression snapId(QStringLiteral("^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?(?:_[a-z0-9]{1,10})?$"));
        static const QRegularExpression snapCore(QStringLiteral("^(?:core[0-9]*|bare|snapd|snapd-desktop-integration|gnome-[0-9].*|gtk-common-themes|mesa-[0-9].*)$"));
        if (m_app.id.size() > 128 || !snapId.match(m_app.id).hasMatch()) {
            *reason = dlTr("Snap 软件包标识不合法");
            return false;
        }
        if (snapCore.match(m_app.id.section(QLatin1Char('_'), 0, 0)).hasMatch()) {
            *reason = dlTr("不能从此工具卸载 Snap 核心组件或共享运行环境");
            return false;
        }
        break;
    }
    case PackageSource::Flatpak: {
        static const QRegularExpression flatpakId(QStringLiteral(
            "^(?:[A-Za-z_][A-Za-z0-9_-]*\\.){2,}[A-Za-z_][A-Za-z0-9_-]*$"));
        if (m_app.id.size() > 256 || !flatpakId.match(m_app.id).hasMatch()
            || (m_app.scope != QStringLiteral("user") && m_app.scope != QStringLiteral("system"))
            || (!m_app.targetRef.isEmpty() && !validFlatpakRef(m_app.targetRef, m_app.id))) {
            *reason = dlTr("Flatpak 应用标识、安装范围或完整引用不合法");
            return false;
        }
        break;
    }
    default:
        *reason = dlTr("不支持的软件包来源");
        return false;
    }
    return true;
}

void UninstallController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged(busy);
}

void UninstallController::preview(const AppEntry &app)
{
    if (m_busy) {
        emit logMessage(dlTr("另一个卸载操作正在进行，请等待完成。"));
        return;
    }
    m_app = app;
    m_previewValid = false;
    m_plannedPackages.clear();
    m_plannedState.clear();
    m_flatpakRef.clear();
    startPreview(false);
}

void UninstallController::startPreview(bool beforeExecution)
{
    QString reason;
    if (!validTarget(&reason)) {
        fail(reason);
        return;
    }
    m_beforeExecution = beforeExecution;
    m_pendingPackages.clear();
    m_pendingState.clear();
    setBusy(true);
    if (beforeExecution)
        emit logMessage(dlTr("正在重新核对软件包状态与卸载范围…"));
    switch (m_app.source) {
    case PackageSource::Deb:
        run(Stage::TargetMetadata, QStringLiteral("/usr/bin/dpkg-query"),
            {QStringLiteral("-W"), QStringLiteral("-f=") + metadataFormat(),
             QStringLiteral("--"), m_app.id});
        break;
    case PackageSource::Snap:
        run(Stage::SnapInfo, QStringLiteral("/usr/bin/snap"),
            {QStringLiteral("list"), QStringLiteral("--color=never"),
             QStringLiteral("--unicode=never"), QStringLiteral("--"), m_app.id});
        break;
    case PackageSource::Flatpak:
        run(Stage::FlatpakInfo, QStringLiteral("/usr/bin/flatpak"),
            {QStringLiteral("info"), QStringLiteral("--") + m_app.scope,
             QStringLiteral("--show-ref"), QStringLiteral("--"),
             m_app.targetRef.isEmpty() ? m_app.id : m_app.targetRef});
        break;
    }
}

void UninstallController::run(Stage stage, const QString &program, const QStringList &arguments)
{
    if (!QFileInfo(program).isExecutable()) {
        fail(dlTr("未找到可执行程序：%1。请安装对应的软件包管理器。").arg(program));
        return;
    }
    m_stage = stage;
    m_output.clear();
    m_outputTruncated = false;
    m_timer.start(stage == Stage::Removing ? RemovalReminderMs : ReadTimeoutMs);
    m_process->start(program, arguments);
    // Neither our UI nor a pipe can supply authentication or package-manager input.
    m_process->closeWriteChannel();
}

void UninstallController::consumeOutput()
{
    const QString output = QString::fromUtf8(m_process->readAllStandardOutput());
    if (m_output.size() + output.size() > MaxOutputCharacters) {
        m_outputTruncated = true;
        m_output = (m_output + output).right(MaxOutputCharacters);
    } else {
        m_output += output;
    }
    if (m_stage == Stage::Removing && !output.trimmed().isEmpty())
        emit logMessage(output.trimmed());
}

bool UninstallController::readDebMetadata(bool target)
{
    const QStringList rows = m_output.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (rows.isEmpty() || (target && rows.size() != 1)) {
        fail(dlTr("无法唯一确认软件包的已安装状态，请刷新软件列表。"));
        return false;
    }
    QSet<QString> queriedNames;
    QStringList state;
    for (const QString &row : rows) {
        const QStringList fields = row.split(QLatin1Char('\t'), Qt::KeepEmptyParts);
        if (fields.size() != 7 || !isDebId(fields[0]) || fields[1] != QStringLiteral("installed")) {
            fail(dlTr("软件包元数据不完整，或软件包已不再安装。请刷新软件列表。"));
            return false;
        }
        const QString &name = fields[0];
        const QString priority = fields[4].trimmed().toLower();
        if (fields[2] == QStringLiteral("yes") || fields[3] == QStringLiteral("yes")
            || priority == QStringLiteral("required") || priority == QStringLiteral("important")
            || !criticalDebReason(name).isEmpty()) {
            fail(dlTr("已阻止卸载：受影响的软件包 %1 是受保护的系统组件。").arg(name));
            return false;
        }
        static const QRegularExpression arch(QStringLiteral("^[a-z0-9][a-z0-9-]*$"));
        if (!arch.match(fields[5]).hasMatch() || fields[6].isEmpty()
            || (fields[2] != QStringLiteral("yes") && fields[2] != QStringLiteral("no") && !fields[2].isEmpty())
            || (fields[3] != QStringLiteral("yes") && fields[3] != QStringLiteral("no") && !fields[3].isEmpty())) {
            fail(dlTr("无法验证软件包保护属性或体系结构，已停止操作。"));
            return false;
        }
        const QString explicitName = debBaseName(name) + QLatin1Char(':') + fields[5];
        queriedNames.insert(name);
        queriedNames.insert(explicitName);
        queriedNames.insert(debBaseName(name));
        state.append(explicitName + QLatin1Char('\t') + fields.mid(1).join(QLatin1Char('\t')));
        if (target) {
            if (debBaseName(name) != debBaseName(m_app.id)
                || (m_app.id.contains(QLatin1Char(':')) && m_app.id != explicitName)) {
                fail(dlTr("软件包标识与已安装元数据不一致，已停止操作。"));
                return false;
            }
            // An explicit architecture also prevents APT's trailing '+'/'-'
            // package modifiers from interpreting a legal Debian package name.
            m_aptTarget = explicitName;
        }
    }
    if (!target) {
        for (const QString &name : m_pendingPackages) {
            if (!queriedNames.contains(name)) {
                fail(dlTr("未能验证受影响软件包：%1").arg(name));
                return false;
            }
        }
        if (rows.size() != m_pendingPackages.size()) {
            fail(dlTr("卸载范围的元数据数量不一致，已停止操作。"));
            return false;
        }
        std::sort(state.begin(), state.end());
        m_pendingState = state;
    }
    return true;
}

void UninstallController::processFinished(int exitCode, QProcess::ExitStatus status)
{
    m_timer.stop();
    consumeOutput();
    if (m_stage == Stage::Idle)
        return;
    if (m_stage == Stage::Removing) {
        const bool success = exitCode == 0 && status == QProcess::NormalExit;
        m_executing = false;
        m_previewValid = false;
        m_stage = Stage::Idle;
        setBusy(false);
        const QString message = success
            ? dlTr("卸载完成，请刷新软件列表查看最新占用。")
            : dlTr("卸载未完成（退出码 %1）。授权取消、软件包锁或包管理器错误均会中止操作。\n%2")
                  .arg(exitCode).arg(errorDetail(m_output));
        emit finished(success, message);
        return;
    }
    if (status != QProcess::NormalExit || exitCode != 0 || m_outputTruncated) {
        fail(dlTr("卸载预览失败（退出码 %1）：\n%2")
                 .arg(exitCode).arg(m_outputTruncated ? dlTr("输出过大，无法可靠验证卸载范围。")
                                                     : errorDetail(m_output)));
        return;
    }
    switch (m_stage) {
    case Stage::TargetMetadata:
        if (readDebMetadata(true)) {
            QStringList arguments = aptSafetyArguments();
            arguments << QStringLiteral("--simulate") << QStringLiteral("remove")
                      << QStringLiteral("--") << m_aptTarget;
            run(Stage::AptSimulation, QStringLiteral("/usr/bin/apt-get"),
                arguments);
        }
        break;
    case Stage::AptSimulation: {
        static const QRegularExpression removed(QStringLiteral("^Remv\\s+(\\S+)(?:\\s|$)"));
        bool containsTarget = false;
        for (const QString &line : m_output.split(QLatin1Char('\n'))) {
            if (line.startsWith(QStringLiteral("Inst ")) || line.startsWith(QStringLiteral("Conf "))) {
                fail(dlTr("预览包含安装或配置其他软件包，无法作为单纯卸载操作执行。"));
                return;
            }
            const QRegularExpressionMatch match = removed.match(line);
            if (!match.hasMatch())
                continue;
            const QString name = match.captured(1);
            if (!isDebId(name)) {
                fail(dlTr("包管理器返回了无法验证的软件包标识。"));
                return;
            }
            m_pendingPackages.append(name);
            containsTarget = containsTarget || debBaseName(name) == debBaseName(m_app.id);
        }
        m_pendingPackages.removeDuplicates();
        std::sort(m_pendingPackages.begin(), m_pendingPackages.end());
        if (m_pendingPackages.isEmpty() || !containsTarget) {
            fail(dlTr("预览未包含所选软件包的卸载操作。软件可能已被卸载，请刷新列表。"));
            return;
        }
        if (m_pendingPackages.size() > 200) {
            fail(dlTr("此操作会卸载超过 200 个软件包，请使用系统包管理工具检查依赖。"));
            return;
        }
        QStringList arguments{QStringLiteral("-W"), QStringLiteral("-f=") + metadataFormat(), QStringLiteral("--")};
        arguments.append(m_pendingPackages);
        run(Stage::AffectedMetadata, QStringLiteral("/usr/bin/dpkg-query"), arguments);
        break;
    }
    case Stage::AffectedMetadata:
        if (readDebMetadata(false))
            finishPreview();
        break;
    case Stage::SnapInfo: {
        bool installed = false;
        for (const QString &line : m_output.split(QLatin1Char('\n'))) {
            const QStringList fields = line.simplified().split(QLatin1Char(' '));
            if (fields.size() >= 6 && fields[0] == m_app.id) {
                QString metadataState;
                QString reason;
                if (!verifySnapMetadata(m_app.id, fields[2], fields.last().split(QLatin1Char(',')),
                                        &metadataState, &reason)) {
                    fail(reason);
                    return;
                }
                installed = true;
                m_pendingState.append(fields[0] + QLatin1Char('\t') + fields[1] + QLatin1Char('\t')
                                      + fields[2] + QLatin1Char('\t') + metadataState);
            }
        }
        if (!installed) {
            fail(dlTr("未能确认此 Snap 已安装，请刷新软件列表。"));
            return;
        }
        m_pendingPackages = QStringList{m_app.id};
        finishPreview();
        break;
    }
    case Stage::FlatpakInfo: {
        const QString ref = m_output.trimmed();
        if (!validFlatpakRef(ref, m_app.id)
            || (!m_app.targetRef.isEmpty() && ref != m_app.targetRef)
            || (!m_flatpakRef.isEmpty() && ref != m_flatpakRef)) {
            fail(dlTr("未能确认同一安装范围中的 Flatpak 应用引用，请刷新列表。"));
            return;
        }
        m_flatpakRef = ref;
        m_pendingPackages = QStringList{ref};
        m_pendingState = QStringList{m_app.scope + QLatin1Char(':') + ref};
        finishPreview();
        break;
    }
    case Stage::Idle:
    case Stage::Removing:
        break;
    }
}

void UninstallController::finishPreview()
{
    if (m_beforeExecution) {
        if (m_pendingPackages != m_plannedPackages || m_pendingState != m_plannedState) {
            fail(dlTr("软件包状态或卸载范围已变化。已停止操作，请重新预览并确认。"));
            return;
        }
        startRemoval();
        return;
    }
    m_plannedPackages = m_pendingPackages;
    m_plannedState = m_pendingState;
    m_previewValid = true;
    m_stage = Stage::Idle;
    setBusy(false);
    QString summary;
    switch (m_app.source) {
    case PackageSource::Deb:
        summary = dlTr("将卸载 %1 个 DEB 软件包（含依赖关系引起的连带卸载）。\n"
                     "保留软件配置，不执行 autoremove。执行前会再次核对范围；"
                     "APT 模拟不锁定系统状态，请勿同时使用其他包管理工具。")
                      .arg(m_plannedPackages.size());
        break;
    case PackageSource::Snap:
        summary = dlTr("将卸载 Snap：%1。\nSnap 不提供卸载事务模拟，无法预览依赖信息；"
                     "此操作只指定所选 Snap。使用默认卸载行为，可能保留自动数据快照。")
                      .arg(m_app.id);
        break;
    case PackageSource::Flatpak:
        summary = dlTr("将卸载 %1 范围中的 Flatpak 应用：%2。\n"
                     "只卸载此应用引用，保留关联运行时和应用数据，不清理未使用组件。")
                      .arg(m_app.scope == QStringLiteral("user") ? dlTr("当前用户") : dlTr("系统"), m_app.id);
        break;
    }
    emit previewReady(summary, m_plannedPackages);
}

void UninstallController::execute()
{
    if (m_busy) {
        emit logMessage(dlTr("已有卸载操作正在进行。"));
        return;
    }
    if (!m_previewValid || m_plannedPackages.isEmpty()) {
        fail(dlTr("请先预览卸载范围，并在确认后执行。"));
        return;
    }
    m_previewValid = false; // A confirmation is single use, including failed attempts.
    startPreview(true);
}

void UninstallController::startRemoval()
{
    QString program;
    QStringList arguments;
    switch (m_app.source) {
    case PackageSource::Deb:
        program = QStringLiteral("/usr/bin/pkexec");
        arguments = aptSafetyArguments();
        arguments.prepend(QStringLiteral("/usr/bin/apt-get"));
        arguments << QStringLiteral("-y") << QStringLiteral("remove")
                  << QStringLiteral("--") << m_aptTarget;
        break;
    case PackageSource::Snap:
        program = QStringLiteral("/usr/bin/pkexec");
        arguments = QStringList{QStringLiteral("/usr/bin/snap"), QStringLiteral("remove"),
                                QStringLiteral("--"), m_app.id};
        break;
    case PackageSource::Flatpak:
        program = QStringLiteral("/usr/bin/flatpak");
        arguments = QStringList{QStringLiteral("uninstall"), QStringLiteral("--") + m_app.scope,
                                QStringLiteral("--app"), QStringLiteral("--no-related"),
                                QStringLiteral("--noninteractive"), QStringLiteral("--assumeyes"),
                                QStringLiteral("--"), m_flatpakRef};
        // User installs need no privileges; system installs use the OS's polkit
        // agent. We never collect passwords or cache authentication in this UI.
        if (m_app.scope == QStringLiteral("system")) {
            arguments.prepend(program);
            program = QStringLiteral("/usr/bin/pkexec");
        }
        break;
    }
    if (!QFileInfo(program).isExecutable()) {
        fail(dlTr("找不到系统授权程序或软件包管理器：%1").arg(program));
        return;
    }
    emit logMessage(dlTr("卸载范围校验通过。需要管理员权限时，请在系统授权窗口中确认；"
                       "包管理操作开始后请等待完成。"));
    m_executing = true;
    run(Stage::Removing, program, arguments);
}

void UninstallController::cancelPreview()
{
    if (m_executing) {
        emit logMessage(dlTr("包管理器已开始执行，不能中断。请等待完成。"));
        return;
    }
    m_previewValid = false;
    m_plannedPackages.clear();
    m_plannedState.clear();
    m_timer.stop();
    m_stage = Stage::Idle;
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    setBusy(false);
}

void UninstallController::timeout()
{
    if (m_executing) {
        emit logMessage(dlTr("卸载或系统授权仍在进行。为保护软件包数据库，本软件不会强制终止操作。"));
        m_timer.start(RemovalReminderMs);
        return;
    }
    m_stage = Stage::Idle;
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    fail(dlTr("读取软件包信息超时。请确认包管理器服务可用后重试。"));
}

void UninstallController::fail(const QString &message)
{
    m_timer.stop();
    m_previewValid = false;
    m_executing = false;
    m_stage = Stage::Idle;
    setBusy(false);
    emit finished(false, message);
}
