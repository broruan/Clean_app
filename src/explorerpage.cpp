#include "localization.h"
#include "explorerpage.h"

#include "models.h"
#include "widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTabBar>
#include <QResizeEvent>
#include <QTemporaryFile>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <unistd.h>

namespace {
constexpr int pathRole = Qt::UserRole;
constexpr int directoryRole = Qt::UserRole + 1;
constexpr int populatedRole = Qt::UserRole + 2;

QLabel *label(const QString &text, const char *name = nullptr)
{
    auto *result = new QLabel(text);
    result->setTextFormat(Qt::PlainText);
    if (name)
        result->setObjectName(QString::fromLatin1(name));
    return result;
}

QPushButton *button(const QString &text, const char *name, const QString &icon = {})
{
    auto *result = new QPushButton(text);
    result->setObjectName(QString::fromLatin1(name));
    result->setCursor(Qt::PointingHandCursor);
    if (!icon.isEmpty())
        result->setIcon(lineIcon(icon));
    return result;
}

QString normalizedRoot(const QString &path)
{
    return path.trimmed().isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool isWithin(const QString &path, const QString &root)
{
    return !root.isEmpty() && (path == root || path.startsWith(root == QStringLiteral("/") ? root : root + QLatin1Char('/')));
}

QString count(quint64 value) { return QLocale().toString(value); }

QString singleLine(QString value)
{
    value.replace(QLatin1Char('\n'), QStringLiteral("↵"));
    value.replace(QLatin1Char('\r'), QStringLiteral("↵"));
    value.replace(QLatin1Char('\t'), QStringLiteral("⇥"));
    return value;
}

bool partial(const DirectoryScanResult &result)
{
    return result.canceled || result.truncated || result.skippedEntries || !result.error.isEmpty();
}

bool partial(const DuplicateScanResult &result)
{
    return result.canceled || result.truncated || result.skippedEntries || !result.error.isEmpty();
}

QString completion(bool canceled, bool truncated, quint64 skipped, const QString &error)
{
    QStringList notes;
    if (canceled) notes << dlTr("已取消，以下为部分结果");
    if (truncated) notes << dlTr("已达到扫描数量或深度上限，以下为部分结果");
    if (skipped) notes << dlTr("跳过 %1 项（其他挂载点、权限或扫描期间变动等）").arg(count(skipped));
    if (!error.isEmpty()) notes << error;
    return notes.isEmpty() ? dlTr("扫描完成；仅统计所选文件系统，排除符号链接与特殊文件，硬链接只计一次。")
                           : notes.join(QStringLiteral("；"));
}

QString csvField(QString value)
{
    // Quoting alone does not stop spreadsheet formula evaluation. Escape a
    // formula introducer even when preceded by whitespace, and retain all text.
    const QString trimmed = value.trimmed();
    if ((!trimmed.isEmpty() && QStringLiteral("=+-@").contains(trimmed.front()))
        || value.startsWith(QLatin1Char('\t')) || value.startsWith(QLatin1Char('\r')))
        value.prepend(QLatin1Char('\''));
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

void appendCsv(QByteArray &output, const QStringList &fields)
{
    QStringList escaped;
    for (const QString &field : fields) escaped << csvField(field);
    output += escaped.join(QLatin1Char(',')).toUtf8();
    output += "\r\n";
}

QVector<const SpaceNode *> orderedChildren(const SpaceNode &node)
{
    QVector<const SpaceNode *> children;
    children.reserve(node.children.size());
    for (const auto &child : node.children) children.append(&child);
    std::sort(children.begin(), children.end(), [](const SpaceNode *a, const SpaceNode *b) {
        return a->allocatedBytes == b->allocatedBytes ? a->name < b->name : a->allocatedBytes > b->allocatedBytes;
    });
    return children;
}

const SpaceNode *lookup(const SpaceNode &node, const QString &path)
{
    if (node.path == path) return &node;
    for (const auto &child : node.children) {
        if (child.path == path) return &child;
        if (child.directory && path.startsWith(child.path + QLatin1Char('/'))) {
            if (const auto *result = lookup(child, path)) return result;
        }
    }
    return nullptr;
}

bool matches(const SpaceNode &node, const QString &search)
{
    if (search.isEmpty() || node.path.contains(search, Qt::CaseInsensitive)
        || node.name.contains(search, Qt::CaseInsensitive)) return true;
    for (const auto &child : node.children)
        if (matches(child, search)) return true;
    return false;
}

void styleTree(QTreeWidget *tree)
{
    tree->setRootIsDecorated(true);
    tree->setAlternatingRowColors(false);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setExpandsOnDoubleClick(false);
    tree->setMinimumHeight(140);
    tree->header()->setStretchLastSection(false);
    tree->setStyleSheet(QStringLiteral(
        "QTreeWidget { background: white; border: 1px solid #e6eaf2; border-radius: 8px; outline: 0; }"
        "QTreeWidget::item { min-height: 30px; padding: 4px 7px; }"
        "QTreeWidget::item:selected { background: #e8eeff; color: #263551; }"
        "QTreeWidget::item:selected:!active { background: #e8eeff; color: #263551; }"));
    QPalette palette = tree->palette();
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
        palette.setColor(group, QPalette::HighlightedText, QColor("#263551"));
        palette.setColor(group, QPalette::Highlight, QColor("#e8eeff"));
    }
    tree->setPalette(palette);
}
class ReadableTabBar final : public QTabBar {
public:
    explicit ReadableTabBar(QWidget *parent) : QTabBar(parent)
    { setExpanding(false); setElideMode(Qt::ElideNone); setUsesScrollButtons(true); }
protected:
    QSize tabSizeHint(int index) const override
    {
        QFont selectedFont=font(); selectedFont.setWeight(QFont::DemiBold);
        const QFontMetrics selectedMetrics(selectedFont);
        return QTabBar::tabSizeHint(index).expandedTo(QSize(
            qMax(fontMetrics().horizontalAdvance(tabText(index)),selectedMetrics.horizontalAdvance(tabText(index)))+40,
            qMax(fontMetrics().height(),selectedMetrics.height())+20));
    }
    void paintEvent(QPaintEvent *) override
    {
        // Draw with the same measured font. QSS can otherwise switch the
        // selected font weight after computing a smaller text clip rectangle.
        QPainter painter(this);
        for(int index=0;index<count();++index) {
            const bool selected=index==currentIndex();
            const QRect bounds=tabRect(index);
            painter.fillRect(bounds,selected?QColor("#e8eeff"):QColor("#f1f4fa"));
            QFont textFont=font(); if(selected) textFont.setWeight(QFont::DemiBold);
            painter.setFont(textFont); painter.setPen(selected?QColor("#435bd9"):QColor("#66748e"));
            painter.drawText(bounds.adjusted(16,0,-16,0),Qt::AlignCenter,tabText(index));
            if(selected && hasFocus()) {
                painter.setPen(QPen(QColor("#7286e6"),1,Qt::DotLine));
                painter.drawRect(bounds.adjusted(3,3,-4,-4));
            }
        }
    }
};
class ExplorerTabs final : public QTabWidget {
public:
    ExplorerTabs() { setTabBar(new ReadableTabBar(this)); }
};
} // namespace

// Every row uses the current directory's actual allocated bytes as its common
// denominator. Clicking the whole row is deliberate: tiny and empty directories
// remain reachable. The table retains every child omitted from this top 8 chart.
class ExplorerMapWidget final : public QWidget {
public:
    explicit ExplorerMapWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("explorerMap"));
        setMinimumSize(250, 250);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(dlTr("当前目录实际占用比例图"));
        setToolTip(dlTr("按实际占用排序，显示当前层前 8 项；完整结果见左侧。当前目录自身的元数据计入总占用但不单独绘图。点击一行进入目录；文件会在列表中选中。"));
        setMouseTracking(true);
    }

    void setNode(const SpaceNode *node, const QString &search)
    {
        m_items.clear(); m_total = node ? node->allocatedBytes : 0;
        if (node) {
            for (const auto *child : orderedChildren(*node)) {
                if (matches(*child, search)) m_items.append(child);
                if (m_items.size() == 8) break;
            }
        }
        m_focus = m_items.isEmpty() ? -1 : 0;
        updateMinimumHeight();
        update();
    }

    std::function<void(const QString &, bool)> activate;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), Qt::white);
        painter.setPen(QColor("#34415c")); QFont title = font(); title.setBold(true); painter.setFont(title);
        painter.drawText(QRect(12, 6, width() - 24, fontMetrics().height()), Qt::AlignVCenter,
            QFontMetrics(title).elidedText(dlTr("当前层 · 实际占用"), Qt::ElideRight, width()-24));
        painter.setFont(font()); painter.setPen(QColor("#8993a7"));
        painter.drawText(subtitleRect(), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
            dlTr("条长 = 项目占用 / 当前目录总占用"));
        if (m_items.isEmpty()) {
            painter.drawText(rect().adjusted(15, rowsTop(), -15, -15), Qt::AlignCenter | Qt::TextWordWrap,
                             dlTr("暂无可显示的子项\n扫描完成后，可点击占用条逐层查看"));
            return;
        }
        QVariantList textBounds,barBounds;
        const QColor colors[] = {QColor("#5268e9"), QColor("#36a98f"), QColor("#7d83d8"), QColor("#d59a36"),
                                  QColor("#4399bf"), QColor("#ac6daf"), QColor("#7286a4"), QColor("#8ba95a")};
        for (int i = 0; i < m_items.size(); ++i) {
            const QRect row = rowRect(i); const auto *item = m_items[i];
            if (i == m_focus && hasFocus()) painter.fillRect(row, QColor("#edf2ff"));
            const double fraction = m_total ? qBound(0.0, double(item->allocatedBytes) / double(m_total), 1.0) : 0;
            const QString suffix = QStringLiteral("%1 · %2%").arg(formatBytes(item->allocatedBytes)).arg(fraction * 100, 0, 'f', 1);
            const int valueWidth = painter.fontMetrics().horizontalAdvance(suffix);
            const QRect text(row.left()+6, row.top()+2, row.width()-12, fontMetrics().height());
            const int labelWidth = qMax(24, text.width()-valueWidth-12);
            textBounds.append(text);
            painter.setPen(QColor("#34415c"));
            painter.drawText(QRect(text.topLeft(), QSize(labelWidth,text.height())), Qt::AlignLeft | Qt::AlignVCenter,
                painter.fontMetrics().elidedText((item->directory ? QStringLiteral("▸ ") : QString()) + singleLine(item->name),
                    Qt::ElideMiddle, labelWidth));
            painter.setPen(QColor("#68758e"));
            painter.drawText(text, Qt::AlignRight | Qt::AlignVCenter, suffix);
            const QRectF track(row.left() + 6, row.bottom() - 10, row.width() - 12, 6);
            barBounds.append(track.toRect());
            painter.setPen(Qt::NoPen); painter.setBrush(QColor("#eef1f7")); painter.drawRoundedRect(track, 3, 3);
            QRectF bar = track; bar.setWidth(track.width() * fraction);
            if (bar.width() > 0) { painter.setBrush(colors[i]); painter.drawRoundedRect(bar, 3, 3); }
        }
        setProperty("chartTextRects",textBounds);
        setProperty("chartBarRects",barBounds);
        painter.setPen(QColor("#8993a7"));
        painter.drawText(QRect(12, rowsTop()+m_items.size()*rowHeight()+4, width()-24, fontMetrics().height()), Qt::AlignVCenter,
                         dlTr("前 %1 项 · 点击进入目录").arg(m_items.size()));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            for (int i = 0; i < m_items.size(); ++i) {
                if (rowRect(i).contains(event->pos())) {
                    m_focus = i; setFocus(); const auto *node = m_items[i];
                    if (activate) activate(node->path, node->directory);
                    return;
                }
            }
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        bool over = false;
        for (int i = 0; i < m_items.size(); ++i) if (rowRect(i).contains(event->pos())) {
            const auto *node = m_items[i];
            setToolTip(node->path + dlTr("\n逻辑大小：%1\n实际占用：%2\n%3")
                .arg(formatBytes(node->logicalBytes), formatBytes(node->allocatedBytes),
                     node->directory ? dlTr("点击进入目录") : dlTr("点击在列表中选择文件")));
            over = true; break;
        }
        setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Down && !m_items.isEmpty()) m_focus = (m_focus + 1) % m_items.size();
        else if (event->key() == Qt::Key_Up && !m_items.isEmpty()) m_focus = (m_focus + m_items.size() - 1) % m_items.size();
        else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Space) && m_focus >= 0) {
            const auto *node = m_items[m_focus]; if (activate) activate(node->path, node->directory);
        } else { QWidget::keyPressEvent(event); return; }
        update(); event->accept();
    }

    bool event(QEvent *event) override
    {
        const bool result=QWidget::event(event);
        if (event->type()==QEvent::FontChange || event->type()==QEvent::Polish) updateMinimumHeight();
        return result;
    }
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        if (event->size().width()!=event->oldSize().width()) updateMinimumHeight();
    }
