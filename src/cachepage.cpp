#include "cachepage.h"

#include "models.h"
#include "widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionButton>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace {

constexpr quint64 largeThreshold = 100ULL * 1024 * 1024;
constexpr int cacheSortRole = Qt::UserRole + 70;

QLabel *plainLabel(const QString &text, const char *name = nullptr)
{
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    if (name)
        label->setObjectName(QString::fromLatin1(name));
    return label;
}

QFrame *cacheCard()
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("card"));
    return frame;
}

QPushButton *cacheButton(const QString &text, const char *name, const QString &icon = {})
{
    auto *button = new QPushButton(text);
    button->setObjectName(QString::fromLatin1(name));
    button->setCursor(Qt::PointingHandCursor);
    if (!icon.isEmpty()) {
        button->setIcon(lineIcon(icon));
        button->setIconSize({18, 18});
    }
    return button;
}

QString defaultRoot(const char *variable, const QString &fallback)
{
    const QString configured = qEnvironmentVariable(variable);
    return QDir::cleanPath(QDir::isAbsolutePath(configured) ? configured : fallback);
}

QString absoluteRoot(const QString &root)
{
    return root.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(root).absoluteFilePath());
}

QString countText(quint64 count)
{
    return QLocale().toString(count);
}

} // namespace

class CacheModel final : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_entries.size(); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 6; }
    const CacheEntry *entry(int row) const { return row >= 0 && row < m_entries.size() ? &m_entries.at(row) : nullptr; }
    const QVector<CacheEntry> &entries() const { return m_entries; }
    bool isChecked(int row) const { const auto *item = entry(row); return item && m_checked.contains(item->id); }

    QVariant data(const QModelIndex &index, int role) const override
    {
        const CacheEntry *item = entry(index.row());
        if (!item || !index.isValid())
            return {};
        if (role == Qt::CheckStateRole && index.column() == 0)
            return isChecked(index.row()) ? Qt::Checked : Qt::Unchecked;
        if (role == Qt::ToolTipRole) {
            return item->name + QLatin1Char('\n') + item->path
                + QStringLiteral("\n%1 · %2 个文件 · 实际占用 %3\n%4")
                      .arg(item->logOnly ? QStringLiteral("仅日志") : QStringLiteral("普通缓存"))
                      .arg(item->fileCount).arg(formatBytes(item->allocatedBytes))
                      .arg(item->logOnly ? QStringLiteral("仅清理已扫描的日志文件；同目录的普通文件会保留。")
                                         : QStringLiteral("仅清理已扫描的普通缓存；日志独立列出。"));
        }
        if (role == cacheSortRole) {
            switch (index.column()) {
            case 0: return isChecked(index.row());
            case 1: return item->name.toCaseFolded();
            case 2: return item->logOnly;
            case 3: return QVariant::fromValue<qulonglong>(item->fileCount);
            case 4: return QVariant::fromValue<qulonglong>(item->bytes);
            case 5: return QVariant::fromValue<qulonglong>(item->allocatedBytes);
            }
        }
        if (role == Qt::TextAlignmentRole && index.column() >= 3)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case 1: return item->name;
            case 2: return item->logOnly ? QStringLiteral("日志") : QStringLiteral("缓存");
            case 3: return QStringLiteral("%1 / %2").arg(countText(item->fileCount), countText(item->logCount));
            case 4: return formatBytes(item->bytes);
            case 5: return formatBytes(item->allocatedBytes);
            }
        }
        if (role == Qt::UserRole && index.column() == 1)
            return item->path;
        return {};
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        const QStringList labels{QString(), QStringLiteral("应用 / 缓存目录"), QStringLiteral("分类"),
                                 QStringLiteral("文件 / 日志"), QStringLiteral("逻辑大小"), QStringLiteral("实际占用")};
        return labels.value(section);
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        Qt::ItemFlags result = QAbstractTableModel::flags(index);
        if (index.isValid() && index.column() == 0 && entry(index.row())->fileCount)
            result |= Qt::ItemIsUserCheckable;
        return result;
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        const CacheEntry *item = entry(index.row());
        if (!item || !index.isValid() || index.column() != 0 || role != Qt::CheckStateRole || !item->fileCount)
            return false;
        if (value.toInt() == Qt::Checked)
            m_checked.insert(item->id);
        else
            m_checked.remove(item->id);
        emit dataChanged(index, index, {Qt::CheckStateRole, cacheSortRole});
        return true;
    }

    void setEntries(QVector<CacheEntry> entries)
    {
        beginResetModel();
        m_entries = std::move(entries);
        m_checked.clear();
        endResetModel();
    }

    void clearChecked()
    {
        m_checked.clear();
        if (rowCount())
            emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {Qt::CheckStateRole, cacheSortRole});
    }

    QVector<CacheEntry> selectedEntries() const
    {
        QVector<CacheEntry> result;
        for (const CacheEntry &item : m_entries) {
            if (m_checked.contains(item.id) && item.fileCount)
                result.append(item); // Retain the complete immutable provenance.
        }
        return result;
    }

