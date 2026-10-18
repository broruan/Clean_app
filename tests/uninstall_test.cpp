#include "uninstallcontroller.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QtTest>

class UninstallTest : public QObject {
    Q_OBJECT
private slots:
    void executeRequiresPreview();
    void invalidIdentifiers_data();
    void invalidIdentifiers();
    void protectedDesktopComponents_data();
    void protectedDesktopComponents();
    void snapParallelInstanceIdentifierIsAccepted();
    void essentialMetadataOverridesForgedEntry();
    void harmlessReadOnlyPreview();
    void synchronousPreviewCanBeginAndCancelRevalidation();
    void canceledPreviewCannotExecute();
};

void UninstallTest::executeRequiresPreview()
{
    UninstallController controller;
    QSignalSpy finished(&controller, &UninstallController::finished);
    controller.execute();
    QCOMPARE(finished.size(), 1);
    QVERIFY(!finished.first().at(0).toBool());
    QVERIFY(!controller.isBusy());
    QVERIFY(!controller.isExecuting());
}

void UninstallTest::invalidIdentifiers_data()
{
    QTest::addColumn<int>("source");
    QTest::addColumn<QString>("id");
    QTest::addColumn<QString>("scope");
    QTest::addColumn<QString>("ref");
    QTest::newRow("deb-option") << int(PackageSource::Deb) << QStringLiteral("--allow-remove-essential") << QString() << QString();
    QTest::newRow("deb-shell") << int(PackageSource::Deb) << QStringLiteral("gedit; touch /tmp/uninstall-test-bad") << QString() << QString();
    QTest::newRow("deb-path") << int(PackageSource::Deb) << QStringLiteral("../bash") << QString() << QString();
    QTest::newRow("snap-option") << int(PackageSource::Snap) << QStringLiteral("--purge") << QString() << QString();
    QTest::newRow("snap-core") << int(PackageSource::Snap) << QStringLiteral("core22") << QString() << QString();
    QTest::newRow("snap-core-instance") << int(PackageSource::Snap) << QStringLiteral("core22_test") << QString() << QString();
    QTest::newRow("snap-shared-mesa") << int(PackageSource::Snap) << QStringLiteral("mesa-2404") << QString() << QString();
    QTest::newRow("flatpak-runtime") << int(PackageSource::Flatpak) << QStringLiteral("org.example.App") << QStringLiteral("user") << QStringLiteral("runtime/org.example.App/x86_64/stable");
    QTest::newRow("flatpak-scope") << int(PackageSource::Flatpak) << QStringLiteral("org.example.App") << QStringLiteral("--all") << QString();
    QTest::newRow("flatpak-id") << int(PackageSource::Flatpak) << QStringLiteral("org.example.App --all") << QStringLiteral("user") << QString();
    QTest::newRow("unknown-source") << 99 << QStringLiteral("any") << QString() << QString();
}

void UninstallTest::invalidIdentifiers()
{
    QFETCH(int, source);
    QFETCH(QString, id);
    QFETCH(QString, scope);
    QFETCH(QString, ref);
    AppEntry app;
    app.id = id;
    app.source = static_cast<PackageSource>(source);
    app.scope = scope;
    app.targetRef = ref;
    UninstallController controller;
    QSignalSpy finished(&controller, &UninstallController::finished);
    QSignalSpy preview(&controller, &UninstallController::previewReady);
    controller.preview(app);
    QCOMPARE(finished.size(), 1);
    QVERIFY(!finished.first().at(0).toBool());
    QVERIFY(preview.isEmpty());
    QVERIFY(!controller.isExecuting());
    QVERIFY(!controller.isBusy());
}

void UninstallTest::protectedDesktopComponents_data()
{
    QTest::addColumn<QString>("id");
    QTest::newRow("gnome-settings") << QStringLiteral("gnome-settings-daemon");
    QTest::newRow("gnome-settings-ui") << QStringLiteral("gnome-control-center");
    QTest::newRow("window-manager") << QStringLiteral("mutter");
    QTest::newRow("xwayland") << QStringLiteral("xwayland");
    QTest::newRow("snap-backend") << QStringLiteral("snapd");
    QTest::newRow("flatpak-backend") << QStringLiteral("flatpak");
}

void UninstallTest::protectedDesktopComponents()
{
    QFETCH(QString, id);
    AppEntry forged;
    forged.id = id;
    forged.removable = true;
    UninstallController controller;
    QSignalSpy finished(&controller, &UninstallController::finished);
    QSignalSpy ready(&controller, &UninstallController::previewReady);
    controller.preview(forged);
    QCOMPARE(finished.size(), 1);
    QVERIFY(!finished.first().at(0).toBool());
    QVERIFY(ready.isEmpty());
    QVERIFY(!controller.isBusy());
}

void UninstallTest::snapParallelInstanceIdentifierIsAccepted()
{
    if (!QFileInfo(QStringLiteral("/usr/bin/snap")).isExecutable())
        QSKIP("Snap command is unavailable");
    UninstallController controller;
    QSignalSpy finished(&controller, &UninstallController::finished);
    AppEntry app;
    app.id = QStringLiteral("firefox_work123");
    app.source = PackageSource::Snap;
    controller.preview(app);
    // A syntactically valid instance must reach the read-only installed query,
    // rather than being rejected by validation. Do not wait for or contact snapd.
    QVERIFY(controller.isBusy());
    QVERIFY(finished.isEmpty());
    QVERIFY(!controller.isExecuting());
    controller.cancelPreview();
    QVERIFY(!controller.isBusy());
}

