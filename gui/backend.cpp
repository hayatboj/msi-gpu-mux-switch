#include "backend.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace Mux {
static constexpr qsizetype outputLimit = 1024 * 1024;
static const QString demoBoot = QStringLiteral("00000000-0000-0000-0000-000000000001");

Backend::Backend(bool demo, QObject *parent) : QObject(parent), m_demo(demo),
    m_selectionStore(demo ? nullptr : std::make_unique<SelectionStore>(SelectionStore::defaultPath())) {
    m_probeTimeout.setSingleShot(true);
    connect(&m_probeTimeout, &QTimer::timeout, this, [this] {
        m_probe.kill(); // Only the unprivileged read-only probe has a deadline.
        failProbe(QStringLiteral("timeout"));
    });
    connect(&m_probe, &QProcess::readyReadStandardOutput, this, &Backend::consumeProbe);
    connect(&m_probe, &QProcess::readyReadStandardError, this, &Backend::consumeProbe);
    connect(&m_probe, &QProcess::finished, this, &Backend::finishProbe);
    connect(&m_probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) failProbe(QStringLiteral("backend_missing"));
    });
    connect(&m_apply, &QProcess::readyReadStandardOutput, this, &Backend::consumeApply);
    connect(&m_apply, &QProcess::readyReadStandardError, this, &Backend::consumeApply);
    connect(&m_apply, &QProcess::finished, this, &Backend::finishApply);
    connect(&m_apply, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_applying && !m_destroying) {
            m_status.valid = false;
            m_status.error = QStringLiteral("apply_failed");
            completeCommit(false, false, m_apply.errorString());
        }
    });
}

Backend::~Backend() {
    m_destroying = true;
    disconnect(this, nullptr, nullptr, nullptr);
    // Never let QProcess's destructor kill an active firmware transaction.
    if (m_apply.state() != QProcess::NotRunning) m_apply.waitForFinished(-1);
    if (m_probe.state() != QProcess::NotRunning) {
        m_probe.kill();
        m_probe.waitForFinished(1000);
    }
}

QString Backend::bootId() const {
    if (m_demo) return demoBoot;
    QFile file(m_bootIdPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto bytes = file.read(129);
    return bytes.size() <= 128 ? QString::fromLatin1(bytes).trimmed() : QString();
}

void Backend::refresh() {
    if (m_applying || refreshing() || m_destroying) return;
    startProbe(Probe::Poll);
}

void Backend::startProbe(Probe purpose) {
    if (m_destroying) return;
    m_probePurpose = purpose;
    m_probeOutput.clear();
    m_probeError.clear();
    m_probeFailed = false;
    m_probing = true;
    const QPointer<Backend> guard(this);
    emit refreshingChanged(true);
    if (!guard) return;
    if (m_demo) {
        QTimer::singleShot(0, this, [this] {
            acceptProbe(m_status.valid ? m_status : Status::demo());
        });
        return;
    }
    m_probe.setProgram(m_statusProgram);
    m_probe.setArguments(m_statusArguments);
    m_probeTimeout.start(m_probeTimeoutMs);
    m_probe.start(QIODevice::ReadOnly);
}

void Backend::consumeProbe() {
    const auto output = m_probe.readAllStandardOutput();
    const auto error = m_probe.readAllStandardError();
    if (m_probeFailed || m_destroying) return;
    if (output.size() + error.size() + m_probeOutput.size() + m_probeError.size() > outputLimit) {
        m_probe.kill();
        failProbe(QStringLiteral("response_too_large"));
        return;
    }
    m_probeOutput += output;
    m_probeError += error;
}

void Backend::failProbe(QString code) {
    if (m_probeFailed || m_destroying) return;
    m_probeFailed = true;
    m_probing = false;
    m_probeTimeout.stop();
    m_status = {};
    m_status.error = code;
    const QPointer<Backend> guard(this);
    emit statusChanged();
    if (!guard) return;
    emit statusError(code, QString::fromUtf8(m_probeError));
    if (!guard) return;
    emit refreshingChanged(false);
    if (!guard) return;
    if (m_applying) completeCommit(false, false, code + QStringLiteral(": ") + QString::fromUtf8(m_probeError));
}

void Backend::finishProbe(int exitCode, QProcess::ExitStatus exitStatus) {
    if (m_destroying) return;
    m_probeTimeout.stop();
    const QPointer<Backend> guard(this);
    consumeProbe();
    if (!guard || m_probeFailed) return;
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        failProbe(QStringLiteral("status_failed")); return;
    }
    const auto status = Status::parse(m_probeOutput);
    if (!status.valid) { failProbe(status.error); return; }
    acceptProbe(status);
}