private:
    QVector<CacheEntry> m_entries;
    QSet<QString> m_checked;
};

class CacheFilter final : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setSearch(QString search) { m_search = search.trimmed(); invalidateFilter(); }
    void setLargeOnly(bool large) { m_large = large; invalidateFilter(); }
    void setLogsOnly(bool logs) { m_logs = logs; invalidateFilter(); }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &) const override
    {
        const auto *model = static_cast<const CacheModel *>(sourceModel());
        const CacheEntry *item = model->entry(row);
        if (!item || (m_large && item->allocatedBytes <= largeThreshold) || (m_logs && !item->logOnly))
            return false;
        return m_search.isEmpty() || item->name.contains(m_search, Qt::CaseInsensitive)
            || item->path.contains(m_search, Qt::CaseInsensitive);
    }
private:
    QString m_search;
    bool m_large = false;
    bool m_logs = false;
};

namespace {

class CacheTableView final : public QTableView {
public:
    using QTableView::QTableView;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        const QModelIndex pressed = indexAt(event->pos());
        m_pressed = event->button() == Qt::LeftButton && pressed.isValid()
            ? pressed.sibling(pressed.row(), 0) : QModelIndex();
        QTableView::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const QPersistentModelIndex pressed = m_pressed;
        m_pressed = QModelIndex();
        const QModelIndex released = indexAt(event->pos());
        const bool clickedSameRow = event->button() == Qt::LeftButton
            && pressed.isValid() && released.isValid()
            && pressed == released.sibling(released.row(), 0);
        QTableView::mouseReleaseEvent(event);
        if (clickedSameRow)
            toggleRow(pressed);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if ((event->key() == Qt::Key_Space || event->key() == Qt::Key_Select)
            && event->modifiers() == Qt::NoModifier) {
            if (!event->isAutoRepeat())
                toggleRow(currentIndex());
            event->accept();
            return;
        }
        QTableView::keyPressEvent(event);
    }

private:
    void toggleRow(const QModelIndex &index)
    {
        if (!isEnabled() || !model() || !index.isValid())
            return;
        const QModelIndex checkbox = index.sibling(index.row(), 0);
        const Qt::ItemFlags flags = model()->flags(checkbox);
        if (!(flags & Qt::ItemIsEnabled) || !(flags & Qt::ItemIsUserCheckable))
            return;
        const Qt::CheckState next = checkbox.data(Qt::CheckStateRole).toInt() == Qt::Checked
            ? Qt::Unchecked : Qt::Checked;
        model()->setData(checkbox, next, Qt::CheckStateRole);
    }

    QPersistentModelIndex m_pressed;
};

class CacheDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    bool editorEvent(QEvent *, QAbstractItemModel *, const QStyleOptionViewItem &,
                     const QModelIndex &) override
    {
        // CacheTableView owns toggling for the entire row and keyboard. Native
        // editorEvent would hit a different QSS rectangle and toggle twice.
        return false;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.column() == 0) {
            QStyleOptionViewItem background(option);
            initStyleOption(&background, index);
            background.features &= ~QStyleOptionViewItem::HasCheckIndicator;
            background.text.clear();
            const QWidget *widget = option.widget;
            QStyle *style = widget ? widget->style() : QApplication::style();
            style->drawControl(QStyle::CE_ItemViewItem, &background, painter, widget);

            QStyleOptionButton checkbox;
            checkbox.palette = option.palette;
            const int side = qMax(16, style->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, widget));
            checkbox.rect = QRect(0, 0, side, side);
            checkbox.rect.moveCenter(option.rect.center());
            if ((option.state & QStyle::State_Enabled) && (index.flags() & Qt::ItemIsUserCheckable))
                checkbox.state |= QStyle::State_Enabled;
            checkbox.state |= index.data(Qt::CheckStateRole).toInt() == Qt::Checked
                ? QStyle::State_On : QStyle::State_Off;
            style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &checkbox, painter, widget);
            return;
        }
        if (index.column() != 1) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem background(option);
        initStyleOption(&background, index);
        background.text.clear();
        const QWidget *widget = background.widget;
        (widget ? widget->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &background, painter, widget);
        painter->save();
        const QRect area = option.rect.adjusted(14, 7, -12, -7);
        QFont nameFont = option.font;
        nameFont.setBold(true);
        painter->setFont(nameFont);
        painter->setPen(QColor("#34415c"));
        painter->drawText(QRect(area.left(), area.top(), area.width(), 24), Qt::AlignVCenter,
                          QFontMetrics(nameFont).elidedText(index.data().toString(), Qt::ElideRight, area.width()));
        QFont pathFont = option.font;
        pathFont.setPointSizeF(qMax(8.0, pathFont.pointSizeF() - 1));
        painter->setFont(pathFont);
        painter->setPen(QColor("#8c96aa"));
        painter->drawText(QRect(area.left(), area.top() + 25, area.width(), 20), Qt::AlignVCenter,
                          QFontMetrics(pathFont).elidedText(index.data(Qt::UserRole).toString(), Qt::ElideMiddle, area.width()));
        painter->restore();
    }
};

} // namespace

