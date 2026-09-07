#pragma once

#include <QString>
#include <QUrl>

namespace Mux {

// Presentation hints only. Desktop names never grant permission to switch modes.
struct DesktopIntegration {
    bool gnome = false;
    bool trayAvailable = false;

    bool needsGnomeSetup() const { return gnome && !trayAvailable; }

    static bool isGnome(
        const QString &desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP"),
        const QString &sessionDesktop = qEnvironmentVariable("XDG_SESSION_DESKTOP"));
    static DesktopIntegration detect(
        bool trayAvailable,
        const QString &desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP"),
        const QString &sessionDesktop = qEnvironmentVariable("XDG_SESSION_DESKTOP"));
    static QUrl gnomeSetupUrl();
};

}
