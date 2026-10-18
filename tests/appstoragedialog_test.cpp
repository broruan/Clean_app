#include "appstoragedialog.h"
#include "widgets.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

class AppStorageDialogTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QApplication::setStyle("Fusion");
        QApplication::setFont(QFont("Noto Sans CJK SC", 10));
        qApp->setStyleSheet(applicationStyle());
    }
    void canInspectAndCopyWithoutChangingData()
    {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString path = fixture.path() + "/.config/Code/User";
        QVERIFY(QDir().mkpath(path));
        QFile file(path + "/settings.json"); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("keep my settings"), qint64(16)); file.close();
        AppEntry app; app.id = "code"; app.name = "Code <b>literal</b>"; app.installedBytes = 123;
        AppStorageDialog dialog(app, nullptr, fixture.path(), fixture.path() + "/.cache", fixture.path() + "/.config");
        dialog.show(); QTRY_VERIFY_WITH_TIMEOUT(dialog.isSettled(), 10000);
        auto *table = dialog.findChild<QTableWidget*>("appStorageTable"); QVERIFY(table);
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 3)->text(), QStringLiteral("1"));
        auto *copy = dialog.findChild<QPushButton*>("appStorageCopy"); QVERIFY(copy); QVERIFY(!copy->isEnabled());
        table->selectRow(0); QVERIFY(copy->isEnabled());
        QTest::mouseClick(copy, Qt::LeftButton);
        QCOMPARE(QApplication::clipboard()->text(), fixture.path() + "/.config/Code");
        auto *title = dialog.findChild<QLabel*>("cardTitle"); QVERIFY(title);
        QCOMPARE(title->textFormat(), Qt::PlainText); QVERIFY(title->text().contains("<b>literal</b>"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("keep my settings"));
        QVERIFY(dialog.grab().width() >= 760);
    }
    void unknownApplicationDoesNotPretendZeroData()
    {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        AppEntry app; app.id = "unknown"; app.name = "Unknown";
        AppStorageDialog dialog(app, nullptr, fixture.path(), fixture.path() + "/.cache", fixture.path() + "/.config");
        dialog.show(); QTRY_VERIFY_WITH_TIMEOUT(dialog.isSettled(), 10000);
        auto *status = dialog.findChild<QLabel*>("appStorageStatus"); QVERIFY(status);
        QVERIFY(status->text().contains(QStringLiteral("不表示")));
        QCOMPARE(dialog.findChild<QTableWidget*>("appStorageTable")->rowCount(), 0);
    }
};
QTEST_MAIN(AppStorageDialogTest)
#include "appstoragedialog_test.moc"