CachePage::CachePage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("cachePage"));
    m_cacheRoot = defaultRoot("XDG_CACHE_HOME", QDir::homePath() + QStringLiteral("/.cache"));
    m_stateRoot = defaultRoot("XDG_STATE_HOME", QDir::homePath() + QStringLiteral("/.local/state"));
    m_model = new CacheModel(this);
    m_filter = new CacheFilter(this);
    m_filter->setSourceModel(m_model);
    m_filter->setSortRole(cacheSortRole);
    m_filter->setSortCaseSensitivity(Qt::CaseInsensitive);
    createUi();
    connect(&m_scanWatcher, &QFutureWatcher<CacheScanResult>::finished, this, &CachePage::scanFinished);
    connect(&m_cleanWatcher, &QFutureWatcher<CacheCleanResult>::finished, this, &CachePage::cleanFinished);
    connect(m_model, &QAbstractItemModel::dataChanged, this, [this] { updateSelection(); });
    connect(m_filter, &QAbstractItemModel::rowsInserted, this, [this] { updateSelection(); });
    connect(m_filter, &QAbstractItemModel::rowsRemoved, this, [this] { updateSelection(); });
    updateScope();
    updateMetrics();
    updateSelection();
    updateControls();
}

CachePage::~CachePage()
{
    m_shuttingDown = true;
    m_cancel.store(true);
    if (m_confirmation)
        m_confirmation->reject();
    m_scanWatcher.waitForFinished();
    m_cleanWatcher.waitForFinished();
}

bool CachePage::isBusy() const { return m_scanning || m_cleaning || m_confirming; }
bool CachePage::isCleaning() const { return m_cleaning; }
bool CachePage::isSettled() const { return m_started && m_scanDone && !isBusy(); }

void CachePage::setRoots(QString cacheRoot, QString stateRoot)
{
    if (m_started || isBusy()) {
        qWarning("Cache roots cannot change after scanning starts");
        return;
    }
    m_cacheRoot = absoluteRoot(cacheRoot);
    m_stateRoot = absoluteRoot(stateRoot);
    updateScope();
}

void CachePage::ensureScanStarted()
{
    if (!m_started)
        startScan();
}

void CachePage::rescan()
{
    startScan();
}

