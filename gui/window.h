#pragma once

#include "backend.h"
#include "poweractions.h"
#include "translations.h"
#include <QMainWindow>
#include <QSettings>
#include <QSystemTrayIcon>
#include <array>
#include <optional>

class QAction;
class QCheckBox;
class QLabel;
class QMenu;
class QPushButton;
class QToolButton;
class QFrame;

namespace Mux {
class ModeHero;
class Window final : public QMainWindow {
    Q_OBJECT
public:
    Window(bool demo, Language language, bool trayEnabled = true, QWidget *parent = nullptr);
    void showPanel();
    void requestMode(Mode mode);
    bool hasTray() const { return m_tray.isVisible() && QSystemTrayIcon::isSystemTrayAvailable(); }
    Backend *backend() { return &m_backend; }
    bool interactionBusy() const;
    bool firmwareOperationBusy() const;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    friend class UiTests;
    void buildUi();
    void buildMenus();
    void updateUi();
    void applyMode(Mode mode);
    void shutdown();
    void finishPowerPreflight();
    void clearDraft();
    void showAbout(bool changes = false);
    void showDetails(const QString &title, const QString &message, const QString &details);
    void setLanguage(Language language);
    void setAutostart(bool enabled);
    void setReducedMotion(bool enabled);
    void requestQuit();
    QString t(Text text) const { return Mux::tr(text, m_language); }
    QString panelLabel() const;

    Backend m_backend;
    PowerActions m_powerActions;
    Language m_language;
    QSettings m_settings;
    QSystemTrayIcon m_tray;
    QTimer m_refreshTimer;
    QMenu *m_trayMenu = nullptr;
    QMenu *m_preferencesMenu = nullptr;
    QMenu *m_languageMenu = nullptr;
    QAction *m_currentAction = nullptr;
    QAction *m_pendingAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_refreshAction = nullptr;
    QAction *m_autostartAction = nullptr;
    QAction *m_reducedMotionAction = nullptr;
    QAction *m_aboutAction = nullptr;
    QAction *m_whatsNewAction = nullptr;
    QAction *m_gnomeSetupAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_englishAction = nullptr;
    QAction *m_turkishAction = nullptr;
    QAction *m_shutdownAction = nullptr;
    QAction *m_clearDraftAction = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_demoLabel = nullptr;
    ModeHero *m_hero = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_chooseLabel = nullptr;
    QLabel *m_pendingTitle = nullptr;
    QLabel *m_pendingDescription = nullptr;
    QFrame *m_pendingCard = nullptr;
    QPushButton *m_shutdownButton = nullptr;
    QPushButton *m_clearDraftButton = nullptr;
    QLabel *m_version = nullptr;
    QPushButton *m_aboutButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QToolButton *m_preferencesButton = nullptr;
    QToolButton *m_enButton = nullptr;
    QToolButton *m_trButton = nullptr;
    std::array<QFrame *, 3> m_modeCards{};
    std::array<QLabel *, 3> m_modeNames{};
    std::array<QLabel *, 3> m_modeDescriptions{};
    std::array<QPushButton *, 3> m_modeButtons{};
    std::array<QAction *, 3> m_modeActions{};
    QString m_statusError;
    QString m_lastDetails;
    bool m_quitting = false;
    bool m_trayEnabled = true;
    bool m_reducedMotion = false;
    Mode m_requestedMode = Mode::Unknown;
    bool m_requestInFlight = false;
    bool m_confirming = false;
    bool m_powerDialogOpen = false;
    bool m_infoDialogOpen = false;
    bool m_powerAwaitingRefresh = false;
    bool m_committingDraft = false;
    std::optional<PowerAction> m_powerIntent;
    Mode m_powerCurrent = Mode::Unknown;
    Mode m_powerTarget = Mode::Unknown;
    Mode m_powerDraft = Mode::Unknown;
};
}
