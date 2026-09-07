#pragma once

#include "status.h"
#include "translations.h"
#include <QDialog>

namespace Mux {
class AboutDialog final : public QDialog {
public:
    AboutDialog(Language language, const Status &status, QWidget *parent = nullptr, bool showChanges = false);
    static QString diagnosticsText(Language language, const Status &status);
};
}
