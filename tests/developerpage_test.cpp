#include "developerpage.h"
#include "widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool write(const QString &path, const QByteArray &data = QByteArray(8192, 'x'))
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
bool fixture(const QString &root)
{
    return write(root + "/robot/build/.built_by", "colcon") && write(root + "/robot/build/nav.o")
        && write(root + "/recording/metadata.yaml", "rosbag2_bagfile_information: {}")
        && write(root + "/recording/test.db3") && write(root + "/clouds/point.pcd");
}
}

class DeveloperPageTest : public QObject {
    Q_OBJECT
private slots:
    void lazyScanningFilterDetailsAndExport();
    void productionSelectedTextAndCancel();
    void changeRootAndDestructorCancel();
};

void DeveloperPageTest::lazyScanningFilterDetailsAndExport()
{
    QTemporaryDir dir; QVERIFY(dir.isValid()); QVERIFY(fixture(dir.path()));
    DeveloperPage page; page.setRoot(dir.path());
    QVERIFY(!page.isBusy()); QVERIFY(!page.isSettled());
    auto *table = page.findChild<QTableView *>("developerTable"); QVERIFY(table); QCOMPARE(table->model()->rowCount(), 0);
    auto *search = page.findChild<QLineEdit *>("developerSearch"); QVERIFY(search);
    auto *category = page.findChild<QComboBox *>("developerCategory"); QVERIFY(category);
    auto *copy = page.findChild<QPushButton *>("developerCopy"); QVERIFY(copy); QVERIFY(!copy->isEnabled());
    auto *detail = page.findChild<QLabel *>("developerDetail"); QVERIFY(detail);
    QString error; QVERIFY(!page.exportReport(dir.path() + "/before.csv", &error));
    page.resize(1150, 850); page.show(); page.ensureScanStarted();
    QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(), 10000);
    QVERIFY2(page.verifyUi(&error), qPrintable(error)); QCOMPARE(table->model()->rowCount(), 3);
    search->setText(QStringLiteral("recording")); QCOMPARE(table->model()->rowCount(), 1);
    const auto index = table->model()->index(0, 1);
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualRect(index).center());
    QVERIFY(copy->isEnabled()); QVERIFY(detail->text().contains(QStringLiteral("原始采集数据")));
    QVERIFY(detail->text().contains(QStringLiteral("识别依据"))); QVERIFY(detail->text().contains(QStringLiteral("建议动作")));
    copy->click(); QCOMPARE(QApplication::clipboard()->text(), dir.path() + "/recording");
    category->setCurrentIndex(category->findData(int(DeveloperCategory::Conda))); QCOMPARE(table->model()->rowCount(), 0);
    category->setCurrentIndex(0); search->clear(); QCOMPARE(table->model()->rowCount(), 3);
    search->setText(QStringLiteral("DISKLENS_MISSING")); QCOMPARE(table->model()->rowCount(), 0);
    QVERIFY2(page.exportReport(dir.path() + "/report.csv", &error), qPrintable(error));
    QFile report(dir.path() + "/report.csv"); QVERIFY(report.open(QIODevice::ReadOnly)); const QByteArray csv = report.readAll();
    QVERIFY(csv.contains("recording")); QVERIFY(csv.contains("robot/build")); QVERIFY(csv.contains("clouds"));
    QVERIFY(QFileInfo::exists(dir.path() + "/recording/test.db3"));
    search->clear(); table->sortByColumn(2, Qt::AscendingOrder);
    QVERIFY2(page.verifyUi(&error), qPrintable(error));
}

void DeveloperPageTest::productionSelectedTextAndCancel()
{
    QTemporaryDir dir; QVERIFY(dir.isValid()); QVERIFY(fixture(dir.path()));
    DeveloperPage page; page.setRoot(dir.path()); page.resize(1150, 850); page.show(); page.ensureScanStarted();
    QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(), 10000);
    auto *table = page.findChild<QTableView *>("developerTable"); QVERIFY(table);
    auto *search = page.findChild<QLineEdit *>("developerSearch"); QVERIFY(search);
    search->setText(QStringLiteral("recording")); QCOMPARE(table->model()->rowCount(), 1);
    table->selectRow(0); table->setFocus(); QCoreApplication::processEvents();
    const QRect cell = table->visualRect(table->model()->index(0, 2)); QVERIFY(cell.isValid());
    const QImage image = table->viewport()->grab(cell).toImage();
    int dark = 0;
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        const QColor color = image.pixelColor(x, y);
        if (color.red() < 150 && color.green() < 165 && color.blue() < 185) ++dark;
    }
    QVERIFY2(dark > 15, "Selected numeric cells must retain visible dark text with production Fusion/QSS");
    auto *cancel = page.findChild<QPushButton *>("developerCancel"); QVERIFY(cancel);
    search->clear(); page.rescan(); QVERIFY(page.isBusy()); QVERIFY(cancel->isEnabled()); cancel->click();
    QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(), 10000); QVERIFY(!cancel->isEnabled());
    auto *status = page.findChild<QLabel *>("developerStatus"); QVERIFY(status); QVERIFY(status->text().contains(QStringLiteral("取消")));
    QVERIFY(QFileInfo::exists(dir.path() + "/recording/test.db3"));
}

void DeveloperPageTest::changeRootAndDestructorCancel()
{
    QTemporaryDir dir, other; QVERIFY(dir.isValid()); QVERIFY(other.isValid()); QVERIFY(fixture(dir.path()));
    QVERIFY(write(other.path() + "/dataset/second.pcd"));
    DeveloperPage page; page.setRoot(dir.path()); page.ensureScanStarted(); QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(), 10000);
    page.setRoot(other.path()); QVERIFY(!page.isSettled()); QVERIFY(!page.isBusy());
    auto *table = page.findChild<QTableView *>("developerTable"); QVERIFY(table); QCOMPARE(table->model()->rowCount(), 0);
    page.ensureScanStarted(); QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(), 10000); QCOMPARE(table->model()->rowCount(), 1);
    auto *destroyedDuringScan = new DeveloperPage; destroyedDuringScan->setRoot(dir.path());
    destroyedDuringScan->ensureScanStarted(); delete destroyedDuringScan;
    QVERIFY(QFileInfo::exists(dir.path() + "/robot/build/nav.o"));
}

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv); app.setStyle(QStringLiteral("Fusion"));
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 10)); app.setStyleSheet(applicationStyle());
    DeveloperPageTest test; return QTest::qExec(&test, argc, argv);
}
#include "developerpage_test.moc"
