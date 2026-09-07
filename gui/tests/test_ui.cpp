#include "backend.h"
#include "status.h"
#include "translations.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace Mux {
class UiTests final : public QObject {
    Q_OBJECT
private:
    static QByteArray fixture() {
        return R"({"schema_version":1,"machine":{"model":"Vector 16 HX AI A2XWIG","board":"MS-15M3","bios":"E15M3IMS.116","expected_hardware":true},"firmware":{"available":true,"value":{"length":20,"attributes":"0x00000007","current_mode":"ms-hybrid","selected_target_mode":"ms-hybrid","new_switch_supported":true,"discrete_supported":true,"integrated_supported":true}},"ac_power_online":true,"switching_supported":true,"pending_shutdown":false,"internal_displays":[{"connector":"eDP-1","status":"connected","enabled":true,"vendor":"Intel","driver":"i915"}]})";
    }
    static QString script(QTemporaryDir &directory, const QByteArray &contents) {
        const auto path = directory.filePath(QStringLiteral("fake-backend"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        file.write("#!/bin/sh\n");
        file.write(contents);
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }

private slots:
    void statusParsing() {
        const auto state = Status::parse(fixture());
        QVERIFY(state.valid);
        QCOMPARE(state.current, Mode::Hybrid);
        QCOMPARE(state.target, Mode::Hybrid);
        QVERIFY(state.acPower);
        QVERIFY(state.firmwareValid);
        QCOMPARE(state.displays.size(), 1);
        QCOMPARE(state.displays.first().vendor, QStringLiteral("Intel"));
        QVERIFY(state.displays.first().enabled);
        QVERIFY(state.canSwitch(Mode::Discrete));
        QVERIFY(state.canSwitch(Mode::Integrated));
        QVERIFY(!state.canSwitch(Mode::Hybrid));
        QVERIFY(!state.canSwitch(Mode::Unknown));
    }

    void invalidResponses_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("not-json") << QByteArray("hello");
        QTest::newRow("array") << QByteArray("[]");
        QTest::newRow("missing-schema") << QByteArray("{}");
        QTest::newRow("new-schema") << QByteArray(fixture()).replace("\"schema_version\":1", "\"schema_version\":2");
        QTest::newRow("too-large") << QByteArray(1024 * 1024 + 1, ' ');
    }
    void invalidResponses() {
        QFETCH(QByteArray, json);
        const auto state = Status::parse(json);
        QVERIFY(!state.valid);
        QVERIFY(!state.canSwitch(Mode::Discrete));
    }

    void failClosedFields_data() {
        QTest::addColumn<QByteArray>("before");
        QTest::addColumn<QByteArray>("after");
        QTest::newRow("battery") << QByteArray("\"ac_power_online\":true") << QByteArray("\"ac_power_online\":false");
        QTest::newRow("missing-ac") << QByteArray("\"ac_power_online\":true,") << QByteArray("");
        QTest::newRow("unsupported-hardware") << QByteArray("\"expected_hardware\":true") << QByteArray("\"expected_hardware\":false");
        QTest::newRow("unsupported-bios") << QByteArray("E15M3IMS.116") << QByteArray("E15M3IMS.115");
        QTest::newRow("unknown-current") << QByteArray("\"current_mode\":\"ms-hybrid\"") << QByteArray("\"current_mode\":null");
        QTest::newRow("unknown-target") << QByteArray("\"selected_target_mode\":\"ms-hybrid\"") << QByteArray("\"selected_target_mode\":\"other\"");
        QTest::newRow("invalid-length") << QByteArray("\"length\":20") << QByteArray("\"length\":21");
        QTest::newRow("invalid-attributes") << QByteArray("\"attributes\":\"0x00000007\"") << QByteArray("\"attributes\":\"0x00000003\"");
        QTest::newRow("missing-attributes") << QByteArray("\"attributes\":\"0x00000007\",") << QByteArray("");
        QTest::newRow("malformed-attributes") << QByteArray("\"attributes\":\"0x00000007\"") << QByteArray("\"attributes\":\"07junk\"");
        QTest::newRow("missing-capability") << QByteArray("\"new_switch_supported\":true") << QByteArray("\"new_switch_supported\":false");
        QTest::newRow("backend-blocked") << QByteArray("\"switching_supported\":true") << QByteArray("\"switching_supported\":false");
        QTest::newRow("backend-missing-field") << QByteArray("\"switching_supported\":true,") << QByteArray("");
        QTest::newRow("pending") << QByteArray("\"pending_shutdown\":false") << QByteArray("\"pending_shutdown\":true");
        QTest::newRow("changed-target") << QByteArray("\"selected_target_mode\":\"ms-hybrid\"") << QByteArray("\"selected_target_mode\":\"discrete\"");
        QTest::newRow("firmware-unavailable") << QByteArray("\"available\":true") << QByteArray("\"available\":false");
    }
    void failClosedFields() {
        QFETCH(QByteArray, before);
        QFETCH(QByteArray, after);
        const auto state = Status::parse(QByteArray(fixture()).replace(before, after));
        QVERIFY(state.valid);
        QVERIFY(!state.canSwitch(Mode::Discrete));
        QVERIFY(!state.canSwitch(Mode::Integrated));
    }

    void capabilitiesAndBusy() {
        auto state = Status::demo();
        QVERIFY(!state.canSwitch(Mode::Discrete, true));
        state.discreteSupported = false;
        QVERIFY(!state.canSwitch(Mode::Discrete));
        QVERIFY(state.canSwitch(Mode::Integrated));
        state.integratedSupported = false;
        QVERIFY(!state.canSwitch(Mode::Integrated));
    }

    void recoveryIsNotShutdownAdvice() {
        auto json = QJsonDocument::fromJson(fixture()).object();
        json["pending_shutdown"] = false;
        json["switching_supported"] = false;
        json["switching_block_code"] = QStringLiteral("recovery_required");
        auto section = json["firmware"].toObject();
        auto value = section["value"].toObject();
        value["selected_target_mode"] = QStringLiteral("discrete");
        section["value"] = value;
        json["firmware"] = section;
        auto state = Status::parse(QJsonDocument(json).toJson());
        QVERIFY(!state.pendingShutdown);
        QVERIFY(!state.canSwitch(Mode::Integrated));
        QCOMPARE(blockText(state, Language::English), Mux::tr(Text::RecoveryRequired, Language::English));
        json.remove("pending_shutdown");
        state = Status::parse(QJsonDocument(json).toJson());
        QVERIFY(state.pendingShutdown);
    }

    void translationsComplete() {
        for (int index = 0; index < static_cast<int>(Text::Count); ++index) {
            const auto key = static_cast<Text>(index);
            QVERIFY2(!Mux::tr(key, Language::English).isEmpty(), qPrintable(QString::number(index)));
            QVERIFY2(!Mux::tr(key, Language::Turkish).isEmpty(), qPrintable(QString::number(index)));
            QCOMPARE(Mux::tr(key, Language::English).count(QStringLiteral("%1")), Mux::tr(key, Language::Turkish).count(QStringLiteral("%1")));
        }
        QCOMPARE(modeName(Mode::Hybrid, Language::Turkish), QStringLiteral("Hibrit"));
        QCOMPARE(modeName(Mode::Discrete, Language::English), QStringLiteral("Discrete"));
        auto state = Status::demo();
        state.acPower = false;
        QCOMPARE(blockText(state, Language::Turkish), Mux::tr(Text::AcRequired, Language::Turkish));
    }

    void fakeBackendSuccess() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Backend backend(false);
        backend.m_statusProgram = script(directory, "printf '%s' '" + fixture() + "'\n");
        backend.m_statusArguments.clear();
        QSignalSpy changed(&backend, &Backend::statusChanged);
        backend.refresh();
        QVERIFY(changed.wait(3000));
        QVERIFY(backend.status().valid);
        QVERIFY(backend.status().canSwitch(Mode::Discrete));
        QVERIFY(!backend.refreshing());
    }

