#include "appstorage.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void writeFixture(const QString &path, int size = 8192)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArray(size, 'a')), qint64(size));
}
}

class AppStorageTest : public QObject {
    Q_OBJECT
private slots:
    void namespacesAreExactAndDataIsNotCalledCache();
    void catalogAndUnknownPackages();
    void metadataMeasurementAndCancellation();
    void symlinkIsNotMeasured();
};

void AppStorageTest::namespacesAreExactAndDataIsNotCalledCache()
{
    QTemporaryDir fixture; QVERIFY(fixture.isValid()); const QString home = fixture.path();
    writeFixture(home + "/.var/app/org.example.Editor/config/profile");
    writeFixture(home + "/.var/app/org.example.EditorExtra/cache/other");
    AppEntry app; app.id = "org.example.Editor"; app.source = PackageSource::Flatpak;
    auto locations = discoverAppStorage(app, home, home + "/.cache", home + "/.config");
    QCOMPARE(locations.size(), 1);
    QCOMPARE(locations.first().path, home + "/.var/app/org.example.Editor");
    QVERIFY(locations.first().category.contains(QStringLiteral("数据")));
    app.id = "../Editor";
    QVERIFY(discoverAppStorage(app, home, home + "/.cache", home + "/.config").isEmpty());
    app.id = "editor"; app.source = PackageSource::Snap;
    writeFixture(home + "/snap/editor/common/profile");
    locations = discoverAppStorage(app, home, home + "/.cache", home + "/.config");
    QCOMPARE(locations.size(), 1);
    QVERIFY(locations.first().attribution.contains(QStringLiteral("用户数据")));
}

void AppStorageTest::catalogAndUnknownPackages()
{
    QTemporaryDir fixture; QVERIFY(fixture.isValid()); const QString home = fixture.path();
    writeFixture(home + "/.cache/google-chrome/Cache/data");
    writeFixture(home + "/.config/google-chrome/Default/Bookmarks");
    AppEntry app; app.id = "google-chrome-stable:amd64";
    const auto locations = discoverAppStorage(app, home, home + "/.cache", home + "/.config");
    QCOMPARE(locations.size(), 2);
    QVERIFY(locations.at(1).category.contains(QStringLiteral("用户数据")));
    app.id = "google-chrome-unrelated";
    QVERIFY(discoverAppStorage(app, home, home + "/.cache", home + "/.config").isEmpty());
    app.id = "google-chrome-stable";
    QVERIFY(discoverAppStorage(app, home, "relative-cache", home + "/.config").isEmpty());
}

void AppStorageTest::metadataMeasurementAndCancellation()
{
    QTemporaryDir fixture; QVERIFY(fixture.isValid()); const QString home = fixture.path();
    writeFixture(home + "/.config/Code/User/settings.json");
    writeFixture(home + "/.vscode/extensions/example/package.json", 4096);
    AppEntry app; app.id = "code";
    const auto locations = discoverAppStorage(app, home, home + "/.cache", home + "/.config");
    QCOMPARE(locations.size(), 2);
    const auto result = measureAppStorage(locations);
    QVERIFY(!result.canceled);
    quint64 logical = 0;
    for (const auto &location : result.locations) {
        QVERIFY(location.measured); QVERIFY(!location.partial);
        QCOMPARE(location.fileCount, quint64(1)); logical += location.logicalBytes;
        QVERIFY(QFileInfo(location.path).isDir());
    }
    QCOMPARE(logical, quint64(8192 + 4096));
    std::atomic_bool cancel{true};
    const auto canceled = measureAppStorage(locations, &cancel);
    QVERIFY(canceled.canceled);
    for (const auto &location : canceled.locations) QVERIFY(!location.measured);
    QVERIFY(QFileInfo(home + "/.config/Code/User/settings.json").exists());
}

void AppStorageTest::symlinkIsNotMeasured()
{
    QTemporaryDir fixture; QVERIFY(fixture.isValid()); const QString home = fixture.path();
    writeFixture(home + "/outside/private", 4096);
    QVERIFY(QDir().mkpath(home + "/.cache"));
    QVERIFY(QFile::link(home + "/outside", home + "/.cache/google-chrome"));
    AppEntry app; app.id = "google-chrome-stable";
    QVERIFY(discoverAppStorage(app, home, home + "/.cache", home + "/.config").isEmpty());
    AppStorageLocation forged; forged.path = home + "/.cache/google-chrome";
    const auto result = measureAppStorage({forged});
    QVERIFY(!result.locations.first().measured);
    QVERIFY(!result.locations.first().error.isEmpty());
}

QTEST_GUILESS_MAIN(AppStorageTest)
#include "appstorage_test.moc"