void CachePage::createUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *metrics = new QHBoxLayout;
    metrics->setSpacing(14);
    auto addMetric = [metrics](const QString &title, const char *name, QLabel **value, QLabel **summary) {
        auto *frame = cacheCard();
        auto *box = new QVBoxLayout(frame);
        box->setContentsMargins(20, 16, 20, 15);
        box->setSpacing(5);
        box->addWidget(plainLabel(title, "muted"));
        *value = plainLabel(QStringLiteral("—"), name);
        (*value)->setStyleSheet(QStringLiteral("font-size:27px;font-weight:700;color:#263551;"));
        *summary = plainLabel(QStringLiteral("尚未扫描"), "muted");
        box->addWidget(*value);
        box->addWidget(*summary);
        metrics->addWidget(frame, 1);
    };
    addMetric(QStringLiteral("已扫描的实际占用"), "cacheTotalBytes", &m_totalSize, &m_totalSummary);
    addMetric(QStringLiteral("普通缓存"), "cacheOrdinaryBytes", &m_cacheSize, &m_cacheSummary);
    addMetric(QStringLiteral("用户日志"), "cacheLogBytes", &m_logSize, &m_logSummary);
    layout->addLayout(metrics);

    m_scope = plainLabel({}, "cacheScope");
    m_scope->setWordWrap(true);
    m_scope->setStyleSheet(QStringLiteral("color:#7b879d;font-size:11px;"));
    layout->addWidget(m_scope);

    auto *panel = cacheCard();
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(18, 16, 18, 12);
    toolbar->setSpacing(10);
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("cacheSearch"));
    m_search->setPlaceholderText(QStringLiteral("搜索应用名称或缓存路径"));
    m_search->addAction(lineIcon("search"), QLineEdit::LeadingPosition);
    toolbar->addWidget(m_search, 1);
    m_largeOnly = new QCheckBox(QStringLiteral("实际占用 > 100 MiB"));
    m_largeOnly->setObjectName(QStringLiteral("cacheLargeOnly"));
    m_logsOnly = new QCheckBox(QStringLiteral("只看日志"));
    m_logsOnly->setObjectName(QStringLiteral("cacheLogsOnly"));
    toolbar->addWidget(m_largeOnly);
    toolbar->addWidget(m_logsOnly);
    m_scan = cacheButton(QStringLiteral("扫描缓存"), "cacheScan", "refresh");
    toolbar->addWidget(m_scan);
    m_cancelButton = cacheButton(QStringLiteral("取消扫描"), "cacheCancel", "stop");
    toolbar->addWidget(m_cancelButton);
    panelLayout->addLayout(toolbar);

    auto *selectionRow = new QHBoxLayout;
    selectionRow->setContentsMargins(18, 0, 18, 12);
    selectionRow->setSpacing(10);
    m_selectVisible = cacheButton(QStringLiteral("全选当前筛选"), "cacheSelectVisible");
    m_clearSelection = cacheButton(QStringLiteral("清空选择"), "cacheClearSelection");
    selectionRow->addWidget(m_selectVisible);
    selectionRow->addWidget(m_clearSelection);
    m_count = plainLabel({}, "cacheCount");
    m_count->setStyleSheet(QStringLiteral("color:#8993a7;font-size:12px;"));
    selectionRow->addStretch();
    selectionRow->addWidget(m_count);
    panelLayout->addLayout(selectionRow);

    m_progress = new QProgressBar;
    m_progress->setObjectName(QStringLiteral("cacheProgress"));
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(3);
    panelLayout->addWidget(m_progress);

    m_table = new CacheTableView;
    m_table->setObjectName(QStringLiteral("cacheTable"));
    m_table->setStyleSheet(QStringLiteral(
        "QTableView#cacheTable { selection-color:#29364f; selection-background-color:#edf2ff; }"
        "QTableView#cacheTable::item { padding:0; }"
        "QTableView#cacheTable::item:selected { color:#29364f; background:#edf2ff; }"));
    m_table->setModel(m_filter);
    m_table->setItemDelegate(new CacheDelegate(m_table));
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(65);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int column = 2; column < 6; ++column)
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    m_table->setColumnWidth(0, 42);
    m_table->setColumnWidth(2, 72);
    m_table->setColumnWidth(3, 105);
    m_table->setColumnWidth(4, 108);
    m_table->setColumnWidth(5, 116);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(5, Qt::DescendingOrder);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    panelLayout->addWidget(m_table, 1);

    auto *details = new QHBoxLayout;
    details->setContentsMargins(18, 12, 18, 12);
    m_detail = plainLabel(QStringLiteral("点击行或复选框勾选，再次点击取消；空格键也可切换。"), "cacheDetail");
    m_detail->setStyleSheet(QStringLiteral("color:#8993a7;font-size:11px;"));
    m_detail->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    details->addWidget(m_detail, 1);
    m_open = cacheButton(QStringLiteral("打开目录"), "cacheOpenDirectory", "folder");
    m_copy = cacheButton(QStringLiteral("复制路径"), "cacheCopyPath", "copy");
    details->addWidget(m_open);
    details->addWidget(m_copy);
    panelLayout->addLayout(details);
    layout->addWidget(panel, 1);

    m_scanNotice = plainLabel({}, "cacheScanNotice");
    m_scanNotice->setWordWrap(true);
    m_scanNotice->setStyleSheet(QStringLiteral("background:#fff6e5;color:#9b7837;border-radius:7px;padding:8px 12px;font-size:11px;"));
    m_scanNotice->hide();
    layout->addWidget(m_scanNotice);
    m_resultNotice = plainLabel({}, "cacheResultNotice");
    m_resultNotice->setWordWrap(true);
    m_resultNotice->setStyleSheet(QStringLiteral("background:#edf4ff;color:#536aa0;border-radius:7px;padding:8px 12px;font-size:11px;"));
    m_resultNotice->hide();
    layout->addWidget(m_resultNotice);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(12);
    auto *statusBox = new QVBoxLayout;
    statusBox->setSpacing(4);
    m_selection = plainLabel({}, "cacheSelectionSummary");
    m_selection->setStyleSheet(QStringLiteral("font-weight:600;color:#42516e;"));
    m_status = plainLabel(QStringLiteral("尚未扫描 · 默认不选择任何目录"), "cacheStatus");
    m_status->setStyleSheet(QStringLiteral("color:#8993a7;font-size:11px;"));
    statusBox->addWidget(m_selection);
    statusBox->addWidget(m_status);
    footer->addLayout(statusBox, 1);
    m_notes = cacheButton(QStringLiteral("扫描提示"), "cacheNotes");
    m_notes->hide();
    footer->addWidget(m_notes);
    m_clean = cacheButton(QStringLiteral("一键清理所选"), "cacheClean", "trash");
    m_clean->setStyleSheet(QStringLiteral(
        "QPushButton{color:#cd5263;border-color:#edcbd0;background:#fff8f9;font-weight:600;padding:11px 18px;}"
        "QPushButton:hover{background:#ffecef;}"
        "QPushButton:disabled{color:#c6b4b7;background:#faf8f8;border-color:#eee6e7;}"));
    footer->addWidget(m_clean);
    layout->addLayout(footer);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) { m_filter->setSearch(text); updateSelection(); });
    connect(m_largeOnly, &QCheckBox::toggled, this, [this](bool on) { m_filter->setLargeOnly(on); updateSelection(); });
    connect(m_logsOnly, &QCheckBox::toggled, this, [this](bool on) { m_filter->setLogsOnly(on); updateSelection(); });
    connect(m_scan, &QPushButton::clicked, this, &CachePage::startScan);
    connect(m_cancelButton, &QPushButton::clicked, this, &CachePage::cancelWork);
    connect(m_selectVisible, &QPushButton::clicked, this, &CachePage::selectVisible);
    connect(m_clearSelection, &QPushButton::clicked, this, [this] { if (!isBusy()) { m_model->clearChecked(); updateSelection(); } });
    connect(m_clean, &QPushButton::clicked, this, &CachePage::beginClean);
    connect(m_notes, &QPushButton::clicked, this, &CachePage::showScanNotes);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] { updateCurrentEntry(); });
    connect(m_open, &QPushButton::clicked, this, [this] {
        if (isBusy()) return;
        if (const CacheEntry *entry = currentEntry()) {
            const QFileInfo selected(entry->path);
            const QString directory = selected.isDir() ? selected.absoluteFilePath() : selected.absolutePath();
            if (!QFileInfo(directory).isDir() || !QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
                QMessageBox message(QMessageBox::Warning, QStringLiteral("无法打开目录"),
                                    QStringLiteral("目录可能已被移动，或系统未配置文件管理器。\n") + directory,
                                    QMessageBox::Ok, this);
                message.setTextFormat(Qt::PlainText);
                message.exec();
            }
        }
    });
    connect(m_copy, &QPushButton::clicked, this, [this] {
        if (!isBusy()) {
            if (const CacheEntry *entry = currentEntry())
                QApplication::clipboard()->setText(entry->path);
        }
    });
}

