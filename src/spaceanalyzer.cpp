#include "spaceanalyzer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
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
#include <vector>

namespace {

constexpr int MaximumDepth = 128;
constexpr int MaximumEntries = 1000000;
constexpr qint64 SampleBytes = 64 * 1024;
constexpr qint64 HashBlockBytes = 1024 * 1024;

struct Descriptor {
    int value = -1;
    explicit Descriptor(int descriptor = -1) : value(descriptor) {}
    ~Descriptor() { if (value >= 0) ::close(value); }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
};

struct DirectoryCloser {
    void operator()(DIR *directory) const { if (directory) ::closedir(directory); }
};
using Directory = std::unique_ptr<DIR, DirectoryCloser>;

struct Inode {
    dev_t device;
    ino_t number;
    bool operator==(const Inode &other) const
    { return device == other.device && number == other.number; }
};
struct InodeHash {
    std::size_t operator()(const Inode &inode) const
    {
        return std::hash<quint64>{}(quint64(inode.device))
            ^ (std::hash<quint64>{}(quint64(inode.number)) << 1);
    }
};

bool isCanceled(std::atomic_bool *cancel)
{ return cancel && cancel->load(std::memory_order_relaxed); }

quint64 logicalSize(const struct stat &information)
{ return information.st_size > 0 ? quint64(information.st_size) : 0; }

quint64 allocatedSize(const struct stat &information)
{ return information.st_blocks > 0 ? quint64(information.st_blocks) * 512 : 0; }

class Progress {
public:
    explicit Progress(std::function<void(quint64, const QString &)> callback)
        : m_callback(std::move(callback)) {}
    void report(quint64 count, const QString &path, bool force = false)
    {
        const auto now = std::chrono::steady_clock::now();
        if (m_callback && (force || now - m_last >= std::chrono::milliseconds(150))) {
            m_last = now;
            m_callback(count, path);
        }
    }
private:
    std::function<void(quint64, const QString &)> m_callback;
    std::chrono::steady_clock::time_point m_last {};
};

QString systemError(int error)
{ return QString::fromLocal8Bit(std::strerror(error)); }

int openRoot(const QString &path)
{
    int descriptor = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0)
        return -1;
    for (const QString &part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        const QByteArray name = QFile::encodeName(part);
        const int next = ::openat(descriptor, name.constData(),
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

bool mountId(int descriptor, const char *name, quint64 &identifier)
{
#if defined(SYS_statx) && defined(STATX_MNT_ID)
    struct statx information {};
    if (::syscall(SYS_statx, descriptor, name, AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH,
                  STATX_MNT_ID, &information) == 0
        && (information.stx_mask & STATX_MNT_ID)) {
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

QSet<QString> mountPaths(bool &available)
{
    QSet<QString> paths;
    QFile information(QStringLiteral("/proc/self/mountinfo"));
    available = information.open(QIODevice::ReadOnly);
    if (!available)
        return paths;
    for (const QByteArray &line : information.readAll().split('\n')) {
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
    return paths;
}

struct ScanRoot {
    Descriptor descriptor;
    QString path;
    struct stat information {};
    quint64 mount = 0;
    bool hasMountId = false;
    QSet<QString> mounts;

    bool initialize(const QString &requested, int limit, QString &error)
    {
        if (requested.trimmed().isEmpty() || requested.contains(QChar::Null)) {
            error = QStringLiteral("请选择有效的文件夹路径。");
            return false;
        }
        if (limit <= 0 || limit > MaximumEntries) {
            error = QStringLiteral("扫描上限必须在 1 至 %1 之间。").arg(MaximumEntries);
            return false;
        }
        path = QDir::cleanPath(QDir(requested).absolutePath());
        descriptor.value = openRoot(path);
        if (descriptor.value < 0 || ::fstat(descriptor.value, &information) != 0) {
            error = QStringLiteral("无法打开文件夹 %1：%2。路径不能包含符号链接。")
                        .arg(path, systemError(errno));
            return false;
        }
        hasMountId = mountId(descriptor.value, "", mount);
        bool hasMountPaths = false;
        mounts = mountPaths(hasMountPaths);
        if (!hasMountId && !hasMountPaths) {
            error = QStringLiteral("无法读取文件系统挂载边界，请检查 /proc 是否可访问。");
            return false;
        }
        return true;
    }

    bool allowed(int parent, const char *name, const QString &entryPath,
                 const struct stat &entry) const
    {
        quint64 entryMount = 0;
        return entry.st_dev == information.st_dev && !mounts.contains(entryPath)
            && (!hasMountId || (mountId(parent, name, entryMount) && entryMount == mount));
    }

    bool allowedDescriptor(int descriptor, const struct stat &entry) const
    {
        quint64 entryMount = 0;
        return entry.st_dev == information.st_dev
            && (!hasMountId || (mountId(descriptor, "", entryMount) && entryMount == mount));
    }
};

Directory directoryFromDescriptor(int descriptor)
{
    Directory directory(::fdopendir(descriptor));
    if (!directory)
        ::close(descriptor);
    return directory;
}

QString appendPath(const QString &path, const char *name)
{ return path + (path == QStringLiteral("/") ? QString() : QStringLiteral("/")) + QFile::decodeName(name); }

int openChildDirectory(int parent, const char *name, const struct stat &expected,
                       const ScanRoot &root)
{
    const int descriptor = ::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (descriptor < 0)
        return -1;
    struct stat actual {};
    if (::fstat(descriptor, &actual) != 0 || actual.st_ino != expected.st_ino
        || actual.st_dev != expected.st_dev || !root.allowedDescriptor(descriptor, actual)) {
        ::close(descriptor);
        return -1;
    }
    return descriptor;
}

struct Frame {
    Directory directory;
    QString path;
    QByteArray relative;
    SpaceNode *node = nullptr;
};

void finishNode(std::vector<Frame> &frames)
{
    SpaceNode *node = frames.back().node;
    if (node) {
        std::sort(node->children.begin(), node->children.end(),
                  [](const SpaceNode &left, const SpaceNode &right) {
            return left.allocatedBytes != right.allocatedBytes
                ? left.allocatedBytes > right.allocatedBytes : left.path < right.path;
        });
        if (frames.size() > 1) {
            SpaceNode *parent = frames[frames.size() - 2].node;
            parent->logicalBytes += node->logicalBytes;
            parent->allocatedBytes += node->allocatedBytes;
            parent->fileCount += node->fileCount;
        }
    }
    frames.pop_back();
}

struct Candidate {
    QString path;
    QByteArray relative;
    struct stat information {};
};

bool sameVersion(const struct stat &left, const struct stat &right)
{
    return S_ISREG(right.st_mode) && left.st_dev == right.st_dev && left.st_ino == right.st_ino
        && left.st_size == right.st_size
        && left.st_mtim.tv_sec == right.st_mtim.tv_sec && left.st_mtim.tv_nsec == right.st_mtim.tv_nsec
        && left.st_ctim.tv_sec == right.st_ctim.tv_sec && left.st_ctim.tv_nsec == right.st_ctim.tv_nsec;
}

// Every component is reopened relative to the pinned root, with no symlinks.
// O_NONBLOCK prevents a swapped FIFO from blocking before fstat rejects it.
int openCandidate(const ScanRoot &root, const Candidate &candidate)
{
    int parent = ::dup(root.descriptor.value);
    if (parent < 0)
        return -1;
    const auto parts = candidate.relative.split('/');
    QString path = root.path;
    for (int i = 0; i < parts.size(); ++i) {
        const bool final = i == parts.size() - 1;
        const int next = ::openat(parent, parts.at(i).constData(),
                                  O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC
                                      | (final ? 0 : O_DIRECTORY));
        ::close(parent);
        if (next < 0)
            return -1;
        path = appendPath(path, parts.at(i).constData());
        struct stat actual {};
        if (::fstat(next, &actual) != 0 || !root.allowedDescriptor(next, actual)
            || root.mounts.contains(path) || (final && !sameVersion(candidate.information, actual))) {
            ::close(next);
            return -1;
        }
        parent = next;
    }
    return parent;
}

bool hashCandidate(const ScanRoot &root, const Candidate &candidate, bool sample,
                   QByteArray &digest, std::atomic_bool *cancel, Progress &progress, quint64 count)
{
    if (isCanceled(cancel))
        return false;
    Descriptor descriptor(openCandidate(root, candidate));
    if (descriptor.value < 0)
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(int(HashBlockBytes), Qt::Uninitialized);
    const qint64 size = candidate.information.st_size;
    const auto readRange = [&](qint64 start, qint64 length) {
        qint64 offset = start;
        while (offset < start + length) {
            if (isCanceled(cancel))
                return false;
            progress.report(count, candidate.path);
            if (isCanceled(cancel))
                return false;
            const qint64 desired = std::min(HashBlockBytes, start + length - offset);
            const ssize_t read = ::pread(descriptor.value, buffer.data(), size_t(desired), off_t(offset));
            if (read < 0 && errno == EINTR)
                continue;
            if (read <= 0)
                return false;
            hash.addData(buffer.constData(), int(read));
            offset += read;
        }
        return true;
    };
    bool success;
    if (sample && size > SampleBytes * 2)
        success = readRange(0, SampleBytes) && readRange(size - SampleBytes, SampleBytes);
    else
        success = readRange(0, size);
    struct stat after {};
    if (!success || isCanceled(cancel) || ::fstat(descriptor.value, &after) != 0
        || !sameVersion(candidate.information, after))
        return false;
    digest = hash.result();
    return true;
}

} // namespace

DirectoryScanResult analyzeDirectory(const QString &requested, std::atomic_bool *cancel,
                                    std::function<void(quint64, const QString &)> callback, int maxNodes)
{
    DirectoryScanResult result;
    if (isCanceled(cancel)) {
        result.canceled = true;
        return result;
    }
    ScanRoot root;
    if (!root.initialize(requested, maxNodes, result.error))
        return result;
    result.root.path = root.path;
    result.root.name = root.path == QStringLiteral("/") ? root.path : QDir(root.path).dirName();
    result.root.directory = true;
    result.root.allocatedBytes = allocatedSize(root.information);
    Directory directory = directoryFromDescriptor(::dup(root.descriptor.value));
    if (!directory) {
        result.error = QStringLiteral("无法读取文件夹内容：%1").arg(systemError(errno));
        return result;
    }
    std::vector<Frame> frames;
    frames.push_back({std::move(directory), root.path, {}, &result.root});
    std::unordered_set<Inode, InodeHash> hardLinks;
    int nodes = 1;
    Progress progress(std::move(callback));
    progress.report(0, root.path, true);
    while (!frames.empty()) {
        if (isCanceled(cancel)) {
            result.canceled = true;
            break;
        }
        Frame &current = frames.back();
        errno = 0;
        const dirent *entry = ::readdir(current.directory.get());
        if (!entry) {
            if (errno)
                ++result.skippedEntries;
            finishNode(frames);
            continue;
        }
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;
        const QString path = appendPath(current.path, entry->d_name);
        progress.report(result.scannedFiles, path);
        if (isCanceled(cancel)) {
            result.canceled = true;
            break;
        }
        struct stat information {};
        const int parent = ::dirfd(current.directory.get());
        if (::fstatat(parent, entry->d_name, &information, AT_SYMLINK_NOFOLLOW) != 0) {
            ++result.skippedEntries;
            continue;
        }
        if (!S_ISREG(information.st_mode) && !S_ISDIR(information.st_mode))
            continue;
        if (!root.allowed(parent, entry->d_name, path, information)) {
            ++result.skippedEntries;
            continue;
        }
        if (S_ISREG(information.st_mode)
            && !hardLinks.insert({information.st_dev, information.st_ino}).second)
            continue;
        if (nodes >= maxNodes) {
            result.truncated = true;
            break;
        }
        if (S_ISDIR(information.st_mode) && frames.size() >= MaximumDepth) {
            ++result.skippedEntries;
            result.truncated = true;
            continue;
        }
        SpaceNode node;
        node.name = QFile::decodeName(entry->d_name);
        node.path = path;
        node.directory = S_ISDIR(information.st_mode);
        node.allocatedBytes = allocatedSize(information);
        if (node.directory) {
            const int child = openChildDirectory(parent, entry->d_name, information, root);
            if (child < 0) {
                ++result.skippedEntries;
                continue;
            }
            Directory childDirectory = directoryFromDescriptor(child);
            if (!childDirectory) {
                ++result.skippedEntries;
                continue;
            }
            current.node->children.append(std::move(node));
            SpaceNode *childNode = &current.node->children.last();
            ++nodes;
            frames.push_back({std::move(childDirectory), path, {}, childNode});
        } else {
            node.logicalBytes = logicalSize(information);
            node.fileCount = 1;
            current.node->logicalBytes += node.logicalBytes;
            current.node->allocatedBytes += node.allocatedBytes;
            ++current.node->fileCount;
            current.node->children.append(std::move(node));
            ++nodes;
            ++result.scannedFiles;
        }
    }
    while (!frames.empty())
        finishNode(frames);
    if (isCanceled(cancel))
        result.canceled = true;
    return result;
}

DuplicateScanResult findDuplicates(const QString &requested, quint64 minimumBytes,
                                   std::atomic_bool *cancel,
                                   std::function<void(quint64, const QString &)> callback, int maxFiles)
{
    DuplicateScanResult result;
    if (isCanceled(cancel)) {
        result.canceled = true;
        return result;
    }
    ScanRoot root;
    if (!root.initialize(requested, maxFiles, result.error))
        return result;
    Directory directory = directoryFromDescriptor(::dup(root.descriptor.value));
    if (!directory) {
        result.error = QStringLiteral("无法读取文件夹内容：%1").arg(systemError(errno));
        return result;
    }
    std::vector<Frame> frames;
    frames.push_back({std::move(directory), root.path, {}, nullptr});
    QVector<Candidate> candidates;
    std::unordered_set<Inode, InodeHash> hardLinks;
    Progress progress(std::move(callback));
    progress.report(0, root.path, true);
    int directories = 1;
    while (!frames.empty() && !isCanceled(cancel)) {
        Frame &current = frames.back();
        errno = 0;
        const dirent *entry = ::readdir(current.directory.get());
        if (!entry) {
            if (errno)
                ++result.skippedEntries;
            frames.pop_back();
            continue;
        }
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;
        const QString path = appendPath(current.path, entry->d_name);
        progress.report(result.scannedFiles, path);
        if (isCanceled(cancel))
            break;
        struct stat information {};
        const int parent = ::dirfd(current.directory.get());
        if (::fstatat(parent, entry->d_name, &information, AT_SYMLINK_NOFOLLOW) != 0) {
            ++result.skippedEntries;
            continue;
        }
        if (!S_ISREG(information.st_mode) && !S_ISDIR(information.st_mode))
            continue;
        if (!root.allowed(parent, entry->d_name, path, information)) {
            ++result.skippedEntries;
            continue;
        }
        const QByteArray relative = current.relative.isEmpty()
            ? QByteArray(entry->d_name) : current.relative + '/' + entry->d_name;
        if (S_ISDIR(information.st_mode)) {
            if (frames.size() >= MaximumDepth) {
                ++result.skippedEntries;
                result.truncated = true;
                continue;
            }
            if (directories >= maxFiles) {
                result.truncated = true;
                break;
            }
            const int child = openChildDirectory(parent, entry->d_name, information, root);
            if (child < 0) {
                ++result.skippedEntries;
                continue;
            }
            Directory childDirectory = directoryFromDescriptor(child);
            if (!childDirectory) {
                ++result.skippedEntries;
                continue;
            }
            ++directories;
            frames.push_back({std::move(childDirectory), path, relative, nullptr});
            continue;
        }
        if (!hardLinks.insert({information.st_dev, information.st_ino}).second)
            continue;
        if (result.scannedFiles >= quint64(maxFiles)) {
            result.truncated = true;
            break;
        }
        ++result.scannedFiles;
        if (logicalSize(information) >= std::max(quint64(1), minimumBytes))
            candidates.append({path, relative, information});
    }
    // Close directory descriptors before opening candidates for content reads.
    frames.clear();
    if (isCanceled(cancel)) {
        result.canceled = true;
        return result;
    }

    QHash<quint64, QVector<int>> sizes;
    for (int i = 0; i < candidates.size(); ++i)
        sizes[logicalSize(candidates.at(i).information)].append(i);
    QVector<quint64> orderedSizes = sizes.keys().toVector();
    std::sort(orderedSizes.begin(), orderedSizes.end(), std::greater<quint64>());
    bool firstSample = true;
    bool firstFullHash = true;
    for (quint64 size : orderedSizes) {
        if (isCanceled(cancel))
            break;
        const QVector<int> indices = sizes.value(size);
        if (indices.size() < 2)
            continue;
        QHash<QByteArray, QVector<int>> samples;
        for (int index : indices) {
            if (isCanceled(cancel))
                break;
            const Candidate &candidate = candidates.at(index);
            progress.report(result.scannedFiles, candidate.path, firstSample);
            firstSample = false;
            QByteArray digest;
            if (hashCandidate(root, candidate, true, digest, cancel, progress, result.scannedFiles))
                samples[digest].append(index);
            else if (!isCanceled(cancel))
                ++result.skippedEntries;
        }
        const auto sampleGroups = samples.values();
        for (const auto &sample : sampleGroups) {
            if (sample.size() < 2 || isCanceled(cancel))
                continue;
            QHash<QByteArray, QVector<int>> complete;
            for (int index : sample) {
                if (isCanceled(cancel))
                    break;
                const Candidate &candidate = candidates.at(index);
                progress.report(result.scannedFiles, candidate.path, firstFullHash);
                firstFullHash = false;
                QByteArray digest;
                if (hashCandidate(root, candidate, false, digest, cancel, progress, result.scannedFiles)) {
                    ++result.hashedFiles;
                    complete[digest].append(index);
                } else if (!isCanceled(cancel)) {
                    ++result.skippedEntries;
                }
            }
            for (auto full = complete.cbegin(); full != complete.cend(); ++full) {
                if (full.value().size() < 2 || isCanceled(cancel))
                    continue;
                DuplicateGroup group;
                group.bytes = size;
                group.sha256 = full.key();
                // Recheck every published member: a file may have changed while
                // its peers were hashed. The report is still a read-only snapshot.
                for (int index : full.value()) {
                    if (isCanceled(cancel))
                        break;
                    Descriptor descriptor(openCandidate(root, candidates.at(index)));
                    if (descriptor.value >= 0)
                        group.paths.append(candidates.at(index).path);
                    else
                        ++result.skippedEntries;
                }
                if (group.paths.size() >= 2 && !isCanceled(cancel)) {
                    std::sort(group.paths.begin(), group.paths.end());
                    result.redundantBytes += size * quint64(group.paths.size() - 1);
                    result.groups.append(std::move(group));
                }
            }
        }
    }
    result.canceled = isCanceled(cancel);
    std::sort(result.groups.begin(), result.groups.end(), [](const DuplicateGroup &left, const DuplicateGroup &right) {
        const quint64 leftRedundancy = left.bytes * quint64(left.paths.size() - 1);
        const quint64 rightRedundancy = right.bytes * quint64(right.paths.size() - 1);
        return leftRedundancy != rightRedundancy ? leftRedundancy > rightRedundancy
                                               : left.paths.first() < right.paths.first();
    });
    return result;
}
