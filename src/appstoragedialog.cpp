#include "localization.h"
#include "appstoragedialog.h"
#include "widgets.h"
#include "models.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace {
QLabel *plainLabel(const QString &text, const char *name = nullptr)
{
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    if (name) label->setObjectName(name);
    return label;
}
QString xdgRoot(const char *variable, const QString &fallback)
{
    const QString value = qEnvironmentVariable(variable);
    return QDir::isAbsolutePath(value) ? QDir::cleanPath(value) : fallback;
}
}

AppStorageDialog::AppStorageDialog(const AppEntry &app, QWidget *parent,
    QString home, QString cacheRoot, QString configRoot) : QDialog(parent)
{
    setObjectName("appStorageDialog");
    setWindowTitle(dlTr("软件空间画像 · ") + app.name);
    resize(930, 600);
    setMinimumSize(760, 470);
    if (home.isEmpty()) home = QDir::homePath();
    if (cacheRoot.isEmpty()) cacheRoot = xdgRoot("XDG_CACHE_HOME", QDir(home).filePath(".cache"));
    if (configRoot.isEmpty()) configRoot = xdgRoot("XDG_CONFIG_HOME", QDir(home).filePath(".config"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(14);
    layout->addWidget(plainLabel(app.name + " · " + app.id, "cardTitle"));
    layout->addWidget(plainLabel(dlTr("软件包记录体积 %1（%2，估算）").arg(app.installedBytes ? formatBytes(app.installedBytes) : dlTr("未知"), sourceName(app.source)), "smallValue"));
    layout->addWidget(plainLabel(dlTr("下表只读统计可信关联目录。配置、扩展、邮件和容器数据可能包含需要保留的内容；不同目录的统计不直接相加，也不代表可释放空间。"), "muted"));
    m_status = plainLabel(dlTr("正在关联用户数据目录…"), "muted");
    m_status->setObjectName("appStorageStatus");
    layout->addWidget(m_status);
    m_table = new QTableWidget(0, 5);
    m_table->setObjectName("appStorageTable");
    m_table->setHorizontalHeaderLabels({dlTr("类型"), dlTr("实际占用"), dlTr("逻辑大小"), dlTr("文件数"), dlTr("关联路径")});
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setStyleSheet("QTableWidget { selection-color: #29364f; selection-background-color: #edf2ff; } QTableWidget::item { padding: 7px; }");
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->setColumnWidth(0, 150); m_table->setColumnWidth(1, 115);
    m_table->setColumnWidth(2, 115); m_table->setColumnWidth(3, 70);
    layout->addWidget(m_table, 1);
    m_detail = plainLabel(dlTr("选择目录查看关联依据。"), "muted");
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_detail);
    auto *buttons = new QHBoxLayout;
    m_open = new QPushButton(dlTr("打开目录")); m_open->setObjectName("appStorageOpen");
    m_copy = new QPushButton(dlTr("复制路径")); m_copy->setObjectName("appStorageCopy");
    m_open->setEnabled(false); m_copy->setEnabled(false);
    buttons->addWidget(m_open); buttons->addWidget(m_copy); buttons->addStretch();
    m_cancelButton = new QPushButton(dlTr("停止统计"));
    m_cancelButton->setObjectName("appStorageCancel"); buttons->addWidget(m_cancelButton);
    auto *close = new QPushButton(dlTr("关闭")); buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_cancelButton, &QPushButton::clicked, this, [this] {
        m_cancel.store(true); m_cancelButton->setEnabled(false);
        m_status->setText(dlTr("正在停止，已统计数据将标记为部分结果…"));
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &AppStorageDialog::updateSelection);
    connect(m_copy, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0 && row < m_result.locations.size())
            QApplication::clipboard()->setText(m_result.locations.at(row).path);
    });
    connect(m_open, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0 && row < m_result.locations.size()) {
            const QString path = m_result.locations.at(row).path;
            if (!QFileInfo(path).isDir() || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
                QMessageBox::warning(this, dlTr("无法打开目录"), path);
        }
    });
    connect(&m_watcher, &QFutureWatcher<AppStorageResult>::finished, this, &AppStorageDialog::showResult);
    const auto locations = discoverAppStorage(app, home, cacheRoot, configRoot);
    QTimer::singleShot(0, this, [this, locations] {
        m_watcher.setFuture(QtConcurrent::run([this, locations] {
            return measureAppStorage(locations, &m_cancel);
        }));
    });
}

AppStorageDialog::~AppStorageDialog()
{
    m_cancel.store(true);
    m_watcher.waitForFinished();
}
bool AppStorageDialog::isSettled() const { return m_settled; }

void AppStorageDialog::showResult()
{
    m_result = m_watcher.result(); m_settled = true; m_cancelButton->hide();
    m_table->setRowCount(m_result.locations.size());
    bool partial = m_result.canceled;
    for (int row = 0; row < m_result.locations.size(); ++row) {
        const auto &location = m_result.locations.at(row);
        partial = partial || location.partial || !location.error.isEmpty();
        const QString suffix = location.partial ? dlTr("（部分）") : QString();
        const QStringList values{location.category,
            location.measured ? formatBytes(location.allocatedBytes) + suffix : QStringLiteral("—"),
            location.measured ? formatBytes(location.logicalBytes) : QStringLiteral("—"),
            location.measured ? QString::number(location.fileCount) : QStringLiteral("—"), location.path};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setToolTip(column == 4 ? location.path : location.attribution);
            m_table->setItem(row, column, item);
        }
        m_table->setRowHeight(row, 45);
    }
    m_status->setText(m_result.locations.isEmpty()
        ? dlTr("没有找到本版本支持的关联目录；这不表示此应用没有用户数据。")
        : dlTr("%1 · %2 个关联目录；按目录分别统计，不提供总可释放空间。")
            .arg(partial ? dlTr("部分结果，请核对跳过项") : dlTr("统计完成"))
            .arg(m_result.locations.size()));
    updateSelection();
}

void AppStorageDialog::updateSelection()
{
    const int row = m_table->currentRow();
    const bool valid = row >= 0 && row < m_result.locations.size();
    m_open->setEnabled(valid); m_copy->setEnabled(valid);
    if (!valid) { m_detail->setText(dlTr("选择目录查看关联依据。")); return; }
    const auto &location = m_result.locations.at(row);
    QString text = location.attribution + '\n' + location.path;
    if (!location.error.isEmpty()) text += '\n' + location.error;
    else if (location.partial) text += dlTr("\n结果不完整；包含跳过、取消或扫描上限。跳过 %1 项。").arg(location.skippedEntries);
    m_detail->setText(text);
}
