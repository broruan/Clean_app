#include "developerbackend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryFile>
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
struct CloseDirectory { void operator()(DIR *d) const { if (d) ::closedir(d); } };
using Directory = std::unique_ptr<DIR, CloseDirectory>;
struct Identity {
    dev_t device; ino_t inode;
    bool operator==(const Identity &o) const { return device == o.device && inode == o.inode; }
};
struct IdentityHash {
    size_t operator()(const Identity &v) const {
        return std::hash<quint64>{}(quint64(v.device)) ^ (std::hash<quint64>{}(quint64(v.inode)) << 1);
    }
};
struct Size {
    quint64 bytes = 0, allocated = 0, files = 0;
    void add(const Size &other) { bytes += other.bytes; allocated += other.allocated; files += other.files; }
    Size without(const Size &other) const { return {bytes - other.bytes, allocated - other.allocated, files - other.files}; }
};
struct Node {
    QString path, name;
    int parent = -1, depth = 0;
    QVector<int> children;
    Size total, data;
    bool package = false, hasPackage = false, builtByColcon = false, colconPrefix = false;
    bool setup = false, localSetup = false, setupUtility = false;
    bool metadata = false, recording = false, condaHistory = false, condaJson = false;
    bool ccacheConfig = false, stats = false;
};
struct Frame { Directory directory; int node; };