void CachePage::updateScope()
{
    const QString state = m_stateRoot.isEmpty() ? QStringLiteral("用户状态日志已禁用")
        : m_stateRoot + QStringLiteral("（仅 logs 目录 / .log 日志）");
    const QString text = QStringLiteral("当前用户范围：%1（缓存与日志）；%2。直接删除文件，不进入回收站；不清理系统日志。")
                             .arg(m_cacheRoot.isEmpty() ? QStringLiteral("未指定缓存目录") : m_cacheRoot, state);
    m_scope->setText(text);
    m_scope->setToolTip(text + QStringLiteral("\n实际占用为普通文件的磁盘分配空间；符号链接、其他用户文件等不在清理范围。"));
}

CacheProgress CachePage::progressHandler(quint64 operation, bool cleaning)
{
    return [this, operation, cleaning](quint64 count, const QString &path) {
        QMetaObject::invokeMethod(this, [this, operation, cleaning, count, path] {
            if (m_shuttingDown || operation != m_operation || !isBusy() || m_cancel.load())
                return;
            const QString shownPath = QFontMetrics(font()).elidedText(path, Qt::ElideMiddle, qMax(150, m_status->width() - 200));
            m_status->setText(QStringLiteral("%1 %2 个文件 · %3")
                                  .arg(cleaning ? QStringLiteral("正在处理") : QStringLiteral("已扫描"))
                                  .arg(countText(count), shownPath));
            m_status->setToolTip(path);
        }, Qt::QueuedConnection);
    };
}

void CachePage::startScan()
{
    if (isBusy() || m_shuttingDown)
        return;
    m_started = true;
    m_scanning = true;
    m_scanDone = false;
    m_cancel.store(false);
    m_model->clearChecked();
    m_scanNotice->hide();
    m_notes->hide();
    m_status->setText(QStringLiteral("正在只读扫描当前用户缓存与日志…"));
    const QString cacheRoot = m_cacheRoot;
    const QString stateRoot = m_stateRoot;
    const CacheProgress progress = progressHandler(++m_operation, false);
    updateControls();
    m_scanWatcher.setFuture(QtConcurrent::run([this, cacheRoot, stateRoot, progress] {
        return scanCaches(cacheRoot, stateRoot, &m_cancel, progress);
    }));
}

void CachePage::scanFinished()
{
    if (m_shuttingDown)
        return;
    m_scanResult = m_scanWatcher.result();
    m_scanning = false;
    m_scanDone = true;
    m_model->setEntries(m_scanResult.entries);
    m_table->sortByColumn(5, Qt::DescendingOrder);
    m_table->clearSelection();
    m_table->setCurrentIndex({});
    updateMetrics();
    QString partial = QStringLiteral("扫描完成");
    if (m_scanResult.canceled)
        partial = QStringLiteral("扫描已取消 · 以下为部分结果，可重新扫描补全");
    else if (!m_scanResult.error.isEmpty())
        partial = m_scanResult.entries.isEmpty() ? QStringLiteral("扫描失败")
                                                : QStringLiteral("扫描未完成 · 以下为部分结果");
    m_status->setText(QStringLiteral("%1 · 已扫描 %2 个文件 · 跳过 %3 项")
                          .arg(partial, countText(m_scanResult.scannedFiles), countText(m_scanResult.skippedEntries)));
    m_status->setToolTip({});
    QStringList notes = m_scanResult.warnings;
    if (!m_scanResult.error.isEmpty())
        notes.prepend(m_scanResult.error);
    if (m_scanResult.canceled)
        notes.prepend(QStringLiteral("扫描已取消，统计和清理范围仅覆盖本次已扫描到的文件。"));
    if (!notes.isEmpty()) {
        m_scanNotice->setText(notes.first() + (notes.size() > 1 ? QStringLiteral(" · 另有 %1 条提示").arg(notes.size() - 1) : QString()));
        m_scanNotice->setToolTip(notes.join(QLatin1Char('\n')));
        m_scanNotice->show();
        m_notes->show();
    }
    updateSelection();
    updateCurrentEntry();
    updateControls();
}