private:
    QRect subtitleRect() const
    {
        const int textWidth=qMax(1,width()-24);
        const int height=fontMetrics().boundingRect(QRect(0,0,textWidth,1000), Qt::TextWordWrap,
            dlTr("条长 = 项目占用 / 当前目录总占用")).height();
        return QRect(12, 12+fontMetrics().height(),textWidth,height);
    }
    int rowsTop() const { return subtitleRect().bottom()+9; }
    int rowHeight() const { return fontMetrics().height()+16; }
    void updateMinimumHeight()
    {
        const int needed=qMax(250, rowsTop()+m_items.size()*rowHeight()+fontMetrics().height()+14);
        if (minimumHeight()!=needed) { setMinimumHeight(needed); updateGeometry(); }
    }
    QRect rowRect(int index) const
    {
        return QRect(6, rowsTop()+index*rowHeight(), width()-12, rowHeight()-2);
    }
    QVector<const SpaceNode *> m_items;
    quint64 m_total = 0;
    int m_focus = -1;
};

ExplorerPage::ExplorerPage(QWidget *parent) : QWidget(parent)
{
    createUi();
    connect(&m_directoryWatcher, &QFutureWatcher<DirectoryScanResult>::finished, this, &ExplorerPage::directoryFinished);
    connect(&m_duplicateWatcher, &QFutureWatcher<DuplicateScanResult>::finished, this, &ExplorerPage::duplicatesFinished);
    setRoot(QDir::homePath());
}

