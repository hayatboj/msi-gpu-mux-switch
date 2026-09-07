#include <QtTest>

#include "poweractions.h"

#include <QSignalSpy>
#include <QTimer>

namespace Mux {
class FakePowerTransport final : public PowerTransport {
public:
    int calls = 0;
    quint64 lastId = 0;
    PowerAction lastAction = PowerAction::Restart;
    void request(quint64 id, PowerAction action) override {
        ++calls;
        lastId = id;
        lastAction = action;
    }
    void reply(const QString &error = {}, const QString &details = {}) { emit completed(lastId, error, details); }
    void stale(quint64 id) { emit completed(id, {}, QStringLiteral("stale reply")); }
};

class PowerTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<PowerAction>();
        qRegisterMetaType<PowerResult>();
    }
    void constructorNeverRequestsPower() {
        auto transport = std::make_unique<FakePowerTransport>();
        auto *fake = transport.get();
        PowerActions power(false, std::move(transport));
        QVERIFY(!power.busy());
        QCOMPARE(fake->calls, 0);
    }
    void demoIsInert_data() {
        QTest::addColumn<PowerAction>("action");
        QTest::newRow("restart") << PowerAction::Restart;
        QTest::newRow("power-off") << PowerAction::PowerOff;
    }
    void demoIsInert() {
        QFETCH(PowerAction, action);
        auto transport = std::make_unique<FakePowerTransport>();
        auto *fake = transport.get();
        PowerActions power(true, std::move(transport));
        QSignalSpy finished(&power, &PowerActions::finished);
        power.request(action);
        QVERIFY(power.busy());
        QCOMPARE(finished.size(), 0);
        QVERIFY(finished.wait(1000));
        QCOMPARE(fake->calls, 0);
        QCOMPARE(qvariant_cast<PowerResult>(finished.first().at(1)), PowerResult::Demo);
        QCOMPARE(qvariant_cast<PowerAction>(finished.first().at(0)), action);
        QVERIFY(!power.busy());
    }
    void explicitActionAndBusySuppression() {
        auto transport = std::make_unique<FakePowerTransport>();
        auto *fake = transport.get();
        PowerActions power(false, std::move(transport));
        QSignalSpy states(&power, &PowerActions::busyChanged);
        QSignalSpy finished(&power, &PowerActions::finished);
        power.request(PowerAction::Restart);
        QCOMPARE(fake->calls, 1);
        QCOMPARE(fake->lastAction, PowerAction::Restart);
        power.request(PowerAction::PowerOff);
        QCOMPARE(fake->calls, 1);
        QCOMPARE(finished.size(), 0);
        fake->reply();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(qvariant_cast<PowerAction>(finished.first().at(0)), PowerAction::Restart);
        QCOMPARE(qvariant_cast<PowerResult>(finished.first().at(1)), PowerResult::Requested);
        QVERIFY(!power.busy());
        QCOMPARE(states.size(), 2);
        QCOMPARE(states.first().first().toBool(), true);
        QCOMPARE(states.last().first().toBool(), false);
        // Ignored clicks are never replayed after completion.
        QCOMPARE(fake->calls, 1);
    }
    void delayedDuplicateDoesNotCompleteNextRequest() {
        auto transport = std::make_unique<FakePowerTransport>();
        auto *fake = transport.get();
        PowerActions power(false, std::move(transport));
        QSignalSpy finished(&power, &PowerActions::finished);
        power.request(PowerAction::Restart);
        const auto first = fake->lastId;
        fake->reply(QStringLiteral("org.freedesktop.PolicyKit1.Error.Cancelled"));
        power.request(PowerAction::PowerOff);
        fake->stale(first);
        QCOMPARE(finished.size(), 1);
        QVERIFY(power.busy());
        fake->reply();
        QCOMPARE(finished.size(), 2);
        QCOMPARE(qvariant_cast<PowerAction>(finished.last().at(0)), PowerAction::PowerOff);
    }
    void errorsNeverTriggerFallback_data() {
        QTest::addColumn<QString>("error");
        QTest::addColumn<PowerResult>("result");
        QTest::newRow("cancelled") << QStringLiteral("org.freedesktop.PolicyKit1.Error.Cancelled") << PowerResult::Cancelled;
        QTest::newRow("canceled") << QStringLiteral("org.gnome.SessionManager.Canceled") << PowerResult::Cancelled;
        QTest::newRow("permission") << QStringLiteral("org.freedesktop.DBus.Error.AccessDenied") << PowerResult::Denied;
        QTest::newRow("authorization") << QStringLiteral("org.freedesktop.PolicyKit1.Error.NotAuthorized") << PowerResult::Denied;
        QTest::newRow("auth-agent") << QStringLiteral("org.freedesktop.DBus.Error.InteractiveAuthorizationRequired") << PowerResult::Denied;
        QTest::newRow("inhibited") << QStringLiteral("org.gnome.SessionManager.Error.Inhibited") << PowerResult::Denied;
        QTest::newRow("unavailable") << QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown") << PowerResult::Unavailable;
        QTest::newRow("missing-method") << QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod") << PowerResult::Unavailable;
        QTest::newRow("disconnected") << QStringLiteral("org.freedesktop.DBus.Error.Disconnected") << PowerResult::Unavailable;
        QTest::newRow("timeout-ambiguous") << QStringLiteral("org.freedesktop.DBus.Error.NoReply") << PowerResult::Failed;
        QTest::newRow("failure") << QStringLiteral("org.gnome.SessionManager.GeneralError") << PowerResult::Failed;
    }
    void errorsNeverTriggerFallback() {
        QFETCH(QString, error);
        QFETCH(PowerResult, result);
        auto transport = std::make_unique<FakePowerTransport>();
        auto *fake = transport.get();
        PowerActions power(false, std::move(transport));
        QSignalSpy finished(&power, &PowerActions::finished);
        power.request(PowerAction::Restart);
        fake->reply(error, QStringLiteral("synthetic denial"));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(qvariant_cast<PowerResult>(finished.first().at(1)), result);
        QVERIFY(finished.first().at(2).toString().contains(error));
        QVERIFY(!power.busy());
        QCOMPARE(fake->calls, 1);
    }
    void missingTransportIsAsynchronousAndUnavailable() {
        PowerActions power(false, std::unique_ptr<PowerTransport>{});
        QSignalSpy finished(&power, &PowerActions::finished);
        power.request(PowerAction::PowerOff);
        QCOMPARE(finished.size(), 0);
        QVERIFY(finished.wait(1000));
        QCOMPARE(qvariant_cast<PowerResult>(finished.first().at(1)), PowerResult::Unavailable);
    }
    void invalidActionDoesNotReachTransport() {
        auto transport = std::make_unique<FakePowerTransport>();
        auto *fake = transport.get();
        PowerActions power(false, std::move(transport));
        QSignalSpy finished(&power, &PowerActions::finished);
        power.request(static_cast<PowerAction>(99));
        QVERIFY(finished.wait(1000));
        QCOMPARE(fake->calls, 0);
        QCOMPARE(qvariant_cast<PowerResult>(finished.first().at(1)), PowerResult::Failed);
    }
    void detailsAreBounded() {
        auto transport = std::make_unique<FakePowerTransport>();
        auto *fake = transport.get();
        PowerActions power(false, std::move(transport));
        QSignalSpy finished(&power, &PowerActions::finished);
        power.request(PowerAction::Restart);
        fake->reply({}, QString(100000, QLatin1Char('x')));
        QCOMPARE(finished.first().at(2).toString().size(), 4096);
    }
    void destructionWithPendingReplyIsSafe() {
        auto transport = std::make_unique<FakePowerTransport>();
        auto *fake = transport.get();
        auto power = std::make_unique<PowerActions>(false, std::move(transport));
        power->request(PowerAction::Restart);
        QPointer<FakePowerTransport> guarded = fake;
        QTimer::singleShot(0, fake, [fake] { fake->reply(); });
        power.reset();
        QVERIFY(guarded.isNull());
        QCoreApplication::processEvents();
    }
    void deletionByBusyObserverDoesNotCallFreedTransport() {
        auto *power = new PowerActions(false, std::make_unique<FakePowerTransport>());
        QPointer<PowerActions> guarded = power;
        connect(power, &PowerActions::busyChanged, power, [power](bool busy) { if (busy) delete power; });
        power->request(PowerAction::Restart);
        QVERIFY(guarded.isNull());
    }
    void deletionByCompletionObserverDoesNotEmitFromFreedObject() {
        auto transport = std::make_unique<FakePowerTransport>();
        auto *fake = transport.get();
        auto *power = new PowerActions(false, std::move(transport));
        QPointer<PowerActions> guarded = power;
        power->request(PowerAction::Restart);
        connect(power, &PowerActions::busyChanged, power, [power](bool busy) { if (!busy) delete power; });
        fake->reply();
        QVERIFY(guarded.isNull());
    }
    void desktopRouting_data() {
        QTest::addColumn<QString>("desktop");
        QTest::addColumn<QStringList>("ownedNames");
        QTest::addColumn<QString>("service");
        const QStringList kde{QStringLiteral("org.kde.ksmserver"), QStringLiteral("org.kde.plasmashell")};
        const QStringList gnome{QStringLiteral("org.gnome.SessionManager")};
        const auto both = kde + gnome;
        QTest::newRow("kde") << QStringLiteral("KDE") << kde << QStringLiteral("org.kde.Shutdown");
        QTest::newRow("plasma") << QStringLiteral("PLASMA") << kde << QStringLiteral("org.kde.Shutdown");
        QTest::newRow("gnome") << QStringLiteral("GNOME") << gnome << QStringLiteral("org.gnome.SessionManager");
        QTest::newRow("gnome-classic") << QStringLiteral("GNOME-Classic") << gnome << QStringLiteral("org.gnome.SessionManager");
        QTest::newRow("trimmed-gnome-classic") << QStringLiteral(" GNOME-Classic : GNOME ") << gnome << QStringLiteral("org.gnome.SessionManager");
        QTest::newRow("trimmed-kde") << QStringLiteral(" KDE ") << kde << QStringLiteral("org.kde.Shutdown");
        QTest::newRow("ubuntu-gnome") << QStringLiteral("ubuntu:GNOME") << gnome << QStringLiteral("org.gnome.SessionManager");
        QTest::newRow("both-kde-selected") << QStringLiteral("KDE") << both << QStringLiteral("org.kde.Shutdown");
        QTest::newRow("both-gnome-selected") << QStringLiteral("GNOME") << both << QStringLiteral("org.gnome.SessionManager");
        QTest::newRow("both-preference-order") << QStringLiteral("GNOME:KDE") << both << QStringLiteral("org.gnome.SessionManager");
        QTest::newRow("active-kde-no-environment") << QString() << kde << QStringLiteral("org.kde.Shutdown");
        QTest::newRow("active-gnome-no-environment") << QString() << gnome << QStringLiteral("org.gnome.SessionManager");
        QTest::newRow("ambiguous") << QString() << both << QString();
        QTest::newRow("unknown-desktop") << QStringLiteral("XFCE") << both << QString();
        QTest::newRow("wrong-session") << QStringLiteral("GNOME") << kde << QString();
        QTest::newRow("selected-kde-missing") << QStringLiteral("KDE:GNOME") << gnome << QString();
        QTest::newRow("none") << QString() << QStringList() << QString();
        QTest::newRow("installed-is-not-active") << QStringLiteral("KDE") << QStringList{QStringLiteral("org.kde.Shutdown")} << QString();
    }
    void desktopRouting() {
        QFETCH(QString, desktop);
        QFETCH(QStringList, ownedNames);
        QFETCH(QString, service);
        for (const auto action : {PowerAction::Restart, PowerAction::PowerOff}) {
            const auto endpoint = selectPowerEndpoint(desktop, ownedNames, action);
            QCOMPARE(endpoint.service, service);
            if (service.isEmpty()) { QVERIFY(!endpoint.valid()); continue; }
            QVERIFY(endpoint.valid());
            if (service == QLatin1String("org.kde.Shutdown")) {
                QCOMPARE(endpoint.path, QStringLiteral("/Shutdown"));
                QCOMPARE(endpoint.interface, QStringLiteral("org.kde.Shutdown"));
                QCOMPARE(endpoint.method, action == PowerAction::Restart ? QStringLiteral("logoutAndReboot") : QStringLiteral("logoutAndShutdown"));
            } else {
                QCOMPARE(endpoint.path, QStringLiteral("/org/gnome/SessionManager"));
                QCOMPARE(endpoint.interface, QStringLiteral("org.gnome.SessionManager"));
                QCOMPARE(endpoint.method, action == PowerAction::Restart ? QStringLiteral("Reboot") : QStringLiteral("Shutdown"));
            }
        }
    }
};
}

QTEST_GUILESS_MAIN(Mux::PowerTests)
#include "test_power.moc"