void CachePage::updateMetrics()
{
    quint64 total = 0, caches = 0, logs = 0, files = 0, cacheFiles = 0, logFiles = 0;
    for (const CacheEntry &entry : m_model->entries()) {
        total += entry.allocatedBytes;
        files += entry.fileCount;
        if (entry.logOnly) {
            logs += entry.allocatedBytes;
            logFiles += entry.fileCount;
        } else {
            caches += entry.allocatedBytes;
            cacheFiles += entry.fileCount;
        }
    }
    const bool known = m_scanDone && (m_scanResult.error.isEmpty() || !m_scanResult.entries.isEmpty());
    const QString partial = m_scanResult.canceled || !m_scanResult.error.isEmpty() ? QStringLiteral(" · 部分") : QString();
    m_totalSize->setText(known ? formatBytes(total) : QStringLiteral("—"));
    m_cacheSize->setText(known ? formatBytes(caches) : QStringLiteral("—"));
    m_logSize->setText(known ? formatBytes(logs) : QStringLiteral("—"));
    m_totalSummary->setText(known ? QStringLiteral("%1 组 · %2 个文件%3").arg(m_model->rowCount()).arg(countText(files), partial)
                                : (m_scanDone ? QStringLiteral("扫描失败，未取得可用结果") : QStringLiteral("只读扫描后显示")));
    m_cacheSummary->setText(known ? QStringLiteral("%1 个普通缓存文件%2").arg(countText(cacheFiles), partial)
                                : (m_scanDone ? QStringLiteral("未取得可用结果") : QStringLiteral("不含独立列出的日志")));
    m_logSummary->setText(known ? QStringLiteral("%1 个日志文件%2").arg(countText(logFiles), partial)
                              : (m_scanDone ? QStringLiteral("未取得可用结果") : QStringLiteral("含缓存目录与用户状态日志")));
    m_totalSize->setToolTip(QStringLiteral("已扫描文件的实际分配空间之和；不等于保证立即释放的磁盘空间。正在使用的文件可能仍被进程占用。"));
}

void CachePage::updateControls()
{
    const bool busy = isBusy();
    m_scan->setEnabled(!busy);
    m_scan->setText(m_started ? QStringLiteral("重新扫描") : QStringLiteral("扫描缓存"));
    m_cancelButton->setVisible(m_scanning || m_cleaning);
    m_cancelButton->setEnabled((m_scanning || m_cleaning) && !m_cancel.load());
    m_cancelButton->setText(m_cleaning ? QStringLiteral("停止后续删除") : QStringLiteral("取消扫描"));
    m_cancelButton->setToolTip(m_cleaning ? QStringLiteral("只停止后续删除；已经删除的文件不能恢复。") : QStringLiteral("保留已扫描的部分结果。"));
    m_progress->setVisible(m_scanning || m_cleaning);
    m_search->setEnabled(!busy);
    m_largeOnly->setEnabled(!busy);
    m_logsOnly->setEnabled(!busy);
    m_table->setEnabled(!busy);
    m_selectVisible->setEnabled(!busy && m_filter->rowCount());
    const bool selected = !m_model->selectedEntries().isEmpty();
    m_clearSelection->setEnabled(!busy && selected);
    m_clean->setEnabled(!busy && m_scanDone && selected);
    m_notes->setEnabled(!busy);
    const bool current = currentEntry() != nullptr;
    m_open->setEnabled(!busy && current);
    m_copy->setEnabled(!busy && current);
}

void CachePage::updateSelection()
{
    const QVector<CacheEntry> selected = m_model->selectedEntries();
    quint64 bytes = 0, files = 0;
    int hidden = 0;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        if (m_model->isChecked(row) && !m_filter->mapFromSource(m_model->index(row, 0)).isValid())
            ++hidden;
    }
    for (const CacheEntry &entry : selected) {
        bytes += entry.allocatedBytes;
        files += entry.fileCount;
    }
    m_selection->setText(QStringLiteral("已选 %1 组 · %2 个文件 · 实际占用 %3%4")
                             .arg(selected.size()).arg(countText(files), formatBytes(bytes),
                                 hidden ? QStringLiteral("（含 %1 个筛选外选择）").arg(hidden) : QString()));
    m_count->setText(QStringLiteral("显示 %1 / %2 组").arg(m_filter->rowCount()).arg(m_model->rowCount()));
    updateControls();
}

const CacheEntry *CachePage::currentEntry() const
{
    const QModelIndex source = m_filter->mapToSource(m_table->currentIndex());
    return source.isValid() ? m_model->entry(source.row()) : nullptr;
}

