#include "filescanner.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

class ScannerTest : public QObject {
    Q_OBJECT

private:
    static bool makeFile(const QString &path, qint64 bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(QByteArray(int(bytes), 'x')) == bytes;
    }

private slots:
    void hiddenNestedAndBoundedResults()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QVERIFY(QDir(temporary.path()).mkdir(QStringLiteral("nested")));
        QVERIFY(makeFile(temporary.filePath(QStringLiteral("small")), 10));
        QVERIFY(makeFile(temporary.filePath(QStringLiteral(".hidden")), 20));
        QVERIFY(makeFile(temporary.filePath(QStringLiteral("nested/largest")), 40));
        QVERIFY(makeFile(temporary.filePath(QStringLiteral("same-size-a")), 30));
        QVERIFY(makeFile(temporary.filePath(QStringLiteral("same-size-b")), 30));

        const ScanResult result = scanLargeFiles(temporary.path(), 20, 2, nullptr);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.canceled);
        QCOMPARE(result.scannedFiles, quint64(5));
        QCOMPARE(result.matchingFiles, quint64(4));
        QCOMPARE(result.totalMatchingBytes, quint64(120));
        QCOMPARE(result.files.size(), 2);
        QCOMPARE(result.files.at(0).bytes, quint64(40));
        QCOMPARE(result.files.at(1).path, temporary.filePath(QStringLiteral("same-size-a")));
        QVERIFY(result.files.at(0).modified.isValid());

        const auto all = scanLargeFiles(temporary.path(), 0, 100, nullptr);
        QCOMPARE(all.files.size(), 5);
        const auto none = scanLargeFiles(temporary.path(), 41, 100, nullptr);
        QCOMPARE(none.scannedFiles, quint64(5));
        QCOMPARE(none.matchingFiles, quint64(0));
        QVERIFY(none.files.isEmpty());
    }

    void linksAndSparseAllocation()
    {
        QTemporaryDir temporary;
        QTemporaryDir outside;
        QVERIFY(temporary.isValid());
        QVERIFY(outside.isValid());
        QVERIFY(QDir(outside.path()).mkdir(QStringLiteral("subdir")));
        const QString original = temporary.filePath(QStringLiteral("original"));
        QVERIFY(makeFile(original, 128));
        QVERIFY(makeFile(outside.filePath(QStringLiteral("outside-file")), 999));
        QCOMPARE(::link(QFile::encodeName(original).constData(),
                        QFile::encodeName(temporary.filePath(QStringLiteral("hardlink"))).constData()), 0);
        QCOMPARE(::symlink(QFile::encodeName(original).constData(),
                           QFile::encodeName(temporary.filePath(QStringLiteral("symlink-file"))).constData()), 0);
        QCOMPARE(::symlink(QFile::encodeName(outside.path()).constData(),
                           QFile::encodeName(temporary.filePath(QStringLiteral("symlink-dir"))).constData()), 0);
        QCOMPARE(::symlink(".",
                           QFile::encodeName(temporary.filePath(QStringLiteral("loop"))).constData()), 0);

        const QString sparsePath = temporary.filePath(QStringLiteral("sparse"));
        QFile sparse(sparsePath);
        QVERIFY(sparse.open(QIODevice::WriteOnly));
        constexpr qint64 sparseBytes = 16 * 1024 * 1024;
        QVERIFY(sparse.resize(sparseBytes));
        sparse.close();
        struct stat sparseStat {};
        QCOMPARE(::stat(QFile::encodeName(sparsePath).constData(), &sparseStat), 0);

        const ScanResult result = scanLargeFiles(temporary.path(), 0, 10, nullptr);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.scannedFiles, quint64(2));
        QCOMPARE(result.matchingFiles, quint64(2));
        QCOMPARE(result.totalMatchingBytes, quint64(sparseBytes + 128));
        QCOMPARE(result.files.size(), 2);
        QCOMPARE(result.files.first().path, sparsePath);
        QCOMPARE(result.files.first().bytes, quint64(sparseBytes));
        QCOMPARE(result.files.first().allocatedBytes, quint64(sparseStat.st_blocks) * 512);
        QVERIFY(result.files.first().allocatedBytes < result.files.first().bytes);

        const auto symlinkRoot = scanLargeFiles(temporary.filePath(QStringLiteral("symlink-dir")), 0, 10, nullptr);
        QVERIFY(!symlinkRoot.error.isEmpty());
        const auto symlinkAncestor = scanLargeFiles(temporary.filePath(QStringLiteral("symlink-dir/subdir")), 0, 10, nullptr);
        QVERIFY(!symlinkAncestor.error.isEmpty());
    }

    void invalidArgumentsAndCancellation()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString regularPath = temporary.filePath(QStringLiteral("regular"));
        QVERIFY(makeFile(regularPath, 8));
        QVERIFY(!scanLargeFiles(QString(), 0, 10, nullptr).error.isEmpty());
        QVERIFY(!scanLargeFiles(temporary.filePath(QStringLiteral("missing")), 0, 10, nullptr).error.isEmpty());
        QVERIFY(!scanLargeFiles(regularPath, 0, 10, nullptr).error.isEmpty());
        QVERIFY(!scanLargeFiles(temporary.path(), 0, 0, nullptr).error.isEmpty());
        QVERIFY(!scanLargeFiles(temporary.path(), 0, -1, nullptr).error.isEmpty());

        std::atomic_bool cancel {true};
        const auto alreadyCanceled = scanLargeFiles(temporary.path(), 0, 10, &cancel);
        QVERIFY(alreadyCanceled.canceled);
        QCOMPARE(alreadyCanceled.scannedFiles, quint64(0));

        cancel.store(false);
        int notifications = 0;
        const auto canceled = scanLargeFiles(temporary.path(), 0, 10, &cancel,
            [&cancel, &notifications](quint64, const QString &) {
                ++notifications;
                cancel.store(true);
            });
        QVERIFY(canceled.canceled);
        QCOMPARE(canceled.scannedFiles, quint64(0));
        QCOMPARE(notifications, 1);
    }

    void cancellationDuringScanAndProgressThrottle()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QVERIFY(makeFile(temporary.filePath(QStringLiteral("first")), 12));
        QVERIFY(makeFile(temporary.filePath(QStringLiteral("second")), 13));
        std::atomic_bool cancel {false};
        QElapsedTimer clock;
        clock.start();
        qint64 previousNotification = -150;
        bool throttled = true;
        const auto result = scanLargeFiles(temporary.path(), 0, 10, &cancel,
            [&](quint64 scannedFiles, const QString &) {
                const qint64 now = clock.elapsed();
                throttled = throttled && now - previousNotification >= 150;
                previousNotification = now;
                if (scannedFiles > 0)
                    cancel.store(true);
                else
                    QTest::qSleep(160);
            });
        QVERIFY(throttled);
        QVERIFY(result.canceled);
        QCOMPARE(result.scannedFiles, quint64(1));
        QCOMPARE(result.files.size(), 1);
    }

    void permissionsAndSpecialFiles()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString privateDirectory = temporary.filePath(QStringLiteral("private"));
        QVERIFY(QDir().mkdir(privateDirectory));
        QVERIFY(makeFile(privateDirectory + QStringLiteral("/secret"), 100));
        const QString pipe = temporary.filePath(QStringLiteral("pipe"));
        QCOMPARE(::mkfifo(QFile::encodeName(pipe).constData(), 0600), 0);
        QCOMPARE(::chmod(QFile::encodeName(privateDirectory).constData(), 0000), 0);
        const auto result = scanLargeFiles(temporary.path(), 0, 10, nullptr);
        // Restore access even when an assertion below fails, for clean teardown.
        QCOMPARE(::chmod(QFile::encodeName(privateDirectory).constData(), 0700), 0);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        if (::geteuid() != 0) {
            QCOMPARE(result.skippedEntries, quint64(1));
            QCOMPARE(result.scannedFiles, quint64(0));
        } else {
            QCOMPARE(result.scannedFiles, quint64(1));
        }
    }
};

QTEST_GUILESS_MAIN(ScannerTest)

#include "scanner_test.moc"
