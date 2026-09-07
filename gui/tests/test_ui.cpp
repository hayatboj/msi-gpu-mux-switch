#include "backend.h"
#include "modehero.h"
#include "window.h"
#include "status.h"
#include "translations.h"

#include <QFile>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QLineEdit>
#include <QPushButton>
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
            QCOMPARE(Mux::tr(key, Language::English).count(QStringLiteral("%2")), Mux::tr(key, Language::Turkish).count(QStringLiteral("%2")));
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

    void integratedIsAvailableByDefaultAndDemoRemainsInert() {
        Backend backend(true);
        backend.refresh();
        QVERIFY(backend.canApplyMode(Mode::Discrete));
        QVERIFY(backend.canApplyMode(Mode::Integrated));
        QSignalSpy applied(&backend, &Backend::applyFinished);
        backend.apply(Mode::Integrated);
        QVERIFY(backend.busy());
        QCOMPARE(backend.m_apply.state(), QProcess::NotRunning);
        QVERIFY(applied.wait(3000));
        QVERIFY(applied.first().first().toBool());
        QCOMPARE(backend.status().target, Mode::Integrated);
        QVERIFY(backend.status().pendingShutdown);
    }

    void integratedKeepsHardwareGates() {
        Backend backend(true);
        backend.refresh();
        backend.m_status.acPower = false;
        QVERIFY(!backend.canApplyMode(Mode::Integrated));
        backend.apply(Mode::Integrated);
        QVERIFY(!backend.busy());
        backend.m_status.acPower = true;
        backend.m_status.integratedSupported = false;
        QVERIFY(!backend.canApplyMode(Mode::Integrated));
        backend.apply(Mode::Integrated);
        QVERIFY(!backend.busy());
        QCOMPARE(backend.m_apply.state(), QProcess::NotRunning);
    }

    void integratedCanReturnToOtherModes() {
        Backend backend(true);
        backend.refresh();
        backend.m_status.current = Mode::Integrated;
        backend.m_status.target = Mode::Integrated;
        QVERIFY(backend.canApplyMode(Mode::Hybrid));
        QVERIFY(backend.canApplyMode(Mode::Discrete));
    }

    void heroKeepsPendingDirection_data() {
        QTest::addColumn<int>("current");
        QTest::addColumn<int>("target");
        for (auto current : {Mode::Hybrid, Mode::Discrete, Mode::Integrated})
            for (auto target : {Mode::Hybrid, Mode::Discrete, Mode::Integrated})
                if (current != target) QTest::newRow(qPrintable(modeArgument(current) + QStringLiteral("-to-") + modeArgument(target)))
                    << static_cast<int>(current) << static_cast<int>(target);
    }
    void heroKeepsPendingDirection() {
        QFETCH(int, current);
        QFETCH(int, target);
        auto state = Status::demo();
        state.current = static_cast<Mode>(current);
        state.target = static_cast<Mode>(target);
        state.pendingShutdown = true;
        ModeHero hero;
        hero.resize(624, 280);
        hero.setReducedMotion(true);
        hero.setState(state, Language::English, QStringLiteral("NVIDIA"));
        QCOMPARE(hero.currentMode(), state.current);
        QCOMPARE(hero.targetMode(), state.target);
        QVERIFY(hero.pending());
        QCOMPARE(hero.titleText(), modeName(state.current, Language::English) + QStringLiteral(" → ") + modeName(state.target, Language::English));
        QVERIFY(hero.accessibleDescription().contains(QStringLiteral("Still running in ") + modeName(state.current, Language::English)));
        QVERIFY(!hero.animationRunning());
        const auto forward = hero.grab().toImage();
        std::swap(state.current, state.target);
        hero.setState(state, Language::English, QStringLiteral("NVIDIA"));
        const auto reverse = hero.grab().toImage();
        QVERIFY(forward.pixelColor(20, 140) != reverse.pixelColor(20, 140));
    }

    void heroStopsAnimationWhenHiddenOrReduced() {
        auto state = Status::demo();
        state.target = Mode::Discrete;
        state.pendingShutdown = true;
        ModeHero hero;
        hero.setState(state, Language::Turkish, QStringLiteral("Intel"));
        QVERIFY(!hero.animationRunning());
        hero.show();
        QTRY_VERIFY(hero.animationRunning());
        hero.setReducedMotion(true);
        QVERIFY(!hero.animationRunning());
        hero.setReducedMotion(false);
        QVERIFY(hero.animationRunning());
        hero.showMinimized();
        QTRY_VERIFY(!hero.animationRunning());
        hero.showNormal();
        QTRY_VERIFY(hero.animationRunning());
        hero.hide();
        QVERIFY(!hero.animationRunning());
    }

    void heroDoesNotTurnRecoveryIntoPendingSuccess() {
        auto state = Status::demo();
        state.target = Mode::Discrete;
        state.pendingShutdown = true;
        state.blockCode = QStringLiteral("recovery_required");
        ModeHero hero;
        hero.setState(state, Language::English, QStringLiteral("Intel"));
        QVERIFY(!hero.pending());
        QCOMPARE(hero.titleText(), QStringLiteral("Hybrid"));
        state.blockCode.clear();
        state.firmwareValid = false;
        hero.setState(state, Language::English, QStringLiteral("Intel"));
        QVERIFY(!hero.pending());
        state.firmwareValid = true;
        state.target = Mode::Unknown;
        hero.setState(state, Language::English, QStringLiteral("Intel"));
        QVERIFY(!hero.pending());
    }

    void windowExternalRequestWaitsForFreshReadAndOnlyConfirms() {
        QTemporaryDir directory;
        Window window(false, Language::English, false);
        auto *backend = window.backend();
        backend->m_statusProgram = script(directory, "sleep 0.10\nprintf '%s' '" + fixture() + "'\n");
        backend->m_statusArguments.clear();
        QSignalSpy status(backend, &Backend::statusChanged);
        QSignalSpy applied(backend, &Backend::applyFinished);
        bool sawConfirmation = false;
        bool freshState = false;
        bool typedConsentRequired = false;
        QTimer inspector;
        inspector.setInterval(10);
        connect(&inspector, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            inspector.stop();
            sawConfirmation = dialog->windowTitle().contains(QStringLiteral("Discrete"));
            freshState = !status.isEmpty() && backend->status().valid && !backend->refreshing();
            auto *input = dialog->findChild<QLineEdit *>();
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (input && buttons) {
                for (auto *button : buttons->buttons()) {
                    if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole)
                        typedConsentRequired = input->text().isEmpty() && !button->isEnabled();
                }
            }
            dialog->reject();
        });
        inspector.start();
        window.requestMode(Mode::Discrete);
        QVERIFY(!window.m_confirming);
        QVERIFY(backend->refreshing());
        QTRY_VERIFY_WITH_TIMEOUT(!window.m_requestInFlight, 3000);
        QVERIFY(sawConfirmation);
        QVERIFY(freshState);
        QVERIFY(typedConsentRequired);
        QCOMPARE(applied.size(), 0);
        QCOMPARE(backend->m_apply.state(), QProcess::NotRunning);
    }

    void windowDuplicateRequestsNeverNestConfirmations() {
        Window window(true, Language::Turkish, false);
        auto *backend = window.backend();
        QSignalSpy applied(backend, &Backend::applyFinished);
        int confirmations = 0;
        QTimer inspector;
        inspector.setInterval(10);
        connect(&inspector, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            ++confirmations;
            window.requestMode(Mode::Integrated);
            window.requestMode(Mode::Hybrid);
            dialog->reject();
        });
        inspector.start();
        window.requestMode(Mode::Discrete);
        window.requestMode(Mode::Integrated);
        QTRY_VERIFY_WITH_TIMEOUT(!window.m_requestInFlight, 3000);
        QTest::qWait(30);
        inspector.stop();
        QCOMPARE(confirmations, 1);
        QCOMPARE(applied.size(), 0);
        QCOMPARE(backend->status().current, Mode::Hybrid);
        QCOMPARE(backend->status().target, Mode::Hybrid);
        QCOMPARE(backend->m_apply.state(), QProcess::NotRunning);
    }

    void pendingStartupAndLaterNeverRequestPower() {
        Window window(true, Language::English, false);
        window.backend()->setDemoModes(Mode::Discrete, Mode::Hybrid);
        QSignalSpy power(&window.m_powerActions, &PowerActions::finished);
        window.showPanel();
        QTest::qWait(30);
        QVERIFY(!window.m_powerDialogOpen);
        QVERIFY(window.m_pendingCard->isVisible());
        QVERIFY(window.m_shutdownButton->isVisible());
        QCOMPARE(power.size(), 0);
        bool sawExplicitChoice = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (buttons) {
                for (auto *button : buttons->buttons()) {
                    auto *push = qobject_cast<QPushButton *>(button);
                    if (push && push->text() == QStringLiteral("Later")) sawExplicitChoice = push->isDefault();
                }
            }
            dialog->reject();
        });
        window.shutdown();
        QVERIFY(sawExplicitChoice);
        QCOMPARE(power.size(), 0);
        QVERIFY(!window.m_powerActions.busy());
        QCOMPARE(window.backend()->status().current, Mode::Discrete);
    }

    void demoConfirmedModeShowsTruthfulHeroAndSafePowerChoices() {
        Window window(true, Language::English, false);
        auto *backend = window.backend();
        QSignalSpy applied(backend, &Backend::applyFinished);
        QSignalSpy helperStarted(&backend->m_apply, &QProcess::started);
        QSignalSpy probeStarted(&backend->m_probe, &QProcess::started);
        QSignalSpy powerFinished(&window.m_powerActions, &PowerActions::finished);
        QSignalSpy powerBusy(&window.m_powerActions, &PowerActions::busyChanged);
        enum class Stage { Confirmation, PowerChoices, Done };
        Stage stage = Stage::Confirmation;
        bool timedOut = false;
        bool consentWasRequired = false;
        bool heroWasTruthful = false;
        bool allPowerChoicesPresent = false;
        bool laterWasDefault = false;
        const QString captureDirectory = qEnvironmentVariable("MSI_MUX_TEST_CAPTURE_DIR");
        bool captureSaved = captureDirectory.isEmpty();
        QTimer inspector;
        inspector.setInterval(10);
        connect(&inspector, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            // QDialog is a top-level QWidget even with a QObject parent;
            // QWidget::isAncestorOf does not identify this modal ownership.
            // This isolated inert test has no other application's widgets.
            if (!dialog) return;
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!buttons) return;
            if (stage == Stage::Confirmation) {
                auto *input = dialog->findChild<QLineEdit *>();
                QPushButton *confirm = nullptr;
                for (auto *button : buttons->buttons())
                    if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole)
                        confirm = qobject_cast<QPushButton *>(button);
                if (!input || !confirm) return;
                consentWasRequired = input->text().isEmpty() && !confirm->isEnabled();
                input->setFocus();
                QTest::keyClicks(input, QStringLiteral("DISCRETE"));
                if (!confirm->isEnabled()) return;
                stage = Stage::PowerChoices;
                QTest::mouseClick(confirm, Qt::LeftButton);
            } else if (stage == Stage::PowerChoices) {
                QPushButton *later = nullptr;
                bool restart = false;
                bool powerOff = false;
                for (auto *button : buttons->buttons()) {
                    auto *push = qobject_cast<QPushButton *>(button);
                    if (!push) continue;
                    if (push->text() == QStringLiteral("Later")) later = push;
                    if (push->text() == QStringLiteral("Restart…")) restart = push->isEnabled();
                    if (push->text() == QStringLiteral("Shut down…")) powerOff = push->isEnabled();
                }
                if (!later) return;
                heroWasTruthful = window.m_hero->currentMode() == Mode::Hybrid &&
                    window.m_hero->targetMode() == Mode::Discrete && window.m_hero->pending() &&
                    window.m_hero->titleText() == QStringLiteral("Hybrid → Discrete") &&
                    backend->status().current == Mode::Hybrid && backend->status().target == Mode::Discrete &&
                    backend->status().pendingShutdown;
                allPowerChoicesPresent = restart && powerOff && later->isEnabled();
                laterWasDefault = later->isDefault();
                // Optional visual QA is confined to this inert test; normal test
                // runs write no screenshot and production has no capture hook.
                if (!captureDirectory.isEmpty()) {
                    const QDir directory(captureDirectory);
                    captureSaved = directory.exists() && dialog->grab().save(directory.filePath(QStringLiteral("power-dialog-en.png")));
                }
                stage = Stage::Done;
                inspector.stop();
                QTest::mouseClick(later, Qt::LeftButton);
            }
        });
        // QTRY alone cannot bound a nested QDialog::exec(). This independent
        // timer closes any owned modal on timeout, allowing the test to fail.
        QTimer watchdog;
        watchdog.setInterval(4000);
        connect(&watchdog, &QTimer::timeout, &window, [&] {
            timedOut = true;
            inspector.stop();
            watchdog.setInterval(25);
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog) dialog->reject();
        });
        watchdog.start();
        inspector.start();
        window.requestMode(Mode::Discrete);
        QTRY_VERIFY_WITH_TIMEOUT(stage == Stage::Done || timedOut, 5000);
        watchdog.stop();
        inspector.stop();
        QVERIFY2(!timedOut, "The inert confirmation/power-dialog flow exceeded its bounded deadline");
        QVERIFY(consentWasRequired);
        QVERIFY(heroWasTruthful);
        QVERIFY(allPowerChoicesPresent);
        QVERIFY(laterWasDefault);
        QVERIFY(captureSaved);
        QCOMPARE(applied.size(), 1);
        QVERIFY(applied.first().first().toBool());
        QCOMPARE(helperStarted.size(), 0);
        QCOMPARE(probeStarted.size(), 0);
        QCOMPARE(powerFinished.size(), 0);
        QCOMPARE(powerBusy.size(), 0);
        QVERIFY(!window.m_powerActions.busy());
        QVERIFY(!window.m_powerDialogOpen);
        QCOMPARE(backend->status().current, Mode::Hybrid);
        QCOMPARE(backend->status().target, Mode::Discrete);
        QVERIFY(backend->status().pendingShutdown);
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