void CachePage::updateCurrentEntry()
{
    if (const CacheEntry *entry = currentEntry()) {
        const QString text = (entry->logOnly ? QStringLiteral("仅日志 · ") : QStringLiteral("普通缓存 · ")) + entry->path;
        m_detail->setText(QFontMetrics(m_detail->font()).elidedText(text, Qt::ElideMiddle, qMax(180, m_detail->width())));
        m_detail->setToolTip(text);
    } else {
        m_detail->setText(QStringLiteral("点击行或复选框勾选，再次点击取消；空格键也可切换。"));
        m_detail->setToolTip({});
    }
    updateControls();
}

void CachePage::selectVisible()
{
    if (isBusy())
        return;
    // Sorting may react to checkbox changes when the checkbox column is the
    // active sort key. Map all visible rows before changing any checkbox.
    QVector<QModelIndex> visible;
    for (int row = 0; row < m_filter->rowCount(); ++row)
        visible.append(m_filter->mapToSource(m_filter->index(row, 0)));
    for (const QModelIndex &index : visible)
        m_model->setData(index, Qt::Checked, Qt::CheckStateRole);
    updateSelection();
}

void CachePage::cancelWork()
{
    if (m_confirmation)
        m_confirmation->reject();
    if (!m_scanning && !m_cleaning)
        return;
    m_cancel.store(true);
    m_status->setText(m_cleaning
        ? QStringLiteral("正在停止后续删除… 已删除的文件不可恢复，完成后将重新扫描。")
        : QStringLiteral("正在取消扫描… 将保留已扫描的部分结果。"));
    updateControls();
}

void CachePage::beginClean()
{
    if (isBusy() || !m_scanDone)
        return;
    const QVector<CacheEntry> selected = m_model->selectedEntries();
    if (selected.isEmpty())
        return;
    quint64 bytes = 0, files = 0;
    QStringList paths;
    for (const CacheEntry &entry : selected) {
        bytes += entry.allocatedBytes;
        files += entry.fileCount;
        paths << QStringLiteral("%1\n%2\n%3 · %4 个文件 · 实际占用 %5\n")
                     .arg(entry.name, entry.path, entry.logOnly ? QStringLiteral("仅日志") : QStringLiteral("普通缓存"))
                     .arg(entry.fileCount).arg(formatBytes(entry.allocatedBytes));
    }
    m_confirming = true;
    updateControls();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("cacheConfirm"));
    dialog.setWindowTitle(QStringLiteral("确认清理所选缓存与日志"));
    dialog.resize(670, 520);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(15);
    auto *title = plainLabel(QStringLiteral("直接删除 %1 组中的 %2 个文件，预计实际占用 %3")
                                 .arg(selected.size()).arg(countText(files), formatBytes(bytes)), "cardTitle");
    title->setWordWrap(true);
    layout->addWidget(title);
    auto *explanation = plainLabel(QStringLiteral(
        "文件将直接删除，不进入回收站。请先关闭相关软件；正在使用的缓存可能重建。\n"
        "按本次扫描快照在删除前复核，检测到已修改或替换的文件会跳过；新文件与目录会保留。\n"
        "取消清理只会停止后续删除，已删除的文件不可恢复。"));
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    if (m_scanResult.canceled || !m_scanResult.error.isEmpty()) {
        auto *partial = plainLabel(QStringLiteral("本次扫描未完成：仅清理已扫描到的部分文件。"), "notice");
        partial->setWordWrap(true);
        layout->addWidget(partial);
    }
    auto *pathList = new QPlainTextEdit;
    pathList->setObjectName(QStringLiteral("cacheConfirmPaths"));
    pathList->setReadOnly(true);
    pathList->setPlainText(paths.join(QLatin1Char('\n')));
    layout->addWidget(pathList, 1);
    auto *buttons = new QDialogButtonBox;
    auto *confirm = buttons->addButton(QStringLiteral("直接删除所选文件"), QDialogButtonBox::AcceptRole);
    confirm->setObjectName(QStringLiteral("cacheConfirmDelete"));
    confirm->setStyleSheet(QStringLiteral("color:#cd5263;background:#fff8f9;border-color:#edcbd0;"));
    confirm->setAutoDefault(false);
    auto *cancel = buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("cacheConfirmCancel"));
    cancel->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    m_confirmation = &dialog;
    const bool accepted = dialog.exec() == QDialog::Accepted;
    m_confirmation.clear();
    m_confirming = false;
    if (!accepted || m_shuttingDown) {
        updateControls();
        return;
    }
    m_cleaning = true;
    m_cancel.store(false);
    m_scanNotice->hide();
    m_resultNotice->hide();
    m_status->setText(QStringLiteral("正在清理所选文件… 可停止后续删除。"));
    updateControls();
    const QString cacheRoot = m_cacheRoot;
    const QString stateRoot = m_stateRoot;
    const CacheProgress progress = progressHandler(++m_operation, true);
    m_cleanWatcher.setFuture(QtConcurrent::run([this, selected, cacheRoot, stateRoot, progress] {
        return cleanCaches(selected, cacheRoot, stateRoot, &m_cancel, progress);
    }));
}

