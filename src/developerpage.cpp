#include "localization.h"
#include "developerpage.h"

#include "models.h"
#include "widgets.h"
#include <QAbstractTableModel>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace {
constexpr int sortRole = Qt::UserRole + 90;
constexpr int categoryRole = Qt::UserRole + 91;
QLabel *label(const QString &text, const char *name = nullptr)
{
    auto *result = new QLabel(text);
    result->setTextFormat(Qt::PlainText);
    if (name) result->setObjectName(QString::fromLatin1(name));
    return result;
}
QPushButton *button(const QString &text, const char *name)
{
    auto *result = new QPushButton(text); result->setObjectName(QString::fromLatin1(name));
    result->setCursor(Qt::PointingHandCursor); return result;
}
QString number(quint64 value) { return QLocale().toString(value); }
} // namespace

class DeveloperModel final : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_entries.size(); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 6; }
    const DeveloperEntry *entry(int row) const { return row >= 0 && row < m_entries.size() ? &m_entries.at(row) : nullptr; }
    const QVector<DeveloperEntry> &entries() const { return m_entries; }
    void setEntries(QVector<DeveloperEntry> entries) { beginResetModel(); m_entries = std::move(entries); endResetModel(); }
    QVariant data(const QModelIndex &index, int role) const override
    {
        const auto *e = entry(index.row());
        if (!index.isValid() || !e) return {};
        if (role == categoryRole) return int(e->category);
        if (role == Qt::ToolTipRole)
            return e->path + QLatin1Char('\n') + e->evidence + QLatin1Char('\n') + e->rebuildability;
        if (role == Qt::TextAlignmentRole && index.column() >= 2 && index.column() <= 4)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        if (role == sortRole) {
            switch (index.column()) {
            case 0: return developerCategoryName(e->category);
            case 1: return e->name.toCaseFolded();
            case 2: return QVariant::fromValue<qulonglong>(e->allocatedBytes);
            case 3: return QVariant::fromValue<qulonglong>(e->bytes);
            case 4: return QVariant::fromValue<qulonglong>(e->fileCount);
            case 5: return e->path.toCaseFolded();
            }
        }
        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case 0: return developerCategoryName(e->category);
            case 1: return e->name;
            case 2: return formatBytes(e->allocatedBytes);
            case 3: return formatBytes(e->bytes);
            case 4: return number(e->fileCount);
            case 5: return e->path;
            }
        }
        return {};
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
        return QStringList{dlTr("类别"), dlTr("项目 / 目录"), dlTr("实际占用"),
                           dlTr("逻辑大小"), dlTr("文件数"), dlTr("目录路径")}.value(section);
    }
private:
    QVector<DeveloperEntry> m_entries;
};

class DeveloperFilter final : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setSearch(const QString &text) { m_search = text.trimmed(); invalidateFilter(); }
    void setCategory(int category) { m_category = category; invalidateFilter(); }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &) const override
    {
        const auto *model = static_cast<const DeveloperModel *>(sourceModel());
        const DeveloperEntry *entry = model->entry(row);
        return entry && (m_category < 0 || int(entry->category) == m_category)
            && (m_search.isEmpty() || entry->name.contains(m_search, Qt::CaseInsensitive)
                || entry->path.contains(m_search, Qt::CaseInsensitive)
                || developerCategoryName(entry->category).contains(m_search, Qt::CaseInsensitive));
    }
private:
    QString m_search;
    int m_category = -1;
};

DeveloperPage::DeveloperPage(QWidget *parent) : QWidget(parent), m_root(QDir::homePath())
{
    m_model = new DeveloperModel(this); m_filter = new DeveloperFilter(this);
    m_filter->setSourceModel(m_model); m_filter->setSortRole(sortRole);
    m_filter->setSortCaseSensitivity(Qt::CaseInsensitive);
    createUi();
    connect(&m_watcher, &QFutureWatcher<DeveloperScanResult>::finished, this, &DeveloperPage::scanFinished);
    connect(m_filter, &QAbstractItemModel::rowsInserted, this, [this] { updateCount(); });
    connect(m_filter, &QAbstractItemModel::rowsRemoved, this, [this] { updateCount(); });
    connect(m_filter, &QAbstractItemModel::modelReset, this, [this] { updateCount(); });
    updateControls(); updateDetail(); updateCount();
}

