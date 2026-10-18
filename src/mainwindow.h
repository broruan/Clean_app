#pragma once

#include "types.h"
#include <QFutureWatcher>
#include <QMainWindow>
#include <atomic>

class AppModel; class AppFilter; class FileModel; class DiskRing; class RankChart;
class UninstallController; class QTableView; class QStackedWidget; class QLineEdit;
class QComboBox; class QLabel; class QPushButton; class QProgressBar;
class QPlainTextEdit; class QDialog; class QVBoxLayout; class QSortFilterProxyModel;
class CachePage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void setScanRoot(const QString &path);
    bool isSettled() const;
    bool verifyUi(QString *error);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void createUi();
    QWidget *createOverview();
    QWidget *createAppsPage();
    QWidget *createFilesPage();
    void setPage(int page);
    void reloadInventory();
    void inventoryFinished();
    void refreshStorage();
    void startScan();
    void scanFinished();
    void updateAppSelection();
    void updateFileSelection();
    void updateAppCount();
    void beginUninstall();
    void showRemovalPreview(const QString &summary, const QStringList &affected);
    void openSelectedFileDirectory();
    const AppEntry *selectedApp() const;
    const FileEntry *selectedFile() const;
    void updateOverviewFiles();
    void showWarnings();

    AppModel *m_appModel = nullptr;
    AppFilter *m_appFilter = nullptr;
    FileModel *m_fileModel = nullptr;
    QSortFilterProxyModel *m_fileFilter = nullptr;
    QFutureWatcher<InventoryResult> m_inventoryWatcher;
    QFutureWatcher<ScanResult> m_scanWatcher;
    std::atomic_bool m_cancelScan{false};
    ScanResult m_scanResult;
    QStringList m_warnings;
    bool m_inventoryDone = false, m_scanDone = false;
    bool m_scanActive = false;
    bool m_cacheRequested = false;
    CachePage *m_cachePage = nullptr;
    UninstallController *m_uninstaller = nullptr;
    QStackedWidget *m_pages = nullptr;
    QVector<QPushButton*> m_nav;
    QLabel *m_title = nullptr, *m_subtitle = nullptr, *m_updateLabel = nullptr;
    QPushButton *m_refresh = nullptr, *m_warningButton = nullptr;
    QProgressBar *m_inventoryProgress = nullptr;
    QComboBox *m_volumes = nullptr;
    DiskRing *m_diskRing = nullptr;
    QLabel *m_diskUsed = nullptr, *m_diskAvailable = nullptr, *m_diskTotal = nullptr, *m_diskReserved = nullptr;
    QLabel *m_appSize = nullptr, *m_appSummary = nullptr, *m_largeSize = nullptr, *m_largeSummary = nullptr;
    RankChart *m_rank = nullptr;
    QVBoxLayout *m_recentFiles = nullptr;
    QLabel *m_overviewScanStatus = nullptr;
    QPushButton *m_overviewScan = nullptr;
    QTableView *m_appsTable = nullptr, *m_filesTable = nullptr;
    QLineEdit *m_search = nullptr, *m_scanPath = nullptr, *m_fileSearch = nullptr;
    QComboBox *m_appKind = nullptr, *m_source = nullptr, *m_minSize = nullptr;
    QLabel *m_appCount = nullptr, *m_appDetail = nullptr, *m_fileDetail = nullptr, *m_scanStatus = nullptr, *m_fileCount = nullptr;
    QPushButton *m_remove = nullptr, *m_scanButton = nullptr, *m_cancelButton = nullptr;
    QPushButton *m_openDirectory = nullptr, *m_copyPath = nullptr, *m_browse = nullptr;
    QProgressBar *m_scanProgress = nullptr;
    QDialog *m_operationDialog = nullptr;
    QPlainTextEdit *m_operationLog = nullptr;
    QString m_pendingName;
};