ExplorerPage::~ExplorerPage()
{
    m_shuttingDown = true; m_cancel.store(true);
    m_directoryWatcher.waitForFinished(); m_duplicateWatcher.waitForFinished();
}

void ExplorerPage::createUi()
{
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(12);
    auto *scopeRow = new QHBoxLayout;
    m_rootEdit = new QLineEdit; m_rootEdit->setObjectName(QStringLiteral("explorerRoot"));
    m_rootEdit->setPlaceholderText(dlTr("选择要分析的目录，如主目录或外置磁盘中的目录"));
    m_choose = button(dlTr("选择目录"), "explorerChoose", "folder");
    m_scan = button(dlTr("扫描目录"), "explorerScan", "refresh");
    m_cancelButton = button(dlTr("取消"), "explorerCancel", "stop");
    scopeRow->addWidget(m_rootEdit, 1); scopeRow->addWidget(m_choose); scopeRow->addWidget(m_scan); scopeRow->addWidget(m_cancelButton);
    layout->addLayout(scopeRow);

    auto *filterRow = new QHBoxLayout;
    m_search = new QLineEdit; m_search->setObjectName(QStringLiteral("explorerSearch"));
    m_search->setPlaceholderText(dlTr("筛选名称或完整路径（不影响导出范围）"));
    m_export = button(dlTr("导出 CSV"), "explorerExport", "files");
    m_export->setToolTip(dlTr("导出当前目录及子目录，或全部重复分组；包含搜索筛选外的结果。"));
    filterRow->addWidget(m_search, 1); filterRow->addWidget(m_export); layout->addLayout(filterRow);

    m_tabs = new ExplorerTabs; m_tabs->setObjectName(QStringLiteral("explorerTabs"));
    m_tabs->setStyleSheet(QStringLiteral("QTabWidget::pane { border: 0; } QTabBar::tab { padding: 9px 18px; color: #66748e; } QTabBar::tab:selected { background: #e8eeff; color: #435bd9; }"));
    auto *directoryTab = new QWidget; auto *directoryLayout = new QVBoxLayout(directoryTab);
    directoryLayout->setContentsMargins(0, 12, 0, 0); directoryLayout->setSpacing(10);
    auto *navigation = new QHBoxLayout;
    m_up = button(dlTr("上一级"), "explorerUp"); m_home = button(dlTr("扫描根目录"), "explorerHome");
    m_breadcrumb = label(QString(), "explorerBreadcrumb"); m_breadcrumb->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_breadcrumb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    navigation->addWidget(m_up); navigation->addWidget(m_home); navigation->addWidget(m_breadcrumb, 1);
    directoryLayout->addLayout(navigation);
    m_metrics = label(dlTr("尚未扫描"), "explorerMetrics"); m_metrics->setWordWrap(true); directoryLayout->addWidget(m_metrics);
    auto *splitter = new QSplitter(Qt::Horizontal); splitter->setChildrenCollapsible(false);
    m_tree = new QTreeWidget; m_tree->setObjectName(QStringLiteral("explorerTree"));
    m_tree->setHeaderLabels({dlTr("名称 / 目录层级"), dlTr("实际占用"), dlTr("逻辑大小"), dlTr("文件数")});
    styleTree(m_tree);
    // Keep a useful name column even when translated headers or totals grow.
    // The user can resize every column and scroll horizontally for the rest.
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_tree->setColumnWidth(0, 220); m_tree->setColumnWidth(1, 140);
    m_tree->setColumnWidth(2, 140); m_tree->setColumnWidth(3, 90);
    m_map = new ExplorerMapWidget;
    splitter->addWidget(m_tree); splitter->addWidget(m_map); splitter->setStretchFactor(0, 3); splitter->setStretchFactor(1, 2); splitter->setSizes({540,390});
    directoryLayout->addWidget(splitter, 1);
    m_directoryNotice = label(dlTr("只读分析；逻辑大小是文件长度，实际占用是文件系统已分配空间，两者可能不同。"), "explorerDirectoryNotice");
    m_directoryNotice->setWordWrap(true); directoryLayout->addWidget(m_directoryNotice);
    m_tabs->addTab(directoryTab, dlTr("目录空间分析"));

    auto *duplicateTab = new QWidget; auto *duplicateLayout = new QVBoxLayout(duplicateTab);
    duplicateLayout->setContentsMargins(0, 12, 0, 0); duplicateLayout->setSpacing(10);
    auto *duplicateRow = new QHBoxLayout; duplicateRow->addWidget(label(dlTr("最小文件大小")));
    m_minimum = new QComboBox; m_minimum->setObjectName(QStringLiteral("explorerMinimum"));
    for (const auto &entry : QVector<QPair<QString, quint64>>{{QStringLiteral("1 KiB"), 1024}, {QStringLiteral("1 MiB"), 1024 * 1024},
             {QStringLiteral("10 MiB"), 10 * 1024 * 1024}, {QStringLiteral("100 MiB"), 100 * 1024 * 1024}})
        m_minimum->addItem(entry.first, QVariant::fromValue<qulonglong>(entry.second));
    m_minimum->setCurrentIndex(1);
    m_duplicateScan = button(dlTr("查找重复文件"), "explorerDuplicateScan", "search");
    duplicateRow->addWidget(m_minimum); duplicateRow->addStretch(); duplicateRow->addWidget(m_duplicateScan);
    duplicateLayout->addLayout(duplicateRow);
    m_duplicateSummary = label(dlTr("尚未查找重复文件"), "explorerDuplicateSummary");
    m_duplicateSummary->setWordWrap(true); duplicateLayout->addWidget(m_duplicateSummary);
    m_duplicateTree = new QTreeWidget; m_duplicateTree->setObjectName(QStringLiteral("explorerDuplicates"));
    m_duplicateTree->setHeaderLabels({dlTr("重复分组 / 文件完整路径"), dlTr("单文件大小"), dlTr("逻辑冗余量 / SHA-256")});
    styleTree(m_duplicateTree); m_duplicateTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_duplicateTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_duplicateTree->header()->setSectionResizeMode(2, QHeaderView::Interactive); m_duplicateTree->setColumnWidth(2, 180);
    duplicateLayout->addWidget(m_duplicateTree, 1);
    m_duplicateNotice = label(dlTr("仅在点击查找后读取文件内容：先按大小和采样筛选，再校验完整 SHA-256。逻辑冗余量不是可回收空间；本页不选择保留项，也不删除文件。"), "explorerDuplicateNotice");
    m_duplicateNotice->setWordWrap(true); duplicateLayout->addWidget(m_duplicateNotice);
    m_tabs->addTab(duplicateTab, dlTr("重复文件")); layout->addWidget(m_tabs, 1);

    auto *detailRow = new QHBoxLayout;
    m_detail = label(dlTr("选择项目可查看完整路径"), "explorerDetail");
    m_detail->setWordWrap(true); m_detail->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_open = button(dlTr("打开所在目录"), "explorerOpen", "folder"); m_copy = button(dlTr("复制路径"), "explorerCopy", "copy");
    detailRow->addWidget(m_detail, 1); detailRow->addWidget(m_open); detailRow->addWidget(m_copy); layout->addLayout(detailRow);
    m_status = label(dlTr("尚未扫描"), "explorerStatus"); m_status->setWordWrap(true); layout->addWidget(m_status);
    m_progress = new QProgressBar; m_progress->setObjectName(QStringLiteral("explorerProgress")); m_progress->setTextVisible(false); m_progress->setRange(0, 1); layout->addWidget(m_progress);

    connect(m_choose, &QPushButton::clicked, this, [this] {
        const QString root = QFileDialog::getExistingDirectory(this, dlTr("选择分析目录"), m_rootEdit->text());
        if (!root.isEmpty()) { setRoot(root); if (!m_started) ensureScanStarted(); }
    });
    connect(m_scan, &QPushButton::clicked, this, &ExplorerPage::rescan);
    connect(m_rootEdit, &QLineEdit::returnPressed, this, &ExplorerPage::rescan);
    connect(m_cancelButton, &QPushButton::clicked, this, &ExplorerPage::cancelWork);
    connect(m_up, &QPushButton::clicked, this, &ExplorerPage::navigateUp);
    connect(m_home, &QPushButton::clicked, this, [this] { showDirectory(m_directoryResult.root.path); });
    connect(m_search, &QLineEdit::textChanged, this, [this] {
        if (m_directoryDone) showDirectory(m_currentPath);
        rebuildDuplicates();
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { updateControls(); updateSelection(); });
    connect(m_duplicateScan, &QPushButton::clicked, this, [this] { startDuplicateScan(); });
    connect(m_minimum, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        if (m_duplicatesDone) m_duplicateNotice->setText(dlTr("最小大小已更改；已有结果仍属于上次扫描。点击“查找重复文件”应用新阈值。"));
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (item->data(0, populatedRole).toBool()) return;
        if (const auto *node = findNode(item->data(0, pathRole).toString())) addChildren(item, *node);
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (item->data(0, directoryRole).toBool()) showDirectory(item->data(0, pathRole).toString());
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ExplorerPage::updateSelection);
    connect(m_duplicateTree, &QTreeWidget::itemSelectionChanged, this, &ExplorerPage::updateSelection);
    m_map->activate = [this](const QString &path, bool directory) {
        if (directory) { showDirectory(path); return; }
        for (int row = 0; row < m_tree->topLevelItemCount(); ++row) {
            auto *item = m_tree->topLevelItem(row);
            if (item->data(0, pathRole).toString() == path) { m_tree->setCurrentItem(item); m_tree->scrollToItem(item); break; }
        }
    };
    connect(m_copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(selectedPath()); });
    connect(m_open, &QPushButton::clicked, this, [this] {
        const QString path = selectedPath(); if (path.isEmpty()) return;
        const QFileInfo info(path); const QString directory = info.isDir() ? path : info.absolutePath();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) m_status->setText(dlTr("无法打开目录：") + directory);
    });
    connect(m_export, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, dlTr("导出扫描报告"),
            QDir::home().filePath(m_tabs->currentIndex() == 0 ? QStringLiteral("disklens-space.csv") : QStringLiteral("disklens-duplicates.csv")), dlTr("CSV 报告 (*.csv)"));
        if (path.isEmpty()) return;
        QString error;
        if (!exportReport(path, &error)) QMessageBox::warning(this, dlTr("导出失败"), error);
        else m_status->setText(dlTr("报告已导出（包含筛选外结果）：") + path);
    });
    updateControls();
}

