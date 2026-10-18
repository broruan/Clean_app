#include "cachepage.h"
#include "widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QtMath>
#include <QtTest>

class CachePageTest : public QObject {
    Q_OBJECT
private slots:
    void filtersAndSelection();
    void checkboxClickAndHiddenSelection();
    void productionCheckboxVisibleCenter();
    void rowClickAndSpaceToggle_data();
    void rowClickAndSpaceToggle();
    void selectedNumberTextRemainsReadable_data();
    void selectedNumberTextRemainsReadable();
    void confirmationCancelPreservesFiles();
    void selectedCleanupAndRescan();
};

namespace {
bool writeFile(const QString &path,int bytes)
{
    if(!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QByteArray(bytes,'x'))==bytes;
}
bool fixture(const QString &root)
{
    return writeFile(root+"/cache/app-cache/cache.bin",262144)
        && writeFile(root+"/cache/app-cache/logs/error.log",8192)
        && writeFile(root+"/cache/app-small/download.bin",524288)
        && writeFile(root+"/state/editor/session.log",4096)
        && writeFile(root+"/state/editor/history.db",4096);
}
struct Controls {
    QTableView *table;
    QLineEdit *search;
    QCheckBox *large;
    QCheckBox *logs;
    QPushButton *selectVisible;
    QPushButton *clear;
    QPushButton *clean;
};
Controls controls(CachePage &page)
{
    return {page.findChild<QTableView*>("cacheTable"),page.findChild<QLineEdit*>("cacheSearch"),
            page.findChild<QCheckBox*>("cacheLargeOnly"),page.findChild<QCheckBox*>("cacheLogsOnly"),
            page.findChild<QPushButton*>("cacheSelectVisible"),page.findChild<QPushButton*>("cacheClearSelection"),
            page.findChild<QPushButton*>("cacheClean")};
}
bool valid(const Controls &c) { return c.table && c.search && c.large && c.logs && c.selectVisible && c.clear && c.clean; }

class StyleOptionProbe final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::initStyleOption;
};
QRect visibleCheckIndicator(QTableView *table,const QModelIndex &index,QString *diagnostic)
{
    const QRect cell=table->visualRect(index);
    if(!cell.isValid()) return {};
    QStyleOptionViewItem option;
    option.initFrom(table); option.widget=table; option.rect=cell;
    option.font=table->font(); option.fontMetrics=QFontMetrics(option.font);
    option.decorationSize=table->iconSize();
    StyleOptionProbe probe; probe.initStyleOption(&option,index);
    const QRect styled=table->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&option,table);
    // Production QSS pads table items. Locate the actual visible checkbox,
    // rather than assuming a native-style x coordinate remains clickable.
    const QPixmap capture=table->viewport()->grab(cell);
    const QImage image=capture.toImage(); const qreal scale=capture.devicePixelRatio();
    int left=image.width(),top=image.height(),right=-1,bottom=-1;
    for(int y=1;y+1<image.height();++y) for(int x=1;x+1<image.width();++x) {
        const QColor pixel=image.pixelColor(x,y);
        if(pixel.alpha() && pixel.red()<220 && pixel.green()<220 && pixel.blue()<220) {
            left=qMin(left,x); top=qMin(top,y); right=qMax(right,x); bottom=qMax(bottom,y);
        }
    }
    const QRect visible=right>=left && bottom>=top
        ? QRect(QPoint(qFloor(left/scale),qFloor(top/scale)),QPoint(qFloor(right/scale),qFloor(bottom/scale))).translated(cell.topLeft())
        : QRect();
    if(diagnostic) *diagnostic=QStringLiteral("style indicator=(%1,%2 %3x%4), visible indicator=(%5,%6 %7x%8), cell=(%9,%10 %11x%12)")
        .arg(styled.x()).arg(styled.y()).arg(styled.width()).arg(styled.height())
        .arg(visible.x()).arg(visible.y()).arg(visible.width()).arg(visible.height())
        .arg(cell.x()).arg(cell.y()).arg(cell.width()).arg(cell.height());
    return visible;
}
QString artifactPath(const QString &name)
{
    const QString directory=QDir::current().absoluteFilePath(QStringLiteral("artifacts"));
    QDir().mkpath(directory);
    return QDir(directory).absoluteFilePath(name);
}
int darkGlyphPixels(const QImage &image)
{
    int count=0;
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
        const QColor pixel=image.pixelColor(x,y);
        // These thresholds reject the production pale selection background.
        if(pixel.alpha() && pixel.red()<150 && pixel.green()<165 && pixel.blue()<185) ++count;
    }
    return count;
}
}

