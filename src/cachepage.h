#pragma once

#include "cachebackend.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QWidget>
#include <atomic>

class CacheModel;
class CacheFilter;
class QCheckBox;
class QDialog;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableView;

class CachePage final : public QWidget {
    Q_OBJECT
public:
    explicit CachePage(QWidget *parent = nullptr);
    ~CachePage() override;

    void ensureScanStarted();
    void rescan();
    bool isBusy() const;
    bool isCleaning() const;
    bool isSettled() const;
    // Test fixtures may supply isolated roots before the first scan starts.
    // An empty stateRoot disables state-log scanning.
    void setRoots(QString cacheRoot, QString stateRoot);
    void cancelWork();
    // Exercises only view/filter invariants; never starts cleaning or confirms
    // a destructive dialog, and preserves existing checkbox selections.
    bool verifyUi(QString *error);

signals:
    void spaceChanged();

private:
    void createUi();
    void startScan();
    void scanFinished();
    void beginClean();
    void cleanFinished();
    void updateControls();
    void updateMetrics();
    void updateSelection();
    void updateCurrentEntry();
    void updateScope();
    void selectVisible();
    void showScanNotes();
    const CacheEntry *currentEntry() const;
    CacheProgress progressHandler(quint64 operation, bool cleaning);

    QString m_cacheRoot;
    QString m_stateRoot;
    CacheScanResult m_scanResult;
    QFutureWatcher<CacheScanResult> m_scanWatcher;
    QFutureWatcher<CacheCleanResult> m_cleanWatcher;
    std::atomic_bool m_cancel{false};
    bool m_started = false;
    bool m_scanDone = false;
    bool m_scanning = false;
    bool m_cleaning = false;
    bool m_confirming = false;
    bool m_shuttingDown = false;
    quint64 m_operation = 0;
    QPointer<QDialog> m_confirmation;

    CacheModel *m_model = nullptr;
    CacheFilter *m_filter = nullptr;
    QTableView *m_table = nullptr;
    QLabel *m_scope = nullptr;
    QLabel *m_totalSize = nullptr;
    QLabel *m_cacheSize = nullptr;
    QLabel *m_logSize = nullptr;
    QLabel *m_totalSummary = nullptr;
    QLabel *m_cacheSummary = nullptr;
    QLabel *m_logSummary = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_count = nullptr;
    QLabel *m_selection = nullptr;
    QLabel *m_detail = nullptr;
    QLabel *m_resultNotice = nullptr;
    QLabel *m_scanNotice = nullptr;
    QLineEdit *m_search = nullptr;
    QCheckBox *m_largeOnly = nullptr;
    QCheckBox *m_logsOnly = nullptr;
    QPushButton *m_scan = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_selectVisible = nullptr;
    QPushButton *m_clearSelection = nullptr;
    QPushButton *m_clean = nullptr;
    QPushButton *m_open = nullptr;
    QPushButton *m_copy = nullptr;
    QPushButton *m_notes = nullptr;
    QProgressBar *m_progress = nullptr;
};