void ExplorerPage::setRoot(const QString &root)
{
    const QString value = normalizedRoot(root);
    if (isBusy()) {
        m_pendingRoot = value; m_pendingScan = m_started; m_pendingDuplicates = false; cancelWork(); return;
    }
    m_rootEdit->setText(value); m_rootEdit->setToolTip(value);
    if (m_root == value) return;
    m_root = value; m_currentPath.clear(); m_directoryDone = false; m_duplicatesDone = false;
    m_duplicatesRequested = false;
    m_directoryResult = {}; m_duplicateResult = {};
    m_map->setNode(nullptr, {}); m_tree->clear(); m_duplicateTree->clear();
    m_metrics->setText(dlTr("尚未扫描")); m_breadcrumb->setText(dlTr("扫描根目录：") + m_root);
    m_breadcrumb->setToolTip(m_root); m_duplicateSummary->setText(dlTr("尚未查找重复文件"));
    m_status->setText(dlTr("所选目录：") + m_root); updateControls(); updateSelection();
    if (m_started) startDirectoryScan();
}

void ExplorerPage::ensureScanStarted()
{
    if (m_started) return;
    m_started = true; startDirectoryScan();
}

void ExplorerPage::rescan()
{
    if (isBusy()) return;
    const QString nextRoot = normalizedRoot(m_rootEdit->text());
    if (nextRoot != m_root) {
        const bool alreadyStarted = m_started; setRoot(nextRoot);
        if (!alreadyStarted) ensureScanStarted();
        return;
    }
    m_started = true; startDirectoryScan();
}

