#include "developerbackend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>
#include <unistd.h>

namespace {
bool write(const QString &path, const QByteArray &data = QByteArray(8192, 'x'))
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
const DeveloperEntry *find(const DeveloperScanResult &result, const QString &path)
{
    for (const DeveloperEntry &entry : result.entries) if (entry.path == path) return &entry;
    return nullptr;
}
quint64 allocated(const QString &path)
{
    struct stat st {}; return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? quint64(st.st_blocks) * 512 : 0;
}
DeveloperScanOptions options(const QString &root)
{
    DeveloperScanOptions result; result.root = root; result.homePath = root; result.cachePath = root + "/.cache"; return result;
}
}

class DeveloperTest : public QObject {
    Q_OBJECT
private slots:
    void rosWorkspaceEvidenceAndOriginalData();
    void knownCachesAndDependencyEnvironments();
    void nestedGroupsAndHardlinksAreCountedOnce();
    void boundariesCancellationAndLimits();
    void csvQuotesFormulaAndPartialState();
};

void DeveloperTest::rosWorkspaceEvidenceAndOriginalData()
{
    QTemporaryDir dir; QVERIFY(dir.isValid()); const QString root = dir.path();
    QVERIFY(write(root + "/robot/src/navigation/package.xml", "<package format=\"3\"><name>nav</name></package>"));
    QVERIFY(write(root + "/robot/build/.built_by", "colcon\n"));
    QVERIFY(write(root + "/robot/build/nav/object.o"));
    QVERIFY(write(root + "/robot/install/setup.bash")); QVERIFY(write(root + "/robot/install/local_setup.bash"));
    QVERIFY(write(root + "/robot/install/_local_setup_util_sh.py"));
    QVERIFY(write(root + "/robot/log/build_2026/nav/stdout.log"));
    QVERIFY(write(root + "/recording/metadata.yaml", "rosbag2_bagfile_information:\n  version: 9\n"));
    QVERIFY(write(root + "/recording/run_0.db3")); QVERIFY(write(root + "/clouds/room.pcd"));
    QVERIFY(write(root + "/ordinary/build/object.o")); QVERIFY(write(root + "/ordinary/log/file.log"));
    QVERIFY(write(root + "/fake/src/pkg/package.xml", "this is not a ROS package"));
    QVERIFY(write(root + "/fake/build/file.bin"));
    QVERIFY(write(root + "/source-only/src/pkg/package.xml", "<package><name>pkg</name></package>"));
    QVERIFY(write(root + "/source-only/build/hand-authored-file"));
    const auto result = scanDeveloperUsage(options(root));
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QVERIFY(!result.limited); QVERIFY(!result.canceled);
    QCOMPARE(result.entries.size(), 5);
    QVERIFY(find(result, root + "/robot/build")); QCOMPARE(find(result, root + "/robot/build")->category, DeveloperCategory::RosBuild);
    QCOMPARE(find(result, root + "/robot/install")->category, DeveloperCategory::RosInstall);
    QCOMPARE(find(result, root + "/robot/log")->category, DeveloperCategory::RosLog);
    const auto *bag = find(result, root + "/recording"); QVERIFY(bag); QCOMPARE(bag->category, DeveloperCategory::Rosbag);
    QVERIFY(bag->rebuildability.contains(QStringLiteral("不可重建")));
    QVERIFY(bag->suggestion.contains(QStringLiteral("备份")));
    const auto *cloud = find(result, root + "/clouds"); QVERIFY(cloud); QCOMPARE(cloud->fileCount, quint64(1));
    QVERIFY(!find(result, root + "/ordinary/build")); QVERIFY(!find(result, root + "/ordinary/log"));
    QVERIFY(!find(result, root + "/fake/build"));
    QVERIFY(!find(result, root + "/source-only/build"));
    QVERIFY(QFileInfo::exists(root + "/recording/run_0.db3"));
}

