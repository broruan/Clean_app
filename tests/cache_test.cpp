#include "cachebackend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
struct Fixture {
    QTemporaryDir temporary;
    QString cache = temporary.filePath(QStringLiteral("cache"));
    QString state = temporary.filePath(QStringLiteral("state"));
    Fixture() { QDir().mkpath(cache); QDir().mkpath(state); }
};

bool writeFile(const QString &path, const QByteArray &data = QByteArray("cache"))
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

quint64 allocated(const QString &path)
{
    struct stat information {};
    if (::stat(QFile::encodeName(path).constData(), &information) != 0)
        return 0;
    return quint64(information.st_blocks) * 512;
}

const CacheEntry *findEntry(const CacheScanResult &result, const QString &root,
                           const QString &scope, bool log)
{
    for (const auto &entry : result.entries)
        if (entry.rootPath == root && entry.relativePath == scope && entry.logOnly == log)
            return &entry;
    return nullptr;
}
}

class CacheTest : public QObject {
    Q_OBJECT
private slots:
    void groupsCacheAndLogsWithoutStateData()
    {
        Fixture fixture;
        QVERIFY(fixture.temporary.isValid());
        const QList<QPair<QString, int>> cacheFiles = {
            {"app/cache.bin", 16}, {"app/.hidden", 32}, {"app/nested/part", 64},
            {"app/logs/trace001", 8}, {"app/report.log", 4}, {"app/rotated.log.1", 12},
            {"root.bin", 4}, {"root.log", 5}
        };
        for (const auto &file : cacheFiles)
            QVERIFY(writeFile(fixture.cache + '/' + file.first, QByteArray(file.second, 'x')));
        const QList<QPair<QString, int>> stateFiles = {
            {"app/data.json", 42}, {"app/run.log", 7}, {"app/logs/rawtext", 9},
            {"app/deep/keep.state", 50}, {"app/deep/debug.log.2", 7},
            {"root.log", 3}, {"catalog", 12}
        };
        for (const auto &file : stateFiles)
            QVERIFY(writeFile(fixture.state + '/' + file.first, QByteArray(file.second, 'x')));

        const auto result = scanCaches(fixture.cache, fixture.state);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.entries.size(), 6);
        QCOMPARE(result.scannedFiles, quint64(15));
        const auto *ordinary = findEntry(result, fixture.cache, "app", false);
        const auto *logs = findEntry(result, fixture.cache, "app", true);
        const auto *state = findEntry(result, fixture.state, "app", true);
        QVERIFY(ordinary);
        QVERIFY(logs);
        QVERIFY(state);
        QCOMPARE(ordinary->fileCount, quint64(3));
        QCOMPARE(ordinary->bytes, quint64(112));
        QCOMPARE(ordinary->logCount, quint64(0));
        QCOMPARE(ordinary->logAllocatedBytes, quint64(0));
        QCOMPARE(logs->fileCount, quint64(3));
        QCOMPARE(logs->logCount, quint64(3));
        QCOMPARE(logs->bytes, quint64(24));
        QCOMPARE(logs->logAllocatedBytes, logs->allocatedBytes);
        QCOMPARE(state->fileCount, quint64(3));
        QCOMPARE(state->bytes, quint64(23));
        QVERIFY(logs->id != ordinary->id);
        QVERIFY(logs->id != state->id);
        QVERIFY(!findEntry(result, fixture.state, "app", false));
        quint64 actual = 0;
        for (const auto &file : cacheFiles)
            actual += allocated(fixture.cache + '/' + file.first);
        for (const auto &entry : result.entries) {
            QVERIFY(!entry.manifestToken.isEmpty());
            if (entry.rootPath == fixture.cache)
                actual -= entry.allocatedBytes;
            for (const auto &file : entry.files)
                QVERIFY(!file.relativePath.endsWith("data.json") && !file.relativePath.endsWith("keep.state"));
        }
        QCOMPARE(actual, quint64(0));
    }

    void cleanOnlySnapshotsAndKeepNewFilesAndDirectories()
    {
        Fixture fixture;
        const QStringList cached = {"app/a", "app/sub/b", "app/logs/current", "app/run.log"};
        for (const auto &relative : cached)
            QVERIFY(writeFile(fixture.cache + '/' + relative));
        QVERIFY(writeFile(fixture.state + "/app/logs/plain"));
        QVERIFY(writeFile(fixture.state + "/app/settings.json"));
        const auto scan = scanCaches(fixture.cache, fixture.state);
        QVERIFY2(scan.error.isEmpty(), qPrintable(scan.error));
        quint64 expected = 0;
        for (const auto &entry : scan.entries)
            expected += entry.allocatedBytes;
        QVERIFY(writeFile(fixture.cache + "/app/new-file"));
        QVERIFY(writeFile(fixture.state + "/app/logs/new.log"));

        const auto result = cleanCaches(scan.entries, fixture.cache, fixture.state);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QCOMPARE(result.removedFiles, quint64(5));
        QCOMPARE(result.removedBytes, expected);
        QCOMPARE(result.failedFiles, quint64(0));
        QCOMPARE(result.skippedFiles, quint64(0));
        for (const auto &relative : cached)
            QVERIFY(!QFileInfo::exists(fixture.cache + '/' + relative));
        QVERIFY(!QFileInfo::exists(fixture.state + "/app/logs/plain"));
        QVERIFY(QFileInfo::exists(fixture.cache + "/app/new-file"));
        QVERIFY(QFileInfo::exists(fixture.state + "/app/logs/new.log"));
        QVERIFY(QFileInfo::exists(fixture.state + "/app/settings.json"));
        QVERIFY(QFileInfo(fixture.cache + "/app/sub").isDir());
        QVERIFY(QFileInfo(fixture.cache).isDir());
    }

    void logSelectionDoesNotDeleteOrdinaryCache()
    {
        Fixture fixture;
        QVERIFY(writeFile(fixture.cache + "/app/cache.bin"));
        QVERIFY(writeFile(fixture.cache + "/app/logs/trace"));
        QVERIFY(writeFile(fixture.cache + "/app/debug.log.1"));
        const auto scan = scanCaches(fixture.cache, fixture.state);
        const auto *entry = findEntry(scan, fixture.cache, "app", true);
        QVERIFY(entry);
        const auto result = cleanCaches({*entry}, fixture.cache, fixture.state);
        QCOMPARE(result.removedFiles, quint64(2));
        QVERIFY(QFileInfo::exists(fixture.cache + "/app/cache.bin"));
        QVERIFY(!QFileInfo::exists(fixture.cache + "/app/logs/trace"));
        QVERIFY(!QFileInfo::exists(fixture.cache + "/app/debug.log.1"));
    }

    void sparseFilesUseAllocatedBytes()
    {
        Fixture fixture;
        const QString path = fixture.cache + "/app/large.bin";
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        constexpr qint64 size = 3LL * 1024 * 1024 * 1024;
        QVERIFY(file.resize(size));
        file.close();
        QVERIFY(writeFile(fixture.cache + "/app/plain", "small"));
        const auto scan = scanCaches(fixture.cache, {});
        QVERIFY2(scan.error.isEmpty(), qPrintable(scan.error));
        QCOMPARE(scan.entries.size(), 1);
        const auto &entry = scan.entries.first();
        QCOMPARE(entry.bytes, quint64(size + 5));
        QCOMPARE(entry.allocatedBytes, allocated(path) + allocated(fixture.cache + "/app/plain"));
        QVERIFY(entry.allocatedBytes < entry.bytes);
        const auto result = cleanCaches(scan.entries, fixture.cache, {});
        QCOMPARE(result.removedFiles, quint64(2));
        QCOMPARE(result.removedBytes, entry.allocatedBytes);
    }

    void changedReplacedAndHardLinkedFilesArePreserved()
    {
        Fixture fixture;
        const QString changed = fixture.cache + "/app/changed";
        const QString replaced = fixture.cache + "/app/replaced";
        const QString linked = fixture.cache + "/app/linked";
        QVERIFY(writeFile(changed, "original"));
        QVERIFY(writeFile(replaced, "original"));
        QVERIFY(writeFile(linked, "original"));
        QCOMPARE(::link(QFile::encodeName(linked).constData(),
                        QFile::encodeName(fixture.cache + "/app/alias").constData()), 0);
        struct stat original {};
        QCOMPARE(::stat(QFile::encodeName(changed).constData(), &original), 0);
        const auto scan = scanCaches(fixture.cache, {});
        QCOMPARE(scan.entries.first().fileCount, quint64(3));
        QTest::qSleep(2);
        QVERIFY(writeFile(changed, "modified"));
        const timespec times[] = {original.st_atim, original.st_mtim};
        QCOMPARE(::utimensat(AT_FDCWD, QFile::encodeName(changed).constData(), times, 0), 0);
        QVERIFY(QFile::rename(replaced, fixture.temporary.filePath("original-replaced")));
        QVERIFY(writeFile(replaced, "original"));
        const auto result = cleanCaches(scan.entries, fixture.cache, {});
        QCOMPARE(result.removedFiles, quint64(0));
        QCOMPARE(result.skippedFiles, quint64(3));
        QVERIFY(QFileInfo::exists(changed));
        QVERIFY(QFileInfo::exists(replaced));
        QVERIFY(QFileInfo::exists(linked));
        QVERIFY(QFileInfo::exists(fixture.cache + "/app/alias"));
    }

    void symlinksNeverEscapeRoots()
    {
        Fixture fixture;
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        const QString external = outside.filePath("file");
        QVERIFY(writeFile(external));
        QVERIFY(writeFile(fixture.cache + "/app/sub/file"));
        QCOMPARE(::symlink(QFile::encodeName(external).constData(),
                           QFile::encodeName(fixture.cache + "/app/link-file").constData()), 0);
        QCOMPARE(::symlink(QFile::encodeName(outside.path()).constData(),
                           QFile::encodeName(fixture.cache + "/app/link-dir").constData()), 0);
        QCOMPARE(::symlink(".", QFile::encodeName(fixture.cache + "/app/loop").constData()), 0);
        const auto scan = scanCaches(fixture.cache, {});
        QVERIFY2(scan.error.isEmpty(), qPrintable(scan.error));
        QVERIFY(scan.skippedEntries >= 3);
        QCOMPARE(scan.entries.first().fileCount, quint64(1));
        const QString moved = fixture.temporary.filePath("moved-sub");
        QVERIFY(QDir().rename(fixture.cache + "/app/sub", moved));
        QCOMPARE(::symlink(QFile::encodeName(outside.path()).constData(),
                           QFile::encodeName(fixture.cache + "/app/sub").constData()), 0);
        const auto result = cleanCaches(scan.entries, fixture.cache, {});
        QCOMPARE(result.removedFiles, quint64(0));
        QCOMPARE(result.skippedFiles, quint64(1));
        QVERIFY(QFileInfo::exists(external));
        QVERIFY(QFileInfo::exists(moved + "/file"));
    }

    void movingRootDuringProgressDoesNotDeleteDetachedTree()
    {
        Fixture fixture;
        QVERIFY(writeFile(fixture.cache + "/app/file"));
        const auto scan = scanCaches(fixture.cache, {});
        const QString moved = fixture.temporary.filePath("outside-root");
        bool movedOk = false;
        const auto result = cleanCaches(scan.entries, fixture.cache, {}, nullptr,
            [&](quint64, const QString &) {
                movedOk = QDir().rename(fixture.cache, moved) && QDir().mkdir(fixture.cache);
            });
        QVERIFY(movedOk);
        QCOMPARE(result.removedFiles, quint64(0));
        QCOMPARE(result.skippedFiles, quint64(1));
        QVERIFY(QFileInfo::exists(moved + "/app/file"));
    }

    void replacedAncestorDirectoryIsPreserved()
    {
        Fixture fixture;
        const QString sub = fixture.cache + "/app/sub";
        QVERIFY(writeFile(sub + "/file"));
        const auto scan = scanCaches(fixture.cache, {});
        const QString moved = fixture.temporary.filePath("outside-sub");
        QVERIFY(QDir().rename(sub, moved));
        QVERIFY(QDir().mkdir(sub));
        QVERIFY(QFile::rename(moved + "/file", sub + "/file"));
        const auto result = cleanCaches(scan.entries, fixture.cache, {});
        QCOMPARE(result.removedFiles, quint64(0));
        QCOMPARE(result.skippedFiles, quint64(1));
        QVERIFY(QFileInfo::exists(sub + "/file"));
    }

    void forgedManifestsAndTraversalAreRejected()
    {
        Fixture fixture;
        QVERIFY(writeFile(fixture.cache + "/app/file"));
        const QString outside = fixture.temporary.filePath("outside");
        QVERIFY(writeFile(outside));
        const auto scan = scanCaches(fixture.cache, fixture.state);
        QCOMPARE(scan.entries.size(), 1);
        CacheEntry forged = scan.entries.first();
        forged.files[0].relativePath = "../outside";
        const auto traversal = cleanCaches({forged}, fixture.cache, fixture.state);
        QCOMPARE(traversal.removedFiles, quint64(0));
        QVERIFY(traversal.failedFiles > 0);
        forged = scan.entries.first();
        forged.logOnly = true;
        const auto changedKind = cleanCaches({forged}, fixture.cache, fixture.state);
        QVERIFY(changedKind.failedFiles > 0);
        forged = scan.entries.first();
        forged.rootPath = fixture.temporary.path();
        forged.path = outside;
        const auto changedRoot = cleanCaches({forged}, fixture.cache, fixture.state);
        QVERIFY(changedRoot.failedFiles > 0);
        CacheEntry fabricated;
        fabricated.rootPath = fixture.cache;
        fabricated.relativePath = "app";
        fabricated.path = fixture.cache + "/app";
        fabricated.files = scan.entries.first().files;
        QVERIFY(cleanCaches({fabricated}, fixture.cache, fixture.state).failedFiles > 0);
        const auto wrongAuthority = cleanCaches(scan.entries, fixture.state, {});
        QVERIFY(wrongAuthority.failedFiles > 0);
        QVERIFY(QFileInfo::exists(outside));
        QVERIFY(QFileInfo::exists(fixture.cache + "/app/file"));
    }

    void unsafeRootsAndMissingOptionalState()
    {
        Fixture fixture;
        QVERIFY(!scanCaches("/", {}).error.isEmpty());
        QVERIFY(!scanCaches(QDir::homePath(), {}).error.isEmpty());
        QVERIFY(!scanCaches(QFileInfo(QDir::homePath()).absolutePath(), {}).error.isEmpty());
        QVERIFY(!scanCaches("/tmp", {}).error.isEmpty());
        QVERIFY(!scanCaches("relative", {}).error.isEmpty());
        QVERIFY(!scanCaches(fixture.cache, fixture.cache + "/inside").error.isEmpty());
        QVERIFY(!scanCaches(fixture.cache, fixture.cache).error.isEmpty());
        const QString link = fixture.temporary.filePath("cache-link");
        QCOMPARE(::symlink(QFile::encodeName(fixture.cache).constData(), QFile::encodeName(link).constData()), 0);
        QVERIFY(!scanCaches(link, {}).error.isEmpty());
        QVERIFY(writeFile(fixture.cache + "/file"));
        const auto missing = scanCaches(fixture.cache, fixture.temporary.filePath("missing-state"));
        QVERIFY(missing.error.isEmpty());
        QVERIFY(!missing.warnings.isEmpty());
        QCOMPARE(missing.entries.size(), 1);
    }

    void cancellationBeforeAndDuringCleaning()
    {
        Fixture fixture;
        QVERIFY(writeFile(fixture.cache + "/app/first"));
        QVERIFY(writeFile(fixture.cache + "/app/second"));
        QVERIFY(writeFile(fixture.cache + "/app/third"));
        std::atomic_bool cancel {true};
        QVERIFY(scanCaches(fixture.cache, {}, &cancel).canceled);
        cancel.store(false);
        const auto scan = scanCaches(fixture.cache, {});
        cancel.store(true);
        const auto before = cleanCaches(scan.entries, fixture.cache, {}, &cancel);
        QVERIFY(before.canceled);
        QCOMPARE(before.removedFiles, quint64(0));
        cancel.store(false);
        const auto partial = cleanCaches(scan.entries, fixture.cache, {}, &cancel,
            [&](quint64 processed, const QString &) {
                if (processed > 0)
                    cancel.store(true);
                else
                    QTest::qSleep(160);
            });
        QVERIFY(partial.canceled);
        QCOMPARE(partial.removedFiles, quint64(1));
        QCOMPARE(QDir(fixture.cache + "/app").entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 2);
    }

    void cancellationDuringScanReturnsPartialSnapshots()
    {
        Fixture fixture;
        QVERIFY(writeFile(fixture.cache + "/first"));
        QVERIFY(writeFile(fixture.cache + "/second"));
        std::atomic_bool cancel {false};
        const auto result = scanCaches(fixture.cache, {}, &cancel,
            [&](quint64 scanned, const QString &) {
                if (scanned > 0)
                    cancel.store(true);
                else
                    QTest::qSleep(160);
            });
        QVERIFY(result.canceled);
        QCOMPARE(result.scannedFiles, quint64(1));
        QCOMPARE(result.entries.size(), 1);
        QCOMPARE(result.entries.first().fileCount, quint64(1));
        QVERIFY(QFileInfo::exists(fixture.cache + "/first"));
        QVERIFY(QFileInfo::exists(fixture.cache + "/second"));
    }
};

QTEST_GUILESS_MAIN(CacheTest)
#include "cache_test.moc"
