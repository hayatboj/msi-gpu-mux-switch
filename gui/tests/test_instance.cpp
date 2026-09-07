#include <QtTest>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

class InstanceTests final : public QObject {
    Q_OBJECT
private:
    static QProcessEnvironment environment(const QString &directory) {
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        env.insert(QStringLiteral("XDG_RUNTIME_DIR"), directory);
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), directory + QStringLiteral("/config"));
        env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"), QStringLiteral("unix:path=/nonexistent"));
        return env;
    }
private slots:
    void forwardsConfirmationOnlyToExistingInstance() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("msi-mux.socket"));
        QLockFile lock(path + QStringLiteral(".lock"));
        QVERIFY(lock.tryLock());
        QLocalServer server;
        QVERIFY(server.listen(path));
        QByteArray received;
        connect(&server, &QLocalServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QLocalSocket::readyRead, &server, [&, socket] { received += socket->readAll(); });
            received += socket->readAll();
        });
        QProcess child;
        child.setProcessEnvironment(environment(directory.path()));
        child.start(QStringLiteral(MSI_MUX_TRAY_TEST_BINARY), {QStringLiteral("--request-mode"), QStringLiteral("integrated")});
        QVERIFY(child.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        QTRY_COMPARE(received, QByteArray("confirm:integrated\n"));
    }
    void unavailablePrimaryIsReportedAsFailure() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QLockFile lock(directory.filePath(QStringLiteral("msi-mux.socket.lock")));
        QVERIFY(lock.tryLock());
        QProcess child;
        child.setProcessEnvironment(environment(directory.path()));
        child.start(QStringLiteral(MSI_MUX_TRAY_TEST_BINARY), {QStringLiteral("--request-mode"), QStringLiteral("discrete")});
        QVERIFY(child.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5000);
        QCOMPARE(child.exitCode(), 1);
        QVERIFY(child.readAllStandardError().contains("Cannot deliver the request"));
    }
};

QTEST_GUILESS_MAIN(InstanceTests)
#include "test_instance.moc"