DeveloperPage::~DeveloperPage()
{
    m_shuttingDown = true; m_cancel.store(true); m_watcher.waitForFinished();
}
bool DeveloperPage::isBusy() const { return m_scanning; }
bool DeveloperPage::isSettled() const { return m_started && m_done && !m_scanning; }
void DeveloperPage::cancelWork() { if (m_scanning) { m_cancel.store(true); m_cancelButton->setEnabled(false); } }
void DeveloperPage::ensureScanStarted() { if (!m_started) startScan(); }
void DeveloperPage::rescan() { startScan(); }
void DeveloperPage::setRoot(QString root)
{
    if (isBusy()) return;
    m_root = root.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    m_rootField->setText(m_root); m_rootField->setToolTip(m_root);
    m_started = false; m_done = false; m_result = {};
    m_model->setEntries({}); m_total->setText(QStringLiteral("—")); m_identified->setText(QStringLiteral("—"));
    m_summary->setText(dlTr("进入页面或点击重新扫描后开始，仅统计所选目录。"));
    m_status->setText(dlTr("尚未扫描")); m_notice->hide(); updateDetail(); updateControls();
}

void DeveloperPage::createUi()
{
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(10);
    auto *metrics = new QHBoxLayout; metrics->setSpacing(14);
    auto metric = [metrics](const QString &title, const char *objectName, QLabel **value, const QString &subtitle) {
        auto *card = new QFrame; card->setObjectName(QStringLiteral("card"));
        auto *box = new QVBoxLayout(card); box->setContentsMargins(18, 9, 18, 9); box->setSpacing(2);
        auto *heading = label(title); heading->setStyleSheet(QStringLiteral("font-size:12px;"));
        box->addWidget(heading); *value = label(QStringLiteral("—"), objectName);
        (*value)->setStyleSheet(QStringLiteral("font-size:23px;font-weight:700;color:#29364f;"));
        box->addWidget(*value); auto *hint = label(subtitle); hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color:#6c7991;font-size:11px;")); box->addWidget(hint); metrics->addWidget(card);
    };
    metric(dlTr("已扫描文件占用"), "developerTotal", &m_total, dlTr("普通文件的磁盘分配空间"));
    metric(dlTr("已识别开发内容"), "developerIdentified", &m_identified, dlTr("嵌套分组和硬链接不重复相加"));
    layout->addLayout(metrics);

    auto *scope = new QFrame; scope->setObjectName(QStringLiteral("card"));
    auto *scopeBox = new QVBoxLayout(scope); scopeBox->setContentsMargins(18, 9, 18, 9); scopeBox->setSpacing(5);
    auto *scopeRow = new QHBoxLayout;
    scopeRow->addWidget(label(dlTr("分析目录")));
    m_rootField = new QLineEdit(m_root); m_rootField->setObjectName(QStringLiteral("developerRoot"));
    m_rootField->setStyleSheet(QStringLiteral("padding:6px 10px;"));
    m_rootField->setReadOnly(true); m_rootField->setToolTip(m_root); scopeRow->addWidget(m_rootField, 1);
    m_choose = button(dlTr("选择目录"), "developerChoose"); scopeRow->addWidget(m_choose);
    m_scan = button(dlTr("重新扫描"), "developerScan"); scopeRow->addWidget(m_scan);
    m_cancelButton = button(dlTr("取消"), "developerCancel"); scopeRow->addWidget(m_cancelButton);
    scopeBox->addLayout(scopeRow);
    auto *scopeHint = label(dlTr("识别 ROS / colcon、录制数据、点云、pip / npm / ccache 与 Conda 环境。只读分析；不跟随符号链接，不跨挂载点。"));
    scopeHint->setWordWrap(true); scopeHint->setStyleSheet(QStringLiteral("color:#6c7991;font-size:11px;")); scopeBox->addWidget(scopeHint);
    layout->addWidget(scope);

    auto *panel = new QFrame; panel->setObjectName(QStringLiteral("card"));
    auto *panelBox = new QVBoxLayout(panel); panelBox->setContentsMargins(0, 9, 0, 9); panelBox->setSpacing(7);
    auto *filterRow = new QHBoxLayout; filterRow->setContentsMargins(18, 0, 18, 0);
    m_search = new QLineEdit; m_search->setObjectName(QStringLiteral("developerSearch"));
    m_search->setStyleSheet(QStringLiteral("padding:6px 10px;"));
    m_search->setPlaceholderText(dlTr("搜索项目、类别或路径")); m_search->setClearButtonEnabled(true); filterRow->addWidget(m_search, 1);
    m_category = new QComboBox; m_category->setObjectName(QStringLiteral("developerCategory"));
    m_category->addItem(dlTr("全部类别"), -1);
    for (int i = int(DeveloperCategory::RosBuild); i <= int(DeveloperCategory::Conda); ++i)
        m_category->addItem(developerCategoryName(DeveloperCategory(i)), i);
    filterRow->addWidget(m_category);
    m_count = label({}, "developerCount"); filterRow->addWidget(m_count); panelBox->addLayout(filterRow);
    m_table = new QTableView; m_table->setObjectName(QStringLiteral("developerTable"));
    m_table->setModel(m_filter); m_table->setSortingEnabled(true); m_table->sortByColumn(2, Qt::DescendingOrder);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); m_table->setShowGrid(false);
    m_table->verticalHeader()->hide(); m_table->verticalHeader()->setDefaultSectionSize(38);
    m_table->horizontalHeader()->setStretchLastSection(true); m_table->horizontalHeader()->setMinimumSectionSize(75);
    m_table->setColumnWidth(0, 135); m_table->setColumnWidth(1, 160); m_table->setColumnWidth(2, 115);
    m_table->setColumnWidth(3, 115); m_table->setColumnWidth(4, 85); m_table->setMinimumHeight(120);
    m_table->setStyleSheet(QStringLiteral("QTableView#developerTable { selection-color:#29364f; selection-background-color:#edf2ff; }"
        "QTableView#developerTable::item { padding:5px 10px; }"
        "QTableView#developerTable::item:selected { color:#29364f; background:#edf2ff; }"));
    panelBox->addWidget(m_table, 1);
    auto *actionRow = new QHBoxLayout; actionRow->setContentsMargins(18, 0, 18, 0);
    m_open = button(dlTr("打开目录"), "developerOpen"); actionRow->addWidget(m_open);
    m_copy = button(dlTr("复制路径"), "developerCopy"); actionRow->addWidget(m_copy);
    actionRow->addStretch(); m_export = button(dlTr("导出 CSV"), "developerExport"); actionRow->addWidget(m_export);
    panelBox->addLayout(actionRow); layout->addWidget(panel, 1);

    auto *details = new QFrame; details->setObjectName(QStringLiteral("card")); details->setFixedHeight(92);
    auto *detailFrameBox = new QVBoxLayout(details); detailFrameBox->setContentsMargins(2, 2, 2, 2);
    auto *detailScroll = new QScrollArea; detailScroll->setObjectName(QStringLiteral("developerDetailsScroll"));
    detailScroll->setFrameShape(QFrame::NoFrame); detailScroll->setWidgetResizable(true);
    detailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailScroll->setStyleSheet(QStringLiteral("QScrollArea#developerDetailsScroll { background:transparent; }"));
    auto *detailBody = new QWidget; detailBody->setObjectName(QStringLiteral("developerDetailsBody"));
    detailBody->setStyleSheet(QStringLiteral("QWidget#developerDetailsBody { background:#ffffff; }"));
    detailBody->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    auto *detailBox = new QVBoxLayout(detailBody); detailBox->setContentsMargins(16, 7, 16, 7); detailBox->setSpacing(6);
    m_detail = label({}, "developerDetail"); m_detail->setWordWrap(true); m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detail->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_detail->setStyleSheet(QStringLiteral("color:#42516e;font-size:11px;")); detailBox->addWidget(m_detail);
    m_notice = label({}, "developerNotice"); m_notice->setWordWrap(true);
    m_notice->setStyleSheet(QStringLiteral("color:#93631e;background:#fff6e6;padding:8px;border-radius:6px;font-size:11px;"));
    m_notice->hide(); detailBox->addWidget(m_notice);
    m_summary = label(dlTr("进入页面后开始扫描，仅统计所选目录。"), "developerSummary");
    m_summary->setWordWrap(true); m_summary->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_summary->setStyleSheet(QStringLiteral("color:#6c7991;font-size:11px;")); detailBox->addWidget(m_summary);
    detailBox->setAlignment(Qt::AlignTop);
    detailScroll->setWidget(detailBody); detailFrameBox->addWidget(detailScroll); layout->addWidget(details);
    auto *statusRow = new QHBoxLayout; m_status = label(dlTr("尚未扫描"), "developerStatus");
    m_status->setStyleSheet(QStringLiteral("color:#6c7991;font-size:11px;")); statusRow->addWidget(m_status, 1);
    m_progress = new QProgressBar; m_progress->setObjectName(QStringLiteral("developerProgress"));
    m_progress->setRange(0, 0); m_progress->setTextVisible(false); m_progress->setFixedWidth(140); m_progress->setFixedHeight(6);
    statusRow->addWidget(m_progress); layout->addLayout(statusRow);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) { m_filter->setSearch(text); updateCount(); updateDetail(); });
    connect(m_category, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_filter->setCategory(m_category->itemData(index).toInt()); updateCount(); updateDetail();
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] { updateDetail(); });
    connect(m_scan, &QPushButton::clicked, this, &DeveloperPage::rescan);
    connect(m_cancelButton, &QPushButton::clicked, this, &DeveloperPage::cancelWork);
    connect(m_choose, &QPushButton::clicked, this, [this] {
        const QString root = QFileDialog::getExistingDirectory(this, dlTr("选择开发环境分析目录"), m_root);
        if (!root.isEmpty()) { setRoot(root); startScan(); }
    });
    connect(m_copy, &QPushButton::clicked, this, [this] { if (const auto *entry = currentEntry()) QApplication::clipboard()->setText(entry->path); });
    connect(m_open, &QPushButton::clicked, this, [this] {
        if (const auto *entry = currentEntry()) {
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(entry->path)))
                QMessageBox::warning(this, dlTr("无法打开目录"), dlTr("系统文件管理器未能打开所选路径。"));
        }
    });
    connect(m_export, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, dlTr("导出开发环境分析报告"),
            QDir::home().filePath(dlTr("DiskLens-开发环境.csv")), dlTr("CSV 表格 (*.csv)"));
        if (path.isEmpty()) return;
        QString error;
        if (!exportReport(path, &error)) QMessageBox::warning(this, dlTr("导出失败"), error);
        else m_status->setText(dlTr("报告已导出 · 包含全部已显示分组，搜索和类别筛选不影响导出范围"));
    });
}

