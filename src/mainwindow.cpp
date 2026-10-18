#include "mainwindow.h"
#include "filescanner.h"
#include "cachepage.h"
#include "explorerpage.h"
#include "developerpage.h"
#include "appstoragedialog.h"
#include "models.h"
#include "packagebackend.h"
#include "uninstallcontroller.h"
#include "widgets.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStorageInfo>
#include <QTableView>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace {
QLabel *label(const QString &text, const char *name = nullptr)
{
    auto *result = new QLabel(text);
    result->setTextFormat(Qt::PlainText);
    if (name) result->setObjectName(name);
    return result;
}
QFrame *card()
{
    auto *result = new QFrame;
    result->setObjectName("card");
    return result;
}
QPushButton *button(const QString &text, const QString &icon = {}, const char *name = nullptr)
{
    auto *result = new QPushButton(text);
    if (name) result->setObjectName(name);
    if (!icon.isEmpty()) { result->setIcon(lineIcon(icon, name && QString(name)=="primary" ? QColor("#ffffff") : QColor("#7b87a1"))); result->setIconSize({18,18}); }
    result->setCursor(Qt::PointingHandCursor);
    return result;
}
void configureTable(QTableView *table, bool files)
{
    table->setItemDelegate(new EntryDelegate(files, table));
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(66);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setStretchLastSection(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setShowGrid(false);
    table->setMouseTracking(true);
    table->setSortingEnabled(true);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Fixed);
    table->setColumnWidth(1,100);
    table->setColumnWidth(2,files ? 170 : 210);
    table->setColumnWidth(3,150);
    table->sortByColumn(3,Qt::DescendingOrder);
}
void clearLayout(QLayout *layout)
{
    while (auto *item=layout->takeAt(0)) {
        if (item->widget()) delete item->widget();
        if (item->layout()) clearLayout(item->layout());
        delete item;
    }
}
class OperationDialog final : public QDialog {
public:
    using QDialog::QDialog;
    // Escape must not hide an ongoing package-manager operation.
    void reject() override {}
protected:
    void closeEvent(QCloseEvent *event) override { event->ignore(); }
};
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("磁盘透镜 · DiskLens"));
    setWindowIcon(QIcon(":/disklens.svg"));
    resize(1280,860); setMinimumSize(1080,750);
    m_appModel=new AppModel(this); m_appFilter=new AppFilter(this);
    m_appFilter->setSourceModel(m_appModel); m_appFilter->setSortRole(SortRole); m_appFilter->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_fileModel=new FileModel(this); m_fileFilter=new QSortFilterProxyModel(this);
    m_fileFilter->setSourceModel(m_fileModel); m_fileFilter->setSortRole(SortRole);
    m_fileFilter->setFilterRole(SearchRole); m_fileFilter->setFilterCaseSensitivity(Qt::CaseInsensitive); m_fileFilter->setFilterKeyColumn(0);
    m_uninstaller=new UninstallController(this);
    createUi();
    connect(&m_inventoryWatcher,&QFutureWatcher<InventoryResult>::finished,this,&MainWindow::inventoryFinished);
    connect(&m_scanWatcher,&QFutureWatcher<ScanResult>::finished,this,&MainWindow::scanFinished);
    connect(m_uninstaller,&UninstallController::previewReady,this,&MainWindow::showRemovalPreview);
    connect(m_uninstaller,&UninstallController::busyChanged,this,[this](bool busy) {
        m_refresh->setEnabled(!busy && !m_inventoryWatcher.isRunning());
        updateAppSelection();
    });
    connect(m_uninstaller,&UninstallController::logMessage,this,[this](const QString &text) {
        if (m_operationLog) m_operationLog->appendPlainText(text);
    });
    connect(m_uninstaller,&UninstallController::finished,this,[this](bool success,const QString &message) {
        if (m_operationDialog) { m_operationDialog->hide(); m_operationDialog->deleteLater(); m_operationDialog=nullptr; m_operationLog=nullptr; }
        if (success) { QMessageBox::information(this,QStringLiteral("卸载完成"),message); reloadInventory(); refreshStorage(); }
        else QMessageBox::warning(this,QStringLiteral("卸载未完成"),message);
        updateAppSelection();
    });
    QTimer::singleShot(0,this,&MainWindow::reloadInventory);
    QTimer::singleShot(250,this,&MainWindow::startScan);
}
MainWindow::~MainWindow()
{
    m_cachePage->cancelWork();
    m_explorerPage->cancelWork();
    m_developerPage->cancelWork();
    m_cancelScan.store(true);
    m_scanWatcher.waitForFinished();
    m_inventoryWatcher.waitForFinished();
}
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_uninstaller->isExecuting()) {
        QMessageBox::information(this,QStringLiteral("软件管理操作正在进行"),QStringLiteral("请等待当前操作完成后再关闭窗口，避免中断包管理事务。"));
        event->ignore(); return;
    }
    if (m_uninstaller->isBusy()) m_uninstaller->cancelPreview();
    m_cachePage->cancelWork();
    m_explorerPage->cancelWork();
    m_developerPage->cancelWork();
    m_cancelScan.store(true);
    event->accept();
}
void MainWindow::setScanRoot(const QString &path)
{
    const QString root = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    m_scanPath->setText(root);
    m_explorerPage->setRoot(root);
    m_developerPage->setRoot(root);
}
bool MainWindow::isSettled() const
{
    return m_inventoryDone && m_scanDone && !m_inventoryWatcher.isRunning() && !m_scanWatcher.isRunning()
        && (!m_cacheRequested || m_cachePage->isSettled())
        && (!m_explorerRequested || m_explorerPage->isSettled())
        && (!m_developerRequested || m_developerPage->isSettled());
}