void ExplorerPage::startDirectoryScan()
{
    if (isBusy() || m_shuttingDown) return;
    m_busy = true; m_cancel.store(false); m_directoryDone = false;
    m_map->setNode(nullptr, {}); m_tree->clear(); m_currentPath.clear();
    m_directoryResult = {}; m_metrics->setText(dlTr("正在统计目录…"));
    m_status->setText(dlTr("只读扫描中：") + m_root);
    m_directoryNotice->setText(dlTr("正在读取目录元数据；逻辑大小与实际占用分别统计，符号链接与其他挂载点会跳过。"));
    const auto progress = progressHandler(++m_operation); const QString root = m_root;
    updateControls(); updateSelection();
    m_directoryWatcher.setFuture(QtConcurrent::run([this, root, progress] { return analyzeDirectory(root, &m_cancel, progress); }));
}

void ExplorerPage::showDuplicates() { m_tabs->setCurrentIndex(1); }

bool ExplorerPage::duplicateScanRequested() const { return m_duplicatesRequested; }
quint64 ExplorerPage::duplicateMinimum() const { return m_minimum->currentData().toULongLong(); }
void ExplorerPage::setDuplicateMinimum(quint64 bytes)
{
    if (isBusy()) return;
    const int index = m_minimum->findData(QVariant::fromValue<qulonglong>(bytes));
    if (index >= 0) m_minimum->setCurrentIndex(index);
}

void ExplorerPage::startDuplicateScan(bool activateTab)
{
    if (activateTab) showDuplicates();
    m_duplicatesRequested = true;
    if (isBusy()) { m_pendingDuplicates = true; m_pendingDuplicateActivation = activateTab; return; }
    const QString nextRoot = normalizedRoot(m_rootEdit->text());
    if (nextRoot != m_root) {
        // A changed scope first replaces the directory snapshot. Hashing remains
        // queued only because this method itself is an explicit user action.
        setRoot(nextRoot);
        m_duplicatesRequested = true;
        if (isBusy()) { m_pendingDuplicates = true; m_pendingDuplicateActivation = activateTab; return; }
    }
    m_started = true; m_busy = true; m_duplicatesDone = false; m_cancel.store(false);
    m_duplicateResult = {}; m_duplicateTree->clear();
    m_duplicateSummary->setText(dlTr("正在按大小、采样及完整 SHA-256 查找…"));
    m_duplicateNotice->setText(dlTr("正在读取候选文件内容。取消会保留已验证的部分分组；本页不执行删除。"));
    m_status->setText(dlTr("重复检测中：") + m_root);
    const auto progress = progressHandler(++m_operation); const QString root = m_root;
    const quint64 minimum = m_minimum->currentData().toULongLong(); m_duplicateMinimum = minimum;
    updateControls(); updateSelection();
    m_duplicateWatcher.setFuture(QtConcurrent::run([this, root, minimum, progress] { return findDuplicates(root, minimum, &m_cancel, progress); }));
}