void DeveloperPage::startScan()
{
    if (isBusy() || m_shuttingDown) return;
    m_started = true; m_done = false; m_scanning = true; m_cancel.store(false); ++m_operation;
    const quint64 operation = m_operation;
    m_result = {}; m_model->setEntries({}); m_notice->hide();
    m_status->setText(dlTr("正在只读扫描开发目录…"));
    m_total->setText(QStringLiteral("—")); m_identified->setText(QStringLiteral("—"));
    updateControls(); updateDetail();
    DeveloperScanOptions options; options.root = m_root;
    DeveloperProgress progress = [this, operation](quint64 count, const QString &path) {
        QMetaObject::invokeMethod(this, [this, operation, count, path] {
            if (m_shuttingDown || operation != m_operation || !m_scanning || m_cancel.load()) return;
            m_status->setText(dlTr("已扫描 %1 个文件 · %2").arg(number(count),
                QFontMetrics(font()).elidedText(path, Qt::ElideMiddle, qMax(150, m_status->width() - 180))));
            m_status->setToolTip(path);
        }, Qt::QueuedConnection);
    };
    m_watcher.setFuture(QtConcurrent::run([this, options, progress] { return scanDeveloperUsage(options, &m_cancel, progress); }));
}

void DeveloperPage::scanFinished()
{
    if (m_shuttingDown) return;
    m_result = m_watcher.result(); m_scanning = false; m_done = true;
    m_model->setEntries(m_result.entries); m_table->sortByColumn(2, Qt::DescendingOrder);
    m_table->clearSelection(); m_table->setCurrentIndex({});
    m_total->setText(formatBytes(m_result.totalAllocatedBytes));
    m_identified->setText(formatBytes(m_result.identifiedAllocatedBytes));
    QString state = dlTr("扫描完成");
    if (m_result.canceled) state = dlTr("扫描已取消 · 当前为部分结果");
    else if (!m_result.error.isEmpty()) state = dlTr("扫描失败");
    else if (m_result.limited || m_result.skippedEntries) state = dlTr("扫描完成 · 部分目录未纳入");
    m_status->setText(dlTr("%1 · %2 个文件 / %3 个目录").arg(state, number(m_result.scannedFiles), number(m_result.scannedDirectories)));
    m_status->setToolTip({});
    QStringList notes = m_result.warnings;
    if (!m_result.error.isEmpty()) notes.prepend(m_result.error);
    if (m_result.canceled) notes.prepend(dlTr("扫描被取消，统计尚不完整。重新扫描可补全。"));
    if (!notes.isEmpty()) { m_notice->setText(notes.join(QLatin1Char('\n'))); m_notice->show(); }
    m_summary->setText(dlTr("占用按普通文件已分配磁盘块统计；不包含目录元数据。嵌套分组单列并从上层扣除，硬链接仅计一次。"
        "最多检查 50 万项 / 5 万目录 / 32 层，显示前 2000 组；大文件阈值 256 MiB。Docker 镜像与卷分析计划后续提供。"));
    updateCount(); updateDetail(); updateControls();
}

