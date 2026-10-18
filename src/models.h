#pragma once

#include "types.h"
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>

QString formatBytes(quint64 bytes, int precision = 1);
enum EntryRoles { SortRole = Qt::UserRole + 1, BytesRole, SecondaryRole, IconRole,
                  DesktopRole, PackageSourceRole, SearchRole };

class AppModel : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void setEntries(QVector<AppEntry> entries);
    const AppEntry *entry(int row) const;
    const QVector<AppEntry> &entries() const { return m_entries; }
private:
    QVector<AppEntry> m_entries;
};

class AppFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setSearch(const QString &search);
    void setOnlyDesktop(bool enabled);
    void setSourceFilter(int source);
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
private:
    QString m_search;
    bool m_desktop = true;
    int m_source = -1;
};

class FileModel : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void setEntries(QVector<FileEntry> entries);
    const FileEntry *entry(int row) const;
private:
    QVector<FileEntry> m_entries;
};

class EntryDelegate : public QStyledItemDelegate {
public:
    explicit EntryDelegate(bool files, QObject *parent = nullptr);
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
private:
    bool m_files;
};
