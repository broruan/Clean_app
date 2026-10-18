#include "localization.h"
#include "cachebackend.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSet>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/stat.h>
#include <memory>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct Descriptor {
    explicit Descriptor(int value = -1) : value(value) {}
    ~Descriptor() { if (value >= 0) ::close(value); }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
    Descriptor(Descriptor &&other) noexcept : value(other.release()) {}
    int release() { const int descriptor = value; value = -1; return descriptor; }
    int value;
};

struct DirectoryCloser {
    void operator()(DIR *directory) const { if (directory) ::closedir(directory); }
};
using Directory = std::unique_ptr<DIR, DirectoryCloser>;

struct Inode {
    quint64 device;
    quint64 number;
    bool operator==(const Inode &other) const { return device == other.device && number == other.number; }
};
struct InodeHash {
    std::size_t operator()(const Inode &value) const
    {
        return std::hash<quint64>{}(value.device) ^ (std::hash<quint64>{}(value.number) << 1);
    }
};

struct Frame {
    Directory directory;
    QString relativePath;
    CacheDirectorySnapshot snapshot;
};

struct Builder {
    CacheEntry entry;
    QSet<QString> directories;
};

bool canceled(std::atomic_bool *cancel)
{
    return cancel && cancel->load(std::memory_order_relaxed);
}

class Reporter {
public:
    explicit Reporter(CacheProgress progress) : m_progress(std::move(progress)) {}
    void report(quint64 count, const QString &path)
    {
        const auto now = Clock::now();
        if (m_progress && now - m_last >= std::chrono::milliseconds(150)) {
            m_last = now;
            m_progress(count, path);
        }
    }
private:
    using Clock = std::chrono::steady_clock;
    CacheProgress m_progress;
    Clock::time_point m_last = Clock::now() - std::chrono::milliseconds(150);
};

QString errorText(int error)
{
    return QString::fromLocal8Bit(std::strerror(error));
}

QString absolutePath(const QString &root, const QString &relative)
{
    return root + QLatin1Char('/') + relative;
}

bool validRelative(const QString &path)
{
    if (path.isEmpty() || path.contains(QChar::Null) || QDir::isAbsolutePath(path))
        return false;
    const auto parts = path.split(QLatin1Char('/'));
    for (const auto &part : parts)
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral(".."))
            return false;
    return true;
}

bool belongsToScope(const QString &path, const QString &scope)
{
    return path == scope || path.startsWith(scope + QLatin1Char('/'));
}

