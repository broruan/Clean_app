#pragma once

#include "developerbackend.h"
#include <QFutureWatcher>
#include <QWidget>
#include <atomic>

class DeveloperModel;
class DeveloperFilter;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableView;

class DeveloperPage final : public QWidget {
    Q_OBJECT
public:
    explicit DeveloperPage(QWidget *parent = nullptr);
    ~DeveloperPage() override;
    void setRoot(QString root);
    void ensureScanStarted();
    void rescan();
    void cancelWork();
    bool isBusy() const;
    bool isSettled() const;
    bool verifyUi(QString *error);
    bool exportReport(const QString &path, QString *error = nullptr) const;

private:
    void createUi();
    void startScan();
    void scanFinished();
    void updateControls();
    void updateDetail();
    void updateCount();
    const DeveloperEntry *currentEntry() const;

    QString m_root;
    DeveloperScanResult m_result;
    QFutureWatcher<DeveloperScanResult> m_watcher;
    std::atomic_bool m_cancel{false};
    bool m_started = false, m_scanning = false, m_done = false, m_shuttingDown = false;
    quint64 m_operation = 0;
    DeveloperModel *m_model = nullptr;
    DeveloperFilter *m_filter = nullptr;
    QTableView *m_table = nullptr;
    QLineEdit *m_search = nullptr;
    QLineEdit *m_rootField = nullptr;
    QComboBox *m_category = nullptr;
    QLabel *m_total = nullptr;
    QLabel *m_identified = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_notice = nullptr;
    QLabel *m_count = nullptr;
    QLabel *m_detail = nullptr;
    QPushButton *m_choose = nullptr;
    QPushButton *m_scan = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_open = nullptr;
    QPushButton *m_copy = nullptr;
    QPushButton *m_export = nullptr;
    QProgressBar *m_progress = nullptr;
};