void MainWindow::createUi()
{
    auto *shell=new QWidget; auto *shellLayout=new QHBoxLayout(shell); shellLayout->setContentsMargins(0,0,0,0); shellLayout->setSpacing(0);
    auto *sidebar=new QFrame; sidebar->setObjectName("sidebar"); sidebar->setFixedWidth(205);
    auto *side=new QVBoxLayout(sidebar); side->setContentsMargins(19,32,19,24); side->setSpacing(8);
    auto *logo=new QLabel; logo->setPixmap(QIcon(":/disklens.svg").pixmap(42,42));
    auto *brandRow=new QHBoxLayout; brandRow->setSpacing(11); brandRow->addWidget(logo); brandRow->addWidget(label("DiskLens","brand")); brandRow->addStretch();
    side->addLayout(brandRow); side->addWidget(label(QStringLiteral("磁盘透镜  /  空间管理"),"brandSub")); side->addSpacing(37);
    side->addWidget(label(QStringLiteral("工作空间"),"sideCaption")); side->addSpacing(5);
    const QStringList names{QStringLiteral("空间总览"),QStringLiteral("已安装软件"),QStringLiteral("大文件"),QStringLiteral("缓存与日志"),QStringLiteral("空间分析"),QStringLiteral("开发环境")};
    const QStringList icons{"grid","apps","files","trash","grid","folder"};
    for(int i=0;i<names.size();++i) {
        auto *nav=button(names[i]); nav->setObjectName("navButton"); nav->setCheckable(true);
        nav->setIcon(lineIcon(icons[i],QColor("#a4b4d2"))); nav->setIconSize({20,20});
        side->addWidget(nav); m_nav.append(nav); connect(nav,&QPushButton::clicked,this,[this,i]{setPage(i);});
    }
    side->addStretch();
    auto *divider=new QFrame; divider->setFixedHeight(1); divider->setStyleSheet("background:#2c3b53;"); side->addWidget(divider); side->addSpacing(15);
    side->addWidget(label(QStringLiteral("●  本机 · Ubuntu / Debian"),"brandSub"));
    side->addWidget(label(QStringLiteral("本地统计 · 无需联网"),"sideCaption"));
    shellLayout->addWidget(sidebar);
    auto *workspace=new QWidget; workspace->setObjectName("workspace");
    auto *content=new QVBoxLayout(workspace); content->setContentsMargins(30,28,30,14); content->setSpacing(18);
    auto *header=new QHBoxLayout; auto *titles=new QVBoxLayout; titles->setSpacing(5);
    m_title=label({},"pageTitle"); m_subtitle=label({},"subtitle"); titles->addWidget(m_title); titles->addWidget(m_subtitle); header->addLayout(titles); header->addStretch();
    m_warningButton=button(QStringLiteral("数据提示")); m_warningButton->hide(); connect(m_warningButton,&QPushButton::clicked,this,&MainWindow::showWarnings); header->addWidget(m_warningButton);
    m_refresh=button(QStringLiteral("刷新数据"),"refresh"); connect(m_refresh,&QPushButton::clicked,this,[this]{
        reloadInventory();refreshStorage();
        if(m_pages->currentIndex()==3)m_cachePage->rescan();
        if(m_pages->currentIndex()==4)m_explorerPage->rescan();
        if(m_pages->currentIndex()==5)m_developerPage->rescan();
    }); header->addWidget(m_refresh);
    content->addLayout(header);
    m_inventoryProgress=new QProgressBar; m_inventoryProgress->setRange(0,0); m_inventoryProgress->setTextVisible(false); m_inventoryProgress->setFixedHeight(3); content->addWidget(m_inventoryProgress);
    m_pages=new QStackedWidget; m_pages->addWidget(createOverview()); m_pages->addWidget(createAppsPage()); m_pages->addWidget(createFilesPage());
    m_cachePage=new CachePage; m_pages->addWidget(m_cachePage);
    m_explorerPage=new ExplorerPage; m_pages->addWidget(m_explorerPage);
    m_developerPage=new DeveloperPage; m_pages->addWidget(m_developerPage);
    connect(m_cachePage,&CachePage::spaceChanged,this,[this]{refreshStorage();if(!m_scanActive)startScan();});
    content->addWidget(m_pages,1);
    auto *bottom=new QHBoxLayout; bottom->addWidget(label(QStringLiteral("●  仅显示本机已安装软件"),"muted")); bottom->addStretch(); m_updateLabel=label(QStringLiteral("正在读取本机数据…"),"muted"); bottom->addWidget(m_updateLabel); content->addLayout(bottom);
    shellLayout->addWidget(workspace,1); setCentralWidget(shell); setPage(0); refreshStorage();
}