int openRoot(const QString &path)
{
    int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    for (const QString &part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        const QByteArray name = QFile::encodeName(part);
        const int next = ::openat(fd, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        const int saved = errno;
        ::close(fd);
        if (next < 0) { errno = saved; return -1; }
        fd = next;
    }
    return fd;
}
bool mountId(int fd, const char *name, quint64 &id)
{
#if defined(SYS_statx) && defined(STATX_MNT_ID)
    struct statx st {};
    if (::syscall(SYS_statx, fd, name, AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH, STATX_MNT_ID, &st) == 0
        && (st.stx_mask & STATX_MNT_ID)) { id = st.stx_mnt_id; return true; }
#else
    Q_UNUSED(fd) Q_UNUSED(name) Q_UNUSED(id)
#endif
    return false;
}
QSet<QString> mountPaths(bool &available)
{
    QFile file(QStringLiteral("/proc/self/mountinfo"));
    available = file.open(QIODevice::ReadOnly);
    QSet<QString> paths;
    if (!available) return paths;
    for (const QByteArray &line : file.readAll().split('\n')) {
        const auto fields = line.split(' ');
        if (fields.size() < 5) continue;
        QByteArray value = fields.at(4);
        value.replace("\\040", " "); value.replace("\\011", "\t");
        value.replace("\\012", "\n"); value.replace("\\134", "\\");
        paths.insert(QFile::decodeName(value));
    }
    return paths;
}
QByteArray readMarker(int parent, const char *name, const struct stat &expected, int limit)
{
    const int fd = ::openat(parent, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return {};
    struct stat actual {};
    QByteArray bytes;
    if (::fstat(fd, &actual) == 0 && S_ISREG(actual.st_mode) && actual.st_dev == expected.st_dev
        && actual.st_ino == expected.st_ino && actual.st_size >= 0 && actual.st_size <= limit) {
        bytes.resize(int(actual.st_size));
        const ssize_t count = ::read(fd, bytes.data(), size_t(bytes.size()));
        if (count < 0) bytes.clear(); else bytes.resize(int(count));
    }
    ::close(fd);
    return bytes;
}
bool isDataName(const QString &name)
{
    const QString ext = QFileInfo(name).suffix().toLower();
    return ext == QStringLiteral("pcd") || ext == QStringLiteral("bag") || ext == QStringLiteral("db3")
        || ext == QStringLiteral("mcap") || ext == QStringLiteral("ply") || ext == QStringLiteral("las")
        || ext == QStringLiteral("laz");
}
QString absolute(const QString &path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }
int childNamed(const std::vector<Node> &nodes, int index, const QString &name)
{
    for (int child : nodes.at(size_t(index)).children)
        if (nodes.at(size_t(child)).name == name) return child;
    return -1;
}
QString csvString(QString value)
{
    // Some spreadsheet importers trim leading whitespace before parsing formulas.
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && QStringLiteral("=+-@").contains(trimmed.front())) value.prepend(QLatin1Char('\''));
    if (value.startsWith(QLatin1Char('\t')) || value.startsWith(QLatin1Char('\r'))) value.prepend(QLatin1Char('\''));
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}
DeveloperEntry describe(DeveloperCategory category, const Node &node, const QString &evidence)
{
    DeveloperEntry e;
    e.category = category; e.path = node.path; e.name = node.name; e.evidence = evidence;
    switch (category) {
    case DeveloperCategory::RosBuild:
        e.purpose = QStringLiteral("colcon 工作空间编译中间产物与构建配置。");
        e.rebuildability = QStringLiteral("通常可重建；需保留源码、依赖、构建参数与工具链。");
        e.suggestion = QStringLiteral("先确认没有编译任务，并记录构建参数；本页仅分析，请按项目说明管理构建目录。"); break;
    case DeveloperCategory::RosInstall:
        e.purpose = QStringLiteral("colcon 工作空间安装产物，运行节点时可能正在使用。");
        e.rebuildability = QStringLiteral("通常可重新编译生成；删除会使当前工作空间的可执行程序与环境失效。");
        e.suggestion = QStringLiteral("先确认源码与依赖完整，并停止依赖此目录的进程，再决定是否重建。"); break;
    case DeveloperCategory::RosLog:
        e.purpose = QStringLiteral("已识别 colcon 工作空间中的构建日志目录。");
        e.rebuildability = QStringLiteral("日志记录历史编译过程，历史内容无法通过重新编译还原。");
        e.suggestion = QStringLiteral("先保留故障排查所需日志，确认目录内容后按项目保留策略处理。"); break;
    case DeveloperCategory::Rosbag:
        e.purpose = QStringLiteral("ROS 数据录制目录，包含 metadata.yaml 与 DB3 / MCAP 记录。");
        e.rebuildability = QStringLiteral("原始采集数据；通常不可重建，不属于缓存。");
        e.suggestion = QStringLiteral("保留原始数据；需要腾空间时先备份或迁移，并验证备份可回放。"); break;
    case DeveloperCategory::Data:
        e.purpose = QStringLiteral("同一目录内的点云、录制数据或大于阈值的普通文件，按目录聚合。");
        e.rebuildability = QStringLiteral("可能包含原始采集数据、模型或数据集；不能推定可重建。");
        e.suggestion = QStringLiteral("打开目录确认来源与用途；优先备份或迁移，不提供自动清理。"); break;
    case DeveloperCategory::PipCache:
        e.purpose = QStringLiteral("pip 的下载与 wheel 构建缓存。");
        e.rebuildability = QStringLiteral("一般可重新下载或构建；离线环境和私有软件源可能无法恢复。");
        e.suggestion = QStringLiteral("确认网络、私有源与离线需求后，使用 pip 自带缓存管理命令处理。"); break;
    case DeveloperCategory::NpmCache:
        e.purpose = QStringLiteral("npm 内容寻址缓存，不包含项目 node_modules 的整体统计。");
        e.rebuildability = QStringLiteral("一般可重新获取；网络、已下架依赖与离线需求可能限制恢复。");
        e.suggestion = QStringLiteral("先使用 npm 自带缓存校验功能，保留离线开发所需内容。"); break;
    case DeveloperCategory::Ccache:
        e.purpose = QStringLiteral("ccache 编译结果缓存，可加快重复编译。");
        e.rebuildability = QStringLiteral("通常可重新编译生成；清理后首次编译耗时可能增加。");
        e.suggestion = QStringLiteral("确认无编译任务后，通过 ccache 自带容量与清理策略管理。"); break;
    case DeveloperCategory::Conda:
        e.purpose = QStringLiteral("包含 conda-meta 包记录的 Conda 依赖环境。");
        e.rebuildability = QStringLiteral("这是可运行的依赖环境，不是缓存；重建取决于环境清单、渠道与包可用性。");
        e.suggestion = QStringLiteral("确认环境用途，导出依赖清单并保留项目约束，再用 Conda 环境管理功能处理。"); break;
    }
    return e;
}
} // namespace

