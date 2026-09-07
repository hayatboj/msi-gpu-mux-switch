#include "poweractions.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QTimer>
#include <QPointer>

namespace Mux {
namespace {
constexpr int discoveryTimeoutMs = 5000;
constexpr int requestTimeoutMs = 30000;
constexpr qsizetype detailsLimit = 4096;

bool validAction(PowerAction action) {
    return action == PowerAction::Restart || action == PowerAction::PowerOff;
}

PowerEndpoint kdeEndpoint(PowerAction action) {
    // The same confirmed-action API used by KDE's SessionManagement. KSMServer
    // and KWin retain responsibility for closing apps and session cancellation.
    // https://invent.kde.org/plasma/plasma-workspace/-/blob/master/libkworkspace/sessionmanagement.cpp
    return {QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"),
            QStringLiteral("org.kde.Shutdown"),
            action == PowerAction::Restart ? QStringLiteral("logoutAndReboot") : QStringLiteral("logoutAndShutdown")};
}

PowerEndpoint gnomeEndpoint(PowerAction action) {
    // GNOME documents these as requests for native reboot/shutdown dialogs.
    // It owns inhibitor handling, authorization, and subsequent cancellation.
    // https://gnome.pages.gitlab.gnome.org/gnome-session/re04.html
    return {QStringLiteral("org.gnome.SessionManager"), QStringLiteral("/org/gnome/SessionManager"),
            QStringLiteral("org.gnome.SessionManager"),
            action == PowerAction::Restart ? QStringLiteral("Reboot") : QStringLiteral("Shutdown")};
}

class SessionPowerTransport final : public PowerTransport {
public:
    void request(quint64 requestId, PowerAction action) override {
        const auto bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) {
            // Keep even immediate failures asynchronous and safe if a signal
            // handler closes its owner.
            QTimer::singleShot(0, this, [this, requestId] {
                emit completed(requestId, QStringLiteral("org.freedesktop.DBus.Error.Disconnected"),
                               QStringLiteral("The desktop session bus is unavailable. Use the desktop power menu."));
            });
            return;
        }
        const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
        const auto discovery = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ListNames"));
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(discovery, discoveryTimeoutMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, requestId, action, desktop, bus](QDBusPendingCallWatcher *pending) {
            QDBusPendingReply<QStringList> reply = *pending;
            pending->deleteLater();
            if (reply.isError()) {
                emit completed(requestId, reply.error().name(),
                    QStringLiteral("Cannot identify the active desktop session: %1").arg(reply.error().message()));
                return;
            }
            const auto endpoint = selectPowerEndpoint(desktop, reply.value(), action);
            if (!endpoint.valid()) {
                emit completed(requestId, QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown"),
                    QStringLiteral("No supported power interface belongs to this active desktop. Use its normal power menu."));
                return;
            }
            auto message = QDBusMessage::createMethodCall(endpoint.service, endpoint.path, endpoint.interface, endpoint.method);
            message.setInteractiveAuthorizationAllowed(true);
            // No force arguments, direct logind calls, shell commands, retries,
            // or fallback to a different desktop after rejection/cancellation.
            auto *operation = new QDBusPendingCallWatcher(bus.asyncCall(message, requestTimeoutMs), this);
            connect(operation, &QDBusPendingCallWatcher::finished, this, [this, requestId](QDBusPendingCallWatcher *call) {
                const QDBusMessage reply = call->reply();
                call->deleteLater();
                if (reply.type() == QDBusMessage::ErrorMessage) {
                    QString details = reply.errorMessage();
                    if (reply.errorName() == QLatin1String("org.freedesktop.DBus.Error.NoReply") ||
                        reply.errorName() == QLatin1String("org.freedesktop.DBus.Error.Timeout")) {
                        details = QStringLiteral("The desktop did not confirm the request in time; its outcome is unknown. No fallback was attempted. %1").arg(details);
                    }
                    emit completed(requestId, reply.errorName(), details);
                } else if (reply.type() != QDBusMessage::ReplyMessage || !reply.arguments().isEmpty()) {
                    emit completed(requestId, QStringLiteral("org.freedesktop.DBus.Error.InvalidSignature"),
                        QStringLiteral("Unexpected desktop reply. The request may already be in progress; no fallback was attempted."));
                } else {
                    emit completed(requestId, {},
                        QStringLiteral("The desktop accepted the request. It may still show an authorization, unsaved-work, or cancellation dialog. This does not verify a restart or the new GPU mode."));
                }
            });
        });
    }
};
}