QWidget *MainWindow::createOverview()
{
    auto *page=new QWidget; auto *layout=new QVBoxLayout(page); layout->setContentsMargins(0,0,0,0); layout->setSpacing(18);
    auto *top=new QHBoxLayout; top->setSpacing(16);
    auto *disk=card(); auto *diskLayout=new QVBoxLayout(disk); diskLayout->setContentsMargins(21,18,21,17); diskLayout->setSpacing(4);
    auto *diskTitle=new QHBoxLayout; diskTitle->addWidget(label(QStringLiteral("磁盘使用情况"),"cardTitle")); diskTitle->addStretch();
    m_volumes=new QComboBox; m_volumes->setMaximumWidth(180); m_volumes->setMinimumWidth(90);
    connect(m_volumes,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refreshStorage();}); diskTitle->addWidget(m_volumes); diskLayout->addLayout(diskTitle);
    auto *diskBody=new QHBoxLayout; diskBody->setSpacing(20); m_diskRing=new DiskRing; diskBody->addWidget(m_diskRing);
    auto *diskValues=new QVBoxLayout; diskValues->setSpacing(5); diskValues->addStretch();
    m_diskUsed=label(QStringLiteral("—"),"smallValue"); diskValues->addWidget(m_diskUsed); diskValues->addWidget(label(QStringLiteral("已使用"),"muted")); diskValues->addSpacing(10);
    m_diskAvailable=label(QStringLiteral("可用 —")); diskValues->addWidget(m_diskAvailable); m_diskTotal=label({},"muted"); diskValues->addWidget(m_diskTotal); diskValues->addStretch();
    diskBody->addLayout(diskValues,1); diskLayout->addLayout(diskBody,1); m_diskReserved=label({},"muted"); diskLayout->addWidget(m_diskReserved); top->addWidget(disk,6);
    auto *apps=card(); auto *appsLayout=new QVBoxLayout(apps); appsLayout->setContentsMargins(21,21,21,21); appsLayout->setSpacing(10);
    auto *appHead=new QHBoxLayout; appHead->addWidget(label(QStringLiteral("已安装的软件"),"cardTitle")); appHead->addStretch(); auto *appIcon=new QLabel; appIcon->setPixmap(lineIcon("apps",QColor("#7788e6")).pixmap(25,25)); appHead->addWidget(appIcon); appsLayout->addLayout(appHead);
    appsLayout->addStretch(); m_appSize=label(QStringLiteral("—"),"metricValue"); appsLayout->addWidget(m_appSize); appsLayout->addWidget(label(QStringLiteral("软件包体积 · 估算"),"muted"));
    m_appSummary=label(QStringLiteral("读取中…"),"muted"); appsLayout->addWidget(m_appSummary); appsLayout->addStretch();
    auto *appsLink=button(QStringLiteral("管理已安装软件  →"),{},"link"); appsLink->setStyleSheet("text-align:left;"); connect(appsLink,&QPushButton::clicked,this,[this]{setPage(1);}); appsLayout->addWidget(appsLink); top->addWidget(apps,4);
    auto *large=card(); auto *largeLayout=new QVBoxLayout(large); largeLayout->setContentsMargins(21,21,21,21); largeLayout->setSpacing(10);
    auto *largeHead=new QHBoxLayout; largeHead->addWidget(label(QStringLiteral("发现的大文件"),"cardTitle")); largeHead->addStretch(); auto *fileIcon=new QLabel; fileIcon->setPixmap(lineIcon("files",QColor("#39b59c")).pixmap(25,25)); largeHead->addWidget(fileIcon); largeLayout->addLayout(largeHead);
    largeLayout->addStretch(); m_largeSize=label(QStringLiteral("—"),"metricValue"); largeLayout->addWidget(m_largeSize); largeLayout->addWidget(label(QStringLiteral("匹配文件的逻辑大小"),"muted")); m_largeSummary=label(QStringLiteral("等待扫描主目录"),"muted"); largeLayout->addWidget(m_largeSummary); largeLayout->addStretch();
    auto *largeLink=button(QStringLiteral("查看大文件  →"),{},"link"); largeLink->setStyleSheet("text-align:left;"); connect(largeLink,&QPushButton::clicked,this,[this]{setPage(2);}); largeLayout->addWidget(largeLink); top->addWidget(large,4);
    layout->addLayout(top,0);
    auto *middle=new QHBoxLayout; middle->setSpacing(18);
    auto *ranking=card(); auto *rankLayout=new QVBoxLayout(ranking); rankLayout->setContentsMargins(22,20,22,18); rankLayout->setSpacing(9);
    auto *rankHead=new QHBoxLayout; rankHead->addWidget(label(QStringLiteral("软件占用排行"),"cardTitle")); rankHead->addStretch(); auto *rankAll=button(QStringLiteral("全部软件  →"),{},"link"); connect(rankAll,&QPushButton::clicked,this,[this]{setPage(1);}); rankHead->addWidget(rankAll); rankLayout->addLayout(rankHead);
    rankLayout->addWidget(label(QStringLiteral("桌面应用 · 按软件包体积排序"),"muted")); m_rank=new RankChart; rankLayout->addWidget(m_rank,1); middle->addWidget(ranking,1);
    auto *recent=card(); auto *recentLayout=new QVBoxLayout(recent); recentLayout->setContentsMargins(22,20,22,18); recentLayout->setSpacing(9);
    auto *recentHead=new QHBoxLayout; recentHead->addWidget(label(QStringLiteral("值得关注的大文件"),"cardTitle")); recentHead->addStretch(); auto *recentAll=button(QStringLiteral("查看全部  →"),{},"link"); connect(recentAll,&QPushButton::clicked,this,[this]{setPage(2);}); recentHead->addWidget(recentAll); recentLayout->addLayout(recentHead);
    m_overviewScanStatus=label(QStringLiteral("默认扫描主目录 · 单文件 ≥ 100 MiB"),"muted"); m_overviewScanStatus->setWordWrap(true); recentLayout->addWidget(m_overviewScanStatus);
    m_recentFiles=new QVBoxLayout; m_recentFiles->setSpacing(0); recentLayout->addLayout(m_recentFiles,1); updateOverviewFiles();
    m_overviewScan=button(QStringLiteral("选择扫描目录"),"folder"); connect(m_overviewScan,&QPushButton::clicked,this,[this]{setPage(2);}); recentLayout->addWidget(m_overviewScan); middle->addWidget(recent,1);
    layout->addLayout(middle,1);
    auto *cacheRow=new QHBoxLayout;
    auto *note=label(QStringLiteral("软件体积不含用户文件和缓存。大文件实际占用可在详情查看。"),"muted"); note->setWordWrap(true); cacheRow->addWidget(note,1);
    auto *cacheLink=button(QStringLiteral("查看缓存与日志  →"),{},"link");connect(cacheLink,&QPushButton::clicked,this,[this]{setPage(3);});cacheRow->addWidget(cacheLink);layout->addLayout(cacheRow);
    return page;
}

