#include "filescanner.h"

#include <QDir>
#include <QFile>
#include <QSet>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/stat.h>
#include <memory>
#include <queue>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

namespace {

struct DirectoryCloser {
    void operator()(DIR *directory) const { if (directory) ::closedir(directory); }
};

using Directory = std::unique_ptr<DIR, DirectoryCloser>;

struct DirectoryFrame {
    Directory directory;
    QString path;
};

struct Inode {
    dev_t device;
    ino_t number;

    bool operator==(const Inode &other) const
    {
        return device == other.device && number == other.number;
    }
};

struct InodeHash {
    std::size_t operator()(const Inode &inode) const
    {
        return std::hash<quint64>{}(quint64(inode.device))
            ^ (std::hash<quint64>{}(quint64(inode.number)) << 1);
    }
};

// Used both for the final descending order and the bounded heap. The heap's
// root is the least useful retained file: smallest size, then greatest path.
struct BetterFile {
    bool operator()(const FileEntry &left, const FileEntry &right) const
    {
        return left.bytes != right.bytes ? left.bytes > right.bytes : left.path < right.path;
    }
};

QString systemError(int error)
{
    return QString::fromLocal8Bit(std::strerror(error));
}

// Opening every path component separately also rejects symlinks in the root
// path's ancestors. open(root, O_NOFOLLOW) alone only protects the last part.
int openDirectoryWithoutSymlinks(const QString &path)
{
    int descriptor = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0)
        return -1;

