#include "models.h"
#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QStyle>

QString formatBytes(quint64 bytes, int precision)
{
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) { value /= 1024.0; ++unit; }
    return QString::number(value, 'f', unit == 0 ? 0 : precision) + " " + units[unit];
}

int AppModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_entries.size(); }
int AppModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : 4; }
QVariant AppModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    return QStringList{QStringLiteral("软件名称"), QStringLiteral("来源"), QStringLiteral("版本"), QStringLiteral("软件包体积")}.value(section);
}
const AppEntry *AppModel::entry(int row) const { return row >= 0 && row < m_entries.size() ? &m_entries[row] : nullptr; }
void AppModel::setEntries(QVector<AppEntry> entries) { beginResetModel(); m_entries = std::move(entries); endResetModel(); }
QVariant AppModel::data(const QModelIndex &index, int role) const
{
    const auto *app = entry(index.row());
    if (!app) return {};
    if (role == BytesRole) return QVariant::fromValue(app->installedBytes);
    if (role == DesktopRole) return app->desktopApp;
    if (role == PackageSourceRole) return static_cast<int>(app->source);
    if (role == SecondaryRole) return app->id;
    if (role == IconRole) return app->iconName;
    if (role == SearchRole) return app->name + " " + app->id + " " + app->description;
    if (role == Qt::ToolTipRole) {
        QString tip = app->name + "\n" + app->id + "\n" + app->description;
        tip += QStringLiteral("\n体积口径：DEB 为安装元数据估值；Snap 为当前压缩包；Flatpak 为部署体积。\n不含用户文件、缓存；共享数据可能重复计算。0 / — 表示未知。");
        if (!app->removable) tip += "\n" + app->protectionReason;
        return tip;
    }
    if (role == Qt::TextAlignmentRole && index.column() == 3) return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == SortRole && index.column() == 3) return QVariant::fromValue(app->installedBytes);
    if (role == Qt::DisplayRole || role == SortRole) {
        switch (index.column()) {
        case 0: return app->name;
        case 1: return sourceName(app->source);
        case 2: return app->version;
        case 3: return app->installedBytes ? formatBytes(app->installedBytes) : QStringLiteral("—");
        }
    }
    return {};
}

void AppFilter::setSearch(const QString &search) { m_search = search.trimmed(); invalidateFilter(); }
void AppFilter::setOnlyDesktop(bool enabled) { m_desktop = enabled; invalidateFilter(); }
void AppFilter::setSourceFilter(int source) { m_source = source; invalidateFilter(); }
bool AppFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    auto index = sourceModel()->index(row, 0, parent);
    return (!m_desktop || index.data(DesktopRole).toBool())
        && (m_source < 0 || index.data(PackageSourceRole).toInt() == m_source)
        && (m_search.isEmpty() || index.data(SearchRole).toString().contains(m_search, Qt::CaseInsensitive));
}

int FileModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_entries.size(); }
int FileModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : 4; }
QVariant FileModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    return QStringList{QStringLiteral("文件 / 所在位置"), QStringLiteral("类型"), QStringLiteral("修改时间"), QStringLiteral("文件大小")}.value(section);
}
void FileModel::setEntries(QVector<FileEntry> entries) { beginResetModel(); m_entries = std::move(entries); endResetModel(); }
const FileEntry *FileModel::entry(int row) const { return row >= 0 && row < m_entries.size() ? &m_entries[row] : nullptr; }
QVariant FileModel::data(const QModelIndex &index, int role) const
{
    const auto *file = entry(index.row());
    if (!file) return {};
    QFileInfo info(file->path);
    if (role == BytesRole) return QVariant::fromValue(file->bytes);
    if (role == SecondaryRole) return info.absolutePath();
    if (role == IconRole) return QStringLiteral("text-x-generic");
    if (role == SearchRole) return file->path;
    if (role == Qt::ToolTipRole) return file->path + QStringLiteral("\n逻辑大小：") + formatBytes(file->bytes)
        + QStringLiteral("\n实际分配空间：") + formatBytes(file->allocatedBytes);
    if (role == Qt::TextAlignmentRole && index.column() == 3) return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == SortRole && index.column() == 3) return QVariant::fromValue(file->bytes);
    if (role == SortRole && index.column() == 2) return file->modified;
    if (role == Qt::DisplayRole || role == SortRole) {
        switch (index.column()) {
        case 0: return info.fileName();
        case 1: return info.suffix().isEmpty() ? QStringLiteral("文件") : info.suffix().toUpper();
        case 2: return file->modified.toLocalTime().toString("yyyy-MM-dd HH:mm");
        case 3: return formatBytes(file->bytes);
        }
    }
    return {};
}