QString developerCategoryName(DeveloperCategory category)
{
    switch (category) {
    case DeveloperCategory::RosBuild: return QStringLiteral("ROS 构建产物");
    case DeveloperCategory::RosInstall: return QStringLiteral("ROS 安装产物");
    case DeveloperCategory::RosLog: return QStringLiteral("ROS 构建日志");
    case DeveloperCategory::Rosbag: return QStringLiteral("ROS 原始录制");
    case DeveloperCategory::Data: return QStringLiteral("点云 / 大数据");
    case DeveloperCategory::PipCache: return QStringLiteral("pip 缓存");
    case DeveloperCategory::NpmCache: return QStringLiteral("npm 缓存");
    case DeveloperCategory::Ccache: return QStringLiteral("ccache 缓存");
    case DeveloperCategory::Conda: return QStringLiteral("Conda 环境");
    }
    return {};
}

DeveloperScanResult scanDeveloperUsage(const DeveloperScanOptions &options, std::atomic_bool *cancel,
                                      DeveloperProgress progress)
{
    DeveloperScanResult result;
    const auto canceled = [cancel] { return cancel && cancel->load(std::memory_order_relaxed); };
    if (canceled()) { result.canceled = true; return result; }
    if (options.root.trimmed().isEmpty() || options.root.contains(QChar::Null)) {
        result.error = QStringLiteral("请选择真实且可读取的文件夹。"); return result;
    }
    if (!options.maxEntries || options.maxDirectories <= 0 || options.maxDepth < 0 || options.maxResults <= 0) {
        result.error = QStringLiteral("扫描数量、目录深度与显示上限设置无效。"); return result;
    }
    result.root = absolute(options.root);
    const QString home = absolute(options.homePath.isEmpty() ? QDir::homePath() : options.homePath);
    QString cache = options.cachePath;
    if (cache.isEmpty()) cache = qEnvironmentVariable("XDG_CACHE_HOME");
    if (!QDir::isAbsolutePath(cache)) cache = home + QStringLiteral("/.cache");
    cache = QDir::cleanPath(cache);
    const int rootFd = openRoot(result.root);
    if (rootFd < 0) {
        result.error = QStringLiteral("无法打开 %1：%2。路径不能包含符号链接。")
            .arg(result.root, QString::fromLocal8Bit(std::strerror(errno))); return result;
    }
    struct stat rootStat {};
    if (::fstat(rootFd, &rootStat) != 0) {
        ::close(rootFd); result.error = QStringLiteral("无法读取扫描根目录信息。"); return result;
    }
    quint64 rootMount = 0;
    const bool hasMountId = mountId(rootFd, "", rootMount);
    bool hasMountPaths = false;
    const QSet<QString> mounts = mountPaths(hasMountPaths);
    if (!hasMountId && !hasMountPaths) {
        ::close(rootFd); result.error = QStringLiteral("无法确认挂载边界，请检查 /proc 是否可访问。"); return result;
    }
    Directory rootDirectory(::fdopendir(rootFd));
    if (!rootDirectory) { ::close(rootFd); result.error = QStringLiteral("无法读取根目录内容。"); return result; }
    std::vector<Node> nodes;
    Node root; root.path = result.root; root.name = QFileInfo(result.root).fileName();
    if (root.name.isEmpty()) root.name = result.root;
    nodes.push_back(root);
    std::vector<Frame> stack;
    stack.push_back({std::move(rootDirectory), 0});
    result.scannedDirectories = 1;
    std::unordered_set<Identity, IdentityHash> hardlinks;
    quint64 inspected = 0;
    using Clock = std::chrono::steady_clock;
    auto lastProgress = Clock::now() - std::chrono::milliseconds(150);
    auto report = [&](const QString &path) {
        const auto now = Clock::now();
        if (progress && now - lastProgress >= std::chrono::milliseconds(150)) {
            lastProgress = now; progress(result.scannedFiles, path);
        }
    };
    report(result.root);
    while (!stack.empty()) {
        if (canceled()) { result.canceled = true; break; }
        Frame &frame = stack.back();
        const int currentIndex = frame.node;
        errno = 0;
        const dirent *entry = ::readdir(frame.directory.get());
        if (!entry) {
            if (errno) ++result.skippedEntries;
            stack.pop_back(); continue;
        }
        if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
        if (++inspected > options.maxEntries) { result.limited = true; break; }
        const int parent = ::dirfd(frame.directory.get());
        const QString name = QFile::decodeName(entry->d_name);
        const QString path = nodes[size_t(currentIndex)].path == QStringLiteral("/")
            ? QLatin1Char('/') + name : nodes[size_t(currentIndex)].path + QLatin1Char('/') + name;
        report(path);
        struct stat st {};
        if (::fstatat(parent, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) { ++result.skippedEntries; continue; }
        if (S_ISLNK(st.st_mode)) { ++result.skippedLinks; continue; }
        if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) { ++result.skippedEntries; continue; }
        quint64 entryMount = 0;
        if (st.st_dev != rootStat.st_dev || (hasMountId && (!mountId(parent, entry->d_name, entryMount) || entryMount != rootMount))
            || mounts.contains(path)) { ++result.skippedEntries; continue; }
        if (S_ISDIR(st.st_mode)) {
            if (nodes[size_t(currentIndex)].depth >= options.maxDepth || int(nodes.size()) >= options.maxDirectories) {
                ++result.skippedEntries; result.limited = true; continue;
            }
            const int fd = ::openat(parent, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (fd < 0) { ++result.skippedEntries; continue; }
            struct stat opened {};
            quint64 openedMount = 0;
            if (::fstat(fd, &opened) != 0 || opened.st_dev != st.st_dev || opened.st_ino != st.st_ino
                || (hasMountId && (!mountId(fd, "", openedMount) || openedMount != rootMount))) {
                ::close(fd); ++result.skippedEntries; continue;
            }
            Directory directory(::fdopendir(fd));
            if (!directory) { ::close(fd); ++result.skippedEntries; continue; }
            const int index = int(nodes.size());
            Node child; child.path = path; child.name = name; child.parent = currentIndex;
            child.depth = nodes[size_t(currentIndex)].depth + 1;
            nodes[size_t(currentIndex)].children.append(index);
            nodes.push_back(child);
            stack.push_back({std::move(directory), index});
            ++result.scannedDirectories;
            continue;
        }
        Node &node = nodes[size_t(currentIndex)];
        // Structural evidence is independent of hard-link accounting order.
        if (name == QStringLiteral("package.xml")) node.package = readMarker(parent, entry->d_name, st, 65536).contains("<package");
        if (name == QStringLiteral(".built_by")) node.builtByColcon = readMarker(parent, entry->d_name, st, 1024).trimmed() == "colcon";
        if (name == QStringLiteral("colcon_command_prefix_build.sh")) node.colconPrefix = true;
        if (name == QStringLiteral("setup.bash")) node.setup = true;
        if (name == QStringLiteral("local_setup.bash")) node.localSetup = true;
        if (name == QStringLiteral("_local_setup_util_sh.py")) node.setupUtility = true;
        if (name == QStringLiteral("metadata.yaml")) node.metadata = true;
        const QString suffix = QFileInfo(name).suffix().toLower();
        if (suffix == QStringLiteral("db3") || suffix == QStringLiteral("mcap")) node.recording = true;
        if (name == QStringLiteral("history")) node.condaHistory = true;
        if (suffix == QStringLiteral("json")) node.condaJson = true;
        if (name == QStringLiteral("ccache.conf")) node.ccacheConfig = true;
        if (name == QStringLiteral("stats")) node.stats = true;
        if (st.st_nlink > 1 && !hardlinks.insert({st.st_dev, st.st_ino}).second) continue;
        const Size size{st.st_size > 0 ? quint64(st.st_size) : 0,
                        st.st_blocks > 0 ? quint64(st.st_blocks) * 512 : 0, 1};
        node.total.add(size);
        if (isDataName(name) || size.bytes >= options.largeFileBytes) node.data.add(size);
        ++result.scannedFiles;
        result.totalAllocatedBytes += size.allocated;
    }
    stack.clear(); // Close traversal descriptors before constructing results.
    if (canceled()) result.canceled = true;
    // A single postorder pass accumulates subtree sizes and source evidence.
    for (int i = int(nodes.size()) - 1; i >= 0; --i) {
        Node &node = nodes[size_t(i)]; node.hasPackage = node.hasPackage || node.package;
        if (node.parent >= 0) {
            nodes[size_t(node.parent)].total.add(node.total);
            nodes[size_t(node.parent)].hasPackage = nodes[size_t(node.parent)].hasPackage || node.hasPackage;
        }
    }
    struct Candidate { bool selected = false; DeveloperCategory category = DeveloperCategory::Data; QString evidence; };
    std::vector<Candidate> candidates(nodes.size());
    auto select = [&](int index, DeveloperCategory category, const QString &evidence) {
        if (index >= 0 && !candidates[size_t(index)].selected) candidates[size_t(index)] = {true, category, evidence};
    };
    // Protected data and dependency environments take precedence over artifacts.
    for (int i = 0; i < int(nodes.size()); ++i) {
        const Node &node = nodes[size_t(i)];
        if (node.metadata && node.recording)
            select(i, DeveloperCategory::Rosbag, QStringLiteral("同一目录包含 metadata.yaml 与 .db3 / .mcap 普通文件"));
        const int meta = childNamed(nodes, i, QStringLiteral("conda-meta"));
        if (meta >= 0 && (nodes[size_t(meta)].condaHistory || nodes[size_t(meta)].condaJson))
            select(i, DeveloperCategory::Conda, QStringLiteral("conda-meta 目录内包含 history 或 JSON 包记录"));
    }
    for (int i = 0; i < int(nodes.size()); ++i) {
        const Node &node = nodes[size_t(i)];
        const int source = childNamed(nodes, i, QStringLiteral("src"));
        const int build = childNamed(nodes, i, QStringLiteral("build"));
        const int install = childNamed(nodes, i, QStringLiteral("install"));
        const int log = childNamed(nodes, i, QStringLiteral("log"));
        bool buildMarker = build >= 0 && nodes[size_t(build)].builtByColcon;
        if (build >= 0) {
            for (int child : nodes[size_t(build)].children) buildMarker = buildMarker || nodes[size_t(child)].colconPrefix;
        }
        const bool sourceMarker = source >= 0 && nodes[size_t(source)].hasPackage;
        if (sourceMarker || buildMarker) {
            const QString proof = (sourceMarker ? QStringLiteral("src 子树中包含 package.xml 的 <package> 结构")
                                                 : QStringLiteral("build 中包含 colcon 构建标记"))
                + QStringLiteral("；工作空间：") + node.path;
            // A ROS source tree does not establish that an arbitrary sibling
            // named build is generated. Require its own colcon provenance.
            if (buildMarker)
                select(build, DeveloperCategory::RosBuild, proof + QStringLiteral("；build 含可验证 colcon 标记"));
            // A valid workspace alone does not prove arbitrary install/log
            // contents are generated; keep the explanation deliberately scoped.
            if (install >= 0 && nodes[size_t(install)].setup && nodes[size_t(install)].localSetup && nodes[size_t(install)].setupUtility)
                select(install, DeveloperCategory::RosInstall, proof + QStringLiteral("；install 含三项 colcon 环境脚本"));
            if (log >= 0 && buildMarker)
                select(log, DeveloperCategory::RosLog, proof);
        }
        if (node.path == cache + QStringLiteral("/pip")
            && (childNamed(nodes, i, QStringLiteral("http-v2")) >= 0 || childNamed(nodes, i, QStringLiteral("http")) >= 0
                || childNamed(nodes, i, QStringLiteral("wheels")) >= 0 || childNamed(nodes, i, QStringLiteral("selfcheck")) >= 0))
            select(i, DeveloperCategory::PipCache, QStringLiteral("当前用户缓存根下 pip 目录，含 pip 缓存结构"));
        if (node.path == home + QStringLiteral("/.npm/_cacache")
            && (childNamed(nodes, i, QStringLiteral("content-v2")) >= 0 || childNamed(nodes, i, QStringLiteral("index-v5")) >= 0))
            select(i, DeveloperCategory::NpmCache, QStringLiteral("当前用户 .npm/_cacache，含内容寻址缓存结构"));
        if (node.path == cache + QStringLiteral("/ccache") || node.path == home + QStringLiteral("/.ccache")) {
            bool marker = node.ccacheConfig;
            for (int child : node.children) {
                const Node &part = nodes[size_t(child)];
                if (part.name.size() == 1 && QStringLiteral("0123456789abcdef").contains(part.name) && part.stats) marker = true;
            }
            if (marker) select(i, DeveloperCategory::Ccache, QStringLiteral("当前用户 ccache 标准位置，含配置或分片统计文件"));
        }
    }
    std::vector<Size> covered(nodes.size());
    for (int i = int(nodes.size()) - 1; i >= 0; --i) {
        const Node &node = nodes[size_t(i)];
        const Candidate &candidate = candidates[size_t(i)];
        if (candidate.selected || node.data.files) {
            DeveloperEntry entry = describe(candidate.selected ? candidate.category : DeveloperCategory::Data, node,
                candidate.selected ? candidate.evidence : QStringLiteral("按文件扩展名或逻辑大小阈值 %1 字节识别；仅统计本目录直接包含的匹配文件").arg(options.largeFileBytes));
            const Size size = candidate.selected ? node.total.without(covered[size_t(i)]) : node.data;
            entry.bytes = size.bytes; entry.allocatedBytes = size.allocated; entry.fileCount = size.files;
            entry.excludesNestedGroups = candidate.selected && covered[size_t(i)].files > 0;
            if (entry.fileCount) {
                result.entries.append(entry);
                covered[size_t(i)].add(size);
                result.identifiedAllocatedBytes += size.allocated;
            }
        }
        if (node.parent >= 0) covered[size_t(node.parent)].add(covered[size_t(i)]);
    }
    std::sort(result.entries.begin(), result.entries.end(), [](const DeveloperEntry &a, const DeveloperEntry &b) {
        return a.allocatedBytes != b.allocatedBytes ? a.allocatedBytes > b.allocatedBytes : a.path < b.path;
    });
    if (result.entries.size() > options.maxResults) {
        result.hiddenEntries = result.entries.size() - options.maxResults;
        for (int i = options.maxResults; i < result.entries.size(); ++i) result.hiddenAllocatedBytes += result.entries.at(i).allocatedBytes;
        result.entries.resize(options.maxResults);
    }
    if (result.limited) result.warnings << QStringLiteral("已达到扫描数量或深度上限，当前为部分结果。可选择更小的目录重新扫描。");
    if (result.skippedEntries) result.warnings << QStringLiteral("有 %1 项因权限、挂载边界、特殊文件或读取失败而跳过。").arg(result.skippedEntries);
    if (result.skippedLinks) result.warnings << QStringLiteral("已跳过 %1 个符号链接；路径和统计不会越过链接目标。").arg(result.skippedLinks);
    if (result.hiddenEntries) result.warnings << QStringLiteral("按实际占用显示前 %1 组，另外 %2 组未显示或导出。总计仍包含这些组。")
        .arg(options.maxResults).arg(result.hiddenEntries);
    return result;
}

bool exportDeveloperCsv(const DeveloperScanResult &result, const QString &path, QString *error)
{
    const QFileInfo output(path);
    const QString absoluteOutput = absolute(path);
    const QString scanRoot = result.root.isEmpty() ? QString() : absolute(result.root);
    const QString canonicalRoot = QFileInfo(scanRoot).canonicalFilePath();
    const QString canonicalOutput = output.canonicalFilePath();
    const QString parentCanonical = output.absoluteDir().canonicalPath();
    const QString resolvedOutput = parentCanonical.isEmpty() ? QString() : QDir(parentCanonical).filePath(output.fileName());
    const auto within = [](const QString &candidate, const QString &root) {
        return !root.isEmpty() && (candidate == root || candidate.startsWith(root == QStringLiteral("/") ? root : root + QLatin1Char('/')));
    };
    const bool inScanRoot = !scanRoot.isEmpty() && (within(absoluteOutput, scanRoot)
        || within(canonicalOutput, canonicalRoot) || within(resolvedOutput, canonicalRoot));
    if (output.isDir() || (inScanRoot && (output.exists() || output.isSymLink()))) {
        if (error) *error = QStringLiteral("为保护已扫描数据，不能覆盖分析目录内的已有文件或目录。请选择新的报告文件名，或保存到分析目录之外。");
        return false;
    }
    // Inside the source tree, commit to a private staging file then publish via
    // link(2), which atomically refuses to replace an existing destination. This
    // closes the existence-check/commit race if another process creates the
    // selected report name while the CSV is being written.
    QTemporaryFile staging(output.absoluteDir().filePath(QStringLiteral(".disklens-report-XXXXXX")));
    QString commitPath = absoluteOutput;
    if (inScanRoot) {
        if (!staging.open()) { if (error) *error = staging.errorString(); return false; }
        commitPath = staging.fileName(); staging.close();
    }
    QSaveFile file(commitPath);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    QByteArray csv("\xEF\xBB\xBF");
    const QStringList header{QStringLiteral("类别"), QStringLiteral("项目"), QStringLiteral("实际占用（字节）"),
        QStringLiteral("逻辑大小（字节）"), QStringLiteral("文件数"), QStringLiteral("目录"), QStringLiteral("识别依据"),
        QStringLiteral("可重建性"), QStringLiteral("建议动作"), QStringLiteral("排除嵌套分组"), QStringLiteral("扫描状态")};
    auto row = [&](const QStringList &values) {
        QStringList quoted; for (const QString &value : values) quoted << csvString(value);
        csv += (quoted.join(QLatin1Char(',')) + QStringLiteral("\r\n")).toUtf8();
    };
    const bool partial = result.canceled || !result.error.isEmpty() || result.limited || result.skippedEntries || result.hiddenEntries;
    row({QStringLiteral("report"), QStringLiteral("developer_storage")});
    row({QStringLiteral("scan_root"), result.root});
    row({QStringLiteral("export_scope"), QStringLiteral("all_displayed_groups_search_filter_not_applied")});
    row({QStringLiteral("partial"), partial ? QStringLiteral("true") : QStringLiteral("false")});
    row({QStringLiteral("canceled"), result.canceled ? QStringLiteral("true") : QStringLiteral("false")});
    row({QStringLiteral("limited"), result.limited ? QStringLiteral("true") : QStringLiteral("false")});
    row({QStringLiteral("scanned_files"), QString::number(result.scannedFiles)});
    row({QStringLiteral("scanned_directories"), QString::number(result.scannedDirectories)});
    row({QStringLiteral("skipped_entries"), QString::number(result.skippedEntries)});
    row({QStringLiteral("skipped_symlinks"), QString::number(result.skippedLinks)});
    row({QStringLiteral("total_allocated_bytes"), QString::number(result.totalAllocatedBytes)});
    row({QStringLiteral("identified_allocated_bytes"), QString::number(result.identifiedAllocatedBytes)});
    row({QStringLiteral("displayed_groups"), QString::number(result.entries.size())});
    row({QStringLiteral("hidden_groups"), QString::number(result.hiddenEntries)});
    row({QStringLiteral("hidden_allocated_bytes"), QString::number(result.hiddenAllocatedBytes)});
    row({QStringLiteral("accounting"), QStringLiteral("Regular-file blocks only; nested groups excluded from parents; hard-linked inode counted once.")});
    row({QStringLiteral("error"), result.error});
    row({QStringLiteral("warnings"), result.warnings.join(QLatin1Char('\n'))});
    row(header);
    const QString state = result.canceled ? QStringLiteral("已取消，部分结果")
        : (partial ? QStringLiteral("部分结果") : QStringLiteral("已完成"));
    for (const DeveloperEntry &entry : result.entries)
        row({developerCategoryName(entry.category), entry.name, QString::number(entry.allocatedBytes), QString::number(entry.bytes),
             QString::number(entry.fileCount), entry.path, entry.evidence, entry.rebuildability, entry.suggestion,
             entry.excludesNestedGroups ? QStringLiteral("是") : QStringLiteral("否"), state});
    if (file.write(csv) != csv.size() || !file.commit()) { if (error) *error = file.errorString(); return false; }
    if (inScanRoot && ::link(QFile::encodeName(commitPath).constData(), QFile::encodeName(absoluteOutput).constData()) != 0) {
        if (error) *error = QStringLiteral("无法安全创建报告（不会覆盖已有数据）：%1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    if (error) error->clear();
    return true;
}