PowerEndpoint selectPowerEndpoint(const QString &desktop, const QStringList &ownedNames,
                                  PowerAction action) {
    if (!validAction(action)) return {};
    const auto desktops = desktop.toUpper().split(QLatin1Char(':'), Qt::SkipEmptyParts);
    const bool kdeActive = ownedNames.contains(QStringLiteral("org.kde.ksmserver")) ||
        ownedNames.contains(QStringLiteral("org.kde.plasmashell"));
    const bool gnomeActive = ownedNames.contains(QStringLiteral("org.gnome.SessionManager"));
    // XDG_CURRENT_DESKTOP is an ordered preference list. Never choose an
    // unrelated desktop simply because its package/service is installed.
    for (const auto &entry : desktops) {
        const auto item = entry.trimmed();
        if (item == QLatin1String("KDE") || item == QLatin1String("PLASMA"))
            return kdeActive ? kdeEndpoint(action) : PowerEndpoint{};
        if (item == QLatin1String("GNOME") || item == QLatin1String("GNOME-CLASSIC"))
            return gnomeActive ? gnomeEndpoint(action) : PowerEndpoint{};
    }
    if (!desktop.trimmed().isEmpty()) return {};
    // With no declared desktop, a single active session is unambiguous. Merely
    // activatable org.kde.Shutdown is not enough to select KDE.
    if (kdeActive == gnomeActive) return {};
    return kdeActive ? kdeEndpoint(action) : gnomeEndpoint(action);
}

PowerResult classifyPowerError(const QString &errorName) {
    if (errorName.isEmpty()) return PowerResult::Requested;
    const auto name = errorName.toLower();
    if (name.endsWith(QLatin1String(".cancelled")) || name.endsWith(QLatin1String(".canceled")))
        return PowerResult::Cancelled;
    if (name.endsWith(QLatin1String(".accessdenied")) || name.endsWith(QLatin1String(".notauthorized")) ||
        name.endsWith(QLatin1String(".authfailed")) || name.endsWith(QLatin1String(".notprivileged")) ||
        name.endsWith(QLatin1String(".interactiveauthorizationrequired")) || name.endsWith(QLatin1String(".inhibited")) ||
        name.endsWith(QLatin1String(".operationinprogress")))
        return PowerResult::Denied;
    if (name.endsWith(QLatin1String(".serviceunknown")) || name.endsWith(QLatin1String(".namehasnoowner")) ||
        name.endsWith(QLatin1String(".unknownmethod")) || name.endsWith(QLatin1String(".unknownobject")) ||
        name.endsWith(QLatin1String(".unknowninterface")) || name.endsWith(QLatin1String(".disconnected")) ||
        name.endsWith(QLatin1String(".notsupported")))
        return PowerResult::Unavailable;
    return PowerResult::Failed;
}

PowerActions::PowerActions(bool demo, QObject *parent)
    : PowerActions(demo, demo ? nullptr : std::make_unique<SessionPowerTransport>(), parent) {}

PowerActions::PowerActions(bool demo, std::unique_ptr<PowerTransport> transport, QObject *parent)
    : QObject(parent), m_demo(demo), m_transport(std::move(transport)) {
    if (m_transport) {
        connect(m_transport.get(), &PowerTransport::completed, this,
            [this](quint64 id, const QString &error, const QString &details) {
                complete(id, classifyPowerError(error), error.isEmpty() ? details : error + QStringLiteral(": ") + details);
            });
    }
}

PowerActions::~PowerActions() = default;

void PowerActions::request(PowerAction action) {
    if (m_busy) return; // One user choice per in-flight request, with no queue.
    m_action = action;
    const quint64 id = ++m_requestId;
    m_busy = true;
    const QPointer<PowerActions> guard(this);
    emit busyChanged(true);
    if (!guard) return;
    if (!validAction(action)) {
        QTimer::singleShot(0, this, [this, id] { complete(id, PowerResult::Failed, QStringLiteral("Unsupported power action.")); });
    } else if (m_demo) {
        QTimer::singleShot(0, this, [this, id] { complete(id, PowerResult::Demo, QStringLiteral("Demo only; no desktop power request was sent.")); });
    } else if (!m_transport) {
        QTimer::singleShot(0, this, [this, id] { complete(id, PowerResult::Unavailable, QStringLiteral("Desktop power transport is unavailable.")); });
    } else {
        m_transport->request(id, action);
    }
}

void PowerActions::complete(quint64 id, PowerResult result, const QString &details) {
    if (!m_busy || id != m_requestId) return;
    const PowerAction action = m_action;
    const QString boundedDetails = details.left(detailsLimit);
    m_busy = false;
    const QPointer<PowerActions> guard(this);
    emit busyChanged(false);
    if (!guard) return;
    emit finished(action, result, boundedDetails);
}
}