QWidget *MainWindow::createAppsPage()
{
    auto *page=new QWidget; auto *layout=new QVBoxLayout(page); layout->setContentsMargins(0,0,0,0); layout->setSpacing(14);
    auto *tools=new QHBoxLayout; tools->setSpacing(12); m_search=new QLineEdit; m_search->setPlaceholderText(QStringLiteral("搜索软件名称、包名或描述…")); m_search->addAction(lineIcon("search"),QLineEdit::LeadingPosition); m_search->setClearButtonEnabled(true); tools->addWidget(m_search,1);
    m_appKind=new QComboBox; m_appKind->addItems({QStringLiteral("桌面应用"),QStringLiteral("所有已安装软件包")}); tools->addWidget(m_appKind);
    m_source=new QComboBox; m_source->addItems({QStringLiteral("所有来源"),"DEB","Snap","Flatpak"}); tools->addWidget(m_source); layout->addLayout(tools);
    connect(m_search,&QLineEdit::textChanged,this,[this](const QString &text){m_appFilter->setSearch(text);updateAppCount();updateAppSelection();});
    connect(m_appKind,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){m_appFilter->setOnlyDesktop(i==0);updateAppCount();updateAppSelection();});
    connect(m_source,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){m_appFilter->setSourceFilter(i-1);updateAppCount();updateAppSelection();});
    auto *panel=card(); auto *panelLayout=new QVBoxLayout(panel); panelLayout->setContentsMargins(0,0,0,0); panelLayout->setSpacing(0);
    auto *tableHeader=new QHBoxLayout; tableHeader->setContentsMargins(19,15,19,15); tableHeader->addWidget(label(QStringLiteral("已安装软件"),"cardTitle")); tableHeader->addStretch(); m_appCount=label(QStringLiteral("正在读取…"),"muted"); tableHeader->addWidget(m_appCount); panelLayout->addLayout(tableHeader);
    m_appsTable=new QTableView; m_appsTable->setObjectName("appsTable"); m_appsTable->setModel(m_appFilter); configureTable(m_appsTable,false); panelLayout->addWidget(m_appsTable,1);
    connect(m_appsTable->selectionModel(),&QItemSelectionModel::selectionChanged,this,&MainWindow::updateAppSelection);
    layout->addWidget(panel,1);
    auto *footer=card(); auto *footerLayout=new QHBoxLayout(footer); footerLayout->setContentsMargins(18,14,18,14); footerLayout->setSpacing(18);
    m_appDetail=label(QStringLiteral("选择一款软件，查看详情或卸载。"),"muted"); m_appDetail->setWordWrap(true); m_appDetail->setTextInteractionFlags(Qt::TextSelectableByMouse); footerLayout->addWidget(m_appDetail,1);
    m_appStorage=button(QStringLiteral("空间画像"),"folder"); m_appStorage->setObjectName("appStorageButton"); m_appStorage->setEnabled(false);
    connect(m_appStorage,&QPushButton::clicked,this,&MainWindow::showAppStorage); footerLayout->addWidget(m_appStorage);
    m_remove=button(QStringLiteral("卸载软件"),"trash","danger"); m_remove->setEnabled(false); connect(m_remove,&QPushButton::clicked,this,&MainWindow::beginUninstall); footerLayout->addWidget(m_remove); layout->addWidget(footer);
    auto *cancelPreview=button(QStringLiteral("取消分析"));cancelPreview->hide();footerLayout->addWidget(cancelPreview);
    connect(cancelPreview,&QPushButton::clicked,this,[this]{m_uninstaller->cancelPreview();updateAppSelection();});
    connect(m_uninstaller,&UninstallController::busyChanged,this,[this,cancelPreview](bool busy){
        cancelPreview->setVisible(busy && !m_operationDialog);
        m_remove->setText(busy ? QStringLiteral("正在处理…") : QStringLiteral("卸载软件"));
    });
    auto *note=label(QStringLiteral("仅列出已安装内容。卸载前会预览影响范围，系统组件受保护；软件包体积为估算值，不等于可释放空间。"),"muted"); note->setWordWrap(true); layout->addWidget(note);
    return page;
}