void Backend::reconcileDraft() {
    bool changed = false;
    if (!m_selectionLoaded) {
        m_selectionLoaded = true;
        if (!m_demo && m_selectionStore && !m_selectionStore->load(m_draft, m_draftError)) {
            emit draftChanged();
            return;
        }
        changed = m_draft.has_value();
    }
    if (m_draft && !m_draft->matchesBaseline(m_status, m_statusBootId)) {
        m_draft.reset();
        m_draftError = QStringLiteral("draft_stale");
        QString removalError;
        if (m_selectionStore && !m_selectionStore->clear(removalError)) m_draftError += QStringLiteral(": ") + removalError;
        changed = true;
    }
    if (changed) emit draftChanged();
}

void Backend::acceptProbe(Status status) {
    if (m_destroying || m_probeFailed) return;
    const auto purpose = m_probePurpose;
    m_probing = false;
    m_status = std::move(status);
    m_statusBootId = bootId();
    const QPointer<Backend> guard(this);
    reconcileDraft();
    if (!guard) return;
    emit statusChanged();
    if (!guard) return;
    emit refreshingChanged(false);
    if (!guard) return;
    if (m_commit == Commit::WaitingForProbe) {
        m_commit = Commit::Preflight;
        startProbe(Probe::BeforeCommit);
    } else if (purpose == Probe::BeforeCommit && m_commit == Commit::Preflight) {
        if (!m_commitSelection || !m_commitSelection->matchesBaseline(m_status, m_statusBootId) ||
            !m_status.canSwitch(m_commitSelection->mode)) {
            completeCommit(false, false, QStringLiteral("draft_preflight_changed"));
            return;
        }
        // Consume the UI preference before spawning the privileged helper.
        // A crash or cancelled authorization must never silently resume it.
        if (m_selectionStore && !m_selectionStore->clear(m_draftError)) {
            completeCommit(false, false, m_draftError);
            return;
        }
        m_draft.reset();
        emit draftChanged();
        if (guard) beginApply();
    } else if (purpose == Probe::AfterCommit && m_commit == Commit::Verifying) {
        const bool verified = m_commitSelection && m_commitSelection->matchesApplied(m_status, m_statusBootId);
        completeCommit(verified, false, verified ? m_commitDetails : QStringLiteral("draft_postflight_mismatch: ") + m_commitDetails);
    }
}

bool Backend::canSelectMode(Mode mode) const {
    if (m_applying || m_finishing || refreshing() || !m_status.valid) return false;
    if (mode == m_status.current) return m_draft && m_draft->matchesBaseline(m_status, m_statusBootId);
    return m_status.canSwitch(mode) && Selection::bind(mode, m_status, m_statusBootId).valid();
}

bool Backend::selectMode(Mode mode) {
    if (!canSelectMode(mode)) { m_draftError = QStringLiteral("draft_selection_blocked"); return false; }
    if (mode == m_status.current) return clearDraft();
    const auto selection = Selection::bind(mode, m_status, m_statusBootId);
    if (selection.bootId != bootId()) { m_draftError = QStringLiteral("draft_stale"); return false; }
    if (m_draft && m_draft->mode == mode && m_draft->matchesBaseline(m_status, m_statusBootId)) {
        m_draftError.clear();
        return true;
    }
    if (m_selectionStore && !m_selectionStore->save(selection, m_draftError)) return false;
    m_selectionLoaded = true;
    m_draftError.clear();
    m_draft = selection;
    emit draftChanged();
    return true;
}

bool Backend::clearDraft() {
    if (m_applying || m_finishing) { m_draftError = QStringLiteral("draft_busy"); return false; }
    if (m_selectionStore && !m_selectionStore->clear(m_draftError)) return false;
    m_selectionLoaded = true;
    m_draft.reset();
    m_draftError.clear();
    emit draftChanged();
    return true;
}

bool Backend::setDemoModes(Mode current, Mode target) {
    if (!m_demo || m_applying || current == Mode::Unknown || target == Mode::Unknown) return false;
    m_status = Status::demo();
    m_status.current = current;
    m_status.target = target;
    m_statusBootId = demoBoot;
    m_status.pendingShutdown = current != target;
    m_status.displays[0].vendor = current == Mode::Discrete ? QStringLiteral("NVIDIA") : QStringLiteral("Intel");
    m_status.displays[0].driver = current == Mode::Discrete ? QStringLiteral("nvidia") : QStringLiteral("i915");
    const QPointer<Backend> guard(this);
    reconcileDraft();
    if (guard) emit statusChanged();
    return true;
}

