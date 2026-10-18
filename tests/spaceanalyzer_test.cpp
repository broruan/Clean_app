#include "spaceanalyzer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

class SpaceAnalyzerTest : public QObject {
    Q_OBJECT

    static bool writeFile(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }

    static const SpaceNode *childNamed(const SpaceNode &node, const QString &name)
    {
        for (const auto &child : node.children)
            if (child.name == name)
                return &child;
        return nullptr;
    }

    static quint64 blocks(const QString &path)
    {
        struct stat information {};
        if (::stat(QFile::encodeName(path).constData(), &information) != 0)
            return 0;
        return quint64(information.st_blocks) * 512;
    }

private slots:
    void directoryAggregationHiddenSparseAndHardLinks()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QVERIFY(QDir(temporary.path()).mkdir(QStringLiteral("nested")));
        QVERIFY(writeFile(temporary.filePath(QStringLiteral(".hidden")), QByteArray(64, 'h')));
        const QString original = temporary.filePath(QStringLiteral("nested/original"));
        QVERIFY(writeFile(original, QByteArray(123, 'x')));
        QCOMPARE(::link(QFile::encodeName(original).constData(),
                        QFile::encodeName(temporary.filePath(QStringLiteral("alias"))).constData()), 0);
        const QString sparsePath = temporary.filePath(QStringLiteral("nested/sparse"));
        QFile sparse(sparsePath);
        QVERIFY(sparse.open(QIODevice::WriteOnly));
        constexpr qint64 sparseSize = 16 * 1024 * 1024;
        QVERIFY(sparse.resize(sparseSize));
        sparse.close();
        const auto result = analyzeDirectory(temporary.path());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.canceled);
        QVERIFY(!result.truncated);
        QCOMPARE(result.skippedEntries, quint64(0));
        QCOMPARE(result.scannedFiles, quint64(3));
        QCOMPARE(result.root.fileCount, quint64(3));
        QCOMPARE(result.root.logicalBytes, quint64(sparseSize + 123 + 64));
        QCOMPARE(result.root.allocatedBytes, blocks(temporary.path())
                 + blocks(temporary.filePath(QStringLiteral("nested"))) + blocks(original)
                 + blocks(sparsePath) + blocks(temporary.filePath(QStringLiteral(".hidden"))));
        const auto *nested = childNamed(result.root, QStringLiteral("nested"));
        QVERIFY(nested);
        QVERIFY(nested->directory);
        const auto *sparseNode = childNamed(*nested, QStringLiteral("sparse"));
        QVERIFY(sparseNode);
        QCOMPARE(sparseNode->logicalBytes, quint64(sparseSize));
        QCOMPARE(sparseNode->allocatedBytes, blocks(sparsePath));
        QVERIFY(sparseNode->allocatedBytes < sparseNode->logicalBytes);
        QVERIFY(childNamed(result.root, QStringLiteral(".hidden")));
        for (int i = 1; i < result.root.children.size(); ++i)
            QVERIFY(result.root.children.at(i - 1).allocatedBytes >= result.root.children.at(i).allocatedBytes);
    }

    void ignoresLinksSpecialFilesAndRejectsSymlinkAncestors()
    {
        QTemporaryDir temporary;
        QTemporaryDir outside;
        QVERIFY(temporary.isValid());
        QVERIFY(outside.isValid());
        QVERIFY(QDir(outside.path()).mkdir(QStringLiteral("sub")));
        QVERIFY(writeFile(outside.filePath(QStringLiteral("sub/outside")), QByteArray(10, 'x')));
        const QString link = temporary.filePath(QStringLiteral("link"));
        QCOMPARE(::symlink(QFile::encodeName(outside.path()).constData(), QFile::encodeName(link).constData()), 0);
        QCOMPARE(::mkfifo(QFile::encodeName(temporary.filePath(QStringLiteral("fifo"))).constData(), 0600), 0);
        const auto result = analyzeDirectory(temporary.path());
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.scannedFiles, quint64(0));
        QVERIFY(result.root.children.isEmpty());
        QVERIFY(!analyzeDirectory(link).error.isEmpty());
        QVERIFY(!analyzeDirectory(link + QStringLiteral("/sub")).error.isEmpty());
        QVERIFY(!findDuplicates(link, 1).error.isEmpty());
        QVERIFY(!findDuplicates(link + QStringLiteral("/sub"), 1).error.isEmpty());
    }

    void directoryBoundsAndCancellation()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        for (int i = 0; i < 4; ++i)
            QVERIFY(writeFile(temporary.filePath(QString::number(i)), QByteArray(10, 'x')));
        const auto bounded = analyzeDirectory(temporary.path(), nullptr, {}, 3);
        QVERIFY(bounded.error.isEmpty());
        QVERIFY(bounded.truncated);
        QCOMPARE(bounded.root.children.size(), 2);
        QCOMPARE(bounded.root.fileCount, quint64(2));
        QCOMPARE(bounded.root.logicalBytes, quint64(20));
        const auto rootOnly = analyzeDirectory(temporary.path(), nullptr, {}, 1);
        QVERIFY(rootOnly.truncated);
        QVERIFY(rootOnly.root.children.isEmpty());
        QVERIFY(!analyzeDirectory(temporary.path(), nullptr, {}, 0).error.isEmpty());
        QVERIFY(!analyzeDirectory(QString()).error.isEmpty());
        std::atomic_bool cancel {true};
        QVERIFY(analyzeDirectory(temporary.path(), &cancel).canceled);
        cancel.store(false);
        const auto canceled = analyzeDirectory(temporary.path(), &cancel,
            [&](quint64, const QString &) { cancel.store(true); });
        QVERIFY(canceled.canceled);
        QCOMPARE(canceled.scannedFiles, quint64(0));
    }

    void depthBoundIsPartial()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString deepest = temporary.path();
        for (int i = 0; i < 130; ++i) {
            deepest += QStringLiteral("/d");
            QVERIFY(QDir().mkdir(deepest));
        }
        QVERIFY(writeFile(deepest + QStringLiteral("/file"), QByteArray(1, 'x')));
        const auto tree = analyzeDirectory(temporary.path());
        QVERIFY(tree.truncated);
        QVERIFY(tree.skippedEntries > 0);
        QCOMPARE(tree.scannedFiles, quint64(0));
        const auto duplicates = findDuplicates(temporary.path(), 1);
        QVERIFY(duplicates.truncated);
        QVERIFY(duplicates.skippedEntries > 0);
    }

    void duplicatesRequireCompleteHashAndIgnoreHardLinks()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QByteArray contents(300 * 1024, 'a');
        const QString first = temporary.filePath(QStringLiteral("first"));
        const QString second = temporary.filePath(QStringLiteral("second"));
        QVERIFY(writeFile(first, contents));
        QVERIFY(writeFile(second, contents));
        QByteArray collision = contents;
        collision[150 * 1024] = 'b'; // Same size/head/tail, different full contents.
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("sample-collision")), collision));
        QCOMPARE(::link(QFile::encodeName(first).constData(),
                        QFile::encodeName(temporary.filePath(QStringLiteral("hard-link"))).constData()), 0);
        QCOMPARE(::symlink(QFile::encodeName(first).constData(),
                           QFile::encodeName(temporary.filePath(QStringLiteral("symlink"))).constData()), 0);
        QCOMPARE(::mkfifo(QFile::encodeName(temporary.filePath(QStringLiteral("fifo"))).constData(), 0600), 0);
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("empty-a")), {}));
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("empty-b")), {}));
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("small-a")), QByteArray(4, 's')));
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("small-b")), QByteArray(4, 's')));
        const auto result = findDuplicates(temporary.path(), 100);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.canceled);
        QVERIFY(!result.truncated);
        QCOMPARE(result.scannedFiles, quint64(7));
        QCOMPARE(result.hashedFiles, quint64(3));
        QCOMPARE(result.groups.size(), 1);
        const auto &group = result.groups.first();
        QCOMPARE(group.bytes, quint64(contents.size()));
        QCOMPARE(group.paths.size(), 2);
        QVERIFY(group.paths.contains(second));
        // Whichever hard-link name is encountered first represents the inode.
        QVERIFY(group.paths.contains(first)
                || group.paths.contains(temporary.filePath(QStringLiteral("hard-link"))));
        QCOMPARE(group.sha256, QCryptographicHash::hash(contents, QCryptographicHash::Sha256));
        QCOMPARE(result.redundantBytes, quint64(contents.size()));
        const auto includeSmall = findDuplicates(temporary.path(), 0);
        QCOMPARE(includeSmall.groups.size(), 2); // Empty files remain excluded.
        QCOMPARE(includeSmall.redundantBytes, quint64(contents.size() + 4));
    }

    void duplicateBoundsAndCancellation()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        for (int i = 0; i < 3; ++i)
            QVERIFY(writeFile(temporary.filePath(QString::number(i)), QByteArray(100, 'x')));
        const auto bounded = findDuplicates(temporary.path(), 1, nullptr, {}, 2);
        QVERIFY(bounded.error.isEmpty());
        QVERIFY(bounded.truncated);
        QCOMPARE(bounded.scannedFiles, quint64(2));
        QCOMPARE(bounded.groups.size(), 1);
        QCOMPARE(bounded.redundantBytes, quint64(100));
        QVERIFY(!findDuplicates(temporary.path(), 1, nullptr, {}, 0).error.isEmpty());
        QVERIFY(findDuplicates(temporary.path(), 101).groups.isEmpty());
        std::atomic_bool cancel {true};
        QVERIFY(findDuplicates(temporary.path(), 1, &cancel).canceled);
        cancel.store(false);
        int contentNotifications = 0;
        const auto canceled = findDuplicates(temporary.path(), 1, &cancel,
            [&](quint64 count, const QString &) {
                if (count == 3 && ++contentNotifications == 2)
                    cancel.store(true); // Stop before full hash after sampling.
            });
        QVERIFY(canceled.canceled);
        QVERIFY(canceled.groups.isEmpty());
        QCOMPARE(canceled.hashedFiles, quint64(0));
    }

    void changedFileBetweenSampleAndFullHashIsExcluded()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        for (int i = 0; i < 3; ++i)
            QVERIFY(writeFile(temporary.filePath(QString::number(i)), QByteArray(300 * 1024, 'x')));
        int contentNotifications = 0;
        QString changed;
        bool mutationSucceeded = false;
        const auto result = findDuplicates(temporary.path(), 1, nullptr,
            [&](quint64 count, const QString &path) {
                if (count != 3 || ++contentNotifications != 2)
                    return;
                changed = path;
                struct stat before {};
                if (::stat(QFile::encodeName(path).constData(), &before) != 0)
                    return;
                QFile file(path);
                if (!file.open(QIODevice::ReadWrite) || !file.seek(150 * 1024) || file.write("y", 1) != 1)
                    return;
                file.close();
                // Preserve mtime to exercise the ctime check as well.
                const timespec times[2] = {before.st_atim, before.st_mtim};
                mutationSucceeded = ::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times, 0) == 0;
            });
        QVERIFY(mutationSucceeded);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.groups.size(), 1);
        QCOMPARE(result.groups.first().paths.size(), 2);
        QVERIFY(!result.groups.first().paths.contains(changed));
        QVERIFY(result.skippedEntries >= 1);
    }

    void cancellationDuringLargeFileHash()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        for (int i = 0; i < 2; ++i) {
            QFile sparse(temporary.filePath(QString::number(i)));
            QVERIFY(sparse.open(QIODevice::WriteOnly));
            QVERIFY(sparse.resize(512 * 1024 * 1024));
        }
        std::atomic_bool cancel {false};
        std::thread canceler;
        int contentNotifications = 0;
        const auto result = findDuplicates(temporary.path(), 1, &cancel,
            [&](quint64 count, const QString &) {
                if (count == 2 && ++contentNotifications == 2) {
                    canceler = std::thread([&] {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        cancel.store(true);
                    });
                }
            });
        if (canceler.joinable())
            canceler.join();
        QVERIFY(result.canceled);
        QVERIFY(result.groups.isEmpty());
        QCOMPARE(result.hashedFiles, quint64(0));
    }

    void swappedAncestorIsNeverFollowedForContent()
    {
        QTemporaryDir temporary;
        QTemporaryDir outside;
        QVERIFY(temporary.isValid());
        QVERIFY(outside.isValid());
        QVERIFY(QDir(temporary.path()).mkdir(QStringLiteral("sub")));
        for (int i = 0; i < 2; ++i) {
            QVERIFY(writeFile(temporary.filePath(QStringLiteral("sub/") + QString::number(i)), QByteArray(100, 'x')));
            QVERIFY(writeFile(outside.filePath(QString::number(i)), QByteArray(100, 'x')));
        }
        bool swapped = false;
        const auto result = findDuplicates(temporary.path(), 1, nullptr,
            [&](quint64 count, const QString &) {
                if (count != 2 || swapped)
                    return;
                const QString original = temporary.filePath(QStringLiteral("sub"));
                if (QDir().rename(original, temporary.filePath(QStringLiteral("moved"))))
                    swapped = ::symlink(QFile::encodeName(outside.path()).constData(),
                                        QFile::encodeName(original).constData()) == 0;
            });
        QVERIFY(swapped);
        QVERIFY(result.groups.isEmpty());
        QCOMPARE(result.hashedFiles, quint64(0));
        QCOMPARE(result.skippedEntries, quint64(2));
    }
};

QTEST_GUILESS_MAIN(SpaceAnalyzerTest)
#include "spaceanalyzer_test.moc"