QWidget *MainWindow::createFilesPage()
{
    auto *page=new QWidget; auto *layout=new QVBoxLayout(page); layout->setContentsMargins(0,0,0,0); layout->setSpacing(14);
    auto *controls=card(); auto *controlsLayout=new QVBoxLayout(controls); controlsLayout->setContentsMargins(18,16,18,16); controlsLayout->setSpacing(12);
    auto *pathRow=new QHBoxLayout; pathRow->setSpacing(10); m_scanPath=new QLineEdit(QDir::homePath()); m_scanPath->setPlaceholderText(QStringLiteral("输入要扫描的目录")); m_scanPath->addAction(lineIcon("folder"),QLineEdit::LeadingPosition); pathRow->addWidget(m_scanPath,1);
    m_browse=button(QStringLiteral("选择目录")); connect(m_browse,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getExistingDirectory(this,QStringLiteral("选择扫描目录"),m_scanPath->text());if(!path.isEmpty())m_scanPath->setText(path);}); pathRow->addWidget(m_browse);
    m_minSize=new QComboBox; m_minSize->addItem(QStringLiteral("≥ 10 MiB"),qulonglong(10ULL*1024*1024)); m_minSize->addItem(QStringLiteral("≥ 100 MiB"),qulonglong(100ULL*1024*1024)); m_minSize->addItem(QStringLiteral("≥ 500 MiB"),qulonglong(500ULL*1024*1024)); m_minSize->addItem(QStringLiteral("≥ 1 GiB"),qulonglong(1024ULL*1024*1024)); m_minSize->setCurrentIndex(1); pathRow->addWidget(m_minSize);
    m_scanButton=button(QStringLiteral("开始扫描"),"search","primary"); connect(m_scanButton,&QPushButton::clicked,this,&MainWindow::startScan); pathRow->addWidget(m_scanButton);
    m_cancelButton=button(QStringLiteral("停止"),"stop"); m_cancelButton->hide(); connect(m_cancelButton,&QPushButton::clicked,this,[this]{m_cancelScan.store(true);m_cancelButton->setEnabled(false);m_scanStatus->setText(QStringLiteral("正在停止，将保留已找到的文件…"));}); pathRow->addWidget(m_cancelButton); controlsLayout->addLayout(pathRow);
    auto *stateRow=new QHBoxLayout; m_scanStatus=label(QStringLiteral("包含隐藏文件，不跟随软链接，不跨挂载点。"),"muted"); m_scanStatus->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred); stateRow->addWidget(m_scanStatus,1); stateRow->addWidget(label(QStringLiteral("只读扫描"),"tag")); controlsLayout->addLayout(stateRow);
    m_scanProgress=new QProgressBar; m_scanProgress->setRange(0,0); m_scanProgress->setTextVisible(false); m_scanProgress->hide(); controlsLayout->addWidget(m_scanProgress); layout->addWidget(controls);
    auto *panel=card(); auto *panelLayout=new QVBoxLayout(panel); panelLayout->setContentsMargins(0,0,0,0); panelLayout->setSpacing(0);
    auto *tableHeader=new QHBoxLayout; tableHeader->setContentsMargins(18,12,18,12); m_fileCount=label(QStringLiteral("大文件列表"),"cardTitle"); tableHeader->addWidget(m_fileCount); tableHeader->addStretch(); m_fileSearch=new QLineEdit; m_fileSearch->setPlaceholderText(QStringLiteral("筛选文件名或路径…")); m_fileSearch->setClearButtonEnabled(true); m_fileSearch->setMaximumWidth(280); m_fileSearch->addAction(lineIcon("search"),QLineEdit::LeadingPosition); connect(m_fileSearch,&QLineEdit::textChanged,this,[this](const QString &text){m_fileFilter->setFilterFixedString(text);updateFileSelection();}); tableHeader->addWidget(m_fileSearch); panelLayout->addLayout(tableHeader);
    m_filesTable=new QTableView; m_filesTable->setObjectName("filesTable"); m_filesTable->setModel(m_fileFilter); configureTable(m_filesTable,true); panelLayout->addWidget(m_filesTable,1); layout->addWidget(panel,1);
    connect(m_filesTable->selectionModel(),&QItemSelectionModel::selectionChanged,this,&MainWindow::updateFileSelection);
    connect(m_filesTable,&QTableView::doubleClicked,this,[this]{openSelectedFileDirectory();});
    auto *footer=card(); auto *footerLayout=new QHBoxLayout(footer); footerLayout->setContentsMargins(18,14,18,14); footerLayout->setSpacing(10);
    m_fileDetail=label(QStringLiteral("选择文件查看逻辑大小与实际占用。"),"muted"); m_fileDetail->setWordWrap(true); m_fileDetail->setTextInteractionFlags(Qt::TextSelectableByMouse); footerLayout->addWidget(m_fileDetail,1);
    m_copyPath=button(QStringLiteral("复制路径"),"copy"); m_copyPath->setEnabled(false); connect(m_copyPath,&QPushButton::clicked,this,[this]{if(auto *entry=selectedFile())QApplication::clipboard()->setText(entry->path);}); footerLayout->addWidget(m_copyPath);
    m_openDirectory=button(QStringLiteral("打开所在目录"),"folder"); m_openDirectory->setEnabled(false); connect(m_openDirectory,&QPushButton::clicked,this,&MainWindow::openSelectedFileDirectory); footerLayout->addWidget(m_openDirectory); layout->addWidget(footer);
    auto *note=label(QStringLiteral("最多展示最大的 500 个文件；硬链接只计一次。仅扫描当前用户可访问的目录，跳过项和中止状态会显示在扫描结果中。"),"muted"); note->setWordWrap(true); layout->addWidget(note);
    return page;
}

void MainWindow::setPage(int page)
{
    m_pages->setCurrentIndex(page);
    for(int i=0;i<m_nav.size();++i)m_nav[i]->setChecked(i==page);
    m_title->setText(QStringList{QStringLiteral("你的磁盘，一目了然"),QStringLiteral("管理已安装的软件"),QStringLiteral("找到占空间的大文件"),QStringLiteral("清理缓存与日志"),QStringLiteral("逐层看清空间去向"),QStringLiteral("看懂开发环境占用")}.value(page));
    m_subtitle->setText(QStringList{QStringLiteral("了解空间去向，让每一份存储都有迹可循。"),QStringLiteral("查看软件体积与来源，关联用户数据，按需卸载。"),QStringLiteral("从一个目录开始，查看体积最大的文件。"),QStringLiteral("按应用查看缓存占用，勾选后批量清理。"),QStringLiteral("目录分层分析、完整内容校验与只读报告。"),QStringLiteral("识别构建产物、开发缓存与原始数据，先看用途再决定。")}.value(page));
    if(page==3){m_cacheRequested=true;m_cachePage->ensureScanStarted();}
    if(page==4){m_explorerRequested=true;m_explorerPage->ensureScanStarted();}
    if(page==5){m_developerRequested=true;m_developerPage->ensureScanStarted();}
}

void MainWindow::refreshStorage()
{
    if (m_volumes->count()==0) {
        const QSignalBlocker block(m_volumes);
        QStringList paths;
        for (const auto &volume:QStorageInfo::mountedVolumes()) {
            if(!volume.isValid() || !volume.isReady() || volume.bytesTotal()<=0)continue;
            const QString path=volume.rootPath();
            if(path.startsWith("/snap/")||path.startsWith("/proc")||path.startsWith("/sys")||path.startsWith("/dev")||path.startsWith("/run")||paths.contains(path))continue;
            paths.append(path);
        }
        if(!paths.contains("/"))paths.prepend("/");
        std::sort(paths.begin(),paths.end());
        for(const auto &path:paths)m_volumes->addItem(path,path);
    }
    QStorageInfo info(m_volumes->currentData().toString()); info.refresh();
    if(!info.isValid()||!info.isReady()||info.bytesTotal()<=0) {
        m_diskUsed->setText(QStringLiteral("不可用"));m_diskAvailable->setText(QStringLiteral("无法读取此磁盘")); m_diskTotal->clear();m_diskReserved->clear();m_diskRing->setSpace(0,0,0);return;
    }
    const quint64 total=info.bytesTotal(),free=qMax<qint64>(0,info.bytesFree()),available=qMax<qint64>(0,info.bytesAvailable());
    const quint64 used=total-qMin(total,free),reserved=free>available ? free-available : 0;
    m_diskRing->setSpace(total,used,reserved); m_diskUsed->setText(formatBytes(used)); m_diskAvailable->setText(QStringLiteral("●  可用 ")+formatBytes(available));m_diskTotal->setText(QStringLiteral("总容量 ")+formatBytes(total));
    m_diskReserved->setText(QString::fromLatin1(info.fileSystemType()).toUpper()+QStringLiteral("  ·  系统预留 ")+formatBytes(reserved));
    m_diskReserved->setToolTip(QStringLiteral("已使用 = 总容量 − 空闲空间；可用空间扣除了文件系统预留。独立挂载的软件或文件可能位于其他磁盘。"));
}