void UninstallTest::essentialMetadataOverridesForgedEntry()
{
    if (!QFileInfo::exists(QStringLiteral("/usr/bin/dpkg-query")))
        QSKIP("DEB metadata tool is unavailable");
    QProcess query;
    query.start(QStringLiteral("/usr/bin/dpkg-query"),
                {QStringLiteral("-W"), QStringLiteral("-f=${db:Status-Status}"), QStringLiteral("base-files")});
    QVERIFY(query.waitForFinished(10000));
    if (query.exitCode() != 0 || query.readAllStandardOutput().trimmed() != QByteArrayLiteral("installed"))
        QSKIP("base-files is not installed on this system");

    AppEntry forged;
    forged.id = QStringLiteral("base-files");
    forged.name = QStringLiteral("Harmless application");
    forged.removable = true;
    UninstallController controller;
    QSignalSpy finished(&controller, &UninstallController::finished);
    QSignalSpy preview(&controller, &UninstallController::previewReady);
    controller.preview(forged);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
    QVERIFY(!finished.first().at(0).toBool());
    QVERIFY(finished.first().at(1).toString().contains(QStringLiteral("受保护")));
    QVERIFY(preview.isEmpty());
    QVERIFY(!controller.isBusy());
    QVERIFY(!controller.isExecuting());
}

void UninstallTest::harmlessReadOnlyPreview()
{
    if (!QFileInfo::exists(QStringLiteral("/usr/bin/apt-get")))
        QSKIP("APT is unavailable");
    QProcess query;
    query.start(QStringLiteral("/usr/bin/dpkg-query"),
                {QStringLiteral("-W"), QStringLiteral("-f=${db:Status-Status}\t${Architecture}"), QStringLiteral("baobab")});
    QVERIFY(query.waitForFinished(10000));
    const QList<QByteArray> metadata = query.readAllStandardOutput().trimmed().split('\t');
    if (query.exitCode() != 0 || metadata.value(0) != QByteArrayLiteral("installed"))
        QSKIP("Optional baobab application is not installed; only safe preview tests apply");
    QCOMPARE(metadata.size(), 2);
    QVERIFY(!metadata.at(1).isEmpty());
    const QString qualifiedId = QStringLiteral("baobab:") + QString::fromLatin1(metadata.at(1));

    AppEntry app;
    app.id = QStringLiteral("baobab");
    UninstallController controller;
    QSignalSpy ready(&controller, &UninstallController::previewReady);
    QSignalSpy finished(&controller, &UninstallController::finished);
    controller.preview(app);
    QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !finished.isEmpty(), 60000);
    QVERIFY2(finished.isEmpty(), finished.isEmpty() ? "" : qPrintable(finished.first().at(1).toString()));
    QCOMPARE(ready.size(), 1);
    const QStringList affected = ready.first().at(1).toStringList();
    QVERIFY(affected.contains(QStringLiteral("baobab")) || affected.contains(qualifiedId));
    QVERIFY(!controller.isBusy());
    QVERIFY(!controller.isExecuting());
    // Never call execute after a valid plan: tests must not authorize removal.
    controller.cancelPreview();
}

void UninstallTest::canceledPreviewCannotExecute()
{
    UninstallController controller;
    AppEntry app;
    app.id = QStringLiteral("base-files");
    controller.preview(app);
    controller.cancelPreview();
    QSignalSpy finished(&controller, &UninstallController::finished);
    controller.execute();
    QCOMPARE(finished.size(), 1);
    QVERIFY(!finished.first().at(0).toBool());
    QVERIFY(!controller.isBusy());
    QVERIFY(!controller.isExecuting());
}

void UninstallTest::synchronousPreviewCanBeginAndCancelRevalidation()
{
    QProcess query;
    query.start(QStringLiteral("/usr/bin/dpkg-query"),
                {QStringLiteral("-W"), QStringLiteral("-f=${db:Status-Status}"), QStringLiteral("baobab")});
    QVERIFY(query.waitForFinished(10000));
    if (query.exitCode() != 0 || query.readAllStandardOutput().trimmed() != QByteArrayLiteral("installed"))
        QSKIP("Optional baobab application is not installed");

    UninstallController controller;
    QSignalSpy finished(&controller, &UninstallController::finished);
    bool revalidated = false;
    connect(&controller, &UninstallController::previewReady, &controller,
            [&controller, &revalidated](const QString &, const QStringList &) {
        QVERIFY(!controller.isBusy());
        controller.execute();
        QVERIFY(controller.isBusy());
        QVERIFY(!controller.isExecuting());
        // This is the same synchronous signal path used by the confirmation UI.
        // Cancel immediately, before any event-loop iteration: only dpkg-query
        // can have started, never pkexec or an actual removal process.
        controller.cancelPreview();
        QVERIFY(!controller.isBusy());
        QVERIFY(!controller.isExecuting());
        revalidated = true;
    });
    AppEntry app;
    app.id = QStringLiteral("baobab");
    controller.preview(app);
    QTRY_VERIFY_WITH_TIMEOUT(revalidated || !finished.isEmpty(), 60000);
    QVERIFY(revalidated);
    QVERIFY(finished.isEmpty());
    controller.execute();
    QCOMPARE(finished.size(), 1);
    QVERIFY(!finished.first().at(0).toBool());
}

QTEST_GUILESS_MAIN(UninstallTest)
#include "uninstall_test.moc"
