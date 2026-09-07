#include "backend.h"
#include "selection.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace Mux {
class SelectionTests final : public QObject {
    Q_OBJECT
    QTemporaryDir m_isolation;
    static QString boot() { return QStringLiteral("11111111-2222-3333-4444-555555555555"); }
    static QByteArray fixture(Mode current = Mode::Hybrid, Mode target = Mode::Hybrid, bool pending = false) {
        QJsonObject value{{"length",20},{"attributes","0x00000007"},
            {"current_mode",current == Mode::Hybrid ? "ms-hybrid" : modeArgument(current)},
            {"selected_target_mode",target == Mode::Hybrid ? "ms-hybrid" : modeArgument(target)},
            {"new_switch_supported",true},{"discrete_supported",true},{"integrated_supported",true}};
        return QJsonDocument(QJsonObject{{"schema_version",1},
            {"machine",QJsonObject{{"model","Vector 16 HX AI A2XWIG"},{"board","MS-15M3"},{"bios","E15M3IMS.116"},{"expected_hardware",true}}},
            {"firmware",QJsonObject{{"available",true},{"value",value}}}, {"ac_power_online",true},
            {"switching_supported",!pending},{"switching_block_code",pending ? "pending_shutdown" : ""},
            {"pending_shutdown",pending}}).toJson(QJsonDocument::Compact);
    }
    static void write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(contents), contents.size());
    }
    static void configure(Backend &backend, QTemporaryDir &directory, const QByteArray &status = fixture()) {
        backend.setProperty("testHelperRequests", 0);
        backend.m_testApplyLauncher = [&backend] {
            backend.setProperty("testHelperRequests", backend.property("testHelperRequests").toInt() + 1);
            QTimer::singleShot(0, &backend, [&backend] {
                backend.completeCommit(false, false, QStringLiteral("synthetic_noexec"));
            });
        };
        backend.m_selectionStore = std::make_unique<SelectionStore>(directory.filePath(QStringLiteral("selection.json")));
        backend.m_bootIdPath = directory.filePath(QStringLiteral("boot"));
        write(backend.m_bootIdPath, boot().toLatin1());
        const auto response = directory.filePath(QStringLiteral("status.json"));
        write(response, status);
        backend.m_statusProgram = QStringLiteral("/usr/bin/cat");
        backend.m_statusArguments = QStringList{response};
    }
    static QByteArray success() {
        return QByteArray(R"({"event":"success","data":{"mode":"discrete","manual_shutdown_required":true}})");
    }
    static void injectHelperResult(Backend &backend, const QByteArray &output, int exitCode = 0) {
        // Inject only an already-finished result. Never start a helper in tests.
        backend.m_commitSelection = Selection::bind(Mode::Discrete, Status::parse(fixture()), boot());
        backend.m_requested = Mode::Discrete;
        backend.m_applying = true;
        backend.m_commit = Backend::Commit::Applying;
        backend.m_applyOutput = output;
        backend.finishApply(exitCode, QProcess::NormalExit);
    }
