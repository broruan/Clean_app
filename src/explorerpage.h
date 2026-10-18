#pragma once

#include "spaceanalyzer.h"

#include <QFutureWatcher>
#include <QWidget>
#include <atomic>

class QLabel;
class QLineEdit;
class QComboBox;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class ExplorerMapWidget;

// Read-only directory exploration and explicitly requested duplicate hashing.
// Closing the page cancels workers and waits before releasing their state.
class ExplorerPage final : public QWidget {
    Q_OBJECT
public:
    explicit ExplorerPage(QWidget *parent = nullptr);
    ~ExplorerPage() override;

    void setRoot(const QString &root);
    void ensureScanStarted();
    void rescan();
    void cancelWork();
    bool isBusy() const;
    bool isSettled() const;
    bool verifyUi(QString *error);
    void showDuplicates();
    void startDuplicateScan();
    // Exports the active tab, including its scope/completeness and scan totals.
    // This writes only the user-selected report; scanned files are never edited.
    bool exportReport(const QString &path, QString *error = nullptr) const;

private:
    void createUi();
    void startDirectoryScan();
    void directoryFinished();
    void duplicatesFinished();
    void updateControls();
    void showDirectory(const QString &path);
    void rebuildDuplicates();
    void updateSelection();
    void navigateUp();
    void addChildren(QTreeWidgetItem *parent, const SpaceNode &node);
    const SpaceNode *findNode(const QString &path) const;
    QString selectedPath() const;
    std::function<void(quint64, const QString &)> progressHandler(quint64 operation);

    QString m_root;
    QString m_currentPath;
    QString m_pendingRoot;
    DirectoryScanResult m_directoryResult;
    DuplicateScanResult m_duplicateResult;
    QFutureWatcher<DirectoryScanResult> m_directoryWatcher;
    QFutureWatcher<DuplicateScanResult> m_duplicateWatcher;
    std::atomic_bool m_cancel{false};
    bool m_started = false;
    bool m_directoryDone = false;
    bool m_duplicatesDone = false;
    bool m_busy = false;
    bool m_shuttingDown = false;
    bool m_pendingScan = false;
    bool m_pendingDuplicates = false;
    quint64 m_operation = 0;
    quint64 m_duplicateMinimum = 0;

    QLineEdit *m_rootEdit = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_minimum = nullptr;
    QTabWidget *m_tabs = nullptr;
    QTreeWidget *m_tree = nullptr;
    QTreeWidget *m_duplicateTree = nullptr;
    ExplorerMapWidget *m_map = nullptr;
    QLabel *m_breadcrumb = nullptr;
    QLabel *m_metrics = nullptr;
    QLabel *m_directoryNotice = nullptr;
    QLabel *m_duplicateSummary = nullptr;
    QLabel *m_duplicateNotice = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_detail = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_choose = nullptr;
    QPushButton *m_scan = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_up = nullptr;
    QPushButton *m_home = nullptr;
    QPushButton *m_duplicateScan = nullptr;
    QPushButton *m_export = nullptr;
    QPushButton *m_open = nullptr;
    QPushButton *m_copy = nullptr;
};