void MainWindow::reloadInventory()
{
    if(m_inventoryWatcher.isRunning()||m_uninstaller->isBusy())return;
    m_refresh->setEnabled(false); m_inventoryProgress->show(); m_updateLabel->setText(QStringLiteral("正在读取软件信息…"));
    m_inventoryWatcher.setFuture(QtConcurrent::run([]{return readInstalledSoftware();}));
}
void MainWindow::inventoryFinished()
{
    auto result=m_inventoryWatcher.result(); m_warnings=result.warnings; m_appModel->setEntries(std::move(result.apps));
    m_inventoryDone=true; m_inventoryProgress->hide(); m_refresh->setEnabled(!m_uninstaller->isBusy());
    quint64 total=0;int desktop=0;QVector<RankItem> ranked;
    for(const auto &app:m_appModel->entries()) {total+=app.installedBytes;if(app.desktopApp){++desktop;if(app.installedBytes)ranked.append({app.name,app.installedBytes});}}
    std::sort(ranked.begin(),ranked.end(),[](const RankItem&a,const RankItem&b){return a.bytes>b.bytes;});if(ranked.size()>6)ranked.resize(6);m_rank->setItems(ranked);
    m_appSize->setText(formatBytes(total));m_appSize->setToolTip(QStringLiteral("全部已安装软件包的体积之和，非仅桌面应用。DEB 安装元数据、Snap 当前压缩包、Flatpak 部署估值；未知体积未计入，不等于卸载可释放空间。"));
    m_appSummary->setText(QStringLiteral("%1 个应用 · %2 个软件包").arg(desktop).arg(m_appModel->rowCount()));
    m_warningButton->setVisible(!m_warnings.isEmpty());m_updateLabel->setText(QStringLiteral("数据更新于 ")+QDateTime::currentDateTime().toString("HH:mm:ss"));updateAppCount();updateAppSelection();
}
void MainWindow::updateAppCount()
{
    if(!m_appCount)return;
    m_appCount->setText(QStringLiteral("%1 项 · %2").arg(m_appFilter->rowCount()).arg(m_appKind->currentIndex()==0?QStringLiteral("桌面应用"):QStringLiteral("所有软件包")));
}
const AppEntry *MainWindow::selectedApp() const
{
    if(!m_appsTable->currentIndex().isValid())return nullptr;
    return m_appModel->entry(m_appFilter->mapToSource(m_appsTable->currentIndex()).row());
}
void MainWindow::updateAppSelection()
{
    if(!m_remove)return;
    const auto *entry=selectedApp(); m_remove->setEnabled(entry && entry->removable && !m_uninstaller->isBusy());
    m_appStorage->setEnabled(entry && !m_uninstaller->isBusy());
    if(!entry){m_appDetail->setText(m_appFilter->rowCount()==0&&m_inventoryDone?QStringLiteral("没有匹配的软件。可更换筛选条件或搜索词。 "):QStringLiteral("选择一款软件，查看详情或卸载。"));return;}
    QString detail=entry->name+"  ·  "+entry->id;
    if(!entry->removable)detail+=QStringLiteral("\n系统组件受保护：")+entry->protectionReason;
    else detail+="\n"+entry->description.left(150);
    m_appDetail->setText(detail);m_remove->setToolTip(entry->removable?QStringLiteral("先预览卸载影响，再确认操作"):entry->protectionReason);
}