    void fakeBackendFailureClearsStaleStatus() {
        QTemporaryDir directory;
        Backend backend(false);
        backend.m_status = Status::demo();
        backend.m_statusProgram = script(directory, "printf 'failed' >&2\nexit 1\n");
        backend.m_statusArguments.clear();
        QSignalSpy errors(&backend, &Backend::statusError);
        backend.refresh();
        QVERIFY(errors.wait(3000));
        QVERIFY(!backend.status().valid);
        QVERIFY(!backend.status().canSwitch(Mode::Discrete));
    }

    void fakeInvalidJsonPreservesErrorCode() {
        QTemporaryDir directory;
        Backend backend(false);
        backend.m_statusProgram = script(directory, "printf 'not-json'\n");
        backend.m_statusArguments.clear();
        QSignalSpy errors(&backend, &Backend::statusError);
        backend.refresh();
        QVERIFY(errors.wait(3000));
        QCOMPARE(errors.first().first().toString(), QStringLiteral("invalid_json"));
        QCOMPARE(backend.status().error, QStringLiteral("invalid_json"));
    }

    void fakeBackendTimeout() {
        QTemporaryDir directory;
        Backend backend(false);
        backend.m_statusProgram = script(directory, "exec sleep 2\n");
        backend.m_statusArguments.clear();
        backend.m_probeTimeoutMs = 30;
        QSignalSpy errors(&backend, &Backend::statusError);
        backend.refresh();
        QVERIFY(errors.wait(3000));
        QCOMPARE(errors.first().first().toString(), QStringLiteral("timeout"));
        QTRY_VERIFY(!backend.refreshing());
    }

