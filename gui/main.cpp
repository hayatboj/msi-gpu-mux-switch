#include "window.h"
#include "aboutdialog.h"

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
#include <memory>
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
    QCoreApplication::setApplicationVersion(QStringLiteral(MSI_MUX_VERSION));
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
    parser.addOption({QStringLiteral("request-mode"), QStringLiteral("Open mode confirmation: mshybrid, discrete, integrated / Mod onayını aç"), QStringLiteral("mode")});
    parser.addOption({QStringLiteral("demo-mode"), QStringLiteral("Synthetic current mode (requires --demo)"), QStringLiteral("mode")});
    parser.addOption({QStringLiteral("demo-target"), QStringLiteral("Synthetic target mode (requires --demo)"), QStringLiteral("mode")});
    parser.addOption({QStringLiteral("screenshot-page"), QStringLiteral("Demo screenshot page: main, about, changes"), QStringLiteral("page"), QStringLiteral("main")});
    parser.process(app);
    const bool demo = parser.isSet(QStringLiteral("demo"));
    const auto parseArgumentMode = [](const QString &value) {
        return value == QLatin1String("mshybrid") || value == QLatin1String("hybrid") ? Mux::Mode::Hybrid : Mux::parseMode(value);
    };
    for (const auto &option : {QStringLiteral("request-mode"), QStringLiteral("demo-mode"), QStringLiteral("demo-target")}) {
        if (parser.isSet(option) && parseArgumentMode(parser.value(option)) == Mux::Mode::Unknown) {
            std::fputs("Mode must be mshybrid, discrete or integrated / Geçersiz mod\n", stderr);
            return 2;
        }
    }
    if ((!demo && (parser.isSet(QStringLiteral("demo-mode")) || parser.isSet(QStringLiteral("demo-target")))) ||
        (parser.isSet(QStringLiteral("screenshot-page")) && !parser.isSet(QStringLiteral("screenshot")))) {
        std::fputs("Synthetic preview options require --demo and screenshot pages require --screenshot\n", stderr);
        return 2;
    }
    const auto page = parser.value(QStringLiteral("screenshot-page"));
    if (page != QLatin1String("main") && page != QLatin1String("about") && page != QLatin1String("changes")) return 2;
    if (parser.isSet(QStringLiteral("request-mode")) && parser.isSet(QStringLiteral("screenshot"))) return 2;
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
        if (lock.error() != QLockFile::LockFailedError) {
            std::fputs("Cannot access the app instance lock / Uygulama kilidine erişilemiyor\n", stderr);
            return 1;
        }
        QLocalSocket socket;
        socket.connectToServer(instanceName, QIODevice::WriteOnly);
        if (socket.waitForConnected(1500)) {
            const auto request = parser.isSet(QStringLiteral("request-mode")) ?
                QByteArray("confirm:") + Mux::modeArgument(parseArgumentMode(parser.value(QStringLiteral("request-mode")))).toLatin1() + '\n' : QByteArray("show\n");
            const bool queued = socket.write(request) == request.size();
            const bool delivered = queued && (socket.bytesToWrite() == 0 || socket.waitForBytesWritten(1500)) && socket.bytesToWrite() == 0;
            if (delivered) {
                socket.disconnectFromServer();
                return 0;
            }
        }
        std::fputs("Cannot deliver the request to the running MSI MUX app / İstek açık MSI MUX uygulamasına iletilemedi\n", stderr);
        return 1;
    }
    QLocalServer::removeServer(instanceName);
    if (!server.listen(instanceName)) {
        std::fputs("Cannot initialize local app instance / Yerel uygulama başlatılamadı\n", stderr);
        return 1;
    }

    Mux::Window window(demo, language, !parser.isSet(QStringLiteral("screenshot")));
    if (demo && (parser.isSet(QStringLiteral("demo-mode")) || parser.isSet(QStringLiteral("demo-target")))) {
        const auto current = parser.isSet(QStringLiteral("demo-mode")) ? parseArgumentMode(parser.value(QStringLiteral("demo-mode"))) : Mux::Mode::Hybrid;
        const auto target = parser.isSet(QStringLiteral("demo-target")) ? parseArgumentMode(parser.value(QStringLiteral("demo-target"))) : current;
        window.backend()->setDemoModes(current, target);
    }
    app.firmwareBusy = [&window] { return window.backend()->busy(); };
    QObject::connect(&app, &QGuiApplication::commitDataRequest, &window, [&window](QSessionManager &session) {
        if (window.backend()->busy()) session.cancel();
    });
    QObject::connect(&app, &QGuiApplication::saveStateRequest, &window, [&window](QSessionManager &session) {
        if (window.backend()->busy()) session.cancel();
    });
    const auto activeClients = std::make_shared<int>(0);
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&, activeClients] {
        while (auto *socket = server.nextPendingConnection()) {
            if (*activeClients >= 16) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            ++*activeClients;
            QObject::connect(socket, &QObject::destroyed, &server, [activeClients] { --*activeClients; });
            socket->setReadBufferSize(128);
            auto *deadline = new QTimer(socket);
            deadline->setSingleShot(true);
            QObject::connect(deadline, &QTimer::timeout, socket, [socket] { socket->abort(); socket->deleteLater(); });
            deadline->start(1500);
            const auto consume = [&window, socket, deadline, parseArgumentMode] {
                if (!socket->canReadLine()) return;
                const auto request = socket->readLine(128);
                deadline->stop();
                socket->disconnectFromServer();
                socket->deleteLater();
                if (request == "show\n") window.showPanel();
                else if (request.startsWith("confirm:") && request.endsWith('\n')) {
                    const auto mode = parseArgumentMode(QString::fromLatin1(request.mid(8).trimmed()));
                    if (mode != Mux::Mode::Unknown)
                        QTimer::singleShot(0, &window, [&window, mode] { window.requestMode(mode); });
                }
            };
            QObject::connect(socket, &QLocalSocket::readyRead, &window, consume);
            consume();
        }
    });
    if (!parser.isSet(QStringLiteral("background")) || !window.hasTray()) window.showPanel();
    if (parser.isSet(QStringLiteral("request-mode"))) {
        const auto mode = parseArgumentMode(parser.value(QStringLiteral("request-mode")));
        QTimer::singleShot(0, &window, [&window, mode] { window.requestMode(mode); });
    }
    if (parser.isSet(QStringLiteral("screenshot"))) {
        window.resize(680, 900);
        window.showPanel();
        const QString output = parser.value(QStringLiteral("screenshot"));
        QWidget *capture = &window;
        if (page != QLatin1String("main")) {
            auto *dialog = new Mux::AboutDialog(language, window.backend()->status(), &window, page == QLatin1String("changes"));
            dialog->show();
            capture = dialog;
        }
        QTimer::singleShot(450, &window, [&app, capture, output] {
            const bool saved = capture->grab().save(output);
            app.exit(saved ? 0 : 1);
        });
    }
    const int result = app.exec();
    app.firmwareBusy = {};
    return result;
}