void MainWindow::startScan()
{
    if(m_scanActive)return;
    const QString path=QDir::cleanPath(QFileInfo(m_scanPath->text().trimmed()).absoluteFilePath());
    if(m_scanPath->text().trimmed().isEmpty()||!QFileInfo(path).isDir()) {
        m_scanResult={}; m_scanResult.error=QStringLiteral("请选择一个存在的目录。"); m_scanDone=true;
        m_fileModel->setEntries({});m_fileCount->setText(QStringLiteral("大文件 · 0 个"));
        m_largeSize->setText(QStringLiteral("—"));m_largeSummary->setText(QStringLiteral("扫描失败"));
        m_scanStatus->setText(m_scanResult.error);m_scanStatus->setToolTip(m_scanResult.error);
        m_overviewScanStatus->setText(QStringLiteral("无法扫描 · 请选择一个存在的目录"));
        updateOverviewFiles();updateFileSelection();
        return;
    }
    m_scanPath->setText(path);m_cancelScan.store(false);m_scanDone=false;m_scanActive=true;
    m_scanResult={};m_fileModel->setEntries({});m_fileCount->setText(QStringLiteral("扫描中…"));
    m_largeSize->setText(QStringLiteral("—"));m_largeSummary->setText(QStringLiteral("扫描中…"));updateOverviewFiles();updateFileSelection();
    m_scanButton->setEnabled(false);m_scanPath->setEnabled(false);m_minSize->setEnabled(false);m_browse->setEnabled(false);m_cancelButton->show();m_cancelButton->setEnabled(true);m_scanProgress->show();
    m_scanStatus->setText(QStringLiteral("正在扫描…"));m_overviewScanStatus->setText(QStringLiteral("后台扫描中，可在「大文件」页面停止"));
    const quint64 minimum=m_minSize->currentData().toULongLong();
    m_scanWatcher.setFuture(QtConcurrent::run([this,path,minimum]{
        return scanLargeFiles(path,minimum,500,&m_cancelScan,[this](quint64 count,const QString &current){
            QMetaObject::invokeMethod(this,[this,count,current]{
                if(!m_scanActive || m_cancelScan.load()) return;
                const QString text=QStringLiteral("已检查 %1 个文件 · %2").arg(count).arg(current);
                m_scanStatus->setText(text);m_scanStatus->setToolTip(text);
            },Qt::QueuedConnection);
        });
    }));
}
void MainWindow::scanFinished()
{
    m_scanActive=false;m_scanResult=m_scanWatcher.result();m_fileModel->setEntries(m_scanResult.files);m_scanDone=true;
    m_scanButton->setEnabled(true);m_scanPath->setEnabled(true);m_minSize->setEnabled(true);m_browse->setEnabled(true);m_cancelButton->hide();m_scanProgress->hide();
    QString status;
    if(!m_scanResult.error.isEmpty())status=QStringLiteral("扫描失败：")+m_scanResult.error;
    else status=QStringLiteral("%1 · 已检查 %2 个文件 · 匹配 %3 个 · 跳过 %4 项").arg(m_scanResult.canceled?QStringLiteral("已停止，结果不完整"):QStringLiteral("扫描完成")).arg(m_scanResult.scannedFiles).arg(m_scanResult.matchingFiles).arg(m_scanResult.skippedEntries);
    m_scanStatus->setText(status);m_scanStatus->setToolTip(status+QStringLiteral("\n跳过项包括权限不足、其他挂载点以及扫描时发生变化的条目。软链接与特殊文件直接忽略。"));
    m_fileCount->setText(QStringLiteral("大文件 · 展示 %1 / %2 个").arg(m_scanResult.files.size()).arg(m_scanResult.matchingFiles));
    if(!m_scanResult.error.isEmpty()) {m_largeSize->setText(QStringLiteral("—"));m_largeSummary->setText(QStringLiteral("扫描失败"));}
    else {m_largeSize->setText(formatBytes(m_scanResult.totalMatchingBytes));m_largeSummary->setText(QStringLiteral("%1 个 ≥ %2%3").arg(m_scanResult.matchingFiles).arg(formatBytes(m_minSize->currentData().toULongLong(),0)).arg(m_scanResult.canceled?QStringLiteral(" · 部分"):QString()));}
    m_overviewScanStatus->setText(m_scanResult.error.isEmpty()?(m_scanResult.canceled?QStringLiteral("已停止 · 以下是部分结果"):QStringLiteral("扫描完成 · 最大的文件优先显示")):QStringLiteral("扫描失败 · 请到大文件页面查看原因"));
    updateOverviewFiles();updateFileSelection();refreshStorage();
}
void MainWindow::updateOverviewFiles()
{
    if(!m_recentFiles)return;
    clearLayout(m_recentFiles);
    if(m_scanResult.files.isEmpty()) {
        QString text=m_scanDone?QStringLiteral("此目录中没有找到符合条件的大文件\n可以调整扫描目录或文件大小。"):QStringLiteral("正在查找占用空间的大文件…\n你可以随时切换页面或停止扫描。");
        if(!m_scanResult.error.isEmpty())text=QStringLiteral("扫描未完成\n请到大文件页面检查目录。");
        else if(m_scanResult.canceled)text=QStringLiteral("扫描已停止，暂未找到大文件\n可以调整目录并重新扫描。");
        auto *empty=label(text,"muted");empty->setAlignment(Qt::AlignCenter);empty->setWordWrap(true);m_recentFiles->addWidget(empty,1);return;
    }
    for(int i=0;i<qMin(5,int(m_scanResult.files.size()));++i) {
        const auto &entry=m_scanResult.files[i];QFileInfo info(entry.path);
        auto *row=new QWidget;auto *rowLayout=new QHBoxLayout(row);rowLayout->setContentsMargins(0,10,0,10);rowLayout->setSpacing(10);
        auto *icon=new QLabel;icon->setPixmap(lineIcon("files",QColor("#4bb9a1")).pixmap(24,24));rowLayout->addWidget(icon);
        auto *text=new QVBoxLayout;text->setSpacing(4);
        auto *name=label(QFontMetrics(font()).elidedText(info.fileName(),Qt::ElideMiddle,230));name->setToolTip(entry.path);text->addWidget(name);
        auto *path=label(QFontMetrics(font()).elidedText(info.absolutePath(),Qt::ElideMiddle,240),"muted");path->setToolTip(entry.path);text->addWidget(path);rowLayout->addLayout(text,1);
        auto *size=label(formatBytes(entry.bytes));size->setStyleSheet("color:#4b5c7c;font-weight:600;font-size:12px;");rowLayout->addWidget(size);m_recentFiles->addWidget(row);
    }
    m_recentFiles->addStretch();
}
const FileEntry *MainWindow::selectedFile() const
{
    if(!m_filesTable->currentIndex().isValid())return nullptr;
    return m_fileModel->entry(m_fileFilter->mapToSource(m_filesTable->currentIndex()).row());
}
void MainWindow::updateFileSelection()
{
    const auto *entry=selectedFile();m_openDirectory->setEnabled(entry);m_copyPath->setEnabled(entry);
    if(!entry){m_fileDetail->setText(m_scanDone && m_fileFilter->rowCount()==0?QStringLiteral("没有匹配文件。可以降低大小阈值、调整目录或清除筛选。 "):QStringLiteral("选择文件查看逻辑大小与实际占用。"));return;}
    m_fileDetail->setText(QStringLiteral("逻辑大小 %1  ·  实际占用 %2\n%3").arg(formatBytes(entry->bytes),formatBytes(entry->allocatedBytes),QFontMetrics(font()).elidedText(entry->path,Qt::ElideMiddle,520)));m_fileDetail->setToolTip(entry->path);
}
void MainWindow::openSelectedFileDirectory()
{
    if(auto *entry=selectedFile()) {
        const QString path=QFileInfo(entry->path).absolutePath();
        if(!QFileInfo(path).isDir()||!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))QMessageBox::warning(this,QStringLiteral("无法打开目录"),QStringLiteral("目录可能已被移动，或系统未配置文件管理器。\n")+path);
    }
}