std::function<void(quint64, const QString &)> ExplorerPage::progressHandler(quint64 operation)
{
    const QPointer<ExplorerPage> self(this);
    auto last = std::make_shared<std::atomic<qint64>>(0);
    return [self, operation, last](quint64 scanned, const QString &path) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 previous = last->load();
        if (now - previous < 120 || !last->compare_exchange_strong(previous, now)) return;
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, operation, scanned, path] {
            if (!self || self->m_shuttingDown || !self->m_busy || self->m_operation != operation) return;
            self->m_status->setText(dlTr("已检查 %1 个文件 · %2").arg(count(scanned), path));
            self->m_status->setToolTip(path);
        }, Qt::QueuedConnection);
    };
}

void ExplorerPage::directoryFinished()
{
    if (m_shuttingDown) return;
    m_directoryResult = m_directoryWatcher.result(); m_directoryDone = true; m_busy = false;
    m_status->setText(completion(m_directoryResult.canceled, m_directoryResult.truncated, m_directoryResult.skippedEntries, m_directoryResult.error));
    m_directoryNotice->setText(dlTr("%1逻辑大小是文件长度；实际占用含目录元数据，硬链接只计一次。")
        .arg(partial(m_directoryResult) ? dlTr("部分结果。") : QString()));
    if (m_directoryResult.root.directory) showDirectory(m_directoryResult.root.path);
    else {
        m_currentPath = m_root;
        m_metrics->setText(dlTr("未获得可分析的目录结果；请检查路径与访问权限。"));
    }
    updateControls();
    if (m_pendingScan) {
        const QString root = m_pendingRoot; m_pendingScan = false; m_pendingRoot.clear(); setRoot(root);
    } else if (m_pendingDuplicates) {
        const bool activateTab = m_pendingDuplicateActivation;
        m_pendingDuplicates = false; startDuplicateScan(activateTab);
    }
}

void ExplorerPage::duplicatesFinished()
{
    if (m_shuttingDown) return;
    m_duplicateResult = m_duplicateWatcher.result(); m_duplicatesDone = true; m_busy = false;
    m_status->setText(completion(m_duplicateResult.canceled, m_duplicateResult.truncated, m_duplicateResult.skippedEntries, m_duplicateResult.error));
    m_duplicateNotice->setText(dlTr("%1完整 SHA-256 验证；逻辑冗余量不等于可回收空间，不自动选择保留项或删除文件。")
        .arg(partial(m_duplicateResult) ? dlTr("部分结果。") : QString()));
    rebuildDuplicates(); updateControls(); updateSelection();
    if (m_pendingScan) {
        const QString root = m_pendingRoot; m_pendingScan = false; m_pendingRoot.clear(); setRoot(root);
    } else if (m_pendingDuplicates) {
        const bool activateTab = m_pendingDuplicateActivation;
        m_pendingDuplicates = false; startDuplicateScan(activateTab);
    }
}

void ExplorerPage::cancelWork()
{
    m_duplicatesRequested = false;
    if (!isBusy()) return;
    m_cancel.store(true); m_pendingDuplicates = false;
    m_status->setText(dlTr("正在取消；等待当前读取结束并保留已完成的部分结果…"));
    m_cancelButton->setEnabled(false);
}

bool ExplorerPage::isBusy() const { return m_busy; }
bool ExplorerPage::isSettled() const { return m_started && !m_busy && (m_directoryDone || m_duplicatesDone); }

const SpaceNode *ExplorerPage::findNode(const QString &path) const
{
    return path.isEmpty() ? nullptr : lookup(m_directoryResult.root, path);
}

