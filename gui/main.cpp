#include "window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QEvent>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QSettings>
#include <QSessionManager>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <unistd.h>

class Application final : public QApplication {
public:
    using QApplication::QApplication;
    std::function<bool()> firmwareBusy;
protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::Quit && firmwareBusy && firmwareBusy()) {
            event->ignore();
            return true;
        }
        return QApplication::event(event);
    }
};

int main(int argc, char *argv[]) {
    if (geteuid() == 0) {
        std::fputs("Run MSI MUX as your normal desktop user. The helper requests authentication when needed.\n"
                   "MSI MUX'u normal masaüstü kullanıcınla çalıştır. Gerektiğinde yardımcı program kimlik doğrulaması ister.\n", stderr);
        return 1;
    }
    Application app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MSI MUX"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0-rc.1"));
    QCoreApplication::setOrganizationName(QStringLiteral("hayatboj"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.hayatboj.msimux"));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("MSI MUX — KDE graphics mode control / KDE grafik modu kontrolü"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("background"), QStringLiteral("Start in the system tray / Sistem tepsisinde başlat")});
    parser.addOption({QStringLiteral("demo"), QStringLiteral("Preview with synthetic data; no firmware writes / Sentetik veriyle önizleme")});
    parser.addOption({QStringLiteral("language"), QStringLiteral("Interface language: en or tr / Arayüz dili: en veya tr"), QStringLiteral("en|tr")});
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("Save demo screenshot and exit (requires --demo) / Demo ekran görüntüsü kaydet"), QStringLiteral("path")});
    parser.process(app);
    const bool demo = parser.isSet(QStringLiteral("demo"));
    if (parser.isSet(QStringLiteral("screenshot")) && !demo) {
        std::fputs("--screenshot requires --demo / --screenshot için --demo gerekir\n", stderr);
        return 2;
    }
    if (parser.isSet(QStringLiteral("language")) && parser.value(QStringLiteral("language")) != QLatin1String("en") &&
        parser.value(QStringLiteral("language")) != QLatin1String("tr")) {
        std::fputs("Language must be en or tr / Dil en veya tr olmalıdır\n", stderr);
        return 2;
    }

    QSettings settings;
    Mux::Language language = Mux::systemLanguage();
    const auto languagePreference = parser.isSet(QStringLiteral("language")) ? parser.value(QStringLiteral("language")) :
        settings.value(QStringLiteral("language")).toString();
    if (languagePreference == QLatin1String("tr")) language = Mux::Language::Turkish;
    else if (languagePreference == QLatin1String("en")) language = Mux::Language::English;
    if (!demo && parser.isSet(QStringLiteral("language"))) settings.setValue(QStringLiteral("language"), languagePreference);

    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty()) runtime = QDir::tempPath() + QStringLiteral("/msi-mux-%1").arg(getuid());
    QDir().mkpath(runtime);
    const auto instanceName = runtime + QStringLiteral("/msi-mux%1.socket").arg(demo ? QStringLiteral("-demo-%1").arg(getpid()) : QString());
    QLockFile lock(instanceName + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!lock.tryLock(0)) {
        QLocalSocket socket;
        socket.connectToServer(instanceName, QIODevice::WriteOnly);
        if (socket.waitForConnected(1500)) {
            socket.write("show\n");
            socket.waitForBytesWritten(1500);
        }
        return 0;
    }
    QLocalServer::removeServer(instanceName);
    if (!server.listen(instanceName)) {
        std::fputs("Cannot initialize local app instance / Yerel uygulama başlatılamadı\n", stderr);
        return 1;
    }

    Mux::Window window(demo, language, !parser.isSet(QStringLiteral("screenshot")));
    app.firmwareBusy = [&window] { return window.backend()->busy(); };
    QObject::connect(&app, &QGuiApplication::commitDataRequest, &window, [&window](QSessionManager &session) {
        if (window.backend()->busy()) session.cancel();
    });
    QObject::connect(&app, &QGuiApplication::saveStateRequest, &window, [&window](QSessionManager &session) {
        if (window.backend()->busy()) session.cancel();
    });
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            // The only local IPC action is opening the existing window.
            socket->close();
            socket->deleteLater();
            window.showPanel();
        }
    });
    if (!parser.isSet(QStringLiteral("background")) || !window.hasTray()) window.showPanel();
    if (parser.isSet(QStringLiteral("screenshot"))) {
        window.resize(680, 900);
        window.showPanel();
        const QString output = parser.value(QStringLiteral("screenshot"));
        QTimer::singleShot(450, &window, [&app, &window, output] {
            const bool saved = window.grab().save(output);
            app.exit(saved ? 0 : 1);
        });
    }
    const int result = app.exec();
    app.firmwareBusy = {};
    return result;
}