void DeveloperTest::knownCachesAndDependencyEnvironments()
{
    QTemporaryDir dir; QVERIFY(dir.isValid()); const QString root = dir.path();
    QVERIFY(write(root + "/custom-cache/pip/http-v2/entry.bin"));
    QVERIFY(write(root + "/.npm/_cacache/content-v2/sha512/entry"));
    QVERIFY(write(root + "/custom-cache/ccache/0/stats", "0\n"));
    QVERIFY(write(root + "/custom-cache/ccache/0/entry.o"));
    QVERIFY(write(root + "/miniforge/envs/dev/conda-meta/history", "# env history\n"));
    QVERIFY(write(root + "/miniforge/envs/dev/lib/dependency.so"));
    QVERIFY(write(root + "/project/pip/http-v2/entry.bin"));
    QVERIFY(QDir().mkpath(root + "/fake-conda/conda-meta"));
    QVERIFY(write(root + "/fake-conda/lib/library.so"));
    auto config = options(root); config.cachePath = root + "/custom-cache";
    const auto result = scanDeveloperUsage(config);
    QCOMPARE(result.entries.size(), 4);
    QCOMPARE(find(result, root + "/custom-cache/pip")->category, DeveloperCategory::PipCache);
    QCOMPARE(find(result, root + "/.npm/_cacache")->category, DeveloperCategory::NpmCache);
    QCOMPARE(find(result, root + "/custom-cache/ccache")->category, DeveloperCategory::Ccache);
    const auto *env = find(result, root + "/miniforge/envs/dev"); QVERIFY(env); QCOMPARE(env->category, DeveloperCategory::Conda);
    QVERIFY(env->rebuildability.contains(QStringLiteral("不是缓存"))); QVERIFY(!find(result, root + "/fake-conda"));
    QVERIFY(!find(result, root + "/project/pip"));
    const QByteArray oldCache = qgetenv("XDG_CACHE_HOME");
    qputenv("XDG_CACHE_HOME", QFile::encodeName(root + "/custom-cache"));
    config.cachePath.clear();
    const auto xdg = scanDeveloperUsage(config);
    if (oldCache.isNull()) qunsetenv("XDG_CACHE_HOME"); else qputenv("XDG_CACHE_HOME", oldCache);
    QCOMPARE(xdg.entries.size(), result.entries.size());
}

void DeveloperTest::nestedGroupsAndHardlinksAreCountedOnce()
{
    QTemporaryDir dir; QVERIFY(dir.isValid()); const QString root = dir.path();
    QVERIFY(write(root + "/ws/build/.built_by", "colcon"));
    QVERIFY(write(root + "/ws/build/object.o"));
    QVERIFY(write(root + "/ws/build/rosbag/metadata.yaml", "metadata"));
    QVERIFY(write(root + "/ws/build/rosbag/raw.mcap"));
    QVERIFY(write(root + "/ws/build/models/frame.pcd"));
    const QByteArray source = QFile::encodeName(root + "/ws/build/object.o");
    const QByteArray linked = QFile::encodeName(root + "/ws/build/object-copy.o");
    QVERIFY(::link(source.constData(), linked.constData()) == 0);
    const auto result = scanDeveloperUsage(options(root));
    QCOMPARE(result.entries.size(), 3); QCOMPARE(result.scannedFiles, quint64(5));
    const auto *build = find(result, root + "/ws/build"); QVERIFY(build); QVERIFY(build->excludesNestedGroups);
    QCOMPARE(build->fileCount, quint64(2));
    QCOMPARE(build->allocatedBytes, allocated(root + "/ws/build/.built_by") + allocated(root + "/ws/build/object.o"));
    quint64 sum = 0; for (const auto &entry : result.entries) sum += entry.allocatedBytes;
    QCOMPARE(sum, result.identifiedAllocatedBytes); QCOMPARE(sum, result.totalAllocatedBytes);
    const auto *bag = find(result, root + "/ws/build/rosbag"); QVERIFY(bag); QCOMPARE(bag->fileCount, quint64(2));
}

void DeveloperTest::boundariesCancellationAndLimits()
{
    QTemporaryDir dir, outside; QVERIFY(dir.isValid()); QVERIFY(outside.isValid());
    const QString root = dir.path();
    QVERIFY(write(root + "/real/cloud.pcd")); QVERIFY(write(outside.path() + "/secret.pcd"));
    QVERIFY(QFile::link(outside.path(), root + "/outside-link"));
    QVERIFY(QFile::link(root + "/real", root + "/inside-link"));
    auto config = options(root); const auto result = scanDeveloperUsage(config);
    QCOMPARE(result.entries.size(), 1); QCOMPARE(result.skippedLinks, quint64(2)); QCOMPARE(result.scannedFiles, quint64(1));
    config.root = root + "/inside-link"; QVERIFY(!scanDeveloperUsage(config).error.isEmpty());
    config.root = root + "/inside-link/child"; QVERIFY(!scanDeveloperUsage(config).error.isEmpty());
    config.root = root;
    std::atomic_bool cancel{true}; auto canceled = scanDeveloperUsage(config, &cancel);
    QVERIFY(canceled.canceled); QCOMPARE(canceled.scannedFiles, quint64(0));
    cancel.store(false);
    canceled = scanDeveloperUsage(config, &cancel, [&cancel](quint64, const QString &) { cancel.store(true); });
    QVERIFY(canceled.canceled);
    config.maxDepth = 0; const auto depth = scanDeveloperUsage(config); QVERIFY(depth.limited); QCOMPARE(depth.scannedFiles, quint64(0));
    config.maxDepth = 32; config.maxEntries = 1; QVERIFY(scanDeveloperUsage(config).limited);
    config.maxEntries = 500000; config.maxDirectories = 1; QVERIFY(scanDeveloperUsage(config).limited);
    config.maxDirectories = 50000;
    QVERIFY(write(root + "/second/cloud.pcd"));
    config.maxResults = 1; const auto bounded = scanDeveloperUsage(config);
    QCOMPARE(bounded.entries.size(), 1); QCOMPARE(bounded.hiddenEntries, 1);
    QCOMPARE(bounded.identifiedAllocatedBytes, bounded.entries.first().allocatedBytes + bounded.hiddenAllocatedBytes);
}

