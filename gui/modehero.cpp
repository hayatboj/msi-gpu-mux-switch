#include "modehero.h"

#include <QHBoxLayout>
#include <QEvent>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QVBoxLayout>
#include <cmath>

namespace Mux {
namespace {
QColor baseColor(Mode mode) {
    switch (mode) {
    case Mode::Discrete: return QColor(QStringLiteral("#6c1a2a"));
    case Mode::Hybrid: return QColor(QStringLiteral("#68501d"));
    case Mode::Integrated: return QColor(QStringLiteral("#155548"));
    case Mode::Unknown: return QColor(QStringLiteral("#293c4b"));
    }
    return {};
}

QLabel *heroLabel(QWidget *parent, const QString &style, bool wrap = false) {
    auto *label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(wrap);
    label->setStyleSheet(style);
    return label;
}

void drawSymbol(QPainter &painter, Mode mode, const QRectF &bounds, const QColor &accent, double phase) {
    painter.save();
    painter.translate(bounds.center());
    const qreal scale = qMin(bounds.width(), bounds.height()) / 128.0;
    painter.scale(scale, scale);
    painter.translate(0, std::sin(phase * 0.75) * 2.2);
    const QColor pearl(QStringLiteral("#fff8ee"));
    painter.setPen(QPen(QColor(255, 255, 255, 28), 1.0));
    painter.setBrush(QColor(255, 255, 255, 5));
    painter.drawEllipse(QPointF(0, 0), 56, 56);
    painter.drawEllipse(QPointF(0, 0), 43, 43);
    if (mode == Mode::Discrete) {
        const qreal entry = phase > 0 ? 24 * std::pow(qMax(0.0, 1.0 - phase / 0.9), 3) : 0;
        painter.translate(0, entry);
        painter.rotate(30);
        painter.setPen(Qt::NoPen);
        QPainterPath flame;
        flame.moveTo(-12, 23);
        flame.cubicTo(-19, 41, -7, 49 + std::sin(phase * 2.0) * 4, 0, 55);
        flame.cubicTo(9, 45, 18, 35, 12, 23);
        painter.setBrush(accent);
        painter.drawPath(flame);
        QPainterPath fins;
        fins.moveTo(-16, 0); fins.lineTo(-30, 23); fins.lineTo(-13, 19);
        fins.moveTo(16, 0); fins.lineTo(30, 23); fins.lineTo(13, 19);
        painter.setBrush(accent.lighter(120));
        painter.drawPath(fins);
        QPainterPath body;
        body.moveTo(0, -48);
        body.cubicTo(-25, -29, -22, 6, -13, 26);
        body.lineTo(13, 26);
        body.cubicTo(22, 6, 25, -29, 0, -48);
        painter.setBrush(pearl);
        painter.drawPath(body);
        painter.setPen(QPen(accent.darker(165), 3));
        painter.setBrush(accent.lighter(130));
        painter.drawEllipse(QPointF(0, -16), 9, 9);
        painter.setPen(QPen(accent.darker(135), 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(-7, 17), QPointF(7, 17));
    } else if (mode == Mode::Integrated) {
        painter.rotate(-16 + std::sin(phase * 0.6) * 1.5);
        QPainterPath leaf;
        leaf.moveTo(-31, 33);
        leaf.cubicTo(-52, -11, -10, -42, 39, -43);
        leaf.cubicTo(48, 11, 9, 46, -31, 33);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent.lighter(125));
        painter.drawPath(leaf);
        painter.setPen(QPen(baseColor(mode), 3.5, Qt::SolidLine, Qt::RoundCap));
        QPainterPath stem;
        stem.moveTo(-39, 48); stem.cubicTo(-18, 20, 7, -7, 28, -29);
        painter.drawPath(stem);
        painter.drawLine(QPointF(-12, 15), QPointF(-22, -6));
        painter.drawLine(QPointF(0, 1), QPointF(21, 7));
        painter.drawLine(QPointF(12, -14), QPointF(7, -28));
    } else if (mode == Mode::Hybrid) {
        painter.setPen(QPen(accent.lighter(120), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(0, -25), QPointF(0, 38));
        painter.drawLine(QPointF(-22, 38), QPointF(22, 38));
        const qreal tilt = std::sin(phase * 0.65) * 2;
        painter.drawLine(QPointF(-36, -16 + tilt), QPointF(36, -16 - tilt));
        for (int side : {-1, 1}) {
            const qreal x = side * 31;
            const qreal y = -15 - side * tilt;
            painter.drawLine(QPointF(x, y), QPointF(x - 15, 13));
            painter.drawLine(QPointF(x, y), QPointF(x + 15, 13));
            QPainterPath pan;
            pan.moveTo(x - 16, 13); pan.quadTo(x, 35, x + 16, 13); pan.closeSubpath();
            painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 65));
            painter.drawPath(pan);
            painter.setBrush(Qt::NoBrush);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(pearl);
        painter.drawEllipse(QPointF(0, -30), 8, 8);
        painter.setPen(QPen(accent, 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(0, -47), QPointF(0, -53));
        painter.drawLine(QPointF(-15, -43), QPointF(-19, -47));
        painter.drawLine(QPointF(15, -43), QPointF(19, -47));
    } else {
        painter.setPen(QPen(accent, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(-27, -27, 54, 54), 10, 10);
        painter.drawLine(QPointF(-12, 0), QPointF(12, 0));
    }
    painter.restore();
}
}

QColor ModeHero::accentColor(Mode mode) {
    switch (mode) {
    case Mode::Discrete: return QColor(QStringLiteral("#ff858c"));
    case Mode::Hybrid: return QColor(QStringLiteral("#ffda88"));
    case Mode::Integrated: return QColor(QStringLiteral("#86edc6"));
    case Mode::Unknown: return QColor(QStringLiteral("#afc3d3"));
    }
    return {};
}

ModeHero::ModeHero(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(274);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setObjectName(QStringLiteral("modeHero"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(25, 24, 25, 22);
    layout->setSpacing(14);
    auto *top = new QHBoxLayout;
    m_caption = heroLabel(this, QStringLiteral("color:#eee1cb; font-size:10px; font-weight:700; letter-spacing:1.6px; background:transparent;"));
    m_badge = heroLabel(this, QStringLiteral("color:#fff9ee; font-size:10px; font-weight:600; padding:6px 9px; background:rgba(255,255,255,0.10); border-radius:8px;"));
    top->addWidget(m_caption, 1);
    top->addWidget(m_badge);
    layout->addLayout(top);
    auto *middle = new QHBoxLayout;
    auto *text = new QVBoxLayout;
    text->setSpacing(9);
    m_title = heroLabel(this, QStringLiteral("color:#fffaf4; font-size:38px; font-weight:750; background:transparent;"), true);
    m_description = heroLabel(this, QStringLiteral("color:#f0e4d7; font-size:12px; background:transparent;"), true);
    text->addWidget(m_title);
    text->addWidget(m_description);
    text->addStretch(1);
    middle->addLayout(text, 1);
    middle->addSpacing(128);
    layout->addLayout(middle, 1);
    auto *bottom = new QHBoxLayout;
    m_panel = heroLabel(this, QStringLiteral("color:#fff7ec; font-size:11px; background:transparent;"), true);
    m_power = heroLabel(this, QStringLiteral("color:#f5ecdf; font-size:11px; background:transparent;"));
    bottom->addWidget(m_panel, 1);
    bottom->addSpacing(10);
    bottom->addWidget(m_power);
    layout->addLayout(bottom);
    m_frames.setInterval(42);
    connect(&m_frames, &QTimer::timeout, this, [this] {
        if (!m_pending && m_draft == Mode::Unknown && m_elapsed.elapsed() > 6500) m_frames.stop();
        update();
    });
    m_elapsed.start();
}

void ModeHero::setState(const Status &status, Language language, const QString &panel, bool busy, Mode draft) {
    const Mode current = status.valid ? status.current : Mode::Unknown;
    const Mode target = status.valid ? status.target : Mode::Unknown;
    const bool pending = status.routinePending();
    const bool local = status.valid && status.expectedHardware && status.newSwitchSupported &&
        status.bios == QLatin1String("E15M3IMS.116") && status.blockCode != QLatin1String("recovery_required") &&
        status.firmwareAvailable && status.firmwareValid &&
        !status.pendingShutdown && current != Mode::Unknown && current == target && draft != current &&
        (draft == Mode::Hybrid || (draft == Mode::Discrete && status.discreteSupported) ||
         (draft == Mode::Integrated && status.integratedSupported));
    const Mode selected = local ? draft : Mode::Unknown;
    const bool transition = pending || local;
    const Mode destination = local ? selected : target;
    const bool changed = current != m_current || target != m_target || pending != m_pending || selected != m_draft;
    m_current = current;
    m_target = target;
    m_pending = pending;
    m_draft = selected;
    if (changed) m_elapsed.restart();
    m_caption->setText(Mux::tr(local ? Text::CurrentToSelection : pending ? Text::CurrentToTarget : Text::CurrentMode, language));
    m_title->setStyleSheet(QStringLiteral("color:#fffaf4; font-size:%1px; font-weight:750; background:transparent;").arg(transition ? 30 : 38));
    m_title->setText(transition ? modeName(current, language) + QStringLiteral(" → ") + modeName(destination, language) : modeName(current, language));
    Text description = current == Mode::Discrete ? Text::DiscreteDescription : current == Mode::Integrated ?
        Text::IntegratedDescription : current == Mode::Hybrid ? Text::HybridDescription : Text::Reading;
    m_description->setText(transition ? Mux::tr(local ? Text::HeroDraftDetail : Text::HeroPendingDetail, language).arg(modeName(current, language), modeName(destination, language)) : Mux::tr(description, language));
    m_badge->setText(Mux::tr(busy ? Text::Applying : local ? Text::SelectionSaved : pending ? Text::PowerCyclePending : Text::LiveMode, language));
    m_panel->setText(Mux::tr(Text::InternalPanel, language) + QStringLiteral(" · ") + panel);
    m_power->setText(Mux::tr(status.valid ? (status.acPower ? Text::AcConnected : Text::Battery) : Text::Unknown, language));
    setAccessibleName(m_caption->text() + QStringLiteral(": ") + m_title->text());
    setAccessibleDescription(m_description->text() + QStringLiteral(". ") + m_badge->text());
    updateAnimation();
    update();
}

void ModeHero::setReducedMotion(bool enabled) {
    m_reducedMotion = enabled;
    updateAnimation();
    update();
}

QString ModeHero::titleText() const { return m_title->text(); }

void ModeHero::updateAnimation() {
    if (isVisible() && !window()->isMinimized() && !m_reducedMotion && (m_pending || m_draft != Mode::Unknown || m_elapsed.elapsed() < 6500) && m_current != Mode::Unknown) {
        if (!m_frames.isActive()) m_frames.start();
    } else {
        m_frames.stop();
    }
}

void ModeHero::showEvent(QShowEvent *event) { QWidget::showEvent(event); window()->installEventFilter(this); updateAnimation(); }
void ModeHero::hideEvent(QHideEvent *event) { m_frames.stop(); QWidget::hideEvent(event); }
bool ModeHero::eventFilter(QObject *watched, QEvent *event) {
    if (watched == window() && event->type() == QEvent::WindowStateChange) updateAnimation();
    return QWidget::eventFilter(watched, event);
}

void ModeHero::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), 19, 19);
    painter.setClipPath(clip);
    const double phase = !m_reducedMotion && m_frames.isActive() ? qMax(0.001, m_elapsed.elapsed() / 1000.0) : 0.0;
    const bool transition = m_pending || m_draft != Mode::Unknown;
    const Mode destination = m_draft != Mode::Unknown ? m_draft : m_target;
    const QColor from = baseColor(m_current);
    const QColor to = baseColor(transition ? destination : m_current);
    QLinearGradient gradient(QPointF(0, 0), QPointF(width(), height() * 0.7));
    gradient.setColorAt(0, from);
    gradient.setColorAt(1, to.darker(190));
    if (transition) {
        gradient.setColorAt(0.42 + std::sin(phase * 0.6) * 0.09, from.darker(120));
        gradient.setColorAt(0.72 + std::sin(phase * 0.6) * 0.07, to);
    }
    painter.fillRect(rect(), gradient);
    QRadialGradient glow(QPointF(width() - 62, 95), width() * 0.58);
    const QColor accent = accentColor(transition ? destination : m_current);
    glow.setColorAt(0, QColor(accent.red(), accent.green(), accent.blue(), 43));
    glow.setColorAt(1, QColor(accent.red(), accent.green(), accent.blue(), 0));
    painter.fillRect(rect(), glow);
    painter.setPen(QPen(QColor(255, 255, 255, 22), 1));
    painter.drawLine(QPointF(25, height() - 55), QPointF(width() - 25, height() - 55));
    const QRectF symbol(width() - 158, 73, 122, 133);
    drawSymbol(painter, transition ? destination : m_current, symbol, accent, phase);
    if (transition) {
        // The small source symbol and arrow preserve direction for every pair.
        drawSymbol(painter, m_current, QRectF(width() - 184, 162, 41, 41), accentColor(m_current), 0);
        painter.setPen(QPen(QColor(255, 247, 236, 165), 1.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(width() - 141, 183), QPointF(width() - 128, 183));
        painter.drawLine(QPointF(width() - 132, 179), QPointF(width() - 128, 183));
        painter.drawLine(QPointF(width() - 132, 187), QPointF(width() - 128, 183));
    }
}
}
