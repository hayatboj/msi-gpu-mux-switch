#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

namespace Mux {

enum class PowerAction { Restart, PowerOff };
// Requested means accepted by the desktop session API, never that the machine
// rebooted or that its physical MUX route changed. Native dialogs can still be
// cancelled after their D-Bus request was accepted.
enum class PowerResult { Requested, Cancelled, Denied, Unavailable, Failed, Demo };

struct PowerEndpoint {
    QString service;
    QString path;
    QString interface;
    QString method;
    bool valid() const { return !service.isEmpty(); }
};

// Pure selection from the intended desktop and currently owned session names.
// No service activation, system bus call, environment mutation, or power action.
PowerEndpoint selectPowerEndpoint(const QString &desktop, const QStringList &ownedNames,
                                  PowerAction action);
PowerResult classifyPowerError(const QString &errorName);

// Injectable asynchronous transport. Production uses the session bus only.
// Request IDs prevent delayed/duplicate replies from completing another action.
class PowerTransport : public QObject {
    Q_OBJECT
public:
    explicit PowerTransport(QObject *parent = nullptr) : QObject(parent) {}
    ~PowerTransport() override = default;
    virtual void request(quint64 requestId, PowerAction action) = 0;

signals:
    void completed(quint64 requestId, const QString &errorName, const QString &details);
};

class PowerActions final : public QObject {
    Q_OBJECT
public:
    explicit PowerActions(bool demo, QObject *parent = nullptr);
    PowerActions(bool demo, std::unique_ptr<PowerTransport> transport, QObject *parent = nullptr);
    ~PowerActions() override;
    bool busy() const { return m_busy; }
    bool demo() const { return m_demo; }

    // Call only after an explicit user choice. Constructing this object, polling
    // status, changing a GPU target, or dismissing a dialog never calls request().
    // Never call while a firmware transaction is running.
    void request(PowerAction action);

signals:
    void busyChanged(bool active);
    void finished(Mux::PowerAction action, Mux::PowerResult result, const QString &details);

private:
    void complete(quint64 requestId, PowerResult result, const QString &details);
    bool m_demo;
    bool m_busy = false;
    quint64 m_requestId = 0;
    PowerAction m_action = PowerAction::Restart;
    std::unique_ptr<PowerTransport> m_transport;
};
}

Q_DECLARE_METATYPE(Mux::PowerAction)
Q_DECLARE_METATYPE(Mux::PowerResult)