bool Backend::commitDraft() {
    if (m_applying || m_finishing || m_destroying) return false;
    m_commitSelection = m_draft;
    m_requested = draftMode();
    m_commitDetails.clear();
    m_applying = true;
    m_commit = refreshing() ? Commit::WaitingForProbe : Commit::Preflight;
    const QPointer<Backend> guard(this);
    emit busyChanged(true);
    if (!guard) return true;
    if (!m_commitSelection) {
        QTimer::singleShot(0, this, [this] { completeCommit(false, false, QStringLiteral("draft_missing")); });
    } else if (m_commit == Commit::Preflight) {
        startProbe(Probe::BeforeCommit);
    }
    return true;
}

void Backend::beginApply() {
    m_commit = Commit::Applying;
    if (m_demo) {
        QTimer::singleShot(650, this, [this] {
            if (m_commit != Commit::Applying) return;
            m_status.target = m_requested;
            m_status.pendingShutdown = true;
            m_status.switchingSupported = false;
            m_status.blockCode = QStringLiteral("pending_shutdown");
            m_commitDetails = QStringLiteral("demo_only");
            m_commit = Commit::Verifying;
            startProbe(Probe::AfterCommit);
        });
        return;
    }
    m_applyOutput.clear();
    m_applyError.clear();
    m_applyOverflow = false;
    m_apply.setProgram(QStringLiteral("/usr/bin/pkexec"));
    m_apply.setArguments({QStringLiteral("/usr/lib/msi-mux/msi-mux-helper"), QStringLiteral("apply"), modeArgument(m_requested)});
    // No firmware-process deadline or alternative executable/arguments.
    if (m_testApplyLauncher) m_testApplyLauncher();
    else m_apply.start(QIODevice::ReadOnly);
}

void Backend::consumeApply() {
    if (!m_apply.isOpen()) return;
    const auto output = m_apply.readAllStandardOutput();
    const auto error = m_apply.readAllStandardError();
    const qsizetype remaining = qMax<qsizetype>(0, outputLimit - m_applyOutput.size() - m_applyError.size());
    if (output.size() + error.size() > remaining) m_applyOverflow = true;
    m_applyOutput += output.left(remaining);
    m_applyError += error.left(qMax<qsizetype>(0, remaining - output.size()));
}

void Backend::finishApply(int exitCode, QProcess::ExitStatus exitStatus) {
    if (!m_applying || m_commit != Commit::Applying || m_destroying) return;
    consumeApply();
    bool successEvent = false;
    bool invalidJson = false;
    for (const auto &line : m_applyOutput.split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) { invalidJson = true; continue; }
        const auto object = document.object();
        const auto data = object.value("data").toObject();
        if (object.value("event").toString() == QLatin1String("success") &&
            data.value("manual_shutdown_required").toBool(false) && parseMode(data.value("mode").toString()) == m_requested)
            successEvent = true;
    }
    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0 && successEvent && !invalidJson && !m_applyOverflow;
    const bool cancelled = exitStatus == QProcess::NormalExit && (exitCode == 126 || exitCode == 127) && m_applyOutput.trimmed().isEmpty();
    m_commitDetails = QString::fromUtf8(m_applyOutput + '\n' + m_applyError).trimmed();
    if (!success) {
        m_status.valid = false;
        m_status.error = QStringLiteral("apply_failed");
        completeCommit(false, cancelled, m_commitDetails);
        return;
    }
    // A helper success line alone cannot authorize a desktop power request.
    m_commit = Commit::Verifying;
    startProbe(Probe::AfterCommit);
}

void Backend::completeCommit(bool success, bool cancelled, const QString &details) {
    if (!m_applying || m_destroying) return;
    const QString boundedDetails = details.left(outputLimit);
    m_commit = Commit::Idle;
    m_commitSelection.reset();
    m_applying = false;
    m_finishing = true;
    const QPointer<Backend> guard(this);
    emit statusChanged();
    if (!guard) return;
    emit busyChanged(false);
    if (!guard) return;
    emit applyFinished(success, cancelled, boundedDetails);
    if (guard) {
        m_finishing = false;
        emit draftChanged();
    }
}
}