void CachePageTest::filtersAndSelection()
{
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); QVERIFY(fixture(temporary.path()));
    CachePage page; page.setRoots(temporary.path()+"/cache",temporary.path()+"/state");
    page.ensureScanStarted(); QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(),10000);
    QString error; QVERIFY2(page.verifyUi(&error),qPrintable(error));
    const auto c=controls(page); QVERIFY(valid(c));
    QCOMPARE(c.table->model()->rowCount(),4); QVERIFY(!c.clean->isEnabled());
    c.search->setText("app-cache"); QCOMPARE(c.table->model()->rowCount(),2);
    c.logs->setChecked(true); QCOMPARE(c.table->model()->rowCount(),1);
    c.search->clear(); QCOMPARE(c.table->model()->rowCount(),2);
    c.logs->setChecked(false); QCOMPARE(c.table->model()->rowCount(),4);
    c.large->setChecked(true); QCOMPARE(c.table->model()->rowCount(),0);
    c.large->setChecked(false); c.selectVisible->click(); QVERIFY(c.clean->isEnabled());
    c.clear->click(); QVERIFY(!c.clean->isEnabled());
    QVERIFY(QFileInfo::exists(temporary.path()+"/state/editor/history.db"));
}

void CachePageTest::checkboxClickAndHiddenSelection()
{
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); QVERIFY(fixture(temporary.path()));
    CachePage page; page.setRoots(temporary.path()+"/cache",temporary.path()+"/state");
    page.resize(1100,760); page.show(); page.ensureScanStarted();
    QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(),10000);
    const auto c=controls(page); QVERIFY(valid(c));
    c.search->setText("app-small"); QCOMPARE(c.table->model()->rowCount(),1);
    const QModelIndex check=c.table->model()->index(0,0);
    QVERIFY(check.flags() & Qt::ItemIsUserCheckable);
    QString diagnostic; const QRect indicator=visibleCheckIndicator(c.table,check,&diagnostic);
    QVERIFY2(indicator.isValid(),qPrintable(diagnostic));
    QTest::mouseClick(c.table->viewport(),Qt::LeftButton,Qt::NoModifier,indicator.center());
    QCOMPARE(check.data(Qt::CheckStateRole).toInt(),int(Qt::Checked)); QVERIFY(c.clean->isEnabled());
    c.search->setText("editor"); QVERIFY(c.clean->isEnabled());
    auto *summary=page.findChild<QLabel*>("cacheSelectionSummary"); QVERIFY(summary);
    QVERIFY(summary->text().contains(QStringLiteral("筛选外")));
    c.clear->click(); QVERIFY(!c.clean->isEnabled());
    QVERIFY(QFileInfo::exists(temporary.path()+"/cache/app-small/download.bin"));
}

void CachePageTest::productionCheckboxVisibleCenter()
{
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); QVERIFY(fixture(temporary.path()));
    CachePage page; page.setRoots(temporary.path()+"/cache",temporary.path()+"/state");
    page.resize(1100,760); page.show(); page.ensureScanStarted();
    QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(),10000);
    const auto c=controls(page); QVERIFY(valid(c));
    c.search->setText("app-small"); QCOMPARE(c.table->model()->rowCount(),1);
    c.search->setFocus(); QCoreApplication::processEvents();
    const QModelIndex check=c.table->model()->index(0,0);
    QCOMPARE(check.data(Qt::CheckStateRole).toInt(),int(Qt::Unchecked));
    QString diagnostic; const QRect indicator=visibleCheckIndicator(c.table,check,&diagnostic);
    QVERIFY2(indicator.isValid() && indicator.width()>=8 && indicator.width()<=30
             && indicator.height()>=8 && indicator.height()<=30,qPrintable(diagnostic));
    QVERIFY(page.grab().save(artifactPath(QStringLiteral("cache-checkbox-regression.png"))));
    qInfo().noquote() << diagnostic;
    QTest::mouseClick(c.table->viewport(),Qt::LeftButton,Qt::NoModifier,indicator.center());
    QCOMPARE(check.data(Qt::CheckStateRole).toInt(),int(Qt::Checked)); QVERIFY(c.clean->isEnabled());
    QTest::mouseClick(c.table->viewport(),Qt::LeftButton,Qt::NoModifier,indicator.center());
    QCOMPARE(check.data(Qt::CheckStateRole).toInt(),int(Qt::Unchecked)); QVERIFY(!c.clean->isEnabled());
    QVERIFY(QFileInfo::exists(temporary.path()+"/cache/app-small/download.bin"));
}

