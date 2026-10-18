#pragma once

#include "types.h"

#include <QObject>
#include <QProcess>
#include <QTimer>

// All commands use separate argv entries. No user text is evaluated by a shell.
// The owning window must refuse to close while isExecuting() is true: interrupting
// dpkg while it writes the package database can leave a partially applied removal.
class UninstallController : public QObject {
    Q_OBJECT
public:
    explicit UninstallController(QObject *parent = nullptr);
    ~UninstallController() override;

    bool isBusy() const { return m_busy; }
    bool isExecuting() const { return m_executing; }
    void preview(const AppEntry &app);
    void execute();
    void cancelPreview();

signals:
    void previewReady(const QString &summary, const QStringList &affectedPackages);
    void logMessage(const QString &message);
    void finished(bool success, const QString &message);
    void busyChanged(bool busy);

private:
    enum class Stage { Idle, TargetMetadata, AptSimulation, AffectedMetadata,
                       SnapInfo, FlatpakInfo, Removing };
    void startPreview(bool beforeExecution);
    void run(Stage stage, const QString &program, const QStringList &arguments);
    void processFinished(int exitCode, QProcess::ExitStatus status);
    void consumeOutput();
    void timeout();
    void fail(const QString &message);
    void setBusy(bool busy);
    void finishPreview();
    void startRemoval();
    bool readDebMetadata(bool target);
    bool validTarget(QString *reason) const;
    static QString metadataFormat();
    static QString criticalDebReason(const QString &name);

    QProcess *m_process = nullptr;
    QTimer m_timer;
    Stage m_stage = Stage::Idle;
    AppEntry m_app;
    QString m_aptTarget;
    QString m_flatpakRef;
    QString m_output;
    QStringList m_pendingPackages;
    QStringList m_plannedPackages;
    QStringList m_pendingState;
    QStringList m_plannedState;
    bool m_busy = false;
    bool m_executing = false;
    bool m_beforeExecution = false;
    bool m_previewValid = false;
    bool m_outputTruncated = false;
};
