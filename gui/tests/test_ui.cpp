#include "backend.h"
#include "modehero.h"
#include "window.h"
#include "status.h"
#include "translations.h"

#include <QFile>
#include <QDialog>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QEventLoop>
#include <QMessageBox>
#include <functional>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QLineEdit>
#include <QLabel>
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

    // Bound both nested dialogs independently of the caller's QTRY timeout.
    static bool chooseDraft(Window &window, Mode mode, int powerChoice = 0,
                            const std::function<void()> &beforePowerChoice = {}) {
        QEventLoop loop;
        QTimer inspector;
        QTimer watchdog;
        bool confirmed = false;
        bool finished = false;
        bool timedOut = false;
        inspector.setInterval(10);
        watchdog.setInterval(3000);
        connect(&inspector, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!buttons) return;
            if (!confirmed) {
                for (auto *button : buttons->buttons()) {
                    if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole) {
                        confirmed = true;
                        button->click();
                        return;
                    }
                }
            } else {
                const auto wanted = window.t(powerChoice == 2 ? Text::Restart : powerChoice == 3 ? Text::Shutdown : Text::Later);
                for (auto *button : buttons->buttons()) if (button->text() == wanted) {
                    inspector.stop();
                    if (beforePowerChoice) beforePowerChoice();
                    finished = true;
                    button->click();
                    loop.quit();
                    return;
                }
            }
        });
        connect(&watchdog, &QTimer::timeout, &window, [&] {
            timedOut = true;
            inspector.stop();
            watchdog.setInterval(10);
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
            loop.quit();
        });
        inspector.start();
        watchdog.start();
        window.requestMode(mode);
        loop.exec();
        return finished && !timedOut;
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
        backend.m_testApplyLauncher = [] {};
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
        backend.m_testApplyLauncher = [] {};
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
        backend.m_testApplyLauncher = [] {};
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
        backend.m_testApplyLauncher = [] {};
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
        backend.m_testApplyLauncher = [] {};
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
        QTRY_VERIFY_WITH_TIMEOUT(!backend.refreshing(), 1000);
        QSignalSpy applied(&backend, &Backend::applyFinished);
        QVERIFY(backend.selectMode(Mode::Discrete));
        QVERIFY(!backend.busy());
        QCOMPARE(backend.status().target, Mode::Hybrid);
        backend.commitDraft();
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
        QTRY_VERIFY_WITH_TIMEOUT(!backend.refreshing(), 1000);
        QVERIFY(backend.canSelectMode(Mode::Discrete));
        QVERIFY(backend.canSelectMode(Mode::Integrated));
        QSignalSpy applied(&backend, &Backend::applyFinished);
        QVERIFY(backend.selectMode(Mode::Integrated));
        QVERIFY(!backend.busy());
        QCOMPARE(backend.status().target, Mode::Hybrid);
        backend.commitDraft();
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
        QTRY_VERIFY_WITH_TIMEOUT(!backend.refreshing(), 1000);
        backend.m_status.acPower = false;
        QVERIFY(!backend.canSelectMode(Mode::Integrated));
        QVERIFY(!backend.selectMode(Mode::Integrated));
        QVERIFY(!backend.busy());
        backend.m_status.acPower = true;
        backend.m_status.integratedSupported = false;
        QVERIFY(!backend.canSelectMode(Mode::Integrated));
        QVERIFY(!backend.selectMode(Mode::Integrated));
        QVERIFY(!backend.busy());
        QCOMPARE(backend.m_apply.state(), QProcess::NotRunning);
    }

    void integratedCanReturnToOtherModes() {
        Backend backend(true);
        backend.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.refreshing(), 1000);
        backend.m_status.current = Mode::Integrated;
        backend.m_status.target = Mode::Integrated;
        QVERIFY(backend.canSelectMode(Mode::Hybrid));
        QVERIFY(backend.canSelectMode(Mode::Discrete));
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
        backend->m_testApplyLauncher = [] {};
        backend->m_statusProgram = script(directory, "sleep 0.10\nprintf '%s' '" + fixture() + "'\n");
        backend->m_statusArguments.clear();
        QSignalSpy status(backend, &Backend::statusChanged);
        QSignalSpy applied(backend, &Backend::applyFinished);
        bool sawConfirmation = false;
        bool freshState = false;
        bool explicitButtonConsent = false;
        QTimer inspector;
        inspector.setInterval(10);
        connect(&inspector, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            inspector.stop();
            sawConfirmation = dialog->windowTitle().contains(QStringLiteral("Discrete"));
            freshState = !status.isEmpty() && backend->status().valid && !backend->refreshing();
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            auto *target = dialog->findChild<QLabel *>(QStringLiteral("confirmationTarget"));
            if (!dialog->findChild<QLineEdit *>() && target && target->text() == QStringLiteral("Discrete") && buttons) {
                for (auto *button : buttons->buttons()) {
                    auto *push = qobject_cast<QPushButton *>(button);
                    if (push && buttons->buttonRole(button) == QDialogButtonBox::AcceptRole)
                        explicitButtonConsent = push->isEnabled() && !push->isDefault() && !push->autoDefault();
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
        QVERIFY(explicitButtonConsent);
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

    void simpleModeConfirmationReturnCancels_data() {
        QTest::addColumn<bool>("turkish");
        QTest::newRow("English") << false;
        QTest::newRow("Turkish") << true;
    }
    void simpleModeConfirmationReturnCancels() {
        QFETCH(bool, turkish);
        const auto language = turkish ? Language::Turkish : Language::English;
        Window window(true, language, false);
        auto *backend = window.backend();
        QSignalSpy applied(backend, &Backend::applyFinished);
        QSignalSpy helperStarted(&backend->m_apply, &QProcess::started);
        QSignalSpy power(&window.m_powerActions, &PowerActions::finished);
        bool sawSafeButtons = false;
        bool targetShown = false;
        bool timedOut = false;
        const auto captureDirectory = qEnvironmentVariable("MSI_MUX_TEST_CAPTURE_DIR");
        bool captureSaved = captureDirectory.isEmpty();
        QTimer inspector;
        inspector.setInterval(10);
        connect(&inspector, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            inspector.stop();
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            auto *target = dialog->findChild<QLabel *>(QStringLiteral("confirmationTarget"));
            targetShown = target && target->text() == modeName(Mode::Discrete, language);
            QPushButton *apply = nullptr;
            QPushButton *cancel = nullptr;
            if (buttons) for (auto *button : buttons->buttons()) {
                if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole) apply = qobject_cast<QPushButton *>(button);
                if (buttons->buttonRole(button) == QDialogButtonBox::RejectRole) cancel = qobject_cast<QPushButton *>(button);
            }
            sawSafeButtons = !dialog->findChild<QLineEdit *>() && apply && cancel && apply->isEnabled() &&
                !apply->autoDefault() && !apply->isDefault() && cancel->isDefault();
            if (!captureDirectory.isEmpty()) {
                const QDir directory(captureDirectory);
                captureSaved = directory.exists() && dialog->grab().save(directory.filePath(turkish ?
                    QStringLiteral("mode-confirm-tr.png") : QStringLiteral("mode-confirm-en.png")));
            }
            QTest::keyClick(dialog, Qt::Key_Return);
        });
        QTimer watchdog;
        watchdog.setInterval(3000);
        connect(&watchdog, &QTimer::timeout, &window, [&] {
            timedOut = true;
            inspector.stop();
            watchdog.setInterval(25);
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        watchdog.start();
        inspector.start();
        window.requestMode(Mode::Discrete);
        QTRY_VERIFY_WITH_TIMEOUT(!window.m_requestInFlight || timedOut, 4000);
        watchdog.stop();
        inspector.stop();
        QVERIFY(!timedOut);
        QVERIFY(sawSafeButtons);
        QVERIFY(targetShown);
        QVERIFY(captureSaved);
        QVERIFY(!backend->busy());
        QCOMPARE(applied.size(), 0);
        QCOMPARE(helperStarted.size(), 0);
        QCOMPARE(power.size(), 0);
        QCOMPARE(backend->status().current, Mode::Hybrid);
        QCOMPARE(backend->status().target, Mode::Hybrid);
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

    void demoConfirmedModeShowsTruthfulHeroAndSafePowerChoices_data() {
        QTest::addColumn<bool>("dismissWithReturn");
        QTest::newRow("click-later") << false;
        QTest::newRow("return-without-choice") << true;
    }

    void demoConfirmedModeShowsTruthfulHeroAndSafePowerChoices() {
        QFETCH(bool, dismissWithReturn);
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
        bool explicitButtonConsent = false;
        bool heroWasTruthful = false;
        bool allPowerChoicesPresent = false;
        bool laterWasDefault = false;
        bool powerButtonsCannotBecomeDefault = false;
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
                QPushButton *confirm = nullptr;
                for (auto *button : buttons->buttons())
                    if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole)
                        confirm = qobject_cast<QPushButton *>(button);
                if (!confirm) return;
                auto *target = dialog->findChild<QLabel *>(QStringLiteral("confirmationTarget"));
                explicitButtonConsent = !dialog->findChild<QLineEdit *>() && target &&
                    target->text() == QStringLiteral("Discrete") && confirm->isEnabled() &&
                    !confirm->isDefault() && !confirm->autoDefault();
                if (!confirm->isEnabled()) return;
                stage = Stage::PowerChoices;
                QTest::mouseClick(confirm, Qt::LeftButton);
            } else if (stage == Stage::PowerChoices) {
                QPushButton *later = nullptr;
                bool restart = false;
                bool powerOff = false;
                bool restartCannotDefault = false;
                bool powerOffCannotDefault = false;
                for (auto *button : buttons->buttons()) {
                    auto *push = qobject_cast<QPushButton *>(button);
                    if (!push) continue;
                    if (push->text() == QStringLiteral("Later")) later = push;
                    if (push->text() == QStringLiteral("Restart…")) {
                        restart = push->isEnabled();
                        restartCannotDefault = !push->autoDefault() && !push->isDefault();
                    }
                    if (push->text() == QStringLiteral("Shut down…")) {
                        powerOff = push->isEnabled();
                        powerOffCannotDefault = !push->autoDefault() && !push->isDefault();
                    }
                }
                if (!later) return;
                heroWasTruthful = window.m_hero->currentMode() == Mode::Hybrid &&
                    window.m_hero->targetMode() == Mode::Hybrid && !window.m_hero->pending() &&
                    window.m_hero->draftMode() == Mode::Discrete &&
                    window.m_hero->titleText() == QStringLiteral("Hybrid → Discrete") &&
                    backend->status().current == Mode::Hybrid && backend->status().target == Mode::Hybrid &&
                    !backend->status().pendingShutdown && backend->hasDraft();
                allPowerChoicesPresent = restart && powerOff && later->isEnabled();
                laterWasDefault = later->isDefault();
                powerButtonsCannotBecomeDefault = restartCannotDefault && powerOffCannotDefault;
                // Optional visual QA is confined to this inert test; normal test
                // runs write no screenshot and production has no capture hook.
                if (!captureDirectory.isEmpty()) {
                    const QDir directory(captureDirectory);
                    captureSaved = directory.exists() && dialog->grab().save(directory.filePath(QStringLiteral("power-dialog-en.png")));
                }
                stage = Stage::Done;
                inspector.stop();
                if (dismissWithReturn) QTest::keyClick(dialog, Qt::Key_Return);
                else QTest::mouseClick(later, Qt::LeftButton);
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
        QVERIFY(explicitButtonConsent);
        QVERIFY(heroWasTruthful);
        QVERIFY(allPowerChoicesPresent);
        QVERIFY(laterWasDefault);
        QVERIFY(powerButtonsCannotBecomeDefault);
        QVERIFY(captureSaved);
        QCOMPARE(applied.size(), 0);
        QCOMPARE(helperStarted.size(), 0);
        QCOMPARE(probeStarted.size(), 0);
        QCOMPARE(powerFinished.size(), 0);
        QCOMPARE(powerBusy.size(), 0);
        QVERIFY(!window.m_powerActions.busy());
        QVERIFY(!window.m_powerDialogOpen);
        QCOMPARE(backend->status().current, Mode::Hybrid);
        QCOMPARE(backend->status().target, Mode::Hybrid);
        QVERIFY(!backend->status().pendingShutdown);
        QCOMPARE(backend->draftMode(), Mode::Discrete);
    }

    void heroLocalDraftKeepsFirmwareTruth_data() { heroKeepsPendingDirection_data(); }
    void heroLocalDraftKeepsFirmwareTruth() {
        QFETCH(int, current);
        QFETCH(int, target);
        auto status = Status::demo();
        status.current = static_cast<Mode>(current);
        status.target = status.current;
        ModeHero hero;
        hero.setReducedMotion(true);
        const Mode draft = static_cast<Mode>(target);
        hero.setState(status, Language::English, QStringLiteral("Intel"), false, draft);
        QCOMPARE(hero.currentMode(), status.current);
        QCOMPARE(hero.targetMode(), status.current);
        QCOMPARE(hero.draftMode(), draft);
        QVERIFY(!hero.pending());
        QCOMPARE(hero.titleText(), modeName(status.current, Language::English) + QStringLiteral(" → ") + modeName(draft, Language::English));
        QVERIFY(hero.accessibleDescription().contains(Mux::tr(Text::HeroDraftDetail, Language::English).arg(modeName(status.current, Language::English), modeName(draft, Language::English))));
    }

    void draftCanBeCorrectedBeforeOneExplicitCommit() {
        Window window(true, Language::English, false);
        auto *backend = window.backend();
        QVERIFY(backend->setDemoModes(Mode::Discrete, Mode::Discrete));
        QSignalSpy applied(backend, &Backend::applyFinished);
        QSignalSpy helper(&backend->m_apply, &QProcess::started);
        QSignalSpy power(&window.m_powerActions, &PowerActions::finished);
        QVERIFY(chooseDraft(window, Mode::Integrated));
        QCOMPARE(backend->draftMode(), Mode::Integrated);
        QCOMPARE(backend->status().target, Mode::Discrete);
        QVERIFY(!backend->status().pendingShutdown);
        QCOMPARE(applied.size(), 0);
        QCOMPARE(power.size(), 0);
        QVERIFY(chooseDraft(window, Mode::Hybrid, 3));
        // Demo power reports a message instead of reaching any desktop API.
        QTimer closeDemoNotice;
        closeDemoNotice.setInterval(10);
        connect(&closeDemoNotice, &QTimer::timeout, &window, [] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) message->reject();
        });
        closeDemoNotice.start();
        QTRY_COMPARE_WITH_TIMEOUT(power.size(), 1, 4000);
        closeDemoNotice.stop();
        QCOMPARE(applied.size(), 1);
        QVERIFY(applied.first().first().toBool());
        QCOMPARE(qvariant_cast<PowerAction>(power.first().at(0)), PowerAction::PowerOff);
        QCOMPARE(qvariant_cast<PowerResult>(power.first().at(1)), PowerResult::Demo);
        QCOMPARE(backend->status().current, Mode::Discrete);
        QCOMPARE(backend->status().target, Mode::Hybrid);
        QVERIFY(backend->status().routinePending());
        QVERIFY(!backend->hasDraft());
        QCOMPARE(helper.size(), 0);
        QVERIFY(!window.m_powerIntent.has_value());
        // A duplicate callback cannot replay a consumed power intent.
        emit backend->applyFinished(true, false, QStringLiteral("synthetic_duplicate"));
        QTest::qWait(20);
        QCOMPARE(power.size(), 1);
    }

    void selectingCurrentOrClearButtonDiscardsOnlyLocalDraft() {
        Window window(true, Language::English, false);
        auto *backend = window.backend();
        QSignalSpy applied(backend, &Backend::applyFinished);
        QSignalSpy power(&window.m_powerActions, &PowerActions::finished);
        QVERIFY(chooseDraft(window, Mode::Integrated));
        window.requestMode(Mode::Hybrid);
        QTRY_VERIFY_WITH_TIMEOUT(!window.m_requestInFlight, 1000);
        QVERIFY(!backend->hasDraft());
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(chooseDraft(window, Mode::Discrete));
        QVERIFY(window.m_clearDraftButton->isVisible());
        QTest::mouseClick(window.m_clearDraftButton, Qt::LeftButton);
        QVERIFY(!backend->hasDraft());
        QCOMPARE(backend->status().current, Mode::Hybrid);
        QCOMPARE(backend->status().target, Mode::Hybrid);
        QCOMPARE(applied.size(), 0);
        QCOMPARE(power.size(), 0);
        QCOMPARE(backend->m_apply.state(), QProcess::NotRunning);
    }

    void localDraftNotificationNeverOpensPowerAndBlockedDraftCanBeCleared() {
        Window window(true, Language::English, false);
        auto *backend = window.backend();
        QVERIFY(backend->setDemoModes(Mode::Hybrid, Mode::Hybrid));
        QVERIFY(backend->selectMode(Mode::Integrated));
        QSignalSpy applied(backend, &Backend::applyFinished);
        QSignalSpy power(&window.m_powerActions, &PowerActions::finished);
        window.showPanel();
        QTest::qWait(30);
        QVERIFY(!window.m_powerDialogOpen);
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(backend->draftMode(), Mode::Integrated);
        backend->m_status.acPower = false;
        window.updateUi();
        QVERIFY(!window.m_shutdownButton->isEnabled());
        QVERIFY(window.m_clearDraftButton->isEnabled());
        window.shutdown();
        QVERIFY(!QApplication::activeModalWidget());
        backend->m_status.valid = false;
        window.updateUi();
        QVERIFY(!window.m_shutdownButton->isEnabled());
        window.shutdown();
        QVERIFY(!QApplication::activeModalWidget());
        window.clearDraft();
        QVERIFY(!backend->hasDraft());
        QCOMPARE(applied.size(), 0);
        QCOMPARE(power.size(), 0);
    }

    void powerPreflightLocksSelectionAndFirmwareCloseOnly() {
        Window window(true, Language::English, false);
        QVERIFY(window.backend()->setDemoModes(Mode::Hybrid, Mode::Hybrid));
        window.m_powerAwaitingRefresh = true;
        window.updateUi();
        QVERIFY(window.interactionBusy());
        QVERIFY(window.firmwareOperationBusy());
        QVERIFY(!window.m_modeButtons[2]->isEnabled());
        window.requestMode(Mode::Integrated);
        QCOMPARE(window.m_requestedMode, Mode::Unknown);
        QCloseEvent close;
        window.closeEvent(&close);
        QVERIFY(!close.isAccepted());
        window.m_powerAwaitingRefresh = false;
        QVERIFY(!window.firmwareOperationBusy());
    }

    void changedDraftWhilePowerDialogIsOpenRejectsCommit() {
        Window window(true, Language::English, false);
        auto *backend = window.backend();
        QSignalSpy applied(backend, &Backend::applyFinished);
        QSignalSpy power(&window.m_powerActions, &PowerActions::finished);
        bool changed = false;
        QVERIFY(chooseDraft(window, Mode::Integrated, 3, [&] { changed = backend->selectMode(Mode::Discrete); }));
        QVERIFY(changed);
        QTest::qWait(40);
        QCOMPARE(backend->draftMode(), Mode::Discrete);
        QCOMPARE(backend->status().target, Mode::Hybrid);
        QCOMPARE(applied.size(), 0);
        QCOMPARE(power.size(), 0);
        QVERIFY(!window.interactionBusy());
    }

    void cancelledCommitConsumesPowerIntentWithoutPower() {
        Window window(true, Language::English, false);
        auto *backend = window.backend();
        backend->refresh();
        QSignalSpy power(&window.m_powerActions, &PowerActions::finished);
        window.m_committingDraft = true;
        window.m_powerIntent = PowerAction::Restart;
        window.m_powerCurrent = Mode::Hybrid;
        window.m_powerDraft = Mode::Discrete;
        QTimer closeNotice;
        closeNotice.setInterval(10);
        connect(&closeNotice, &QTimer::timeout, &window, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        closeNotice.start();
        emit backend->applyFinished(false, true, QStringLiteral("synthetic_polkit_cancel"));
        closeNotice.stop();
        QCOMPARE(power.size(), 0);
        QVERIFY(!window.m_powerIntent.has_value());
        QVERIFY(!window.m_committingDraft);
        QCOMPARE(backend->m_apply.state(), QProcess::NotRunning);
    }

    void rejectedCommitAdmissionClearsWindowPowerIntent() {
        Window window(true, Language::English, false);
        auto *backend = window.backend();
        QVERIFY(backend->setDemoModes(Mode::Hybrid, Mode::Hybrid));
        QVERIFY(backend->selectMode(Mode::Integrated));
        QSignalSpy applied(backend, &Backend::applyFinished);
        QSignalSpy power(&window.m_powerActions, &PowerActions::finished);
        backend->m_finishing = true;
        window.m_powerAwaitingRefresh = true;
        window.m_powerIntent = PowerAction::Restart;
        window.m_powerCurrent = Mode::Hybrid;
        window.m_powerTarget = Mode::Hybrid;
        window.m_powerDraft = Mode::Integrated;
        window.finishPowerPreflight();
        QVERIFY(!window.m_committingDraft);
        QVERIFY(!window.m_powerIntent.has_value());
        QVERIFY(!window.interactionBusy());
        QCOMPARE(applied.size(), 0);
        QCOMPARE(power.size(), 0);
        QCOMPARE(backend->m_apply.state(), QProcess::NotRunning);
        backend->m_finishing = false;
    }

    void errorDialogBlocksModePowerAndDraftReentry() {
        Window window(true, Language::English, false);
        auto *backend = window.backend();
        QVERIFY(backend->setDemoModes(Mode::Hybrid, Mode::Hybrid));
        QVERIFY(backend->selectMode(Mode::Integrated));
        QSignalSpy draft(backend, &Backend::draftChanged);
        QSignalSpy applied(backend, &Backend::applyFinished);
        QSignalSpy power(&window.m_powerActions, &PowerActions::finished);
        bool guarded = false;
        bool timedOut = false;
        QTimer inspector;
        inspector.setInterval(10);
        connect(&inspector, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            inspector.stop();
            guarded = window.m_infoDialogOpen;
            window.requestMode(Mode::Discrete);
            window.shutdown();
            window.clearDraft();
            guarded = guarded && !window.m_requestInFlight && !window.m_powerDialogOpen &&
                !window.m_powerAwaitingRefresh && backend->draftMode() == Mode::Integrated;
            dialog->reject();
        });
        QTimer watchdog;
        watchdog.setInterval(3000);
        connect(&watchdog, &QTimer::timeout, &window, [&] {
            timedOut = true;
            watchdog.setInterval(10);
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        inspector.start();
        watchdog.start();
        window.showDetails(QStringLiteral("Synthetic error"), QStringLiteral("Inert failure notice"), {});
        watchdog.stop();
        QVERIFY(!timedOut);
        QVERIFY(guarded);
        QVERIFY(!window.m_infoDialogOpen);
        QCOMPARE(draft.size(), 0);
        QCOMPARE(applied.size(), 0);
        QCOMPARE(power.size(), 0);
        QCOMPARE(backend->m_apply.state(), QProcess::NotRunning);
    }

    void changedFirmwareWhilePowerDialogIsOpenRejectsPower() {
        Window window(true, Language::English, false);
        auto *backend = window.backend();
        QVERIFY(backend->setDemoModes(Mode::Discrete, Mode::Hybrid));
        QSignalSpy power(&window.m_powerActions, &PowerActions::finished);
        bool inspected = false;
        QTimer inspector;
        inspector.setInterval(10);
        connect(&inspector, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!buttons) return;
            for (auto *button : buttons->buttons()) if (button->text() == window.t(Text::Shutdown)) {
                inspector.stop();
                backend->setDemoModes(Mode::Discrete, Mode::Integrated);
                inspected = true;
                button->click();
                return;
            }
        });
        QTimer watchdog;
        watchdog.setInterval(3000);
        connect(&watchdog, &QTimer::timeout, &window, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        inspector.start();
        watchdog.start();
        window.shutdown();
        QVERIFY(inspected);
        QCOMPARE(power.size(), 0);
        QCOMPARE(backend->status().target, Mode::Integrated);
        QVERIFY(!window.m_powerIntent.has_value());
    }

    void orderlyDestructionDoesNotKillApply() {
        QTemporaryDir directory;
        const auto marker = directory.filePath(QStringLiteral("completed"));
        {
            Backend backend(false);
            backend.m_testApplyLauncher = [] {};
            backend.m_apply.setProgram(script(directory, "sleep 0.15\nprintf 'completed' > '" + marker.toUtf8() + "'\n"));
            backend.m_apply.start(QIODevice::ReadOnly);
            QVERIFY(backend.m_apply.waitForStarted(3000));
            backend.m_applying = true;
        }
        QVERIFY(QFileInfo::exists(marker));
    }
};
}

int main(int argc, char **argv) {
    // Even non-demo read-only fixture tests get a private selection/settings
    // namespace. No test may discover or rewrite the desktop user's draft.
    QTemporaryDir home;
    if (!home.isValid()) return 1;
    qputenv("XDG_CONFIG_HOME", home.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_DATA_HOME", home.filePath(QStringLiteral("data")).toUtf8());
    qputenv("XDG_STATE_HOME", home.filePath(QStringLiteral("state")).toUtf8());
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MSI-MUX-Inert-Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("msi-mux-ui-tests"));
    Mux::UiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_ui.moc"