void CachePageTest::rowClickAndSpaceToggle_data()
{
    QTest::addColumn<int>("column"); QTest::addColumn<int>("edge");
    QTest::newRow("checkbox-leading-padding") << 0 << -1;
    QTest::newRow("checkbox-cell-center") << 0 << 0;
    QTest::newRow("checkbox-trailing-padding") << 0 << 1;
    QTest::newRow("application-name") << 1 << 0;
    QTest::newRow("actual-size") << 5 << 0;
}

void CachePageTest::rowClickAndSpaceToggle()
{
    QFETCH(int,column); QFETCH(int,edge);
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); QVERIFY(fixture(temporary.path()));
    CachePage page; page.setRoots(temporary.path()+"/cache",temporary.path()+"/state");
    page.resize(1100,760); page.show(); page.ensureScanStarted();
    QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(),10000);
    const auto c=controls(page); QVERIFY(valid(c));
    c.search->setText("app-small"); QCOMPARE(c.table->model()->rowCount(),1);
    const QModelIndex check=c.table->model()->index(0,0), name=c.table->model()->index(0,1);
    const QRect clickedCell=c.table->visualRect(c.table->model()->index(0,column));
    const QPoint point(edge<0 ? clickedCell.left()+10 : edge>0 ? clickedCell.right()-8 : clickedCell.center().x(),clickedCell.center().y());
    QTest::mouseClick(c.table->viewport(),Qt::LeftButton,Qt::NoModifier,point);
    QCOMPARE(check.data(Qt::CheckStateRole).toInt(),int(Qt::Checked)); QVERIFY(c.clean->isEnabled());
    QTest::mouseClick(c.table->viewport(),Qt::LeftButton,Qt::NoModifier,point);
    QCOMPARE(check.data(Qt::CheckStateRole).toInt(),int(Qt::Unchecked)); QVERIFY(!c.clean->isEnabled());
    c.table->setCurrentIndex(name); c.table->setFocus();
    QTest::keyClick(c.table,Qt::Key_Space);
    QCOMPARE(check.data(Qt::CheckStateRole).toInt(),int(Qt::Checked));
    QTest::keyClick(c.table,Qt::Key_Space);
    QCOMPARE(check.data(Qt::CheckStateRole).toInt(),int(Qt::Unchecked));
    QVERIFY(QFileInfo::exists(temporary.path()+"/cache/app-small/download.bin"));
}

void CachePageTest::selectedNumberTextRemainsReadable_data()
{
    QTest::addColumn<bool>("focusAway");
    QTest::newRow("table-focused") << false;
    QTest::newRow("table-unfocused") << true;
}

void CachePageTest::selectedNumberTextRemainsReadable()
{
    QFETCH(bool,focusAway);
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); QVERIFY(fixture(temporary.path()));
    CachePage page; page.setRoots(temporary.path()+"/cache",temporary.path()+"/state");
    page.resize(1100,760); page.show(); page.ensureScanStarted();
    QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(),10000);
    const auto c=controls(page); QVERIFY(valid(c));
    c.search->setText("app-small"); QCOMPARE(c.table->model()->rowCount(),1);
    // Row selection is independent from cleanup checkbox state. This directly
    // exercises the user's pale-background/white-numeric-text symptom.
    c.table->setCurrentIndex(c.table->model()->index(0,1)); c.table->selectRow(0); c.table->setFocus();
    if(focusAway) c.search->setFocus();
    QCoreApplication::processEvents(); QTest::qWait(30);
    QStringList failures;
    for(int column=3;column<=5;++column) {
        const QModelIndex numeric=c.table->model()->index(0,column);
        QVERIFY(c.table->selectionModel()->isSelected(numeric)); QVERIFY(!numeric.data().toString().isEmpty());
        const QRect area=c.table->visualRect(numeric).adjusted(5,5,-5,-5); QVERIFY(area.isValid());
        const QImage cell=c.table->viewport()->grab(area).toImage();
        const QString suffix=QString::fromLatin1(QTest::currentDataTag());
        const QString path=artifactPath(QStringLiteral("cache-selected-%1-column%2.png").arg(suffix).arg(column));
        QVERIFY2(cell.save(path),qPrintable(path));
        if(column==3) {
            QVERIFY(page.grab().save(artifactPath(QStringLiteral("cache-selected-%1.png").arg(suffix))));
            if(!focusAway) QVERIFY(page.grab().save(artifactPath(QStringLiteral("cache-selection-regression.png"))));
        }
        const int dark=darkGlyphPixels(cell);
        if(dark<20) failures << QStringLiteral("Selected numeric column %1 has %2 dark glyph pixels under production QSS (%3); see %4")
                                  .arg(column).arg(dark).arg(suffix,path);
    }
    QVERIFY2(failures.isEmpty(),qPrintable(failures.join(QLatin1Char('\n'))));
    QVERIFY(QFileInfo::exists(temporary.path()+"/cache/app-small/download.bin"));
}