private slots:
    void initTestCase() {
        QVERIFY(m_isolation.isValid());
        qputenv("XDG_DATA_HOME", m_isolation.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", m_isolation.path().toUtf8());
        qputenv("XDG_STATE_HOME", m_isolation.path().toUtf8());
        QCoreApplication::setOrganizationName(QStringLiteral("MSIMuxSelectionTests"));
        QCoreApplication::setApplicationName(QStringLiteral("IsolatedSelection"));
    }
    void storeRoundTripAndPrivatePermissions() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("selection.json"));
        SelectionStore store(path);
        QString error;
        const auto original = Selection::bind(Mode::Discrete, Status::parse(fixture()), boot());
        QVERIFY(store.save(original, error));
        std::optional<Selection> loaded;
        QVERIFY(store.load(loaded, error));
        QVERIFY(loaded);
        QCOMPARE(loaded->mode, Mode::Discrete);
        QVERIFY(loaded->matchesBaseline(Status::parse(fixture()), boot()));
        QVERIFY(!(QFileInfo(path).permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)));
        QVERIFY(store.clear(error));
        QVERIFY(store.load(loaded, error));
        QVERIFY(!loaded);
    }
    void storeRejectsInvalidData_data() {
        QTest::addColumn<QByteArray>("contents");
        QTest::newRow("large") << QByteArray(4097, ' ');
        QTest::newRow("array") << QByteArray("[]");
        QTest::newRow("broken") << QByteArray("{");
        QTest::newRow("missing") << QByteArray("{}");
        const QJsonObject base{{"schema_version",1},{"mode","discrete"},{"current","ms-hybrid"},{"target","ms-hybrid"},
            {"model","Vector 16 HX AI A2XWIG"},{"board","MS-15M3"},{"bios","E15M3IMS.116"},{"boot_id",boot()}};
        for (const auto &key : {QStringLiteral("mode"),QStringLiteral("current"),QStringLiteral("target"),QStringLiteral("model"),
                                QStringLiteral("board"),QStringLiteral("bios"),QStringLiteral("boot_id")}) {
            auto changed = base;
            changed[key] = false;
            QTest::newRow(qPrintable(QStringLiteral("type-") + key)) << QJsonDocument(changed).toJson();
        }
        auto changed = base; changed["schema_version"] = 2;
        QTest::newRow("future-schema") << QJsonDocument(changed).toJson();
        changed = base; changed["mode"] = "unknown";
        QTest::newRow("unknown-mode") << QJsonDocument(changed).toJson();
        changed = base; changed["boot_id"] = "unknown-boot";
        QTest::newRow("unknown-boot") << QJsonDocument(changed).toJson();
        changed = base; changed["mode"] = "ms-hybrid";
        QTest::newRow("already-current") << QJsonDocument(changed).toJson();
        changed = base; changed["target"] = "integrated";
        QTest::newRow("hardware-pending-baseline") << QJsonDocument(changed).toJson();
        changed = base; changed["extra"] = true;
        QTest::newRow("extra-field") << QJsonDocument(changed).toJson();
    }
    void storeRejectsInvalidData() {
        QFETCH(QByteArray, contents);
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("selection.json"));
        write(path, contents);
        SelectionStore store(path);
        std::optional<Selection> loaded;
        QString error;
        QVERIFY(!store.load(loaded, error));
        QVERIFY(!loaded);
        QVERIFY(!error.isEmpty());
    }
    void storeRejectsSymlinkAndDirectory() {
        QTemporaryDir directory;
        const auto original = directory.filePath(QStringLiteral("original"));
        const auto link = directory.filePath(QStringLiteral("selection.json"));
        write(original, "unrelated");
        QVERIFY(QFile::link(original, link));
        SelectionStore store(link);
        std::optional<Selection> loaded;
        QString error;
        QVERIFY(!store.load(loaded, error));
        QVERIFY(!store.clear(error));
        QVERIFY(!store.save(Selection::bind(Mode::Discrete, Status::parse(fixture()), boot()), error));
        QVERIFY(QFileInfo::exists(original));
        SelectionStore directoryStore(directory.path());
        QVERIFY(!directoryStore.clear(error));
    }
    void demoSelectionReplacesAndClearsWithoutSideEffects() {
        Backend backend(true);
        QVERIFY(backend.setDemoModes(Mode::Hybrid, Mode::Hybrid));
        QSignalSpy helper(&backend.m_apply, &QProcess::started);
        QSignalSpy probe(&backend.m_probe, &QProcess::started);
        QSignalSpy applied(&backend, &Backend::applyFinished);
        QSignalSpy changed(&backend, &Backend::draftChanged);
        QVERIFY(!backend.m_selectionStore);
        QVERIFY(!backend.canSelectMode(Mode::Hybrid));
        QVERIFY(backend.selectMode(Mode::Discrete));
        const auto count = changed.size();
        QVERIFY(backend.selectMode(Mode::Discrete));
        QCOMPARE(changed.size(), count);
        QVERIFY(backend.selectMode(Mode::Integrated));
        QCOMPARE(backend.draftMode(), Mode::Integrated);
        QCOMPARE(backend.status().current, Mode::Hybrid);
        QCOMPARE(backend.status().target, Mode::Hybrid);
        QVERIFY(!backend.status().pendingShutdown);
        QVERIFY(backend.canSelectMode(Mode::Hybrid));
        QVERIFY(backend.selectMode(Mode::Hybrid));
        QVERIFY(!backend.hasDraft());
        QCOMPARE(helper.size(), 0);
        QCOMPARE(probe.size(), 0);
        QCOMPARE(applied.size(), 0);
        QCOMPARE(backend.property("testHelperRequests").toInt(), 0);
    }
    void storeReloadsOnlyMatchingBootAndBaseline() {
        QTemporaryDir directory;
        {
            Backend backend(false);
            configure(backend, directory);
            backend.refresh();
            QTRY_VERIFY(!backend.refreshing());
            QVERIFY(backend.selectMode(Mode::Discrete));
        }
        Backend backend(false);
        configure(backend, directory);
        QSignalSpy applied(&backend, &Backend::applyFinished);
        backend.refresh();
        QTRY_VERIFY(!backend.refreshing());
        QVERIFY(backend.hasDraft());
        QCOMPARE(backend.draftMode(), Mode::Discrete);
        write(backend.m_bootIdPath, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
        backend.refresh();
        QTRY_VERIFY(!backend.refreshing());
        QVERIFY(!backend.hasDraft());
        QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("selection.json"))));
        QCOMPARE(applied.size(), 0);
    }
    void changedBaselineInvalidatesSavedSelection_data() {
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("other-current") << fixture(Mode::Discrete, Mode::Discrete);
        QTest::newRow("pending-target") << fixture(Mode::Hybrid, Mode::Integrated, true);
        QTest::newRow("pending-same-mode") << fixture(Mode::Hybrid, Mode::Hybrid, true);
        for (const auto &key : {QStringLiteral("model"), QStringLiteral("board"), QStringLiteral("bios")}) {
            auto object = QJsonDocument::fromJson(fixture()).object();
            auto machine = object["machine"].toObject(); machine[key] = "different"; object["machine"] = machine;
            QTest::newRow(qPrintable(key)) << QJsonDocument(object).toJson();
        }
    }
    void changedBaselineInvalidatesSavedSelection() {
        QFETCH(QByteArray, response);
        QTemporaryDir directory;
        Backend backend(false);
        configure(backend, directory);
        backend.refresh();
        QTRY_VERIFY(!backend.refreshing());
        QVERIFY(backend.selectMode(Mode::Discrete));
        write(backend.m_statusArguments.first(), response);
        backend.refresh();
        QTRY_VERIFY(!backend.refreshing());
        QVERIFY(!backend.hasDraft());
        QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("selection.json"))));
    }
    void demoCommitIsExplicitAndPublishesVerifiedPendingOnce() {
        Backend backend(true);
        backend.setDemoModes(Mode::Hybrid, Mode::Hybrid);
        QVERIFY(backend.selectMode(Mode::Discrete));
        QSignalSpy applied(&backend, &Backend::applyFinished);
        QSignalSpy helper(&backend.m_apply, &QProcess::started);
        QSignalSpy probe(&backend.m_probe, &QProcess::started);
        QVERIFY(backend.commitDraft());
        QVERIFY(backend.busy());
        QVERIFY(!backend.selectMode(Mode::Integrated));
        QVERIFY(!backend.clearDraft());
        QVERIFY(!backend.commitDraft());
        QTRY_COMPARE(applied.size(), 1);
        QVERIFY(applied.first().at(0).toBool());
        QVERIFY(!backend.busy());
        QVERIFY(!backend.hasDraft());
        QVERIFY(backend.status().routinePending());
        QCOMPARE(backend.status().current, Mode::Hybrid);
        QCOMPARE(backend.status().target, Mode::Discrete);
        QVERIFY(!backend.canSelectMode(Mode::Integrated));
        QCOMPARE(helper.size(), 0);
        QCOMPARE(probe.size(), 0);
    }
    void completeNativeFlowUsesOnlyInjectedLauncher_data() {
        QTest::addColumn<bool>("probeAlreadyRunning");
        QTest::newRow("idle") << false;
        QTest::newRow("waiting-for-existing-probe") << true;
    }
    void completeNativeFlowUsesOnlyInjectedLauncher() {
        QFETCH(bool, probeAlreadyRunning);
        QTemporaryDir directory;
        Backend backend(false);
        configure(backend, directory);
        backend.refresh();
        QTRY_VERIFY(!backend.refreshing());
        QVERIFY(backend.selectMode(Mode::Discrete));
        QSignalSpy probe(&backend.m_probe, &QProcess::started);
        QSignalSpy helper(&backend.m_apply, &QProcess::started);
        QSignalSpy applied(&backend, &Backend::applyFinished);
        backend.m_testApplyLauncher = [&] {
            backend.setProperty("testHelperRequests", backend.property("testHelperRequests").toInt() + 1);
            QVERIFY(backend.busy());
            QVERIFY(!backend.hasDraft());
            QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("selection.json"))));
            QCOMPARE(backend.m_apply.program(), QStringLiteral("/usr/bin/pkexec"));
            QCOMPARE(backend.m_apply.arguments(), QStringList({QStringLiteral("/usr/lib/msi-mux/msi-mux-helper"),
                QStringLiteral("apply"),QStringLiteral("discrete")}));
            write(backend.m_statusArguments.first(), fixture(Mode::Hybrid, Mode::Discrete, true));
            QTimer::singleShot(0, &backend, [&] {
                backend.m_applyOutput = success();
                backend.finishApply(0, QProcess::NormalExit);
            });
        };
        if (probeAlreadyRunning) backend.refresh();
        QVERIFY(backend.commitDraft());
        QVERIFY(backend.busy());
        QVERIFY(!backend.commitDraft());
        QTRY_COMPARE(applied.size(), 1);
        QVERIFY(applied.first().at(0).toBool());
        QVERIFY(!backend.busy());
        QCOMPARE(probe.size(), probeAlreadyRunning ? 3 : 2);
        QCOMPARE(helper.size(), 0);
        QCOMPARE(backend.property("testHelperRequests").toInt(), 1);
        QVERIFY(backend.status().routinePending());
    }
    void failedDraftRemovalStopsCommitBeforeHelper() {
        QTemporaryDir directory;
        Backend backend(false);
        configure(backend, directory);
        backend.refresh();
        QTRY_VERIFY(!backend.refreshing());
        QVERIFY(backend.selectMode(Mode::Discrete));
        const auto path = directory.filePath(QStringLiteral("selection.json"));
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkdir(path));
        QSignalSpy applied(&backend, &Backend::applyFinished);
        QSignalSpy helper(&backend.m_apply, &QProcess::started);
        backend.commitDraft();
        QTRY_COMPARE(applied.size(), 1);
        QVERIFY(!applied.first().at(0).toBool());
        QVERIFY(!backend.busy());
        QCOMPARE(helper.size(), 0);
        QCOMPARE(backend.property("testHelperRequests").toInt(), 0);
        QVERIFY(backend.hasDraft());
    }
    void preflightFailureNeverStartsHelperOrRevivesPowerIntent_data() {
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("invalid") << QByteArray("broken");
        QTest::newRow("stale-target") << fixture(Mode::Hybrid, Mode::Integrated, true);
        QTest::newRow("stale-current") << fixture(Mode::Discrete, Mode::Discrete);
        QTest::newRow("power-lost") << QByteArray(fixture()).replace("\"ac_power_online\":true", "\"ac_power_online\":false");
    }
    void preflightFailureNeverStartsHelperOrRevivesPowerIntent() {
        QFETCH(QByteArray, response);
        QTemporaryDir directory;
        Backend backend(false);
        configure(backend, directory);
        backend.refresh();
        QTRY_VERIFY(!backend.refreshing());
        QVERIFY(backend.selectMode(Mode::Discrete));
        write(backend.m_statusArguments.first(), response);
        QSignalSpy applied(&backend, &Backend::applyFinished);
        QSignalSpy helper(&backend.m_apply, &QProcess::started);
        backend.commitDraft();
        QVERIFY(backend.busy());
        QTRY_COMPARE(applied.size(), 1);
        QVERIFY(!applied.first().at(0).toBool());
        QVERIFY(!backend.busy());
        QCOMPARE(helper.size(), 0);
        QCOMPARE(backend.property("testHelperRequests").toInt(), 0);
        write(backend.m_statusArguments.first(), fixture());
        backend.refresh();
        QTRY_VERIFY(!backend.refreshing());
        QCOMPARE(applied.size(), 1);
        QCOMPARE(helper.size(), 0);
        QCOMPARE(backend.property("testHelperRequests").toInt(), 0);
    }
    void failedPreflightProcessCompletesOnce_data() {
        QTest::addColumn<QString>("program");
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("missing") << QStringLiteral("/nonexistent/msi-mux-test") << QStringList{};
        QTest::newRow("nonzero") << QStringLiteral("/usr/bin/false") << QStringList{};
        QTest::newRow("timeout") << QStringLiteral("/usr/bin/sleep") << QStringList{QStringLiteral("2")};
    }
    void failedPreflightProcessCompletesOnce() {
        QFETCH(QString, program);
        QFETCH(QStringList, arguments);
        QTemporaryDir directory;
        Backend backend(false);
        configure(backend, directory);
        backend.refresh();
        QTRY_VERIFY(!backend.refreshing());
        QVERIFY(backend.selectMode(Mode::Discrete));
        backend.m_statusProgram = program;
        backend.m_statusArguments = arguments;
        backend.m_probeTimeoutMs = 30;
        QSignalSpy applied(&backend, &Backend::applyFinished);
        QSignalSpy helper(&backend.m_apply, &QProcess::started);
        backend.commitDraft();
        QTRY_COMPARE(applied.size(), 1);
        QTRY_VERIFY(!backend.refreshing());
        QVERIFY(!backend.busy());
        QVERIFY(!applied.first().at(0).toBool());
        QCOMPARE(helper.size(), 0);
        QCOMPARE(applied.size(), 1);
        QCOMPARE(backend.property("testHelperRequests").toInt(), 0);
    }
    void helperResultRequiresMatchingVerifiedEvent_data() {
        QTest::addColumn<QByteArray>("output");
        QTest::addColumn<int>("exitCode");
        QTest::addColumn<bool>("expected");
        QTest::newRow("verified") << success() << 0 << true;
        QTest::newRow("missing-event") << QByteArray("{}") << 0 << false;
        QTest::newRow("wrong-mode") << QByteArray(success()).replace("discrete", "integrated") << 0 << false;
        QTest::newRow("nonzero") << success() << 1 << false;
        QTest::newRow("garbage") << success() + "\ngarbage" << 0 << false;
        QTest::newRow("missing-shutdown") << QByteArray(success()).replace("true", "false") << 0 << false;
        QTest::newRow("cancelled") << QByteArray() << 126 << false;
        QTest::newRow("denied") << QByteArray() << 127 << false;
    }
    void helperResultRequiresMatchingVerifiedEvent() {
        QFETCH(QByteArray, output);
        QFETCH(int, exitCode);
        QFETCH(bool, expected);
        QTemporaryDir directory;
        Backend backend(false);
        configure(backend, directory, fixture(Mode::Hybrid, Mode::Discrete, true));
        QSignalSpy applied(&backend, &Backend::applyFinished);
        QSignalSpy helper(&backend.m_apply, &QProcess::started);
        injectHelperResult(backend, output, exitCode);
        if (expected) { QVERIFY(backend.busy()); QCOMPARE(applied.size(), 0); }
        QTRY_COMPARE(applied.size(), 1);
        QCOMPARE(applied.first().at(0).toBool(), expected);
        QCOMPARE(applied.first().at(1).toBool(), exitCode == 126 || exitCode == 127);
        QVERIFY(!backend.busy());
        QCOMPARE(helper.size(), 0);
    }
    void postflightMustMatchBoundState_data() {
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("unreadable") << QByteArray("invalid");
        QTest::newRow("not-applied") << fixture();
        QTest::newRow("wrong-target") << fixture(Mode::Hybrid, Mode::Integrated, true);
        QTest::newRow("current-changed") << fixture(Mode::Discrete, Mode::Discrete, true);
        QTest::newRow("not-pending") << fixture(Mode::Hybrid, Mode::Discrete, false);
        auto object = QJsonDocument::fromJson(fixture(Mode::Hybrid, Mode::Discrete, true)).object();
        object["switching_block_code"] = "recovery_required";
        QTest::newRow("recovery") << QJsonDocument(object).toJson();
        object["switching_block_code"] = "unsupported_interface";
        QTest::newRow("other-block") << QJsonDocument(object).toJson();
        object.remove("switching_block_code");
        QTest::newRow("missing-block") << QJsonDocument(object).toJson();
        QTest::newRow("missing-interface") << QByteArray(fixture(Mode::Hybrid, Mode::Discrete, true))
            .replace("\"new_switch_supported\":true", "\"new_switch_supported\":false");
        QTest::newRow("missing-capability") << QByteArray(fixture(Mode::Hybrid, Mode::Discrete, true))
            .replace("\"discrete_supported\":true", "\"discrete_supported\":false");
        for (const auto &key : {QStringLiteral("model"),QStringLiteral("board"),QStringLiteral("bios")}) {
            object = QJsonDocument::fromJson(fixture(Mode::Hybrid, Mode::Discrete, true)).object();
            auto machine = object["machine"].toObject(); machine[key] = "changed"; object["machine"] = machine;
            QTest::newRow(qPrintable(key)) << QJsonDocument(object).toJson();
        }
    }
    void postflightMustMatchBoundState() {
        QFETCH(QByteArray, response);
        QTemporaryDir directory;
        Backend backend(false);
        configure(backend, directory, response);
        QSignalSpy applied(&backend, &Backend::applyFinished);
        QSignalSpy helper(&backend.m_apply, &QProcess::started);
        injectHelperResult(backend, success());
        QTRY_COMPARE(applied.size(), 1);
        QVERIFY(!applied.first().at(0).toBool());
        QVERIFY(!backend.busy());
        write(backend.m_statusArguments.first(), fixture(Mode::Hybrid, Mode::Discrete, true));
        backend.refresh();
        QTRY_VERIFY(!backend.refreshing());
        QCOMPARE(applied.size(), 1);
        QCOMPARE(helper.size(), 0);
    }
    void postflightDifferentBootCannotAuthorizePower() {
        QTemporaryDir directory;
        Backend backend(false);
        configure(backend, directory, fixture(Mode::Hybrid, Mode::Discrete, true));
        write(backend.m_bootIdPath, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
        QSignalSpy applied(&backend, &Backend::applyFinished);
        injectHelperResult(backend, success());
        QTRY_COMPARE(applied.size(), 1);
        QVERIFY(!applied.first().at(0).toBool());
    }
    void commitWithoutDraftFailsAsynchronously() {
        Backend backend(true);
        QSignalSpy applied(&backend, &Backend::applyFinished);
        backend.commitDraft();
        QVERIFY(backend.busy());
        QCOMPARE(applied.size(), 0);
        QTRY_COMPARE(applied.size(), 1);
        QVERIFY(!applied.first().at(0).toBool());
        QVERIFY(!backend.busy());
    }
    void nestedCompletionRefusalIsExplicitAndLeavesNoBusyState() {
        Backend backend(true);
        bool nestedAccepted = true;
        bool sawIdle = false;
        QSignalSpy applied(&backend, &Backend::applyFinished);
        connect(&backend, &Backend::applyFinished, &backend, [&] {
            sawIdle = !backend.busy();
            nestedAccepted = backend.commitDraft();
        });
        QVERIFY(backend.commitDraft());
        QTRY_COMPARE(applied.size(), 1);
        QVERIFY(sawIdle);
        QVERIFY(!nestedAccepted);
        QVERIFY(!backend.busy());
        QVERIFY(!backend.m_finishing);
    }
    void deletionFromBusySignalDoesNotStartWork() {
        auto *backend = new Backend(true);
        backend->setDemoModes(Mode::Hybrid, Mode::Hybrid);
        QVERIFY(backend->selectMode(Mode::Discrete));
        const QPointer<Backend> pointer(backend);
        connect(backend, &Backend::busyChanged, this, [backend](bool busy) { if (busy) delete backend; });
        backend->commitDraft();
        QVERIFY(!pointer);
    }
};
}
QTEST_GUILESS_MAIN(Mux::SelectionTests)
#include "test_selection.moc"
