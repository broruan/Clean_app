#include "packagebackend.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

// A read-only integration check against the host package database. This test
// intentionally never constructs an uninstall command or modifies packages.
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const InventoryResult result = readInstalledSoftware();
    int failures = 0;
    auto check = [&failures](bool condition, const QString &message) {
        if (!condition) {
            qCritical().noquote() << message;
            ++failures;
        }
    };
    check(!result.apps.isEmpty(), QStringLiteral("Inventory must contain installed software"));
    QSet<QString> unique;
    int desktopCount = 0;
    int debCount = 0;
    int snapCount = 0;
    const AppEntry *bash = nullptr;
    const AppEntry *dpkg = nullptr;
    for (const AppEntry &entry : result.apps) {
        const QString key = sourceName(entry.source) + QLatin1Char(':') + entry.scope + QLatin1Char(':') + entry.targetRef;
        check(!unique.contains(key), QStringLiteral("Duplicate inventory identity: ") + key);
        unique.insert(key);
        check(!entry.id.isEmpty() && !entry.name.isEmpty() && !entry.targetRef.isEmpty(),
              QStringLiteral("Missing identity or display name: ") + key);
        check(entry.removable || !entry.protectionReason.isEmpty(), QStringLiteral("Protected entry lacks reason: ") + key);
        if (entry.desktopApp)
            ++desktopCount;
        if (entry.source == PackageSource::Deb) {
            ++debCount;
            if (entry.id == QStringLiteral("bash"))
                bash = &entry;
            if (entry.id == QStringLiteral("dpkg"))
                dpkg = &entry;
        } else if (entry.source == PackageSource::Snap) {
            ++snapCount;
            const QFileInfo current(QStringLiteral("/snap/") + entry.id + QStringLiteral("/current"));
            if (current.exists()) {
                const QString revision = QFileInfo(current.symLinkTarget()).fileName();
                const QFileInfo archive(QStringLiteral("/var/lib/snapd/snaps/") + entry.id + QLatin1Char('_') + revision + QStringLiteral(".snap"));
                if (archive.isFile())
                    check(entry.installedBytes == static_cast<quint64>(archive.size()), QStringLiteral("Snap archive size mismatch: ") + entry.id);
            }
        }
    }
    if (QFileInfo::exists(QStringLiteral("/var/lib/dpkg/status"))) {
        check(debCount > 0, QStringLiteral("DEB inventory must exist on this system"));
        check(bash && !bash->removable, QStringLiteral("Essential bash package must be protected"));
        check(dpkg && !dpkg->removable, QStringLiteral("Essential dpkg package must be protected"));
        QProcess query;
        query.start(QStringLiteral("dpkg-query"), {QStringLiteral("-W"), QStringLiteral("-f=${Installed-Size}"), QStringLiteral("bash")});
        check(query.waitForFinished(10000), QStringLiteral("Reference dpkg-query timed out"));
        bool validSize = false;
        const quint64 referenceKibibytes = query.readAllStandardOutput().trimmed().toULongLong(&validSize);
        check(validSize && bash && bash->installedBytes == referenceKibibytes * 1024,
              QStringLiteral("DEB Installed-Size must use KiB converted to bytes"));
    }
    check(desktopCount > 0, QStringLiteral("Host desktop application mapping must yield visible apps"));

    // Exercise Flatpak field parsing even on a machine without installed
    // Flatpaks. Only a temporary executable is substituted; the DEB/Snap
    // inventories remain real and no package installation is performed.
    QTemporaryDir fixtureDirectory;
    check(fixtureDirectory.isValid(), QStringLiteral("Cannot create temporary Flatpak fixture"));
    if (fixtureDirectory.isValid()) {
        QFile fixture(fixtureDirectory.path() + QStringLiteral("/flatpak"));
        check(fixture.open(QIODevice::WriteOnly), QStringLiteral("Cannot write Flatpak fixture"));
        fixture.write(
            "#!/bin/sh\ncat <<'INVENTORY_FIXTURE'\n"
            "org.example.Sample\tSample <b>plain text</b>\t1.0\t12.5 MB\tsystem\torg.example.Sample/x86_64/stable\tSample application\n"
            "org.example.Sample\tUser Sample\t2.0\t1.5 MiB\tuser\tapp/org.example.Sample/aarch64/beta\tUser installation\n"
            "org.example.Unknown\tUnknown size\t3.0\tunknown\tuser\torg.example.Unknown/x86_64/stable\tNo invented size\n"
            "--bad-id\tInvalid\t0\t5 GB\tuser\tapp/--bad-id/x86_64/stable\tInvalid identifier\n"
            "org.example.Mismatch\tMismatch\t0\t5 GB\tuser\tapp/org.example.Other/x86_64/stable\tInvalid ref\n"
            "INVENTORY_FIXTURE\n");
        fixture.close();
        check(fixture.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner),
              QStringLiteral("Cannot make Flatpak fixture executable"));
        const QByteArray originalPath = qgetenv("PATH");
        qputenv("PATH", fixtureDirectory.path().toLocal8Bit() + ':' + originalPath);
        const InventoryResult fixtureResult = readInstalledSoftware();
        qputenv("PATH", originalPath);
        int sampleCount = 0;
        bool unknownFound = false;
        for (const AppEntry &entry : fixtureResult.apps) {
            if (entry.source != PackageSource::Flatpak)
                continue;
            if (entry.id == QStringLiteral("org.example.Sample")) {
                ++sampleCount;
                check(entry.desktopApp && entry.targetRef.startsWith(QStringLiteral("app/org.example.Sample/")),
                      QStringLiteral("Flatpak app ref must be normalized"));
                if (entry.scope == QStringLiteral("system")) {
                    check(entry.installedBytes == 12500000, QStringLiteral("Flatpak decimal MB size conversion failed"));
                    check(entry.name == QStringLiteral("Sample <b>plain text</b>"), QStringLiteral("Flatpak display metadata must remain literal text"));
                } else {
                    check(entry.scope == QStringLiteral("user") && entry.installedBytes == 1572864,
                          QStringLiteral("Flatpak user scope or MiB conversion failed"));
                }
            } else if (entry.id == QStringLiteral("org.example.Unknown")) {
                unknownFound = true;
                check(entry.installedBytes == 0, QStringLiteral("Unknown Flatpak size must remain unknown"));
            }
            check(entry.id != QStringLiteral("--bad-id") && entry.id != QStringLiteral("org.example.Mismatch"),
                  QStringLiteral("Invalid Flatpak operation target was accepted"));
        }
        check(sampleCount == 2, QStringLiteral("Flatpak user/system applications must keep separate identities"));
        check(unknownFound, QStringLiteral("Unknown-size Flatpak fixture was lost"));
    }
    qInfo().noquote() << QStringLiteral("Inventory: %1 entries, %2 desktop apps, %3 DEB, %4 Snap; %5 failures")
                            .arg(result.apps.size()).arg(desktopCount).arg(debCount).arg(snapCount).arg(failures);
    for (const QString &warning : result.warnings)
        qInfo().noquote() << warning;
    return failures ? 1 : 0;
}