    const auto parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const auto &part : parts) {
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
    if (::syscall(SYS_statx, descriptor, name,
                  AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH,
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

QByteArray unescapeMountPath(QByteArray path)
{
    // mountinfo escapes spaces, tabs, newlines and backslashes as octal codes.
    path.replace("\\040", " ");
    path.replace("\\011", "\t");
    path.replace("\\012", "\n");
    path.replace("\\134", "\\");
    return path;
}

QSet<QString> mountPaths(bool &available)
{
    QSet<QString> paths;
    QFile information(QStringLiteral("/proc/self/mountinfo"));
    available = information.open(QIODevice::ReadOnly);
    if (!available)
        return paths;
    // procfs reports a zero file size even when readAll() returns content.
    const auto lines = information.readAll().split('\n');
    for (const auto &line : lines) {
        const auto fields = line.split(' ');
        if (fields.size() >= 5)
            paths.insert(QFile::decodeName(unescapeMountPath(fields.at(4))));
    }
    return paths;
}

} // namespace

ScanResult scanLargeFiles(const QString &root, quint64 minimumBytes, int maxResults,
                         std::atomic_bool *cancel,
                         std::function<void(quint64, const QString &)> progress)
{
    ScanResult result;
    const auto canceled = [cancel] {
        return cancel && cancel->load(std::memory_order_relaxed);
    };
    if (canceled()) {
        result.canceled = true;
        return result;
    }
    if (root.trimmed().isEmpty()) {
        result.error = QStringLiteral("请选择要扫描的文件夹。");
        return result;
    }
    if (maxResults <= 0) {
        result.error = QStringLiteral("显示文件数量必须大于 0。");
        return result;
    }
    if (root.contains(QChar::Null)) {
        result.error = QStringLiteral("文件夹路径包含无效字符。");
        return result;
    }

    const QString absoluteRoot = QDir::cleanPath(QDir(root).absolutePath());
    const int rootDescriptor = openDirectoryWithoutSymlinks(absoluteRoot);
    if (rootDescriptor < 0) {
        result.error = QStringLiteral("无法打开文件夹 %1：%2。请选择真实且可读取的文件夹，路径不能包含符号链接。")
                           .arg(absoluteRoot, systemError(errno));
        return result;
    }

    struct stat rootStat {};
    if (::fstat(rootDescriptor, &rootStat) != 0) {
        const int savedError = errno;
        ::close(rootDescriptor);
        result.error = QStringLiteral("无法读取文件夹信息：%1").arg(systemError(savedError));
        return result;
    }

    quint64 rootMount = 0;
    const bool hasMountId = mountId(rootDescriptor, "", rootMount);
    // Includes same-device bind mounts on systems without STATX_MNT_ID.
    bool hasMountPaths = false;
    const QSet<QString> mounts = mountPaths(hasMountPaths);
    if (!hasMountId && !hasMountPaths) {
        ::close(rootDescriptor);
        result.error = QStringLiteral("无法读取文件系统挂载边界，请检查 /proc 是否可访问。");
        return result;
    }
    Directory rootDirectory(::fdopendir(rootDescriptor));
    if (!rootDirectory) {
        const int savedError = errno;
        ::close(rootDescriptor);
        result.error = QStringLiteral("无法读取文件夹内容：%1").arg(systemError(savedError));
        return result;
    }

    std::vector<DirectoryFrame> directories;
    directories.push_back({std::move(rootDirectory), absoluteRoot});
    std::unordered_set<Inode, InodeHash> hardLinks;
    std::priority_queue<FileEntry, std::vector<FileEntry>, BetterFile> largestFiles;
    using Clock = std::chrono::steady_clock;
    auto lastProgress = Clock::now() - std::chrono::milliseconds(150);
    const auto reportProgress = [&](const QString &path) {
        const auto now = Clock::now();
        if (progress && now - lastProgress >= std::chrono::milliseconds(150)) {
            lastProgress = now;
            progress(result.scannedFiles, path);
        }
    };
    reportProgress(absoluteRoot);

    while (!directories.empty()) {
        if (canceled()) {
            result.canceled = true;
            break;
        }

        auto &current = directories.back();
        errno = 0;
        const dirent *entry = ::readdir(current.directory.get());
        if (!entry) {
            if (errno)
                ++result.skippedEntries;
            directories.pop_back();
            continue;
        }
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;

        const QString path = current.path == QStringLiteral("/")
            ? current.path + QFile::decodeName(entry->d_name)
            : current.path + QLatin1Char('/') + QFile::decodeName(entry->d_name);
        reportProgress(path);
        if (canceled()) {
            result.canceled = true;
            break;
        }
        const int parentDescriptor = ::dirfd(current.directory.get());
        struct stat information {};
        if (::fstatat(parentDescriptor, entry->d_name, &information, AT_SYMLINK_NOFOLLOW) != 0) {
            ++result.skippedEntries;
            continue;
        }
        // Never follow links and do not inspect special files or other devices.
        if (S_ISLNK(information.st_mode) || (!S_ISDIR(information.st_mode) && !S_ISREG(information.st_mode)))
            continue;
        if (information.st_dev != rootStat.st_dev) {
            ++result.skippedEntries;
            continue;
        }

        quint64 entryMount = 0;
        if ((hasMountId && (!mountId(parentDescriptor, entry->d_name, entryMount) || entryMount != rootMount))
            || mounts.contains(path)) {
            ++result.skippedEntries;
            continue;
        }

        if (S_ISDIR(information.st_mode)) {
            const int childDescriptor = ::openat(parentDescriptor, entry->d_name,
                                                 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (childDescriptor < 0) {
                ++result.skippedEntries;
                continue;
            }
            struct stat openedInformation {};
            quint64 openedMount = 0;
            const bool wrongDirectory = ::fstat(childDescriptor, &openedInformation) != 0
                || openedInformation.st_dev != rootStat.st_dev
                || openedInformation.st_ino != information.st_ino
                || (hasMountId && (!mountId(childDescriptor, "", openedMount) || openedMount != rootMount));
            if (wrongDirectory) {
                ::close(childDescriptor);
                ++result.skippedEntries;
                continue;
            }
            Directory childDirectory(::fdopendir(childDescriptor));
            if (!childDirectory) {
                ::close(childDescriptor);
                ++result.skippedEntries;
                continue;
            }
            directories.push_back({std::move(childDirectory), path});
            reportProgress(path);
            continue;
        }

        // Files with only one link cannot be duplicates. Avoid retaining an
        // inode record for every file in a multi-million-file directory tree.
        if (information.st_nlink > 1
            && !hardLinks.insert({information.st_dev, information.st_ino}).second)
            continue;

        ++result.scannedFiles;
        const quint64 bytes = information.st_size > 0 ? quint64(information.st_size) : 0;
        if (bytes >= minimumBytes) {
            ++result.matchingFiles;
            result.totalMatchingBytes += bytes;
            FileEntry file;
            file.path = path;
            file.bytes = bytes;
            file.allocatedBytes = information.st_blocks > 0 ? quint64(information.st_blocks) * 512 : 0;
            file.modified = QDateTime::fromMSecsSinceEpoch(
                qint64(information.st_mtim.tv_sec) * 1000 + information.st_mtim.tv_nsec / 1000000,
                Qt::UTC);
            if (largestFiles.size() < std::size_t(maxResults)) {
                largestFiles.push(std::move(file));
            } else if (BetterFile{}(file, largestFiles.top())) {
                largestFiles.pop();
                largestFiles.push(std::move(file));
            }
        }
        reportProgress(path);
    }

    result.files.reserve(int(largestFiles.size()));
    while (!largestFiles.empty()) {
        result.files.append(largestFiles.top());
        largestFiles.pop();
    }
    std::sort(result.files.begin(), result.files.end(), BetterFile{});
    if (canceled())
        result.canceled = true;
    return result;
}
