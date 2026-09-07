#pragma once

#include "status.h"
#include "selection.h"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <memory>
#include <functional>

namespace Mux {
class UiTests;
class SelectionTests;

class Backend final : public QObject {
    Q_OBJECT
public:
    explicit Backend(bool demo, QObject *parent = nullptr);
    ~Backend() override;
    bool busy() const { return m_applying; }
    bool refreshing() const { return m_probing || m_probe.state() != QProcess::NotRunning; }
    bool demo() const { return m_demo; }
    bool hasDraft() const { return m_draft.has_value(); }
    Mode draftMode() const { return m_draft ? m_draft->mode : Mode::Unknown; }
    QString draftError() const { return m_draftError; }
    bool canSelectMode(Mode mode) const;
    bool selectMode(Mode mode);
    bool clearDraft();
    bool commitDraft();
    bool setDemoModes(Mode current, Mode target);
    const Status &status() const { return m_status; }
    void refresh();

signals:
    void statusChanged();
    void refreshingChanged(bool active);
    void busyChanged(bool active);
    void statusError(const QString &code, const QString &details);
    void applyFinished(bool success, bool cancelled, const QString &details);
    void draftChanged();

private:
    friend class UiTests;
    friend class SelectionTests;
    enum class Probe { Poll, BeforeCommit, AfterCommit };
    enum class Commit { Idle, WaitingForProbe, Preflight, Applying, Verifying };
    void startProbe(Probe purpose);
    void acceptProbe(Status status);
    QString bootId() const;
    void reconcileDraft();
    void beginApply();
    void completeCommit(bool success, bool cancelled, const QString &details);
    void consumeProbe();
    void finishProbe(int exitCode, QProcess::ExitStatus exitStatus);
    void failProbe(QString code);
    void consumeApply();
    void finishApply(int exitCode, QProcess::ExitStatus exitStatus);
    bool m_demo;
    bool m_applying = false;
    bool m_probeFailed = false;
    bool m_applyOverflow = false;
    bool m_destroying = false;
    bool m_finishing = false;
    bool m_probing = false;
    bool m_selectionLoaded = false;
    Probe m_probePurpose = Probe::Poll;
    Commit m_commit = Commit::Idle;
    std::unique_ptr<SelectionStore> m_selectionStore;
    std::optional<Selection> m_draft;
    std::optional<Selection> m_commitSelection;
    QString m_draftError;
    QString m_statusBootId;
    QString m_commitDetails;
    Status m_status;
    Mode m_requested = Mode::Unknown;
    QProcess m_probe;
    QProcess m_apply;
    QTimer m_probeTimeout;
    QByteArray m_probeOutput;
    QByteArray m_probeError;
    QByteArray m_applyOutput;
    QByteArray m_applyError;
    // Only the unit-test friend may replace the read-only probe, never the privileged helper.
    QString m_statusProgram = QStringLiteral("/usr/bin/msi-mux-switch");
    QStringList m_statusArguments{QStringLiteral("--status"), QStringLiteral("--json")};
    int m_probeTimeoutMs = 10000;
    QString m_bootIdPath = QStringLiteral("/proc/sys/kernel/random/boot_id");
    // Only test friends can replace process launch with an inert callback.
    // No CLI, environment, selection-file or production setter controls it.
    std::function<void()> m_testApplyLauncher;
};
}
