#include "explorerpage.h"
#include "widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

#include <unistd.h>

namespace {
bool writeFile(const QString &path, int bytes, char value)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QByteArray(bytes, value)) == bytes;
}

bool fixture(const QString &root)
{
    return writeFile(root + "/largest/=SUM(1,1).txt", 8192, 'x')
        && writeFile(root + "/largest/unique.bin", 16384, 'z')
        && writeFile(root + "/second/copy.bin", 8192, 'x')
        && writeFile(root + "/quoted,\"name\n.txt", 4096, 'n');
}

QByteArray readFile(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

struct Controls {
    QTreeWidget *tree;
    QTreeWidget *duplicates;
    QWidget *map;
    QLabel *breadcrumb;
    QLineEdit *search;
    QPushButton *up;
    QPushButton *duplicateScan;
    QComboBox *minimum;
    QLabel *summary;
};

Controls controls(ExplorerPage &page)
{
    return {page.findChild<QTreeWidget *>("explorerTree"), page.findChild<QTreeWidget *>("explorerDuplicates"),
        page.findChild<QWidget *>("explorerMap"), page.findChild<QLabel *>("explorerBreadcrumb"),
        page.findChild<QLineEdit *>("explorerSearch"), page.findChild<QPushButton *>("explorerUp"),
        page.findChild<QPushButton *>("explorerDuplicateScan"), page.findChild<QComboBox *>("explorerMinimum"),
        page.findChild<QLabel *>("explorerDuplicateSummary")};
}

bool valid(const Controls &c)
{
    return c.tree && c.duplicates && c.map && c.breadcrumb && c.search && c.up && c.duplicateScan && c.minimum && c.summary;
}
}

class ExplorerPageTest : public QObject {
    Q_OBJECT
private slots:
    void directoryNavigationAndChart();
    void duplicateScanIsExplicitAndFiltered();
    void reportEscapingAndPartialResults();
    void cancelAndWorkerLifetime();
};

void ExplorerPageTest::directoryNavigationAndChart()
{
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); QVERIFY(fixture(temporary.path()));
    ExplorerPage page; page.setRoot(temporary.path()); page.resize(1150, 760); page.show();
    QVERIFY(!page.isBusy()); page.ensureScanStarted(); QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(), 10000);
    const auto c = controls(page); QVERIFY(valid(c));
    QCOMPARE(c.tree->topLevelItemCount(), 3); QVERIFY(!c.up->isEnabled());
    QCOMPARE(c.tree->topLevelItem(0)->text(0), QStringLiteral("largest"));
    // Open the visible production row rather than invoking a private method.
    auto *largest = c.tree->topLevelItem(0); const QRect row = c.tree->visualItemRect(largest);
    QVERIFY(row.isValid());
    QTest::mouseClick(c.tree->viewport(), Qt::LeftButton, Qt::NoModifier, row.center());
    c.search->setFocus(); QCoreApplication::processEvents();
    const QModelIndex numeric = c.tree->model()->index(0, 1);
    QVERIFY(c.tree->selectionModel()->isSelected(numeric));
    const QImage selected = c.tree->viewport()->grab(c.tree->visualRect(numeric).adjusted(4, 4, -4, -4)).toImage();
    int darkGlyphs = 0;
    for (int y = 0; y < selected.height(); ++y) for (int x = 0; x < selected.width(); ++x) {
        const QColor pixel = selected.pixelColor(x, y);
        if (pixel.red() < 150 && pixel.green() < 165 && pixel.blue() < 185) ++darkGlyphs;
    }
    QVERIFY2(darkGlyphs > 20, "Selected numeric text must retain dark visible glyphs under production QSS, including when focus moves away.");
    QTest::mouseDClick(c.tree->viewport(), Qt::LeftButton, Qt::NoModifier, row.center());
    QTRY_VERIFY(c.breadcrumb->text().contains(QStringLiteral("largest")));
    QCOMPARE(c.tree->topLevelItemCount(), 2); QVERIFY(c.up->isEnabled());
    QTest::mouseClick(c.up, Qt::LeftButton); QCOMPARE(c.tree->topLevelItemCount(), 3);
    QVERIFY(!c.up->isEnabled());
    // The first full-width graph row represents the largest actual allocation.
    QTest::mouseClick(c.map, Qt::LeftButton, Qt::NoModifier, QPoint(c.map->width() / 2, 70));
    QTRY_VERIFY(c.breadcrumb->text().contains(QStringLiteral("largest")));
    QCOMPARE(c.tree->topLevelItemCount(), 2);
    QTest::mouseClick(c.up, Qt::LeftButton);
    auto *directory = c.tree->topLevelItem(0); directory->setExpanded(true);
    QCOMPARE(directory->childCount(), 2);
    c.search->setText(QStringLiteral("copy.bin")); QCOMPARE(c.tree->topLevelItemCount(), 1);
    QCOMPARE(c.tree->topLevelItem(0)->text(0), QStringLiteral("second")); c.search->clear();
    QString error; QVERIFY2(page.verifyUi(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(temporary.path() + "/largest/unique.bin"));
    const QString artifacts = QDir::current().absoluteFilePath(QStringLiteral("artifacts"));
    QVERIFY(QDir().mkpath(artifacts)); QVERIFY(page.grab().save(artifacts + "/explorer-production-ui.png"));
}

