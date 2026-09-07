#pragma once

#include "backend.h"
#include "translations.h"
#include <QMainWindow>
#include <QSettings>
#include <QSystemTrayIcon>
#include <array>

class QAction;
class QCheckBox;
class QLabel;
class QMenu;
class QPushButton;
class QToolButton;
class QFrame;

namespace Mux {
class Window final : public QMainWindow {
    Q_OBJECT
public:
    Window(bool demo, Language language, bool trayEnabled = true, QWidget *parent = nullptr);
    void showPanel();
    bool hasTray() const { return m_tray.isVisible(); }
    Backend *backend() { return &m_backend; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildMenus();
    void updateUi();
    void applyMode(Mode mode);
    void shutdown();
    void showDetails(const QString &title, const QString &message, const QString &details);
    void setLanguage(Language language);
    void setAutostart(bool enabled);
    void requestQuit();
    QString t(Text text) const { return Mux::tr(text, m_language); }
    QString panelLabel() const;

    Backend m_backend;
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
    QAction *m_quitAction = nullptr;
    QAction *m_englishAction = nullptr;
    QAction *m_turkishAction = nullptr;
    QAction *m_shutdownAction = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_demoLabel = nullptr;
    QLabel *m_currentCaption = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_modeDescription = nullptr;
    QLabel *m_panelCaption = nullptr;
    QLabel *m_panelValue = nullptr;
    QLabel *m_powerLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_chooseLabel = nullptr;
    QLabel *m_pendingTitle = nullptr;
    QLabel *m_pendingDescription = nullptr;
    QFrame *m_pendingCard = nullptr;
    QPushButton *m_shutdownButton = nullptr;
    QLabel *m_deviceCaption = nullptr;
    QLabel *m_deviceValue = nullptr;
    QLabel *m_biosCaption = nullptr;
    QLabel *m_biosValue = nullptr;
    QLabel *m_compatibilityLabel = nullptr;
    QLabel *m_experimentalTitle = nullptr;
    QLabel *m_experimentalDetail = nullptr;
    QLabel *m_version = nullptr;
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
    bool m_successfulApply = false;
    bool m_quitting = false;
    bool m_trayEnabled = true;
};
}
