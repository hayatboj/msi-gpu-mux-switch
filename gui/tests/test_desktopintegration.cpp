#include "desktopintegration.h"

#include <QtTest>

namespace Mux {

class DesktopIntegrationTests final : public QObject {
    Q_OBJECT
private slots:
    void classifyDesktop_data() {
        QTest::addColumn<QString>("desktop");
        QTest::addColumn<QString>("session");
        QTest::addColumn<bool>("expected");
        QTest::newRow("gnome") << "GNOME" << "" << true;
        QTest::newRow("ubuntu-composite") << "ubuntu:GNOME" << "ubuntu" << true;
        QTest::newRow("classic-composite") << "GNOME-Classic:GNOME" << "gnome-classic" << true;
        QTest::newRow("classic-alone") << "GNOME-Classic" << "" << true;
        QTest::newRow("case-and-space") << " : ubuntu : gNoMe : " << "" << true;
        QTest::newRow("kde") << "KDE" << "plasma" << false;
        QTest::newRow("kde-stale-session") << "KDE" << "gnome" << false;
        QTest::newRow("xfce") << "XFCE" << "xfce" << false;
        QTest::newRow("cinnamon") << "X-Cinnamon" << "cinnamon" << false;
        QTest::newRow("substring-prefix") << "NotGNOME" << "" << false;
        QTest::newRow("substring-suffix") << "GNOMEish" << "" << false;
        QTest::newRow("classic-substring") << "GNOME-Classicish" << "" << false;
        QTest::newRow("shell-text") << "GNOME; echo unsafe" << "" << false;
        QTest::newRow("missing") << "" << "" << false;
        QTest::newRow("session-gnome") << "" << "gnome" << true;
        QTest::newRow("session-wayland") << "" << "gnome-wayland" << true;
        QTest::newRow("session-xorg") << "" << "GNOME-XORG" << true;
        QTest::newRow("session-classic") << "" << "gnome-classic" << true;
        QTest::newRow("session-ubuntu") << "" << "ubuntu" << true;
        QTest::newRow("session-ubuntu-wayland") << "" << "ubuntu-wayland" << true;
        QTest::newRow("session-ubuntu-xorg") << "" << "ubuntu-xorg" << true;
        QTest::newRow("empty-components-fallback") << " : : " << "gnome" << true;
        QTest::newRow("session-substring") << "" << "gnome-custom" << false;
        QTest::newRow("current-unknown-wins") << "unknown" << "ubuntu" << false;
    }

    void classifyDesktop() {
        QFETCH(QString, desktop);
        QFETCH(QString, session);
        QFETCH(bool, expected);
        QCOMPARE(DesktopIntegration::isGnome(desktop, session), expected);
    }

    void setupDependsOnActualTrayAvailability() {
        auto state = DesktopIntegration::detect(false, QStringLiteral("GNOME"), QString());
        QVERIFY(state.gnome);
        QVERIFY(!state.trayAvailable);
        QVERIFY(state.needsGnomeSetup());
        state = DesktopIntegration::detect(true, QStringLiteral("GNOME"), QString());
        QVERIFY(state.gnome);
        QVERIFY(state.trayAvailable);
        QVERIFY(!state.needsGnomeSetup());
        state = DesktopIntegration::detect(false, QStringLiteral("KDE"), QString());
        QVERIFY(!state.gnome);
        QVERIFY(!state.needsGnomeSetup());
        QVERIFY(!DesktopIntegration{}.needsGnomeSetup());
    }

    void setupUrlIsFixedOfficialPage() {
        const auto url = DesktopIntegration::gnomeSetupUrl();
        QVERIFY(url.isValid());
        QCOMPARE(url.toString(), QStringLiteral("https://extensions.gnome.org/extension/615/appindicator-support/"));
        QCOMPARE(url.scheme(), QStringLiteral("https"));
        QVERIFY(url.userInfo().isEmpty());
        QVERIFY(url.query().isEmpty());
        QVERIFY(url.fragment().isEmpty());
    }
};

}

QTEST_GUILESS_MAIN(Mux::DesktopIntegrationTests)
#include "test_desktopintegration.moc"