void CachePage::cleanFinished()
{
    if (m_shuttingDown)
        return;
    const CacheCleanResult result = m_cleanWatcher.result();
    m_cleaning = false;
    const QString summary = QStringLiteral("%1 · 已删除 %2 个文件（实际占用 %3）· 跳过 %4 · 失败 %5")
                                .arg(result.canceled ? QStringLiteral("清理已停止") : QStringLiteral("清理完成"))
                                .arg(countText(result.removedFiles), formatBytes(result.removedBytes),
                                    countText(result.skippedFiles), countText(result.failedFiles));
    m_resultNotice->setText(summary + QStringLiteral("。正在重新扫描剩余文件。"));
    m_resultNotice->setToolTip(result.errors.join(QLatin1Char('\n')));
    m_resultNotice->show();
    m_model->clearChecked();
    emit spaceChanged();
    // Rebuild the list from the filesystem. Never assume a selected directory
    // is empty: new, changed, skipped, and still-open files can remain there.
    startScan();
    auto *message = new QMessageBox(result.failedFiles ? QMessageBox::Warning : QMessageBox::Information,
                                   result.canceled ? QStringLiteral("清理已停止") : QStringLiteral("清理结果"),
                                   summary + QStringLiteral("\n已删除的文件不可恢复；检测到变更或无法访问的文件会跳过。\n列表正在重新扫描，实际可用空间以磁盘刷新为准。"),
                                   QMessageBox::Ok, this);
    message->setObjectName(QStringLiteral("cacheCleanResult"));
    message->setTextFormat(Qt::PlainText);
    message->setAttribute(Qt::WA_DeleteOnClose);
    if (!result.errors.isEmpty())
        message->setDetailedText(result.errors.join(QLatin1Char('\n')));
    message->open();
}

void CachePage::showScanNotes()
{
    QStringList notes = m_scanResult.warnings;
    if (!m_scanResult.error.isEmpty())
        notes.prepend(m_scanResult.error);
    if (m_scanResult.canceled)
        notes.prepend(QStringLiteral("扫描已取消：当前统计为部分结果，仅能清理已扫描快照中的文件。"));
    notes << QStringLiteral("扫描与清理仅针对当前用户缓存根和用户状态目录内的日志；不请求管理员权限，也不扫描系统 /var/log。");
    QMessageBox message(QMessageBox::Information, QStringLiteral("缓存扫描提示"),
                        QStringLiteral("成功扫描到的文件仍可查看和选择。"), QMessageBox::Ok, this);
    message.setTextFormat(Qt::PlainText);
    message.setDetailedText(notes.join(QStringLiteral("\n\n")));
    message.exec();
}

bool CachePage::verifyUi(QString *error)
{
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!isSettled())
        return fail(QStringLiteral("cache background work has not settled"));
    if (!m_scanResult.error.isEmpty())
        return fail(m_scanResult.error);
    QSet<QString> identities;
    for (const CacheEntry &entry : m_model->entries()) {
        if (entry.id.isEmpty() || identities.contains(entry.id))
            return fail(QStringLiteral("cache inventory contains duplicate/empty identities"));
        identities.insert(entry.id);
        if (entry.fileCount != static_cast<quint64>(entry.files.size()))
            return fail(QStringLiteral("cache snapshot file count mismatch"));
        if (entry.logOnly && entry.logCount != entry.fileCount)
            return fail(QStringLiteral("log-only entry includes non-log files"));
    }
    const QString savedSearch = m_search->text();
    const bool savedLarge = m_largeOnly->isChecked();
    const bool savedLogs = m_logsOnly->isChecked();
    // Only filter state changes below; no checkbox selection or clean action.
    m_filter->setSearch(QStringLiteral("DISKLENS_NO_SUCH_CACHE_09324"));
    const bool searchWorks = m_filter->rowCount() == 0;
    m_filter->setSearch({});
    m_filter->setLargeOnly(true);
    m_filter->setLogsOnly(false);
    bool largeWorks = true;
    for (int row = 0; row < m_filter->rowCount(); ++row) {
        const CacheEntry *entry = m_model->entry(m_filter->mapToSource(m_filter->index(row, 0)).row());
        if (!entry || entry->allocatedBytes <= largeThreshold)
            largeWorks = false;
    }
    m_filter->setLargeOnly(false);
    m_filter->setLogsOnly(true);
    bool logsWork = true;
    for (int row = 0; row < m_filter->rowCount(); ++row) {
        const CacheEntry *entry = m_model->entry(m_filter->mapToSource(m_filter->index(row, 0)).row());
        if (!entry || !entry->logOnly)
            logsWork = false;
    }
    m_filter->setSearch(savedSearch);
    m_filter->setLargeOnly(savedLarge);
    m_filter->setLogsOnly(savedLogs);
    updateSelection();
    if (!searchWorks || !largeWorks || !logsWork)
        return fail(QStringLiteral("cache search/size/log filter failed"));
    if (m_clean->isEnabled() != !m_model->selectedEntries().isEmpty())
        return fail(QStringLiteral("cache clean button does not match checkbox selection"));
    if (error)
        error->clear();
    qInfo("Cache UI read-only smoke check: %d groups, %llu files", m_model->rowCount(),
          static_cast<unsigned long long>(m_scanResult.scannedFiles));
    return true;
}