const DeveloperEntry *DeveloperPage::currentEntry() const
{
    const QModelIndex current = m_table->currentIndex();
    return current.isValid() ? m_model->entry(m_filter->mapToSource(current).row()) : nullptr;
}
void DeveloperPage::updateCount()
{
    m_count->setText(dlTr("%1 / %2 组").arg(m_filter->rowCount()).arg(m_model->rowCount()));
}
void DeveloperPage::updateDetail()
{
    const DeveloperEntry *entry = currentEntry();
    if (!entry) m_detail->setText(dlTr("选择一行，查看识别依据、用途、可重建性与建议动作。原始录制、点云和 Conda 环境均不作为缓存自动处理。"));
    else m_detail->setText(dlTr("识别依据：%1\n用途：%2\n可重建性：%3\n建议动作：%4%5")
        .arg(entry->evidence, entry->purpose, entry->rebuildability, entry->suggestion,
             entry->excludesNestedGroups ? dlTr("\n统计范围：已扣除单独列出的嵌套分组，当前占用可以与其他行相加。") : QString()));
    m_open->setEnabled(entry && !m_scanning); m_copy->setEnabled(entry && !m_scanning);
}
void DeveloperPage::updateControls()
{
    m_scan->setEnabled(!m_scanning); m_choose->setEnabled(!m_scanning);
    m_cancelButton->setEnabled(m_scanning && !m_cancel.load()); m_progress->setVisible(m_scanning);
    m_export->setEnabled(isSettled() && !m_result.entries.isEmpty());
    updateDetail();
}
bool DeveloperPage::exportReport(const QString &path, QString *error) const
{
    if (!isSettled()) { if (error) *error = dlTr("请等待扫描完成或取消后再导出。"); return false; }
    return exportDeveloperCsv(m_result, path, error);
}
bool DeveloperPage::verifyUi(QString *error)
{
    const auto fail = [error](const QString &text) { if (error) *error = text; return false; };
    if (!isSettled()) return fail(dlTr("开发环境扫描尚未结束。"));
    if (!m_result.error.isEmpty()) return fail(m_result.error);
    QSet<QString> paths;
    quint64 sum = m_result.hiddenAllocatedBytes;
    for (const auto &entry : m_model->entries()) {
        if (entry.path.isEmpty() || paths.contains(entry.path)) return fail(dlTr("开发环境分组路径重复或为空。"));
        paths.insert(entry.path); sum += entry.allocatedBytes;
        if (entry.purpose.isEmpty() || entry.rebuildability.isEmpty() || entry.suggestion.isEmpty())
            return fail(dlTr("开发环境分组缺少解释。"));
    }
    if (sum != m_result.identifiedAllocatedBytes || sum > m_result.totalAllocatedBytes)
        return fail(dlTr("开发环境统计存在重复或总计不一致。"));
    const QString search = m_search->text(); const int category = m_category->currentData().toInt();
    m_filter->setSearch(QStringLiteral("DISKLENS_NO_SUCH_DEVELOPER_ENTRY_092481"));
    const bool searchWorks = m_filter->rowCount() == 0;
    m_filter->setSearch(search); m_filter->setCategory(category); updateCount(); updateDetail();
    if (!searchWorks) return fail(dlTr("开发环境搜索筛选失效。"));
    if (error) error->clear();
    return true;
}
