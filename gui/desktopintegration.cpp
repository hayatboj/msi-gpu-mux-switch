#include "desktopintegration.h"

#include <QStringList>

namespace Mux {

bool DesktopIntegration::isGnome(const QString &desktop, const QString &sessionDesktop) {
    bool hasDesktopName = false;
    for (const auto &entry : desktop.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const auto name = entry.trimmed();
        if (name.isEmpty()) continue;
        hasDesktopName = true;
        if (name.compare(QLatin1String("GNOME"), Qt::CaseInsensitive) == 0 ||
            name.compare(QLatin1String("GNOME-Classic"), Qt::CaseInsensitive) == 0)
            return true;
    }
    // A current desktop identity takes priority over an inherited session hint.
    if (hasDesktopName) return false;
    const auto session = sessionDesktop.trimmed();
    for (const auto *name : {"gnome", "gnome-wayland", "gnome-xorg", "gnome-classic",
                             "ubuntu", "ubuntu-wayland", "ubuntu-xorg"}) {
        if (session.compare(QLatin1String(name), Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

DesktopIntegration DesktopIntegration::detect(bool trayAvailable,
                                               const QString &desktop,
                                               const QString &sessionDesktop) {
    return {isGnome(desktop, sessionDesktop), trayAvailable};
}

QUrl DesktopIntegration::gnomeSetupUrl() {
    return QUrl(QStringLiteral("https://extensions.gnome.org/extension/615/appindicator-support/"));
}

}
