#include "backend.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace Mux {
static constexpr qsizetype outputLimit = 1024 * 1024;

Backend::Backend(bool demo, QObject *parent) : QObject(parent), m_demo(demo) {
    m_probeTimeout.setSingleShot(true);
    connect(&m_probeTimeout, &QTimer::timeout, this, [this] {
        failProbe(QStringLiteral("timeout"));
        m_probe.kill(); // Read-only status query, never firmware application.
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
        if (error == QProcess::FailedToStart && m_applying) {
            m_applying = false;
            emit busyChanged(false);
            emit applyFinished(false, false, m_apply.errorString());
        }
    });
}

Backend::~Backend() {
    m_destroying = true;
    disconnect(this, nullptr, nullptr, nullptr);
    // Even unexpected orderly application teardown must not let QProcess's
    // destructor kill a firmware transaction. The operation has no UI deadline.
    if (m_apply.state() != QProcess::NotRunning) m_apply.waitForFinished(-1);
    if (m_probe.state() != QProcess::NotRunning) {
        m_probe.kill();
        m_probe.waitForFinished(1000);
    }
}

void Backend::refresh() {
    if (m_applying || refreshing()) return;
    if (m_demo) {
        if (!m_status.valid) m_status = Status::demo();
        emit statusChanged();
        return;
    }
    m_probeOutput.clear();
    m_probeError.clear();
    m_probeFailed = false;
    m_probe.setProgram(m_statusProgram);
    m_probe.setArguments(m_statusArguments);
    m_probe.start(QIODevice::ReadOnly);
    m_probeTimeout.start(m_probeTimeoutMs);
    emit refreshingChanged(true);
}

void Backend::consumeProbe() {
    const auto output = m_probe.readAllStandardOutput();
    const auto error = m_probe.readAllStandardError();
    if (m_probeFailed) return;
    if (output.size() + error.size() + m_probeOutput.size() + m_probeError.size() > outputLimit) {
        failProbe(QStringLiteral("response_too_large"));
        m_probe.kill();
        return;
    }
    m_probeOutput += output;
    m_probeError += error;
}

void Backend::failProbe(QString code) {
    if (m_probeFailed) return;
    m_probeFailed = true;
    m_probeTimeout.stop();
    m_status = {};
    m_status.error = code;
    emit statusChanged();
    emit statusError(code, QString::fromUtf8(m_probeError));
    emit refreshingChanged(false);
}

void Backend::finishProbe(int exitCode, QProcess::ExitStatus exitStatus) {
    m_probeTimeout.stop();
    consumeProbe();
    if (m_probeFailed) return;
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        failProbe(QStringLiteral("status_failed")); return;
    }
    m_status = Status::parse(m_probeOutput);
    if (!m_status.valid) { failProbe(m_status.error); return; }
    emit statusChanged();
    emit refreshingChanged(false);
}

void Backend::apply(Mode mode) {
    if (!m_status.canSwitch(mode, m_applying) || refreshing()) return;
    m_requested = mode;
    m_applying = true;
    emit busyChanged(true);
    if (m_demo) {
        QTimer::singleShot(650, this, [this] {
            m_status.target = m_requested;
            m_status.pendingShutdown = true;
            m_applying = false;
            emit busyChanged(false);
            emit statusChanged();
            emit applyFinished(true, false, QStringLiteral("demo_only"));
        });
        return;
    }
    m_applyOutput.clear();
    m_applyError.clear();
    m_applyOverflow = false;
    m_apply.setProgram(QStringLiteral("/usr/bin/pkexec"));
    m_apply.setArguments({QStringLiteral("/usr/lib/msi-mux/msi-mux-helper"),
                          QStringLiteral("apply"), modeArgument(mode)});
    // No timeout: terminating a firmware transaction could interrupt rollback.
    m_apply.start(QIODevice::ReadOnly);
}

void Backend::consumeApply() {
    if (!m_apply.isOpen()) return;
    const auto output = m_apply.readAllStandardOutput();
    const auto error = m_apply.readAllStandardError();
    const qsizetype remaining = qMax<qsizetype>(0, outputLimit - m_applyOutput.size() - m_applyError.size());
    if (output.size() + error.size() > remaining) m_applyOverflow = true;
    m_applyOutput += output.left(remaining);
    m_applyError += error.left(qMax<qsizetype>(0, remaining - output.size()));
    // Discard excess output without terminating the firmware process.
}

void Backend::finishApply(int exitCode, QProcess::ExitStatus exitStatus) {
    if (!m_applying) return;
    consumeApply();
    bool successEvent = false;
    bool invalidJson = false;
    for (const auto &line : m_applyOutput.split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            invalidJson = true; continue;
        }
        const auto object = document.object();
        const auto data = object.value("data").toObject();
        if (object.value("event").toString() == QLatin1String("success") &&
            data.value("manual_shutdown_required").toBool(false) &&
            parseMode(data.value("mode").toString()) == m_requested) successEvent = true;
    }
    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0 &&
        successEvent && !invalidJson && !m_applyOverflow;
    const bool cancelled = exitStatus == QProcess::NormalExit &&
        (exitCode == 126 || exitCode == 127) && m_applyOutput.trimmed().isEmpty();
    m_applying = false;
    if (success) {
        m_status.target = m_requested;
        m_status.pendingShutdown = true;
    } else {
        // Never leave a stale enabled selector after an uncertain transaction.
        m_status.valid = false;
        m_status.error = QStringLiteral("apply_failed");
    }
    emit busyChanged(false);
    emit statusChanged();
    emit applyFinished(success, cancelled,
        QString::fromUtf8(m_applyOutput + '\n' + m_applyError).trimmed());
    if (!m_destroying) refresh();
}
}