bool isLog(const QString &relativePath)
{
    const auto parts = relativePath.split(QLatin1Char('/'));
    const QString &name = parts.constLast();
    if (name.endsWith(QStringLiteral(".log"), Qt::CaseInsensitive)
        || name.contains(QStringLiteral(".log."), Qt::CaseInsensitive))
        return true;
    for (int index = 0; index + 1 < parts.size(); ++index)
        if (parts.at(index).compare(QStringLiteral("log"), Qt::CaseInsensitive) == 0
            || parts.at(index).compare(QStringLiteral("logs"), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

QString normalizeRoot(const QString &path, bool optional, QString &error)
{
    if (optional && path.isEmpty())
        return {};
    if (path.isEmpty() || path.contains(QChar::Null) || !QDir::isAbsolutePath(path)) {
        error = dlTr("缓存和日志根目录必须是有效的绝对路径。");
        return {};
    }
    const QString normalized = QDir::cleanPath(path);
    const QString home = QDir::cleanPath(QDir::homePath());
    static const QStringList broadRoots = {
        QStringLiteral("/"), QStringLiteral("/home"), QStringLiteral("/tmp"), QStringLiteral("/var"),
        QStringLiteral("/var/tmp"), QStringLiteral("/usr"), QStringLiteral("/opt"),
        QStringLiteral("/etc"), QStringLiteral("/run"), QStringLiteral("/dev"),
        QStringLiteral("/proc"), QStringLiteral("/sys")
    };
    if (broadRoots.contains(normalized) || home == normalized
        || home.startsWith(normalized + QLatin1Char('/'))) {
        error = dlTr("拒绝使用整个主目录、其祖先或系统公共目录作为缓存清理根目录：%1").arg(normalized);
        return {};
    }
    return normalized;
}

bool validateRoots(const QString &cacheInput, const QString &stateInput,
                   QString &cacheRoot, QString &stateRoot, QString &error)
{
    cacheRoot = normalizeRoot(cacheInput, false, error);
    if (!error.isEmpty())
        return false;
    stateRoot = normalizeRoot(stateInput, true, error);
    if (!error.isEmpty())
        return false;
    if (!stateRoot.isEmpty()
        && (cacheRoot == stateRoot || cacheRoot.startsWith(stateRoot + QLatin1Char('/'))
            || stateRoot.startsWith(cacheRoot + QLatin1Char('/')))) {
        error = dlTr("缓存目录和日志目录不能相同或互相包含。");
        return false;
    }
    return true;
}

int openRoot(const QString &path)
{
    int descriptor = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0)
        return -1;
    const auto parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const auto &part : parts) {
        const QByteArray encoded = QFile::encodeName(part);
        const int next = ::openat(descriptor, encoded.constData(),
                                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        const int savedError = errno;
        ::close(descriptor);
        if (next < 0) {
            errno = savedError;
            return -1;
        }
        descriptor = next;
    }
    return descriptor;
}

bool ownedDirectory(const struct stat &information)
{
    return S_ISDIR(information.st_mode) && information.st_uid == ::geteuid();
}

bool mountId(int descriptor, const char *name, quint64 &identifier)
{
#if defined(SYS_statx) && defined(STATX_MNT_ID)
    struct statx information {};
    if (::syscall(SYS_statx, descriptor, name, AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH,
                  STATX_MNT_ID, &information) == 0 && (information.stx_mask & STATX_MNT_ID)) {
        identifier = information.stx_mnt_id;
        return true;
    }
#else
    Q_UNUSED(descriptor)
    Q_UNUSED(name)
    Q_UNUSED(identifier)
#endif
    return false;
}

struct MountBoundary {
    quint64 rootMount = 0;
    bool hasId = false;
    bool available = false;
    QSet<QString> paths;

    explicit MountBoundary(int rootDescriptor)
    {
        hasId = mountId(rootDescriptor, "", rootMount);
        QFile file(QStringLiteral("/proc/self/mountinfo"));
        if (file.open(QIODevice::ReadOnly)) {
            const auto lines = file.readAll().split('\n');
            for (const auto &line : lines) {
                const auto fields = line.split(' ');
                if (fields.size() < 5)
                    continue;
                QByteArray path = fields.at(4);
                path.replace("\\040", " ");
                path.replace("\\011", "\t");
                path.replace("\\012", "\n");
                path.replace("\\134", "\\");
                paths.insert(QFile::decodeName(path));
            }
        }
        available = hasId || !paths.isEmpty();
    }

    bool permits(int descriptor, const char *name, const QString &path) const
    {
        quint64 identifier = 0;
        return !paths.contains(path)
            && (!hasId || (mountId(descriptor, name, identifier) && identifier == rootMount));
    }
    bool permitsOpened(int descriptor) const
    {
        quint64 identifier = 0;
        return !hasId || (mountId(descriptor, "", identifier) && identifier == rootMount);
    }
};

quint64 logicalBytes(const struct stat &information)
{
    return information.st_size > 0 ? quint64(information.st_size) : 0;
}

quint64 allocatedBytes(const struct stat &information)
{
    return information.st_blocks > 0 ? quint64(information.st_blocks) * 512 : 0;
}

qint64 timeNs(const timespec &time)
{
    return qint64(time.tv_sec) * 1000000000LL + time.tv_nsec;
}

QByteArray manifestSignature(const CacheEntry &entry)
{
    static const QByteArray secret = [] {
        QByteArray key(32, '\0');
        for (int index = 0; index < key.size(); index += 4) {
            const quint32 random = QRandomGenerator::system()->generate();
            std::memcpy(key.data() + index, &random, sizeof(random));
        }
        return key;
    }();
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_12);
    stream << entry.id << entry.name << entry.path << entry.rootPath << entry.relativePath
           << entry.rootDevice << entry.rootInode << entry.rootMountId << entry.logOnly
           << entry.bytes << entry.allocatedBytes << entry.fileCount << entry.logCount
           << entry.logAllocatedBytes << entry.skippedEntries << quint64(entry.directories.size());
    for (const auto &directory : entry.directories)
        stream << directory.relativePath << directory.device << directory.inode;
    stream << quint64(entry.files.size());
    for (const auto &file : entry.files)
        stream << file.relativePath << file.device << file.inode << file.logicalBytes
               << file.allocatedBytes << file.modifiedNs << file.changedNs << file.ownerUid;
    return QMessageAuthenticationCode::hash(payload, secret, QCryptographicHash::Sha256);
}

void addMessage(QStringList &messages, const QString &message)
{
    constexpr int maximumMessages = 100;
    if (messages.size() < maximumMessages)
        messages.append(message);
    else if (messages.size() == maximumMessages)
        messages.append(dlTr("其余条目的详细消息已省略。"));
}

void scanRoot(const QString &path, bool stateOnly, CacheScanResult &result,
              std::atomic_bool *cancel, Reporter &reporter,
              std::unordered_set<Inode, InodeHash> &hardLinks)
{
    Descriptor root(openRoot(path));
    if (root.value < 0) {
        const int savedError = errno;
        const QString message = dlTr("无法扫描 %1：%2；目录不能包含软链接。").arg(path, errorText(savedError));
        if (savedError == ENOENT)
            addMessage(result.warnings, dlTr("目录不存在，已跳过：%1").arg(path));
        else
            result.error = message;
        return;
    }
    struct stat rootInformation {};
    if (::fstat(root.value, &rootInformation) != 0 || !ownedDirectory(rootInformation)) {
        result.error = dlTr("缓存/日志根目录必须由当前用户拥有且可读取：%1").arg(path);
        return;
    }
    MountBoundary boundary(root.value);
    if (!boundary.available) {
        result.error = dlTr("无法验证文件系统挂载边界：%1").arg(path);
        return;
    }
    Directory rootDirectory(::fdopendir(root.value));
    if (!rootDirectory) {
        result.error = dlTr("无法读取目录 %1：%2").arg(path, errorText(errno));
        return;
    }
    root.release(); // fdopendir owns the descriptor from here.
    std::vector<Frame> frames;
    frames.push_back({std::move(rootDirectory), {}, {}});
    std::vector<Builder> builders;
    QHash<QString, int> indices;

    const auto builderFor = [&](const QString &relative, bool log) -> Builder & {
        const QString scope = relative.section(QLatin1Char('/'), 0, 0);
        const QString key = (log ? QStringLiteral("l:") : QStringLiteral("c:")) + scope;
        auto found = indices.constFind(key);
        if (found != indices.constEnd())
            return builders[std::size_t(found.value())];
        Builder builder;
        QByteArray identity = path.toUtf8();
        identity.append('\0');
        identity.append(scope.toUtf8());
        identity.append(log ? "\0logs" : "\0cache", log ? 5 : 6);
        builder.entry.id = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
        builder.entry.name = scope + (log ? dlTr(" · 日志") : QString());
        builder.entry.path = absolutePath(path, scope);
        builder.entry.rootPath = path;
        builder.entry.relativePath = scope;
        builder.entry.rootDevice = rootInformation.st_dev;
        builder.entry.rootInode = rootInformation.st_ino;
        builder.entry.rootMountId = boundary.hasId ? boundary.rootMount : 0;
        builder.entry.logOnly = log;
        const int index = int(builders.size());
        indices.insert(key, index);
        builders.push_back(std::move(builder));
        return builders.back();
    };
    const auto skipped = [&](const QString &relative) {
        ++result.skippedEntries;
        if (!relative.isEmpty() && (!stateOnly || isLog(relative)))
            ++builderFor(relative, isLog(relative)).entry.skippedEntries;
    };
    reporter.report(result.scannedFiles, path);

    while (!frames.empty()) {
        if (canceled(cancel)) {
            result.canceled = true;
            break;
        }
        Frame &current = frames.back();
        errno = 0;
        const dirent *entry = ::readdir(current.directory.get());
        if (!entry) {
            if (errno)
                skipped(current.relativePath);
            frames.pop_back();
            continue;
        }
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;
        const QString name = QFile::decodeName(entry->d_name);
        const QString relative = current.relativePath.isEmpty() ? name : current.relativePath + QLatin1Char('/') + name;
        const QString fullPath = absolutePath(path, relative);
        reporter.report(result.scannedFiles, fullPath);
        if (canceled(cancel)) {
            result.canceled = true;
            break;
        }
        const int parent = ::dirfd(current.directory.get());
        struct stat information {};
        if (::fstatat(parent, entry->d_name, &information, AT_SYMLINK_NOFOLLOW) != 0) {
            skipped(relative);
            continue;
        }
        if (!S_ISDIR(information.st_mode) && !S_ISREG(information.st_mode)) {
            if (S_ISLNK(information.st_mode))
                skipped(relative);
            continue;
        }
        if (information.st_dev != rootInformation.st_dev || information.st_uid != ::geteuid()
            || !boundary.permits(parent, entry->d_name, fullPath)) {
            skipped(relative);
            continue;
        }
        if (S_ISDIR(information.st_mode)) {
            Descriptor child(::openat(parent, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            struct stat opened {};
            if (child.value < 0 || ::fstat(child.value, &opened) != 0 || !ownedDirectory(opened)
                || opened.st_dev != information.st_dev || opened.st_ino != information.st_ino
                || !boundary.permitsOpened(child.value)) {
                skipped(relative);
                continue;
            }
            Directory directory(::fdopendir(child.value));
            if (!directory) {
                skipped(relative);
                continue;
            }
            child.release();
            frames.push_back({std::move(directory), relative,
                              {relative, quint64(information.st_dev), quint64(information.st_ino)}});
            continue;
        }

        ++result.scannedFiles;
        const bool log = isLog(relative);
        if (stateOnly && !log)
            continue;
        if (information.st_nlink > 1
            && !hardLinks.insert({quint64(information.st_dev), quint64(information.st_ino)}).second)
            continue;
        Builder &builder = builderFor(relative, log);
        CacheEntry &item = builder.entry;
        const quint64 logical = logicalBytes(information), allocated = allocatedBytes(information);
        item.files.append({relative, quint64(information.st_dev), quint64(information.st_ino),
                           logical, allocated, timeNs(information.st_mtim),
                           timeNs(information.st_ctim), quint64(information.st_uid)});
        item.bytes += logical;
        item.allocatedBytes += allocated;
        ++item.fileCount;
        if (log) {
            ++item.logCount;
            item.logAllocatedBytes += allocated;
        }
        for (const auto &frame : frames) {
            if (!frame.relativePath.isEmpty() && !builder.directories.contains(frame.relativePath)) {
                builder.directories.insert(frame.relativePath);
                item.directories.append(frame.snapshot);
            }
        }
    }
    for (auto &builder : builders) {
        auto &item = builder.entry;
        item.manifestToken = manifestSignature(item);
        result.entries.append(std::move(item));
    }
}

bool validManifest(const CacheEntry &entry, const QString &cacheRoot, const QString &stateRoot,
                   QString &error)
{
    if (entry.manifestToken.isEmpty() || entry.manifestToken != manifestSignature(entry)) {
        error = dlTr("扫描快照无效或已被修改，请重新扫描。");
        return false;
    }
    const bool cache = entry.rootPath == cacheRoot;
    const bool state = !stateRoot.isEmpty() && entry.rootPath == stateRoot;
    if ((!cache && !state) || (state && !entry.logOnly)
        || !validRelative(entry.relativePath) || entry.relativePath.contains(QLatin1Char('/'))
        || entry.path != absolutePath(entry.rootPath, entry.relativePath)) {
        error = dlTr("条目不属于已授权的缓存/日志根目录。");
        return false;
    }
    QSet<QString> directories, files;
    for (const auto &directory : entry.directories) {
        if (!validRelative(directory.relativePath) || !belongsToScope(directory.relativePath, entry.relativePath)
            || directory.device != entry.rootDevice || directories.contains(directory.relativePath)) {
            error = dlTr("扫描快照含有无效的父目录路径。");
            return false;
        }
        directories.insert(directory.relativePath);
    }
    for (const auto &file : entry.files) {
        if (!validRelative(file.relativePath) || !belongsToScope(file.relativePath, entry.relativePath)
            || isLog(file.relativePath) != entry.logOnly || file.device != entry.rootDevice
            || files.contains(file.relativePath)) {
            error = dlTr("扫描快照含有越界、重复或不符合日志规则的文件。");
            return false;
        }
        files.insert(file.relativePath);
        const auto parts = file.relativePath.split(QLatin1Char('/'));
        QString parent;
        for (int index = 0; index + 1 < parts.size(); ++index) {
            parent = parent.isEmpty() ? parts.at(index) : parent + QLatin1Char('/') + parts.at(index);
            if (!directories.contains(parent)) {
                error = dlTr("扫描快照缺少父目录信息，请重新扫描。");
                return false;
            }
        }
    }
    return true;
}

int openFileParent(int root, const CacheEntry &entry, const QString &relative,
                   const QHash<QString, CacheDirectorySnapshot> &directories,
                   const MountBoundary &boundary)
{
    int descriptor = ::fcntl(root, F_DUPFD_CLOEXEC, 0);
    if (descriptor < 0)
        return -1;
    const auto parts = relative.split(QLatin1Char('/'));
    QString path;
    for (int index = 0; index + 1 < parts.size(); ++index) {
        path = path.isEmpty() ? parts.at(index) : path + QLatin1Char('/') + parts.at(index);
        const auto expected = directories.constFind(path);
        const QByteArray name = QFile::encodeName(parts.at(index));
        int next = -1;
        if (expected != directories.constEnd()
            && boundary.permits(descriptor, name.constData(), absolutePath(entry.rootPath, path)))
            next = ::openat(descriptor, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        const int savedError = errno;
        ::close(descriptor);
        if (next < 0) {
            errno = savedError;
            return -1;
        }
        struct stat information {};
        if (::fstat(next, &information) != 0 || !ownedDirectory(information)
            || quint64(information.st_dev) != expected.value().device
            || quint64(information.st_ino) != expected.value().inode || !boundary.permitsOpened(next)) {
            ::close(next);
            errno = ESTALE;
            return -1;
        }
        descriptor = next;
    }
    return descriptor;
}

bool matchesFile(const struct stat &information, const CacheFileSnapshot &snapshot)
{
    return S_ISREG(information.st_mode) && information.st_nlink == 1
        && information.st_uid == ::geteuid() && quint64(information.st_uid) == snapshot.ownerUid
        && quint64(information.st_dev) == snapshot.device && quint64(information.st_ino) == snapshot.inode
        && logicalBytes(information) == snapshot.logicalBytes
        && allocatedBytes(information) == snapshot.allocatedBytes
        && timeNs(information.st_mtim) == snapshot.modifiedNs && timeNs(information.st_ctim) == snapshot.changedNs;
}

int openVerifiedRoot(const CacheEntry &entry, const MountBoundary &boundary)
{
    Descriptor root(openRoot(entry.rootPath));
    struct stat information {};
    if (root.value < 0 || ::fstat(root.value, &information) != 0 || !ownedDirectory(information)
        || quint64(information.st_dev) != entry.rootDevice || quint64(information.st_ino) != entry.rootInode
        || !boundary.permitsOpened(root.value)) {
        errno = ESTALE;
        return -1;
    }
    return root.release();
}

} // namespace

CacheScanResult scanCaches(const QString &cacheInput, const QString &stateInput,
                           std::atomic_bool *cancel, CacheProgress progress)
{
    CacheScanResult result;
    if (canceled(cancel)) {
        result.canceled = true;
        return result;
    }
    QString cacheRoot, stateRoot;
    if (!validateRoots(cacheInput, stateInput, cacheRoot, stateRoot, result.error))
        return result;
    Reporter reporter(std::move(progress));
    std::unordered_set<Inode, InodeHash> hardLinks;
    scanRoot(cacheRoot, false, result, cancel, reporter, hardLinks);
    if (result.error.isEmpty() && !stateRoot.isEmpty() && !canceled(cancel))
        scanRoot(stateRoot, true, result, cancel, reporter, hardLinks);
    if (canceled(cancel))
        result.canceled = true;
    std::sort(result.entries.begin(), result.entries.end(), [](const CacheEntry &left, const CacheEntry &right) {
        return left.allocatedBytes != right.allocatedBytes ? left.allocatedBytes > right.allocatedBytes : left.path < right.path;
    });
    return result;
}

CacheCleanResult cleanCaches(const QVector<CacheEntry> &selected, const QString &cacheInput,
                             const QString &stateInput, std::atomic_bool *cancel, CacheProgress progress)
{
    CacheCleanResult result;
    if (canceled(cancel)) {
        result.canceled = true;
        return result;
    }
    QString cacheRoot, stateRoot, rootError;
    if (!validateRoots(cacheInput, stateInput, cacheRoot, stateRoot, rootError)) {
        for (const auto &entry : selected)
            result.failedFiles += quint64(std::max(1, int(entry.files.size())));
        addMessage(result.errors, rootError);
        return result;
    }
    Reporter reporter(std::move(progress));
    for (const auto &entry : selected) {
        if (canceled(cancel)) {
            result.canceled = true;
            break;
        }
        QString invalid;
        if (!validManifest(entry, cacheRoot, stateRoot, invalid)) {
            result.failedFiles += quint64(std::max(1, int(entry.files.size())));
            addMessage(result.errors, entry.name + QStringLiteral("：") + invalid);
            continue;
        }
        Descriptor root(openRoot(entry.rootPath));
        struct stat rootInformation {};
        if (root.value < 0 || ::fstat(root.value, &rootInformation) != 0 || !ownedDirectory(rootInformation)
            || quint64(rootInformation.st_dev) != entry.rootDevice || quint64(rootInformation.st_ino) != entry.rootInode) {
            result.skippedFiles += quint64(entry.files.size());
            addMessage(result.errors, dlTr("根目录发生变化或不可访问，已保留：%1").arg(entry.rootPath));
            continue;
        }
        MountBoundary boundary(root.value);
        if (!boundary.available || (entry.rootMountId && (!boundary.hasId || boundary.rootMount != entry.rootMountId))) {
            result.skippedFiles += quint64(entry.files.size());
            addMessage(result.errors, dlTr("无法验证挂载边界，已保留：%1").arg(entry.path));
            continue;
        }
        QHash<QString, CacheDirectorySnapshot> directories;
        for (const auto &directory : entry.directories)
            directories.insert(directory.relativePath, directory);
        for (const auto &file : entry.files) {
            if (canceled(cancel)) {
                result.canceled = true;
                break;
            }
            const QString fullPath = absolutePath(entry.rootPath, file.relativePath);
            const quint64 processed = result.removedFiles + result.skippedFiles + result.failedFiles;
            reporter.report(processed, fullPath);
            if (canceled(cancel)) {
                result.canceled = true;
                break;
            }
            // Reopen the named root after the callback for every file. A root
            // descriptor retained across an entry must never authorize deletion
            // after that directory has been moved outside the trusted path.
            Descriptor currentRoot(openVerifiedRoot(entry, boundary));
            Descriptor parent(currentRoot.value >= 0
                ? openFileParent(currentRoot.value, entry, file.relativePath, directories, boundary) : -1);
            if (parent.value < 0) {
                ++result.skippedFiles;
                addMessage(result.errors, dlTr("父目录发生变化、为软链接或不可访问，已保留：%1").arg(fullPath));
                continue;
            }
            const QByteArray name = QFile::encodeName(file.relativePath.section(QLatin1Char('/'), -1));
            struct stat information {};
            if (::fstatat(parent.value, name.constData(), &information, AT_SYMLINK_NOFOLLOW) != 0
                || !matchesFile(information, file) || !boundary.permits(parent.value, name.constData(), fullPath)) {
                ++result.skippedFiles;
                addMessage(result.errors, dlTr("文件已改变、不属于当前用户或含硬链接，已保留：%1").arg(fullPath));
                continue;
            }
            if (canceled(cancel)) {
                result.canceled = true;
                break;
            }
            // Reopen the complete named ancestry and recheck immediately before
            // unlinkat. POSIX has no conditional
            // unlink-by-inode primitive: hostile same-UID replacement between
            // this check and unlink remains a very small, non-atomic window.
            Descriptor immediateRoot(openVerifiedRoot(entry, boundary));
            Descriptor immediateParent(immediateRoot.value >= 0
                ? openFileParent(immediateRoot.value, entry, file.relativePath, directories, boundary) : -1);
            struct stat immediate {};
            if (immediateParent.value < 0
                || ::fstatat(immediateParent.value, name.constData(), &immediate, AT_SYMLINK_NOFOLLOW) != 0
                || !matchesFile(immediate, file)) {
                ++result.skippedFiles;
                addMessage(result.errors, dlTr("文件在清理前发生变化，已保留：%1").arg(fullPath));
                continue;
            }
            if (::unlinkat(immediateParent.value, name.constData(), 0) == 0) {
                ++result.removedFiles;
                result.removedBytes += allocatedBytes(immediate);
            } else {
                ++result.failedFiles;
                addMessage(result.errors, dlTr("无法清理 %1：%2").arg(fullPath, errorText(errno)));
            }
        }
        if (result.canceled)
            break;
    }
    if (canceled(cancel))
        result.canceled = true;
    return result;
}