void CachePageTest::confirmationCancelPreservesFiles()
{
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); QVERIFY(fixture(temporary.path()));
    CachePage page; page.setRoots(temporary.path()+"/cache",temporary.path()+"/state");
    page.ensureScanStarted(); QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(),10000);
    const auto c=controls(page); QVERIFY(valid(c));
    c.search->setText("app-small"); QCOMPARE(c.table->model()->rowCount(),1);
    c.selectVisible->click(); QVERIFY(c.clean->isEnabled()); bool canceled=false;
    QTimer dismiss; dismiss.setInterval(10);
    connect(&dismiss,&QTimer::timeout,&page,[&] {
        if(auto *dialog=page.findChild<QDialog*>("cacheConfirm")) { canceled=true; dialog->reject(); dismiss.stop(); }
    });
    dismiss.start();
    QTimer::singleShot(2000,&page,[] {if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();});
    c.clean->click(); QVERIFY(canceled); QVERIFY(!page.isCleaning());
    QVERIFY(QFileInfo::exists(temporary.path()+"/cache/app-small/download.bin"));
    QVERIFY(QFileInfo::exists(temporary.path()+"/state/editor/history.db"));
}

void CachePageTest::selectedCleanupAndRescan()
{
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); QVERIFY(fixture(temporary.path()));
    CachePage page; page.setRoots(temporary.path()+"/cache",temporary.path()+"/state");
    page.ensureScanStarted(); QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(),10000);
    const auto c=controls(page); QVERIFY(valid(c));
    c.search->setText("app-small"); QCOMPARE(c.table->model()->rowCount(),1);
    c.selectVisible->click(); QVERIFY(c.clean->isEnabled());
    QSignalSpy spaceChanged(&page,&CachePage::spaceChanged); bool confirmed=false,resultShown=false;
    QTimer interact; interact.setInterval(10);
    connect(&interact,&QTimer::timeout,&page,[&] {
        if(auto *dialog=page.findChild<QDialog*>("cacheConfirm")) {
            if(auto *accept=dialog->findChild<QPushButton*>("cacheConfirmDelete")) { confirmed=true; accept->click(); }
        }
        if(auto *dialog=page.findChild<QDialog*>("cacheCleanResult")) { resultShown=true; dialog->accept(); }
    });
    interact.start();
    QTimer::singleShot(8000,&page,[] {if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();});
    c.clean->click(); QTRY_VERIFY_WITH_TIMEOUT(confirmed,10000); QTRY_VERIFY_WITH_TIMEOUT(resultShown,10000);
    QTRY_VERIFY_WITH_TIMEOUT(page.isSettled(),10000);
    QVERIFY(!QFileInfo::exists(temporary.path()+"/cache/app-small/download.bin"));
    QVERIFY(QFileInfo::exists(temporary.path()+"/cache/app-cache/cache.bin"));
    QVERIFY(QFileInfo::exists(temporary.path()+"/cache/app-cache/logs/error.log"));
    QVERIFY(QFileInfo::exists(temporary.path()+"/state/editor/session.log"));
    QVERIFY(QFileInfo::exists(temporary.path()+"/state/editor/history.db"));
    QVERIFY(QFileInfo(temporary.path()+"/cache").isDir()); QVERIFY(!spaceChanged.isEmpty());
    c.search->clear(); QCOMPARE(c.table->model()->rowCount(),3); QVERIFY(!c.clean->isEnabled());
}

// Mirror src/main.cpp: geometry and colors must use the shipped Fusion style,
// Noto font and application stylesheet rather than an unstyled QTEST_MAIN.
int main(int argc,char **argv)
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication application(argc,argv);
    application.setApplicationName(QStringLiteral("DiskLens")); application.setOrganizationName(QStringLiteral("DiskLens"));
    application.setStyle(QStringLiteral("Fusion"));
    if(QIcon::themeName().isEmpty()) QIcon::setThemeName(QStringLiteral("Yaru"));
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
    application.setFont(QFont(QStringLiteral("Noto Sans CJK SC"),10)); application.setStyleSheet(applicationStyle());
    CachePageTest test; return QTest::qExec(&test,argc,argv);
}
#include "cachepage_test.moc"