    void fakeBackendOutputLimit() {
        QTemporaryDir directory;
        Backend backend(false);
        backend.m_statusProgram = script(directory, "exec head -c 1100000 /dev/zero\n");
        backend.m_statusArguments.clear();
        QSignalSpy errors(&backend, &Backend::statusError);
        backend.refresh();
        QVERIFY(errors.wait(3000));
        QCOMPARE(errors.first().first().toString(), QStringLiteral("response_too_large"));
        QTRY_VERIFY(!backend.refreshing());
    }

    void demoNeverStartsProcesses() {
        Backend backend(true);
        backend.refresh();
        QSignalSpy applied(&backend, &Backend::applyFinished);
        backend.apply(Mode::Discrete);
        QVERIFY(backend.busy());
        QCOMPARE(backend.m_apply.state(), QProcess::NotRunning);
        QCOMPARE(backend.m_probe.state(), QProcess::NotRunning);
        QVERIFY(applied.wait(3000));
        QVERIFY(applied.first().first().toBool());
        QCOMPARE(backend.status().current, Mode::Hybrid);
        QCOMPARE(backend.status().target, Mode::Discrete);
        QVERIFY(backend.status().pendingShutdown);
        QVERIFY(!backend.status().canSwitch(Mode::Integrated));
    }

    void orderlyDestructionDoesNotKillApply() {
        QTemporaryDir directory;
        const auto marker = directory.filePath(QStringLiteral("completed"));
        {
            Backend backend(false);
            backend.m_apply.setProgram(script(directory, "sleep 0.15\nprintf 'completed' > '" + marker.toUtf8() + "'\n"));
            backend.m_apply.start(QIODevice::ReadOnly);
            QVERIFY(backend.m_apply.waitForStarted(3000));
            backend.m_applying = true;
        }
        QVERIFY(QFileInfo::exists(marker));
    }

    void successRequiresMatchingVerifiedEvent_data() {
        QTest::addColumn<QByteArray>("output");
        QTest::addColumn<int>("exitCode");
        QTest::addColumn<bool>("expected");
        const QByteArray success = R"({"event":"success","data":{"mode":"discrete","manual_shutdown_required":true}})";
        QTest::newRow("verified") << success << 0 << true;
        QTest::newRow("no-event") << QByteArray() << 0 << false;
        QTest::newRow("wrong-mode") << QByteArray(success).replace("discrete", "integrated") << 0 << false;
        QTest::newRow("nonzero") << success << 1 << false;
        QTest::newRow("garbage") << (success + "\nnot-json") << 0 << false;
        QTest::newRow("missing-shutdown") << QByteArray(success).replace("true", "false") << 0 << false;
    }
    void successRequiresMatchingVerifiedEvent() {
        QFETCH(QByteArray, output);
        QFETCH(int, exitCode);
        QFETCH(bool, expected);
        Backend backend(true);
        backend.refresh();
        backend.m_applying = true;
        backend.m_requested = Mode::Discrete;
        backend.m_applyOutput = output;
        QSignalSpy applied(&backend, &Backend::applyFinished);
        backend.finishApply(exitCode, QProcess::NormalExit);
        QCOMPARE(applied.size(), 1);
        QCOMPARE(applied.first().first().toBool(), expected);
    }
};
}

QTEST_MAIN(Mux::UiTests)
#include "test_ui.moc"