void ExplorerPageTest::duplicateScanIsExplicitAndFiltered()
{
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); QVERIFY(fixture(temporary.path()));
    ExplorerPage page; page.setRoot(temporary.path()); page.resize(1150, 760); page.show();
    page.ensureScanStarted(); QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(), 10000);
    const auto c = controls(page); QVERIFY(valid(c));
    page.showDuplicates(); QVERIFY(!page.isBusy()); QCOMPARE(c.duplicates->topLevelItemCount(), 0);
    QVERIFY(c.summary->text().contains(QStringLiteral("尚未")));
    c.minimum->setCurrentIndex(0); QTest::mouseClick(c.duplicateScan, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(), 10000);
    QCOMPARE(c.duplicates->topLevelItemCount(), 1);
    auto *group = c.duplicates->topLevelItem(0); QCOMPARE(group->childCount(), 2);
    QVERIFY(group->toolTip(0).contains(QStringLiteral("SHA-256")));
    QVERIFY(group->child(0)->text(2).size() == 64);
    QCOMPARE(group->child(0)->data(0, Qt::CheckStateRole), QVariant());
    const QString selected = group->child(0)->text(0); c.duplicates->setCurrentItem(group->child(0));
    auto *copy = page.findChild<QPushButton *>("explorerCopy"); QVERIFY(copy && copy->isEnabled());
    QTest::mouseClick(copy, Qt::LeftButton); QCOMPARE(QApplication::clipboard()->text(), selected);
    c.search->setText(QStringLiteral("no-match-anywhere")); QCOMPARE(c.duplicates->topLevelItemCount(), 0);
    // Export is the complete scan result, independent of the visible search.
    QTemporaryDir reports; QVERIFY(reports.isValid()); QString error;
    QVERIFY2(page.exportReport(reports.path() + "/duplicates.csv", &error), qPrintable(error));
    const QByteArray csv = readFile(reports.path() + "/duplicates.csv");
    QVERIFY(csv.contains("copy.bin")); QVERIFY(csv.contains("logical_redundancy")); QVERIFY(csv.contains("sha256"));
    c.search->setText(QStringLiteral("copy.bin")); QCOMPARE(c.duplicates->topLevelItemCount(), 1);
    // Keep the entire group visible so filtered results do not imply a keeper.
    QCOMPARE(c.duplicates->topLevelItem(0)->childCount(), 2);
    c.search->clear(); c.minimum->setCurrentIndex(2);
    QTest::mouseClick(c.duplicateScan, Qt::LeftButton); QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(), 10000);
    QCOMPARE(c.duplicates->topLevelItemCount(), 0);
    QVERIFY(QFileInfo::exists(temporary.path() + "/second/copy.bin"));
    QVERIFY2(page.verifyUi(&error), qPrintable(error));
}