EntryDelegate::EntryDelegate(bool files, QObject *parent) : QStyledItemDelegate(parent), m_files(files) {}
QSize EntryDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const { return {100, 66}; }
void EntryDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    painter->fillRect(option.rect, selected ? QColor("#edf2ff") : hover ? QColor("#f8faff") : QColor("#ffffff"));
    painter->setClipRect(option.rect);
    const QRect box = option.rect.adjusted(16, 0, -16, 0);
    if (index.column() == 0) {
        QRect iconRect(box.left(), box.center().y() - 18, 36, 36);
        QColor color = m_files ? QColor("#e6f7f2") : QColor("#edf0ff");
        painter->setPen(Qt::NoPen); painter->setBrush(color); painter->drawRoundedRect(iconRect, 10, 10);
        const QString iconName=index.data(IconRole).toString();
        QIcon icon = QFileInfo(iconName).isAbsolute() ? QIcon(iconName) : QIcon::fromTheme(iconName);
        if (!icon.isNull()) icon.paint(painter, iconRect.adjusted(7, 7, -7, -7));
        else {
            painter->setPen(m_files ? QColor("#159878") : QColor("#5265cb"));
            QFont font = option.font; font.setBold(true); font.setPointSize(12); painter->setFont(font);
            painter->drawText(iconRect, Qt::AlignCenter, index.data().toString().left(1).toUpper());
        }
        QRect textRect = box.adjusted(50, 0, 0, 0);
        QFont font = option.font; font.setBold(true); painter->setFont(font); painter->setPen(QColor("#26324b"));
        painter->drawText(QRect(textRect.left(), box.center().y()-21, textRect.width(), 23), Qt::AlignVCenter,
                          QFontMetrics(font).elidedText(index.data().toString(), Qt::ElideRight, textRect.width()));
        font.setBold(false); font.setPointSizeF(qMax(8.0, font.pointSizeF()-1)); painter->setFont(font); painter->setPen(QColor("#8791a5"));
        painter->drawText(QRect(textRect.left(), box.center().y()+3, textRect.width(), 20), Qt::AlignVCenter,
                          QFontMetrics(font).elidedText(index.data(SecondaryRole).toString(), Qt::ElideMiddle, textRect.width()));
    } else if (!m_files && index.column() == 1) {
        const QString name = index.data().toString();
        const QColor fg = name == "DEB" ? QColor("#5365cf") : name == "Snap" ? QColor("#a67128") : QColor("#128e7c");
        const QColor bg = name == "DEB" ? QColor("#eff1ff") : name == "Snap" ? QColor("#fff5e3") : QColor("#e5f7f2");
        QRect pill(box.left(), box.center().y()-12, 66, 24);
        painter->setPen(Qt::NoPen); painter->setBrush(bg); painter->drawRoundedRect(pill, 6, 6);
        painter->setPen(fg); painter->setFont(option.font); painter->drawText(pill, Qt::AlignCenter, name);
    } else {
        painter->setFont(option.font); painter->setPen(index.column() == 3 ? QColor("#35436b") : QColor("#7b8599"));
        const auto align = index.column() == 3 ? Qt::AlignRight | Qt::AlignVCenter : Qt::AlignLeft | Qt::AlignVCenter;
        painter->drawText(box, align, QFontMetrics(option.font).elidedText(index.data().toString(), Qt::ElideRight, box.width()));
    }
    painter->setPen(QColor("#f0f2f7")); painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
    painter->restore();
}
