#pragma once

#include "status.h"
#include <QObject>
#include <QProcess>
#include <QTimer>

namespace Mux {
class UiTests;

class Backend final : public QObject {
    Q_OBJECT
public:
    explicit Backend(bool demo, QObject *parent = nullptr);
    ~Backend() override;
    bool busy() const { return m_applying; }
    bool refreshing() const { return m_probe.state() != QProcess::NotRunning; }
    bool demo() const { return m_demo; }
    bool canApplyMode(Mode mode) const;
    bool setDemoModes(Mode current, Mode target);
    const Status &status() const { return m_status; }
    void refresh();
    void apply(Mode mode);

signals:
    void statusChanged();
    void refreshingChanged(bool active);
    void busyChanged(bool active);
    void statusError(const QString &code, const QString &details);
    void applyFinished(bool success, bool cancelled, const QString &details);

private:
    friend class UiTests;
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
};
}