void ExplorerPageTest::reportEscapingAndPartialResults()
{
    QTemporaryDir temporary; QTemporaryDir reports;
    QVERIFY(temporary.isValid()); QVERIFY(reports.isValid()); QVERIFY(fixture(temporary.path()));
    const QByteArray source = QFile::encodeName(temporary.path() + "/largest");
    const QByteArray link = QFile::encodeName(temporary.path() + "/link-to-largest");
    QVERIFY(::symlink(source.constData(), link.constData()) == 0);
    QString deep = temporary.path();
    for (int level = 0; level < 130; ++level) deep += QStringLiteral("/d");
    QVERIFY(writeFile(deep + QStringLiteral("/too-deep.bin"), 1024, 'd'));
    ExplorerPage page; page.setRoot(temporary.path()); page.ensureScanStarted();
    QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(), 10000);
    QString error; QVERIFY2(page.exportReport(reports.path() + "/directory.csv", &error), qPrintable(error));
    const QByteArray csv = readFile(reports.path() + "/directory.csv");
    QVERIFY(csv.startsWith("\xef\xbb\xbf"));
    QVERIFY(csv.contains("\"'=SUM(1,1).txt\""));
    QVERIFY(csv.contains("\"quoted,\"\"name\n.txt\""));
    QVERIFY(csv.contains("\"partial\",\"true\""));
    QVERIFY(csv.contains("\"truncated\",\"true\""));
    QVERIFY(csv.contains("\"skipped_entries\",\"1\""));
    QVERIFY(csv.contains("allocated_bytes"));
    const QString original = temporary.path() + QStringLiteral("/largest/unique.bin");
    const QByteArray originalContent = readFile(original);
    QVERIFY(!page.exportReport(original, &error)); QVERIFY(!error.isEmpty());
    QCOMPARE(readFile(original), originalContent);
    const QString alias = reports.path() + QStringLiteral("/alias.csv");
    const QByteArray target = QFile::encodeName(original); const QByteArray aliasName = QFile::encodeName(alias);
    QVERIFY(::symlink(target.constData(), aliasName.constData()) == 0);
    QVERIFY(!page.exportReport(alias, &error)); QCOMPARE(readFile(original), originalContent);
    // A new explicitly selected report may be created inside the scope.
    const QString newReport = temporary.path() + QStringLiteral("/new-report.csv");
    QVERIFY2(page.exportReport(newReport, &error), qPrintable(error));
    QCOMPARE(readFile(newReport), csv);
    QVERIFY(QDir(temporary.path()).entryList({QStringLiteral(".disklens-report-*")}, QDir::Files | QDir::Hidden).isEmpty());
    QVERIFY(!page.exportReport(newReport, &error)); QCOMPARE(readFile(newReport), csv);
    const QString directoryAlias = reports.path() + QStringLiteral("/directory-alias");
    const QByteArray aliasDirectory = QFile::encodeName(directoryAlias);
    QVERIFY(::symlink(source.constData(), aliasDirectory.constData()) == 0);
    QVERIFY(!page.exportReport(directoryAlias + QStringLiteral("/unique.bin"), &error));
    QCOMPARE(readFile(original), originalContent);
    const QString aliasedReport = directoryAlias + QStringLiteral("/new-alias-report.csv");
    QVERIFY2(page.exportReport(aliasedReport, &error), qPrintable(error));
    QCOMPARE(readFile(temporary.path() + QStringLiteral("/largest/new-alias-report.csv")), csv);
    QVERIFY(QDir(temporary.path() + QStringLiteral("/largest"))
        .entryList({QStringLiteral(".disklens-report-*")}, QDir::Files | QDir::Hidden).isEmpty());
    // NAME_MAX failure occurs at atomic publication, after staging succeeds.
    // It exercises deterministic cleanup without a timing-dependent race test.
    const QStringList beforeFailure = QDir(temporary.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    QVERIFY(!page.exportReport(temporary.path() + QLatin1Char('/') + QString(300, QLatin1Char('a')), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(QDir(temporary.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), beforeFailure);
    QCOMPARE(page.findChild<QLabel *>("explorerStatus")->textFormat(), Qt::PlainText);
    QCOMPARE(page.findChild<QLabel *>("explorerDetail")->textFormat(), Qt::PlainText);
    QVERIFY(!csv.contains("\"link-to-largest\","));
}

void ExplorerPageTest::cancelAndWorkerLifetime()
{
    QTemporaryDir temporary; QTemporaryDir reports; QVERIFY(temporary.isValid()); QVERIFY(reports.isValid());
    for (int index = 0; index < 1000; ++index)
        QVERIFY(writeFile(temporary.path() + QStringLiteral("/many/file%1.bin").arg(index), 2048, 'c'));
    ExplorerPage page; page.setRoot(temporary.path()); page.ensureScanStarted(); QVERIFY(page.isBusy());
    page.cancelWork(); QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(), 10000);
    QString error; QVERIFY2(page.exportReport(reports.path() + "/canceled.csv", &error), qPrintable(error));
    const QByteArray csv = readFile(reports.path() + "/canceled.csv");
    QVERIFY(csv.contains("\"partial\",\"true\"")); QVERIFY(csv.contains("\"canceled\",\"true\""));
    {
        ExplorerPage closing; closing.setRoot(temporary.path()); closing.ensureScanStarted();
        // Destructor must cancel and join; queued progress must not dereference
        // the destroyed widget when the application next processes events.
    }
    QCoreApplication::processEvents();
    QVERIFY(QFileInfo::exists(temporary.path() + "/many/file999.bin"));
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv); application.setStyle(QStringLiteral("Fusion"));
    application.setStyleSheet(applicationStyle()); ExplorerPageTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "explorerpage_test.moc"
