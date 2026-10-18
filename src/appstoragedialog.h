#pragma once

#include "appstorage.h"
#include <QDialog>
#include <QFutureWatcher>
#include <atomic>

class QLabel;
class QTableWidget;
class QPushButton;

class AppStorageDialog final : public QDialog {
    Q_OBJECT
public:
    explicit AppStorageDialog(const AppEntry &app, QWidget *parent = nullptr,
        QString home = {}, QString cacheRoot = {}, QString configRoot = {});
    ~AppStorageDialog() override;
    bool isSettled() const;

private:
    void updateSelection();
    void showResult();
    AppStorageResult m_result;
    QFutureWatcher<AppStorageResult> m_watcher;
    std::atomic_bool m_cancel{false};
    QLabel *m_status = nullptr;
    QLabel *m_detail = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_open = nullptr;
    QPushButton *m_copy = nullptr;
    QPushButton *m_cancelButton = nullptr;
    bool m_settled = false;
};
