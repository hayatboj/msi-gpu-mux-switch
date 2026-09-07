#pragma once

#include "status.h"
#include "translations.h"
#include <QColor>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QHideEvent;
class QShowEvent;

namespace Mux {
// Physical state comes from the firmware snapshot. An optional local selection
// is labeled separately and never promoted to a firmware target or current mode.
class ModeHero final : public QWidget {
    Q_OBJECT
public:
    explicit ModeHero(QWidget *parent = nullptr);
    void setState(const Status &status, Language language, const QString &panel, bool busy = false, Mode draft = Mode::Unknown);
    void setReducedMotion(bool enabled);
    bool reducedMotion() const { return m_reducedMotion; }
    bool animationRunning() const { return m_frames.isActive(); }
    Mode currentMode() const { return m_current; }
    Mode targetMode() const { return m_target; }
    bool pending() const { return m_pending; }
    Mode draftMode() const { return m_draft; }
    QString titleText() const;
    static QColor accentColor(Mode mode);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateAnimation();
    Mode m_current = Mode::Unknown;
    Mode m_target = Mode::Unknown;
    Mode m_draft = Mode::Unknown;
    bool m_pending = false;
    bool m_reducedMotion = false;
    QTimer m_frames;
    QElapsedTimer m_elapsed;
    QLabel *m_caption = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_description = nullptr;
    QLabel *m_badge = nullptr;
    QLabel *m_panel = nullptr;
    QLabel *m_power = nullptr;
};
}