void MainWindow::beginUninstall()
{
    const auto *entry=selectedApp();if(!entry||!entry->removable||m_uninstaller->isBusy())return;
    m_pendingName=entry->name;m_uninstaller->preview(*entry);
}
void MainWindow::showAppStorage()
{
    const auto *entry = selectedApp();
    if (!entry || m_uninstaller->isBusy()) return;
    AppStorageDialog dialog(*entry, this);
    dialog.exec();
}
void MainWindow::showRemovalPreview(const QString &summary,const QStringList &affected)
{
    QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("确认卸载软件"));dialog.setMinimumWidth(580);
    auto *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(24,24,24,24);layout->setSpacing(15);
    layout->addWidget(label(QStringLiteral("卸载 ")+m_pendingName+QStringLiteral("？"),"cardTitle"));
    auto *description=label(summary);description->setWordWrap(true);layout->addWidget(description);
    layout->addWidget(label(QStringLiteral("本次卸载影响的软件包（%1）：").arg(affected.size()),"muted"));
    auto *details=new QPlainTextEdit;details->setReadOnly(true);details->setPlainText(affected.join('\n'));details->setMinimumHeight(130);layout->addWidget(details);
    auto *note=label(QStringLiteral("DEB 卸载不执行自动清理或配置清除。卸载 Snap 可能移除其用户数据；Flatpak 默认保留用户数据。系统安装的软件将通过系统授权弹窗验证。"),"muted");note->setWordWrap(true);layout->addWidget(note);
    auto *buttons=new QDialogButtonBox;auto *confirm=buttons->addButton(QStringLiteral("确认卸载"),QDialogButtonBox::AcceptRole);confirm->setObjectName("danger");auto *cancel=buttons->addButton(QStringLiteral("取消"),QDialogButtonBox::RejectRole);cancel->setDefault(true);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted){m_uninstaller->cancelPreview();return;}
    m_operationDialog=new OperationDialog(this);m_operationDialog->setWindowTitle(QStringLiteral("正在卸载软件"));m_operationDialog->setMinimumSize(600,360);m_operationDialog->setWindowFlags(Qt::Dialog|Qt::CustomizeWindowHint|Qt::WindowTitleHint);m_operationDialog->setModal(true);
    auto *operationLayout=new QVBoxLayout(m_operationDialog);auto *hint=label(QStringLiteral("请完成系统授权，并等待卸载结束。"),"cardTitle");operationLayout->addWidget(hint);m_operationLog=new QPlainTextEdit;m_operationLog->setReadOnly(true);operationLayout->addWidget(m_operationLog);m_operationDialog->show();
    m_uninstaller->execute();
}
void MainWindow::showWarnings()
{
    QMessageBox box(QMessageBox::Information,QStringLiteral("软件数据提示"),QStringLiteral("部分软件来源的读取情况如下。已成功获取的数据仍可正常查看。"),QMessageBox::Ok,this);box.setDetailedText(m_warnings.join("\n\n"));box.exec();
}
bool MainWindow::verifyUi(QString *error)
{
    if(!isSettled()){*error="background work has not completed";return false;}
    if(m_appModel->rowCount()==0){*error="no installed packages read";return false;}
    const int before=m_appFilter->rowCount();
    m_search->setText("DISKLENS_NO_SUCH_PACKAGE_09324");
    if(m_appFilter->rowCount()!=0){*error="application search failed";return false;}
    m_search->clear();if(m_appFilter->rowCount()!=before){*error="application filter reset failed";return false;}
    m_appKind->setCurrentIndex(1);if(m_appFilter->rowCount()!=m_appModel->rowCount()){*error="all-package view failed";return false;}m_appKind->setCurrentIndex(0);
    if(m_appFilter->rowCount()>0){m_appsTable->selectRow(0);updateAppSelection();if(!selectedApp()){*error="application selection failed";return false;}}
    m_appsTable->sortByColumn(0,Qt::AscendingOrder);m_appsTable->selectRow(0);
    if(selectedApp() && selectedApp()->name != m_appFilter->index(0,0).data().toString()){*error="sorted application mapping failed";return false;}
    m_appsTable->sortByColumn(3,Qt::DescendingOrder);
    for(int i=1;i<m_fileModel->rowCount();++i)if(m_fileModel->entry(i-1)->bytes<m_fileModel->entry(i)->bytes){*error="file order failed";return false;}
    if(m_fileModel->rowCount()>0){m_filesTable->selectRow(0);updateFileSelection();if(!m_openDirectory->isEnabled()){*error="file action selection failed";return false;}}
    const int fileCount=m_fileFilter->rowCount();m_fileSearch->setText("DISKLENS_NO_SUCH_FILE_09324");
    if(m_fileFilter->rowCount()!=0){*error="file search failed";return false;}
    m_fileSearch->clear();if(m_fileFilter->rowCount()!=fileCount){*error="file search reset failed";return false;}
    if(!m_scanResult.error.isEmpty()){*error=m_scanResult.error;return false;}
    if(m_cacheRequested && !m_cachePage->verifyUi(error))return false;
    if(m_explorerRequested && !m_explorerPage->verifyUi(error))return false;
    if(m_developerRequested && !m_developerPage->verifyUi(error))return false;
    m_appsTable->setCurrentIndex({});m_appsTable->clearSelection();m_appsTable->scrollToTop();updateAppSelection();
    m_filesTable->setCurrentIndex({});m_filesTable->clearSelection();m_filesTable->scrollToTop();updateFileSelection();
    qInfo("UI smoke check: %d packages, %d desktop apps, %d large-file rows",m_appModel->rowCount(),before,m_fileModel->rowCount());
    return true;
}