void DeveloperTest::csvQuotesFormulaAndPartialState()
{
    QTemporaryDir dir; QVERIFY(dir.isValid()); DeveloperScanResult result; result.canceled = true;
    DeveloperEntry entry; entry.name = QStringLiteral(" =SUM(1,2)\"\nname"); entry.path = QStringLiteral("/tmp/data,\"capture\"");
    entry.rebuildability = QStringLiteral("原始采集数据，不可重建"); entry.allocatedBytes = 8192; entry.fileCount = 1;
    result.entries.append(entry); QString error; const QString destination = dir.path() + "/report.csv";
    QVERIFY2(exportDeveloperCsv(result, destination, &error), qPrintable(error));
    QFile file(destination); QVERIFY(file.open(QIODevice::ReadOnly)); const QByteArray bytes = file.readAll();
    QVERIFY(bytes.startsWith("\xEF\xBB\xBF"));
    QVERIFY(bytes.contains("\"' =SUM(1,2)\"\"\nname\""));
    QVERIFY(bytes.contains("\"/tmp/data,\"\"capture\"\"\""));
    QVERIFY(bytes.contains(QStringLiteral("已取消，部分结果").toUtf8()));
    QVERIFY(!exportDeveloperCsv(result, dir.path() + "/absent/report.csv", &error)); QVERIFY(!error.isEmpty());
    // Exporting is the only write feature; it must never overwrite source data.
    result.root = dir.path(); result.canceled = false; result.hiddenEntries = 1;
    result.identifiedAllocatedBytes = 16384; result.hiddenAllocatedBytes = 8192;
    const QString original = dir.path() + "/original.pcd"; QVERIFY(write(original, "raw point cloud"));
    QVERIFY(!exportDeveloperCsv(result, original, &error));
    QFile source(original); QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(), QByteArray("raw point cloud"));
    QVERIFY(!exportDeveloperCsv(result, result.root, &error));
    const QString newReport = dir.path() + "/new-report.csv";
    QVERIFY(exportDeveloperCsv(result, newReport, &error));
    QFile partial(newReport); QVERIFY(partial.open(QIODevice::ReadOnly)); const QByteArray partialCsv = partial.readAll();
    QVERIFY(partialCsv.contains("\"scan_root\",\"" + dir.path().toUtf8() + "\""));
    QVERIFY(partialCsv.contains("\"partial\",\"true\""));
    QVERIFY(partialCsv.contains("\"hidden_groups\",\"1\""));
    QVERIFY(partialCsv.contains("\"identified_allocated_bytes\",\"16384\""));
    QVERIFY(partialCsv.contains(QStringLiteral("部分结果").toUtf8()));
    QVERIFY(!exportDeveloperCsv(result, newReport, &error));
    QTemporaryDir alias; QVERIFY(alias.isValid());
    QVERIFY(QFile::link(dir.path(), alias.path() + "/linked-root"));
    QVERIFY(!exportDeveloperCsv(result, alias.path() + "/linked-root/original.pcd", &error));
    QVERIFY(exportDeveloperCsv(result, alias.path() + "/linked-root/alias-new-report.csv", &error));
    QVERIFY(QFileInfo::exists(dir.path() + "/alias-new-report.csv"));
    QCOMPARE(QDir(dir.path()).entryList({QStringLiteral(".disklens-report-*")}, QDir::Files | QDir::Hidden).size(), 0);
}

QTEST_GUILESS_MAIN(DeveloperTest)
#include "developer_test.moc"