void ExplorerPage::addChildren(QTreeWidgetItem *parent, const SpaceNode &node)
{
    if (parent) { qDeleteAll(parent->takeChildren()); parent->setData(0, populatedRole, true); }
    const QString search = m_search->text().trimmed();
    for (const auto *child : orderedChildren(node)) {
        if (!matches(*child, search)) continue;
        auto *item = new QTreeWidgetItem;
        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        item->setText(0, singleLine(child->name)); item->setText(1, formatBytes(child->allocatedBytes));
        item->setText(2, formatBytes(child->logicalBytes)); item->setText(3, count(child->fileCount));
        item->setData(0, pathRole, child->path); item->setData(0, directoryRole, child->directory);
        item->setData(0, populatedRole, false); item->setIcon(0, lineIcon(child->directory ? "folder" : "files"));
        for (int column = 0; column < 4; ++column) item->setToolTip(column, child->path
            + dlTr("\n实际占用：%1\n逻辑大小：%2\n文件数：%3").arg(formatBytes(child->allocatedBytes), formatBytes(child->logicalBytes), count(child->fileCount)));
        for (int column = 1; column < 4; ++column) item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
        if (parent) parent->addChild(item); else m_tree->addTopLevelItem(item);
        if (child->directory && !child->children.isEmpty()) item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
}

void ExplorerPage::showDirectory(const QString &path)
{
    const auto *node = findNode(path); if (!node || !node->directory) return;
    m_currentPath = node->path;
    m_tree->clear(); addChildren(nullptr, *node); m_map->setNode(node, m_search->text().trimmed());
    QString relative = QDir(m_directoryResult.root.path).relativeFilePath(node->path);
    m_breadcrumb->setText(relative == QStringLiteral(".") ? dlTr("扫描根目录：") + node->path
        : dlTr("当前层：") + relative.replace(QLatin1Char('/'), QStringLiteral(" / ")));
    m_breadcrumb->setToolTip(node->path);
    m_metrics->setText(dlTr("实际占用 %1   ·   逻辑大小 %2   ·   %3 个文件   ·   当前层 %4 项%5")
        .arg(formatBytes(node->allocatedBytes), formatBytes(node->logicalBytes), count(node->fileCount), count(node->children.size()),
             partial(m_directoryResult) ? dlTr("   ·   部分结果") : QString()));
    updateControls(); updateSelection();
}

void ExplorerPage::navigateUp()
{
    if (m_currentPath.isEmpty() || m_currentPath == m_directoryResult.root.path) return;
    showDirectory(QFileInfo(m_currentPath).absolutePath());
}

void ExplorerPage::rebuildDuplicates()
{
    m_duplicateTree->clear(); if (!m_duplicatesDone) return;
    const QString search = m_search->text().trimmed(); int visible = 0;
    for (int groupIndex = 0; groupIndex < m_duplicateResult.groups.size(); ++groupIndex) {
        const auto &group = m_duplicateResult.groups[groupIndex];
        bool accepted = search.isEmpty() || QString::fromLatin1(group.sha256.toHex()).contains(search, Qt::CaseInsensitive);
        for (const QString &path : group.paths) accepted = accepted || path.contains(search, Qt::CaseInsensitive);
        if (!accepted) continue;
        ++visible; auto *item = new QTreeWidgetItem(m_duplicateTree);
        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        const quint64 redundant = quint64(qMax(0, int(group.paths.size()) - 1)) * group.bytes;
        item->setText(0, dlTr("分组 %1 · %2 个相同文件").arg(groupIndex + 1).arg(group.paths.size()));
        item->setText(1, formatBytes(group.bytes)); item->setText(2, formatBytes(redundant) + dlTr(" 逻辑冗余"));
        const QString hash = QString::fromLatin1(group.sha256.toHex());
        for (int column = 0; column < 3; ++column) item->setToolTip(column, dlTr("完整 SHA-256：") + hash
            + dlTr("\n逻辑冗余量 = (%1 − 1) × %2，不代表可回收空间").arg(group.paths.size()).arg(formatBytes(group.bytes)));
        for (const QString &path : group.paths) {
            auto *child = new QTreeWidgetItem(item); child->setText(0, singleLine(path)); child->setText(1, formatBytes(group.bytes)); child->setText(2, hash);
            child->setFlags(child->flags() & ~Qt::ItemIsUserCheckable);
            child->setData(0, pathRole, path);
            for (int column = 0; column < 3; ++column) child->setToolTip(column, path + dlTr("\n完整 SHA-256：") + hash);
        }
        item->setExpanded(true);
    }
    m_duplicateSummary->setText(dlTr("%1 个重复分组（显示 %2） · 逻辑冗余 %3 · 检查 %4 个文件 · 完整校验 %5 个文件%6")
        .arg(count(m_duplicateResult.groups.size()), count(visible), formatBytes(m_duplicateResult.redundantBytes),
             count(m_duplicateResult.scannedFiles), count(m_duplicateResult.hashedFiles),
             partial(m_duplicateResult) ? dlTr(" · 部分结果") : QString()));
    updateSelection();
}

QString ExplorerPage::selectedPath() const
{
    const auto *tree = m_tabs->currentIndex() == 0 ? m_tree : m_duplicateTree;
    const auto *item = tree->currentItem(); return item ? item->data(0, pathRole).toString() : QString();
}

void ExplorerPage::updateSelection()
{
    const QString path = selectedPath(); m_detail->setText(path.isEmpty() ? dlTr("选择项目可查看完整路径；重复分组请选中具体文件") : singleLine(path));
    m_detail->setToolTip(path); m_copy->setEnabled(!path.isEmpty()); m_open->setEnabled(!path.isEmpty());
}

void ExplorerPage::updateControls()
{
    m_choose->setEnabled(!m_busy); m_rootEdit->setEnabled(!m_busy); m_scan->setEnabled(!m_busy);
    m_cancelButton->setEnabled(m_busy && !m_cancel.load()); m_duplicateScan->setEnabled(!m_busy); m_minimum->setEnabled(!m_busy);
    m_up->setEnabled(!m_currentPath.isEmpty() && m_currentPath != m_directoryResult.root.path);
    m_home->setEnabled(m_up->isEnabled());
    m_export->setEnabled(!m_busy && (m_tabs->currentIndex() == 0 ? m_directoryDone : m_duplicatesDone));
    m_progress->setRange(0, m_busy ? 0 : 1); m_progress->setValue(m_busy ? 0 : 1);
}

bool ExplorerPage::exportReport(const QString &path, QString *error) const
{
    const bool duplicates = m_tabs->currentIndex() == 1;
    if (m_busy || (duplicates ? !m_duplicatesDone : !m_directoryDone)) {
        if (error) *error = dlTr("请先完成当前页的扫描。");
        return false;
    }
    const QFileInfo destination(path);
    const QString absoluteOutput = QDir::cleanPath(destination.absoluteFilePath());
    const QString scanRoot = QFileInfo(m_root).canonicalFilePath();
    const QString scope = scanRoot.isEmpty() ? m_root : scanRoot;
    const QString parentCanonical = destination.absoluteDir().canonicalPath();
    const QString resolvedOutput = parentCanonical.isEmpty() ? QString() : QDir(parentCanonical).filePath(destination.fileName());
    const bool inScanRoot = isWithin(absoluteOutput, m_root) || isWithin(destination.canonicalFilePath(), scope)
        || isWithin(resolvedOutput, scope);
    // QSaveFile replaces atomically, but an explicitly selected input path must
    // never turn a read-only report into data loss. Check both lexical and real
    // paths so an external symlink cannot alias an existing scanned file.
    if (destination.isDir() || (inScanRoot && (destination.exists() || destination.isSymLink()))) {
        if (error) *error = dlTr("为保护扫描数据，不能覆盖扫描目录中已有的文件。请使用新文件名，或将报告保存到扫描目录之外。");
        return false;
    }
    // QSaveFile alone replaces a destination created after the existence check.
    // Inside the input tree, write a private sibling and publish with link(2):
    // publication fails atomically if a new entry now occupies the chosen name.
    // QTemporaryFile removes the private sibling on every exit path.
    QTemporaryFile staging(destination.absoluteDir().filePath(QStringLiteral(".disklens-report-XXXXXX")));
    QString commitPath = absoluteOutput;
    if (inScanRoot) {
        if (!staging.open()) {
            if (error) *error = staging.errorString();
            return false;
        }
        commitPath = staging.fileName(); staging.close();
    }
    QByteArray output("\xef\xbb\xbf");
    appendCsv(output, {QStringLiteral("report"), duplicates ? QStringLiteral("duplicates") : QStringLiteral("directory")});
    appendCsv(output, {QStringLiteral("scan_root"), m_root});
    appendCsv(output, {QStringLiteral("export_scope"), duplicates ? m_root : m_currentPath});
    appendCsv(output, {QStringLiteral("search_filter"), QStringLiteral("not_applied_all_results_exported")});
    if (duplicates) {
        appendCsv(output, {QStringLiteral("partial"), partial(m_duplicateResult) ? QStringLiteral("true") : QStringLiteral("false")});
        appendCsv(output, {QStringLiteral("canceled"), m_duplicateResult.canceled ? QStringLiteral("true") : QStringLiteral("false")});
        appendCsv(output, {QStringLiteral("truncated"), m_duplicateResult.truncated ? QStringLiteral("true") : QStringLiteral("false")});
        appendCsv(output, {QStringLiteral("skipped_entries"), QString::number(m_duplicateResult.skippedEntries)});
        appendCsv(output, {QStringLiteral("scanned_files"), QString::number(m_duplicateResult.scannedFiles)});
        appendCsv(output, {QStringLiteral("hashed_files"), QString::number(m_duplicateResult.hashedFiles)});
        appendCsv(output, {QStringLiteral("minimum_bytes"), QString::number(m_duplicateMinimum)});
        appendCsv(output, {QStringLiteral("logical_redundant_bytes"), QString::number(m_duplicateResult.redundantBytes)});
        appendCsv(output, {QStringLiteral("error"), m_duplicateResult.error});
        appendCsv(output, {QStringLiteral("redundancy_note"), QStringLiteral("Logical redundancy is not reclaimable disk space.")});
        appendCsv(output, {QStringLiteral("group"), QStringLiteral("path"), QStringLiteral("logical_bytes_each"), QStringLiteral("group_logical_redundancy"), QStringLiteral("sha256")});
        for (int index = 0; index < m_duplicateResult.groups.size(); ++index) {
            const auto &group = m_duplicateResult.groups[index];
            for (const QString &file : group.paths)
                appendCsv(output, {QString::number(index + 1), file, QString::number(group.bytes),
                    QString::number(quint64(qMax(0, int(group.paths.size()) - 1)) * group.bytes), QString::fromLatin1(group.sha256.toHex())});
        }
    } else {
        appendCsv(output, {QStringLiteral("partial"), partial(m_directoryResult) ? QStringLiteral("true") : QStringLiteral("false")});
        appendCsv(output, {QStringLiteral("canceled"), m_directoryResult.canceled ? QStringLiteral("true") : QStringLiteral("false")});
        appendCsv(output, {QStringLiteral("truncated"), m_directoryResult.truncated ? QStringLiteral("true") : QStringLiteral("false")});
        appendCsv(output, {QStringLiteral("skipped_entries"), QString::number(m_directoryResult.skippedEntries)});
        appendCsv(output, {QStringLiteral("scanned_files"), QString::number(m_directoryResult.scannedFiles)});
        appendCsv(output, {QStringLiteral("error"), m_directoryResult.error});
        appendCsv(output, {QStringLiteral("name"), QStringLiteral("path"), QStringLiteral("type"), QStringLiteral("logical_bytes"), QStringLiteral("allocated_bytes"), QStringLiteral("file_count")});
        const auto *current = findNode(m_currentPath);
        if (current) {
            std::function<void(const SpaceNode &)> writeNode = [&](const SpaceNode &node) {
                appendCsv(output, {node.name, node.path, node.directory ? QStringLiteral("directory") : QStringLiteral("file"),
                    QString::number(node.logicalBytes), QString::number(node.allocatedBytes), QString::number(node.fileCount)});
                for (const auto &child : node.children) writeNode(child);
            };
            writeNode(*current);
        }
    }
    QSaveFile file(commitPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    if (inScanRoot && ::link(QFile::encodeName(commitPath).constData(), QFile::encodeName(absoluteOutput).constData()) != 0) {
        if (error) *error = dlTr("无法安全创建报告（不会覆盖已有数据）：%1")
            .arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    if (error) error->clear();
    return true;
}

bool ExplorerPage::verifyUi(QString *error)
{
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!m_tree || !m_map || !m_duplicateTree || !m_tabs) return fail(dlTr("空间分析控件缺失"));
    if (m_tabs->count() != 2) return fail(dlTr("空间分析 tab 数量错误"));
    if (m_export->isEnabled() && m_busy) return fail(dlTr("扫描中不应允许导出旧结果"));
    if (m_tree->palette().color(QPalette::HighlightedText).lightness() > 160)
        return fail(dlTr("选中行文字颜色对比不足"));
    for (const auto *tree : {m_tree, m_duplicateTree}) {
        QTreeWidgetItemIterator iterator(const_cast<QTreeWidget *>(tree));
        while (*iterator) {
            if ((*iterator)->flags() & Qt::ItemIsUserCheckable) return fail(dlTr("只读列表不应包含删除勾选框"));
            ++iterator;
        }
    }
    if (error) error->clear();
    return true;
}
