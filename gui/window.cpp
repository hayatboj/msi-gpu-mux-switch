#include "window.h"
#include "aboutdialog.h"
#include "desktopintegration.h"
#include "modehero.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScreen>
#include <QStandardPaths>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace Mux {
static constexpr std::array<Mode, 3> modes{Mode::Hybrid, Mode::Discrete, Mode::Integrated};
static constexpr std::array<Text, 3> descriptions{Text::HybridDescription, Text::DiscreteDescription, Text::IntegratedDescription};

static QLabel *label(QWidget *parent, const QString &name = {}, bool wrap = false) {
    auto *result = new QLabel(parent);
    result->setObjectName(name);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(wrap);
    return result;
}

static QString autostartPath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
        QStringLiteral("/autostart/org.hayatboj.msimux.desktop");
}

static QIcon modeTrayIcon(const QIcon &base, Mode mode, bool pending) {
    QPixmap pixmap = base.pixmap(64, 64);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(QStringLiteral("#ffffff")), 2));
    painter.setBrush(QColor(mode == Mode::Discrete ? QStringLiteral("#ac3345") :
        mode == Mode::Integrated ? QStringLiteral("#167d69") :
        mode == Mode::Hybrid ? QStringLiteral("#946518") : QStringLiteral("#58656b")));
    painter.drawRoundedRect(QRectF(33, 33, 29, 29), 9, 9);
    QFont font = painter.font();
    font.setPixelSize(21);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    const QString letter = mode == Mode::Hybrid ? QStringLiteral("H") :
        mode == Mode::Discrete ? QStringLiteral("D") :
        mode == Mode::Integrated ? QStringLiteral("I") : QStringLiteral("?");
    painter.drawText(QRect(33, 33, 29, 29), Qt::AlignCenter, letter);
    if (pending) {
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(QColor(QStringLiteral("#e9ab40")));
        painter.drawEllipse(QPointF(55, 10), 8, 8);
    }
    return QIcon(pixmap);
}

Window::Window(bool demo, Language language, bool trayEnabled, QWidget *parent)
    : QMainWindow(parent), m_backend(demo, this), m_powerActions(demo, this), m_language(language), m_settings(), m_tray(this), m_trayEnabled(trayEnabled) {
    setWindowTitle(QStringLiteral("MSI MUX"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/msi-mux.svg")));
    m_reducedMotion = !demo && m_settings.value(QStringLiteral("reducedMotion"), false).toBool();
    setMinimumSize(590, 640);
    const int availableHeight = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry().height() : 980;
    resize(680, qBound(640, availableHeight - 80, 900));
    buildUi();
    buildMenus();
    connect(&m_backend, &Backend::statusChanged, this, [this] {
        if (m_backend.status().valid) m_statusError.clear();
        updateUi();
        if (m_powerAwaitingRefresh) {
            QTimer::singleShot(0, this, &Window::finishPowerPreflight);
        }
        if (m_requestedMode != Mode::Unknown) {
            const auto mode = m_requestedMode;
            m_requestedMode = Mode::Unknown;
            QTimer::singleShot(0, this, [this, mode] {
                if (m_backend.canSelectMode(mode)) applyMode(mode);
                else showDetails(QStringLiteral("MSI MUX"), blockText(m_backend.status(), m_language).isEmpty() ?
                    t(Text::NoChanges) : blockText(m_backend.status(), m_language), m_lastDetails);
                m_requestInFlight = false;
            });
        }
    });
    connect(&m_backend, &Backend::draftChanged, this, &Window::updateUi);
    connect(&m_backend, &Backend::refreshingChanged, this, [this](bool) { updateUi(); });
    connect(&m_backend, &Backend::busyChanged, this, [this](bool busy) {
        updateUi();
        if (busy) showPanel();
    });
    connect(&m_backend, &Backend::statusError, this, [this](const QString &code, const QString &details) {
        m_statusError = code;
        m_lastDetails = details;
        updateUi();
    });
    connect(&m_backend, &Backend::applyFinished, this, [this](bool success, bool cancelled, const QString &details) {
        // A completion can authorize power only for the one explicit choice
        // that initiated this commit. Clear it before invoking any other API.
        const auto intent = m_committingDraft ? m_powerIntent : std::nullopt;
        m_committingDraft = false;
        m_powerIntent.reset();
        m_lastDetails = details;
        updateUi();
        showPanel();
        const auto &status = m_backend.status();
        if (success && intent && status.routinePending() &&
            status.current == m_powerCurrent && status.target == m_powerDraft) {
            m_powerActions.request(*intent);
        } else if (!success) {
            showDetails(t(Text::ApplyFailed), t(cancelled ? Text::PermissionDenied : Text::ApplyUncertain), details);
        }
    });
    connect(&m_powerActions, &PowerActions::busyChanged, this, [this](bool) { updateUi(); });
    connect(&m_powerActions, &PowerActions::finished, this, [this](PowerAction, PowerResult result, const QString &details) {
        Text text = Text::PowerRequestFailed;
        switch (result) {
        case PowerResult::Requested: text = Text::PowerRequestSent; break;
        case PowerResult::Cancelled: text = Text::PowerRequestCancelled; break;
        case PowerResult::Denied: text = Text::PowerRequestDenied; break;
        case PowerResult::Unavailable: text = Text::PowerRequestUnavailable; break;
        case PowerResult::Failed: text = Text::PowerRequestFailed; break;
        case PowerResult::Demo: text = Text::PowerRequestDemo; break;
        }
        updateUi();
        if (result == PowerResult::Requested) {
            m_tray.showMessage(QStringLiteral("MSI MUX"), t(text), QSystemTrayIcon::Information, 8000);
        } else {
            showDetails(QStringLiteral("MSI MUX"), t(text), details);
        }
    });
    m_refreshTimer.setInterval(15000);
    connect(&m_refreshTimer, &QTimer::timeout, &m_backend, &Backend::refresh);
    m_refreshTimer.start();
    updateUi();
    QTimer::singleShot(0, &m_backend, &Backend::refresh);
}

void Window::buildUi() {
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QString background = dark ? QStringLiteral("#1d2328") : QStringLiteral("#f4f6f7");
    const QString card = dark ? QStringLiteral("#272f35") : QStringLiteral("#ffffff");
    const QString ink = dark ? QStringLiteral("#f1f6f5") : QStringLiteral("#182d32");
    const QString muted = dark ? QStringLiteral("#a9babf") : QStringLiteral("#5a7078");
    const QString line = dark ? QStringLiteral("#3a464c") : QStringLiteral("#dce4e7");
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget#body { background:%1; color:%3; }"
        "QLabel { color:%3; background:transparent; }"
        "QLabel#subtitle, QLabel#muted, QLabel#caption, QLabel#version { color:%4; }"
        "QLabel#title { font-size:28px; font-weight:750; letter-spacing:-1px; }"
        "QLabel#caption { font-size:10px; font-weight:700; letter-spacing:1.5px; }"
        "QLabel#modeName { font-size:15px; font-weight:650; }"
        "QLabel#muted { font-size:12px; }"
        "QLabel#version { font-size:11px; }"
        "QLabel#demo { color:#09644f; background:#d4f6e9; padding:7px 12px; border-radius:7px; font-size:11px; font-weight:700; }"
        "QFrame#modeCard { background:%2; border:1px solid %5; border-radius:12px; }"
        "QFrame#modeCard[active=true] { border:1px solid #269d87; }"
        "QFrame#modeCard[active=true][mode=discrete] { border-color:#bd4857; }"
        "QFrame#modeCard[active=true][mode=hybrid] { border-color:#b18837; }"
        "QLabel#modeBadge { color:#248c79; background:%1; border-radius:11px; font-size:16px; font-weight:700; }"
        "QFrame#pending { background:%2; border:1px solid #269d87; border-radius:12px; }"
        "QLabel#status { color:%4; font-size:12px; padding:5px 0; }"
        "QPushButton, QToolButton { color:%3; background:%2; border:1px solid %5; border-radius:8px; padding:8px 14px; font-weight:600; }"
        "QPushButton:hover, QToolButton:hover { border-color:#269d87; }"
        "QPushButton:focus, QToolButton:focus { border:2px solid #269d87; }"
        "QPushButton:disabled { color:%4; background:%1; border-color:%5; }"
        "QPushButton#primary { color:#ffffff; background:#167d69; border-color:#167d69; }"
        "QPushButton#primary:hover { background:#126857; }"
        "QToolButton#lang { padding:6px 10px; font-size:11px; }"
        "QToolButton#lang:checked { color:#ffffff; background:#167d69; border-color:#167d69; }"
        "QScrollArea { border:0; background:%1; }"
        "QMenu { color:%3; background:%2; border:1px solid %5; padding:6px; }"
        "QMenu::item { padding:7px 22px; border-radius:5px; }"
        "QMenu::item:selected { background:%1; }"
        "QMenu::item:disabled { color:%4; }"
    ).arg(background, card, ink, muted, line));

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("body"));
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(28, 24, 28, 20);
    layout->setSpacing(15);
    auto *header = new QHBoxLayout;
    auto *icon = label(body);
    icon->setPixmap(windowIcon().pixmap(44, 44));
    icon->setFixedSize(44, 44);
    header->addWidget(icon);
    header->addSpacing(8);
    auto *heading = new QVBoxLayout;
    heading->setSpacing(1);
    auto *title = label(body, QStringLiteral("title"));
    title->setText(QStringLiteral("MSI MUX"));
    heading->addWidget(title);
    m_subtitle = label(body, QStringLiteral("subtitle"));
    heading->addWidget(m_subtitle);
    header->addLayout(heading, 1);
    m_preferencesButton = new QToolButton(body);
    m_preferencesButton->setText(QStringLiteral("•••"));
    m_preferencesButton->setPopupMode(QToolButton::InstantPopup);
    m_preferencesButton->setFixedSize(44, 36);
    header->addWidget(m_preferencesButton);
    layout->addLayout(header);
    m_demoLabel = label(body, QStringLiteral("demo"));
    m_demoLabel->setVisible(m_backend.demo());
    layout->addWidget(m_demoLabel);

    m_hero = new ModeHero(body);
    m_hero->setReducedMotion(m_reducedMotion);
    layout->addWidget(m_hero);

    m_statusLabel = label(body, QStringLiteral("status"), true);
    layout->addWidget(m_statusLabel);
    m_pendingCard = new QFrame(body);
    m_pendingCard->setObjectName(QStringLiteral("pending"));
    auto *pendingLayout = new QVBoxLayout(m_pendingCard);
    pendingLayout->setContentsMargins(17, 14, 17, 14);
    m_pendingTitle = label(m_pendingCard, QStringLiteral("modeName"));
    m_pendingDescription = label(m_pendingCard, QStringLiteral("muted"), true);
    pendingLayout->addWidget(m_pendingTitle);
    pendingLayout->addWidget(m_pendingDescription);
    m_shutdownButton = new QPushButton(m_pendingCard);
    m_shutdownButton->setObjectName(QStringLiteral("primary"));
    auto *pendingButtons = new QHBoxLayout;
    m_clearDraftButton = new QPushButton(m_pendingCard);
    m_clearDraftButton->setObjectName(QStringLiteral("clearDraft"));
    pendingButtons->addWidget(m_clearDraftButton);
    pendingButtons->addStretch();
    pendingButtons->addWidget(m_shutdownButton);
    pendingLayout->addLayout(pendingButtons);
    connect(m_clearDraftButton, &QPushButton::clicked, this, &Window::clearDraft);
    connect(m_shutdownButton, &QPushButton::clicked, this, &Window::shutdown);
    layout->addWidget(m_pendingCard);

    m_chooseLabel = label(body, QStringLiteral("caption"));
    layout->addWidget(m_chooseLabel);
    auto *modeLayout = new QVBoxLayout;
    modeLayout->setSpacing(8);
    for (size_t index = 0; index < modes.size(); ++index) {
        auto *modeCard = new QFrame(body);
        modeCard->setObjectName(QStringLiteral("modeCard"));
        modeCard->setProperty("mode", index == 0 ? "hybrid" : index == 1 ? "discrete" : "integrated");
        auto *row = new QHBoxLayout(modeCard);
        row->setContentsMargins(15, 13, 15, 13);
        row->setSpacing(13);
        auto *badge = label(modeCard, QStringLiteral("modeBadge"));
        const QString badgeInk = index == 0 ? (dark ? QStringLiteral("#ffda88") : QStringLiteral("#805918")) :
            index == 1 ? (dark ? QStringLiteral("#ff9ba4") : QStringLiteral("#ab3143")) :
            (dark ? QStringLiteral("#86edc6") : QStringLiteral("#1b7560"));
        const QString badgeBackground = index == 0 ? (dark ? QStringLiteral("#4b4026") : QStringLiteral("#fbf2d9")) :
            index == 1 ? (dark ? QStringLiteral("#492b33") : QStringLiteral("#fdecef")) :
            (dark ? QStringLiteral("#21473e") : QStringLiteral("#e5f6ed"));
        badge->setStyleSheet(QStringLiteral("color:%1; background:%2; border-radius:11px; font-size:16px; font-weight:700;").arg(badgeInk, badgeBackground));
        badge->setText(QString::fromLatin1(index == 0 ? "H" : index == 1 ? "D" : "I"));
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedSize(40, 40);
        row->addWidget(badge);
        auto *text = new QVBoxLayout;
        text->setSpacing(4);
        m_modeNames[index] = label(modeCard, QStringLiteral("modeName"));
        m_modeDescriptions[index] = label(modeCard, QStringLiteral("muted"), true);
        text->addWidget(m_modeNames[index]);
        text->addWidget(m_modeDescriptions[index]);
        row->addLayout(text, 1);
        m_modeButtons[index] = new QPushButton(modeCard);
        m_modeButtons[index]->setMinimumWidth(74);
        row->addWidget(m_modeButtons[index]);
        connect(m_modeButtons[index], &QPushButton::clicked, this, [this, index] { requestMode(modes[index]); });
        m_modeCards[index] = modeCard;
        modeLayout->addWidget(modeCard);
    }
    layout->addLayout(modeLayout);

    auto *footer = new QHBoxLayout;
    m_version = label(body, QStringLiteral("version"));
    footer->addWidget(m_version, 1);
    m_aboutButton = new QPushButton(body);
    connect(m_aboutButton, &QPushButton::clicked, this, [this] { showAbout(); });
    footer->addWidget(m_aboutButton);
    m_trButton = new QToolButton(body);
    m_trButton->setObjectName(QStringLiteral("lang"));
    m_trButton->setText(QStringLiteral("TR"));
    m_trButton->setToolTip(QStringLiteral("Türkçe"));
    m_trButton->setCheckable(true);
    connect(m_trButton, &QToolButton::clicked, this, [this] { setLanguage(Language::Turkish); });
    footer->addWidget(m_trButton);
    m_enButton = new QToolButton(body);
    m_enButton->setObjectName(QStringLiteral("lang"));
    m_enButton->setText(QStringLiteral("EN"));
    m_enButton->setToolTip(QStringLiteral("English"));
    m_enButton->setCheckable(true);
    connect(m_enButton, &QToolButton::clicked, this, [this] { setLanguage(Language::English); });
    footer->addWidget(m_enButton);
    footer->addSpacing(6);
    m_refreshButton = new QPushButton(body);
    connect(m_refreshButton, &QPushButton::clicked, &m_backend, &Backend::refresh);
    footer->addWidget(m_refreshButton);
    layout->addLayout(footer);
    layout->addStretch();
    scroll->setWidget(body);
    setCentralWidget(scroll);
}

void Window::buildMenus() {
    m_trayMenu = new QMenu(this);
    m_currentAction = m_trayMenu->addAction(QString());
    m_currentAction->setEnabled(false);
    m_pendingAction = m_trayMenu->addAction(QString());
    m_pendingAction->setEnabled(false);
    m_trayMenu->addSeparator();
    auto *group = new QActionGroup(this);
    group->setExclusive(true);
    for (size_t index = 0; index < modes.size(); ++index) {
        m_modeActions[index] = m_trayMenu->addAction(QString());
        m_modeActions[index]->setCheckable(true);
        group->addAction(m_modeActions[index]);
        connect(m_modeActions[index], &QAction::triggered, this, [this, index] { requestMode(modes[index]); });
    }
    m_shutdownAction = m_trayMenu->addAction(QString(), this, &Window::shutdown);
    m_clearDraftAction = m_trayMenu->addAction(QString(), this, &Window::clearDraft);
    m_trayMenu->addSeparator();
    m_openAction = m_trayMenu->addAction(QString(), this, &Window::showPanel);
    m_refreshAction = m_trayMenu->addAction(QString(), &m_backend, &Backend::refresh);
    m_preferencesMenu = new QMenu(this);
    m_languageMenu = m_preferencesMenu->addMenu(QString());
    auto *languages = new QActionGroup(this);
    m_englishAction = m_languageMenu->addAction(QStringLiteral("English"), this, [this] { setLanguage(Language::English); });
    m_turkishAction = m_languageMenu->addAction(QStringLiteral("Türkçe"), this, [this] { setLanguage(Language::Turkish); });
    m_englishAction->setCheckable(true);
    m_turkishAction->setCheckable(true);
    languages->addAction(m_englishAction);
    languages->addAction(m_turkishAction);
    m_autostartAction = m_preferencesMenu->addAction(QString());
    m_autostartAction->setCheckable(true);
    m_autostartAction->setChecked(QFileInfo::exists(autostartPath()));
    m_autostartAction->setEnabled(!m_backend.demo());
    connect(m_autostartAction, &QAction::triggered, this, &Window::setAutostart);
    m_reducedMotionAction = m_preferencesMenu->addAction(QString());
    m_reducedMotionAction->setCheckable(true);
    m_reducedMotionAction->setChecked(m_reducedMotion);
    connect(m_reducedMotionAction, &QAction::triggered, this, &Window::setReducedMotion);
    m_preferencesMenu->addSeparator();
    m_aboutAction = m_preferencesMenu->addAction(QString(), this, [this] { showAbout(); });
    m_whatsNewAction = m_preferencesMenu->addAction(QString(), this, [this] { showAbout(true); });
    m_gnomeSetupAction = m_preferencesMenu->addAction(QString(), this, [] {
        QDesktopServices::openUrl(DesktopIntegration::gnomeSetupUrl());
    });
    m_preferencesMenu->addSeparator();
    m_quitAction = m_preferencesMenu->addAction(QString(), this, &Window::requestQuit);
    m_preferencesButton->setMenu(m_preferencesMenu);
    m_trayMenu->addMenu(m_preferencesMenu);
    m_tray.setIcon(windowIcon());
    m_tray.setContextMenu(m_trayMenu);
    connect(&m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showPanel();
    });
    connect(&m_tray, &QSystemTrayIcon::messageClicked, this, &Window::showPanel);
    // Qt automatically registers a visible icon when a tray starts later.
    // Keep startup and close-window fallback based on actual availability.
    if (m_trayEnabled) m_tray.show();
}

QString Window::panelLabel() const {
    QStringList values;
    for (const auto &display : m_backend.status().displays) {
        if (!display.connected || !display.enabled) continue;
        QString vendor = display.vendor;
        if (vendor == QLatin1String("0x8086")) vendor = QStringLiteral("Intel");
        if (vendor == QLatin1String("0x10de")) vendor = QStringLiteral("NVIDIA");
        if (vendor == QLatin1String("0x1002")) vendor = QStringLiteral("AMD");
        const auto value = vendor.isEmpty() ? t(Text::Unknown) : vendor;
        if (!values.contains(value)) values.append(value);
    }
    return values.isEmpty() ? t(Text::NoPanel) : values.join(QStringLiteral(", "));
}

void Window::updateUi() {
    const auto &status = m_backend.status();
    const bool busy = interactionBusy();
    const bool hasDraft = m_backend.hasDraft();
    const Mode draft = m_backend.draftMode();
    const bool refreshing = m_backend.refreshing();
    if (windowFlags().testFlag(Qt::WindowCloseButtonHint) == busy) {
        const bool visible = isVisible();
        setWindowFlag(Qt::WindowCloseButtonHint, !busy);
        if (visible) showPanel();
    }
    m_subtitle->setText(t(Text::Subtitle));
    m_demoLabel->setText(t(Text::Demo));
    const Mode visualDraft = hasDraft ? draft : m_committingDraft ? m_powerDraft : Mode::Unknown;
    m_hero->setState(status, m_language, panelLabel(), busy, visualDraft);
    m_chooseLabel->setText(t(Text::SelectMode));
    QString reason = blockText(status, m_language);
    if (busy) reason = t(Text::Busy);
    else if (m_statusError == QLatin1String("backend_missing")) reason = t(Text::BackendMissing);
    else if (!m_statusError.isEmpty()) reason = t(Text::StatusFailed);
    else if (!status.valid && refreshing) reason = t(Text::Reading);
    m_statusLabel->setText(reason);
    m_statusLabel->setVisible(!reason.isEmpty() && (!status.pendingShutdown || status.blockCode == QLatin1String("recovery_required")));
    m_statusLabel->setToolTip(m_lastDetails);
    const bool routinePending = status.routinePending();
    const bool powerAvailable = hasDraft ? status.canSwitch(draft) : routinePending;
    m_pendingCard->setVisible(routinePending || hasDraft);
    m_pendingTitle->setText(t(hasDraft ? Text::DraftTitle : Text::PendingTitle));
    m_pendingDescription->setText(t(hasDraft ? Text::DraftDetail : Text::PendingDetail).arg(modeName(hasDraft ? draft : status.target, m_language)));
    m_clearDraftButton->setText(t(Text::ClearDraft));
    m_clearDraftButton->setVisible(hasDraft);
    m_clearDraftButton->setEnabled(!busy);
    m_shutdownButton->setText(t(Text::PowerOptions));
    m_shutdownButton->setVisible(routinePending || hasDraft);
    m_shutdownButton->setEnabled(powerAvailable && !busy && !refreshing);
    for (size_t index = 0; index < modes.size(); ++index) {
        const auto mode = modes[index];
        const bool active = status.current == mode;
        const bool enabled = !busy && m_backend.canSelectMode(mode);
        const auto name = modeName(mode, m_language);
        m_modeNames[index]->setText(name);
        m_modeDescriptions[index]->setText(t(descriptions[index]));
        m_modeButtons[index]->setText(t(hasDraft && draft == mode ? Text::DraftSelected : active ? Text::Active : routinePending && status.target == mode ? Text::Target : Text::Select));
        m_modeButtons[index]->setEnabled(enabled);
        m_modeButtons[index]->setAccessibleName(t(Text::ApplyTitle).arg(modeName(mode, m_language)));
        m_modeCards[index]->setProperty("active", active);
        m_modeCards[index]->style()->unpolish(m_modeCards[index]);
        m_modeCards[index]->style()->polish(m_modeCards[index]);
        m_modeActions[index]->setText(name);
        m_modeActions[index]->setChecked(active);
        m_modeActions[index]->setEnabled(enabled);
    }
    m_version->setText(t(Text::Version).arg(QCoreApplication::applicationVersion()));
    m_aboutButton->setText(t(Text::About));
    m_refreshButton->setText(t(refreshing ? Text::Refreshing : Text::Refresh));
    m_refreshButton->setEnabled(!busy && !refreshing && !m_powerDialogOpen && !m_confirming);
    m_preferencesButton->setToolTip(t(Text::Settings));
    m_preferencesButton->setAccessibleName(t(Text::Settings));
    m_preferencesMenu->setTitle(t(Text::Settings));
    m_currentAction->setText(t(Text::CurrentMode) + QStringLiteral(": ") + modeName(status.current, m_language));
    m_pendingAction->setText(t(hasDraft ? Text::DraftTitle : Text::PendingTitle) + QStringLiteral(" · ") + modeName(hasDraft ? draft : status.target, m_language));
    m_pendingAction->setVisible(status.pendingShutdown || hasDraft);
    m_openAction->setText(t(Text::Open));
    m_refreshAction->setText(t(Text::Refresh));
    m_refreshAction->setEnabled(!busy && !refreshing && !m_powerDialogOpen && !m_confirming);
    m_languageMenu->setTitle(t(Text::LanguageMenu));
    m_autostartAction->setText(t(Text::Autostart));
    m_reducedMotionAction->setText(t(Text::ReducedMotion));
    m_reducedMotionAction->setChecked(m_reducedMotion);
    m_aboutAction->setText(t(Text::About));
    m_whatsNewAction->setText(t(Text::WhatsNew));
    m_gnomeSetupAction->setText(t(Text::GnomeSetup));
    m_gnomeSetupAction->setToolTip(t(Text::GnomeSetupDetail));
    m_gnomeSetupAction->setVisible(DesktopIntegration::detect(QSystemTrayIcon::isSystemTrayAvailable()).needsGnomeSetup());
    m_quitAction->setText(t(Text::Quit));
    m_quitAction->setEnabled(!busy);
    m_shutdownAction->setText(t(Text::PowerOptions));
    m_shutdownAction->setVisible(routinePending || hasDraft);
    m_shutdownAction->setEnabled(powerAvailable && !busy && !refreshing);
    m_clearDraftAction->setText(t(Text::ClearDraft));
    m_clearDraftAction->setVisible(hasDraft);
    m_clearDraftAction->setEnabled(!busy);
    m_englishAction->setChecked(m_language == Language::English);
    m_turkishAction->setChecked(m_language == Language::Turkish);
    m_trButton->setChecked(m_language == Language::Turkish);
    m_enButton->setChecked(m_language == Language::English);
    m_tray.setToolTip(QStringLiteral("MSI MUX · %1%2").arg(modeName(status.current, m_language),
        hasDraft ? QStringLiteral(" · ") + t(Text::DraftTitle) + QStringLiteral(": ") + modeName(draft, m_language) :
        status.pendingShutdown ? QStringLiteral(" → ") + modeName(status.target, m_language) : QString()));
    m_tray.setIcon(modeTrayIcon(windowIcon(), status.current, status.pendingShutdown));
}

void Window::applyMode(Mode mode) {
    if (m_confirming || m_infoDialogOpen || interactionBusy() || !m_backend.canSelectMode(mode)) return;
    if (mode == m_backend.status().current && m_backend.hasDraft()) {
        clearDraft();
        return;
    }
    QScopedValueRollback<bool> confirming(m_confirming, true);
    showPanel();
    QDialog dialog(this);
    dialog.setWindowTitle(t(Text::ApplyTitle).arg(modeName(mode, m_language)));
    dialog.setMinimumWidth(490);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(15);
    auto *targetCaption = label(&dialog, QStringLiteral("caption"));
    targetCaption->setText(t(Text::Target).toUpper());
    layout->addWidget(targetCaption);
    auto *target = label(&dialog, QStringLiteral("confirmationTarget"));
    target->setText(modeName(mode, m_language));
    target->setStyleSheet(QStringLiteral("font-size:28px; font-weight:750; background:transparent;"));
    layout->addWidget(target);
    auto *description = label(&dialog, {}, true);
    description->setText(t(Text::ApplyDescription).arg(modeName(mode, m_language)));
    layout->addWidget(description);
    auto *risk = label(&dialog, QStringLiteral("muted"), true);
    risk->setText(m_backend.demo() ? t(Text::Demo) : t(Text::ApplyRisk));
    layout->addWidget(risk);
    auto *buttons = new QDialogButtonBox(&dialog);
    auto *cancel = buttons->addButton(t(Text::Cancel), QDialogButtonBox::RejectRole);
    auto *apply = buttons->addButton(t(Text::ConfirmApply), QDialogButtonBox::AcceptRole);
    apply->setObjectName(QStringLiteral("primary"));
    apply->setAutoDefault(false);
    apply->setDefault(false);
    cancel->setAutoDefault(true);
    cancel->setDefault(true);
    cancel->setFocus(Qt::OtherFocusReason);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QTimer::singleShot(0, &dialog, [cancel] {
        cancel->setDefault(true);
        cancel->setFocus(Qt::OtherFocusReason);
    });
    m_refreshTimer.stop();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    m_refreshTimer.start();
    if (accepted) {
        if (m_backend.selectMode(mode)) {
            updateUi();
            if (m_backend.hasDraft()) QTimer::singleShot(0, this, &Window::shutdown);
        } else {
            showDetails(t(Text::Error), t(Text::DraftSaveFailed), m_backend.draftError());
        }
    }
}

void Window::shutdown() {
    const auto &status = m_backend.status();
    const bool hasDraft = m_backend.hasDraft();
    if (m_powerDialogOpen || m_confirming || m_infoDialogOpen || interactionBusy() || m_backend.refreshing() ||
        (hasDraft ? !status.canSwitch(m_backend.draftMode()) : !status.routinePending())) return;
    QScopedValueRollback<bool> open(m_powerDialogOpen, true);
    const Mode confirmedCurrent = status.current;
    const Mode confirmedTarget = status.target;
    const Mode confirmedDraft = m_backend.draftMode();
    const Mode selectedTarget = hasDraft ? confirmedDraft : confirmedTarget;
    showPanel();
    updateUi();
    QDialog dialog(this);
    dialog.setWindowTitle(t(hasDraft ? Text::DraftPowerTitle : Text::PowerSuccessTitle));
    dialog.setMinimumWidth(520);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(15);
    auto *title = label(&dialog, QStringLiteral("modeName"), true);
    title->setText(modeName(status.current, m_language) + QStringLiteral(" → ") + modeName(selectedTarget, m_language));
    layout->addWidget(title);
    auto *description = label(&dialog, {}, true);
    description->setText(t(hasDraft ? Text::DraftPowerDetail : Text::PowerSuccessDetail).arg(modeName(status.current, m_language), modeName(selectedTarget, m_language)));
    layout->addWidget(description);
    auto *restartNote = label(&dialog, QStringLiteral("muted"), true);
    restartNote->setText(m_backend.demo() ? t(Text::PowerRequestDemo) : t(Text::RestartUnverified));
    layout->addWidget(restartNote);
    auto *buttons = new QDialogButtonBox(&dialog);
    auto *later = buttons->addButton(t(Text::Later), QDialogButtonBox::RejectRole);
    auto *restart = buttons->addButton(t(Text::Restart), QDialogButtonBox::ActionRole);
    auto *powerOff = buttons->addButton(t(Text::Shutdown), QDialogButtonBox::ActionRole);
    powerOff->setObjectName(QStringLiteral("primary"));
    // Older Qt button-box layouts can focus the first action when shown and
    // promote an auto-default button. Power actions must never inherit Return.
    restart->setAutoDefault(false);
    restart->setDefault(false);
    powerOff->setAutoDefault(false);
    powerOff->setDefault(false);
    later->setAutoDefault(true);
    later->setDefault(true);
    later->setFocus(Qt::OtherFocusReason);
    layout->addWidget(buttons);
    connect(later, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(restart, &QPushButton::clicked, &dialog, [&dialog] { dialog.done(2); });
    connect(powerOff, &QPushButton::clicked, &dialog, [&dialog] { dialog.done(3); });
    QTimer::singleShot(0, &dialog, [later] {
        later->setDefault(true);
        later->setFocus(Qt::OtherFocusReason);
    });
    m_refreshTimer.stop();
    const int choice = dialog.exec();
    m_refreshTimer.start();
    m_powerDialogOpen = false;
    updateUi();
    if (choice != 2 && choice != 3) return;
    // Selecting a power option is the only authorization to commit a draft.
    // Reject a replaced/cleared selection or changed firmware snapshot, then
    // obtain a new read before either committing or requesting desktop power.
    const auto &latest = m_backend.status();
    if (interactionBusy() || m_backend.refreshing() || latest.current != confirmedCurrent || latest.target != confirmedTarget ||
        m_backend.hasDraft() != hasDraft || m_backend.draftMode() != confirmedDraft ||
        (hasDraft ? !latest.canSwitch(confirmedDraft) : !latest.routinePending())) return;
    m_powerIntent = choice == 2 ? PowerAction::Restart : PowerAction::PowerOff;
    m_powerCurrent = confirmedCurrent;
    m_powerTarget = confirmedTarget;
    m_powerDraft = confirmedDraft;
    m_powerAwaitingRefresh = true;
    updateUi();
    m_backend.refresh();
}

void Window::finishPowerPreflight() {
    if (!m_powerAwaitingRefresh) return;
    m_powerAwaitingRefresh = false;
    const auto &status = m_backend.status();
    const bool hasDraft = m_powerDraft != Mode::Unknown;
    const bool matches = status.valid && !m_backend.busy() && !m_powerActions.busy() &&
        !m_infoDialogOpen && !m_confirming && !m_powerDialogOpen &&
        status.current == m_powerCurrent && status.target == m_powerTarget &&
        m_backend.hasDraft() == hasDraft && m_backend.draftMode() == m_powerDraft;
    if (!matches || !m_powerIntent || (hasDraft ? !status.canSwitch(m_powerDraft) : !status.routinePending())) {
        m_powerIntent.reset();
        updateUi();
        return;
    }
    if (hasDraft) {
        m_committingDraft = true;
        updateUi();
        if (!m_backend.commitDraft()) {
            m_committingDraft = false;
            m_powerIntent.reset();
            updateUi();
        }
    } else {
        const auto action = *m_powerIntent;
        m_powerIntent.reset();
        updateUi();
        m_powerActions.request(action);
    }
}

bool Window::interactionBusy() const {
    return firmwareOperationBusy() || m_powerActions.busy();
}

bool Window::firmwareOperationBusy() const {
    return m_backend.busy() || m_powerAwaitingRefresh || m_committingDraft;
}

void Window::clearDraft() {
    if (interactionBusy() || m_confirming || m_powerDialogOpen || m_infoDialogOpen || !m_backend.hasDraft()) return;
    if (!m_backend.clearDraft()) showDetails(t(Text::Error), t(Text::DraftSaveFailed), m_backend.draftError());
    updateUi();
}

void Window::showAbout(bool changes) {
    if (m_infoDialogOpen || m_confirming || m_powerDialogOpen || interactionBusy()) return;
    QScopedValueRollback<bool> open(m_infoDialogOpen, true);
    AboutDialog dialog(m_language, m_backend.status(), this, changes);
    dialog.exec();
}

void Window::showDetails(const QString &title, const QString &message, const QString &details) {
    if (m_infoDialogOpen) return;
    QScopedValueRollback<bool> open(m_infoDialogOpen, true);
    QMessageBox box(QMessageBox::Warning, title, message, QMessageBox::NoButton, this);
    box.setTextFormat(Qt::PlainText);
    box.addButton(t(Text::Close), QMessageBox::AcceptRole);
    // Standard Qt's details button may lack a locale catalog; use our own translated button.
    QPushButton *detailsButton = nullptr;
    if (!details.isEmpty()) detailsButton = box.addButton(t(Text::Details), QMessageBox::ActionRole);
    box.exec();
    if (detailsButton && box.clickedButton() == detailsButton) {
        QDialog dialog(this);
        dialog.setWindowTitle(t(Text::Details));
        dialog.resize(650, 390);
        auto *layout = new QVBoxLayout(&dialog);
        auto *text = new QPlainTextEdit(&dialog);
        text->setReadOnly(true);
        text->setPlainText(details);
        layout->addWidget(text);
        auto *close = new QPushButton(t(Text::Close), &dialog);
        layout->addWidget(close, 0, Qt::AlignRight);
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
    }
}

void Window::setLanguage(Language language) {
    m_language = language;
    if (!m_backend.demo()) m_settings.setValue(QStringLiteral("language"), language == Language::Turkish ? "tr" : "en");
    updateUi();
}

void Window::setAutostart(bool enabled) {
    bool success = true;
    const auto path = autostartPath();
    if (enabled) {
        success = QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        const QByteArray entry = "[Desktop Entry]\nType=Application\nName=MSI MUX\nComment=GPU mode in the system tray\nComment[tr]=Sistem tepsisinde GPU modu\nExec=/usr/bin/msi-mux-tray --background\nTryExec=/usr/bin/msi-mux-tray\nIcon=org.hayatboj.msimux\nTerminal=false\nX-GNOME-Autostart-enabled=true\n";
        success = success && file.open(QIODevice::WriteOnly) && file.write(entry) == entry.size() && file.commit();
    } else if (QFileInfo::exists(path)) {
        success = QFile::remove(path);
    }
    m_autostartAction->setChecked(QFileInfo::exists(path));
    if (!success) showDetails(t(Text::Error), t(Text::AutostartFailed), {});
}

void Window::setReducedMotion(bool enabled) {
    m_reducedMotion = enabled;
    m_hero->setReducedMotion(enabled);
    if (!m_backend.demo()) m_settings.setValue(QStringLiteral("reducedMotion"), enabled);
    m_reducedMotionAction->setChecked(enabled);
}

void Window::requestMode(Mode mode) {
    showPanel();
    if (mode == Mode::Unknown || m_requestInFlight || m_confirming || m_powerDialogOpen ||
        m_infoDialogOpen || interactionBusy()) return;
    m_requestInFlight = true;
    m_requestedMode = mode;
    // Status is a read-only probe. The signal handler opens one confirmation
    // only after that fresh reading; external requests never apply a mode.
    m_backend.refresh();
}

void Window::showPanel() {
    showNormal();
    raise();
    activateWindow();
}

void Window::requestQuit() {
    if (interactionBusy()) { showPanel(); return; }
    m_quitting = true;
    qApp->quit();
}

void Window::closeEvent(QCloseEvent *event) {
    if (firmwareOperationBusy()) { event->ignore(); return; }
    if (hasTray() && !m_quitting) {
        hide();
        event->ignore();
    } else {
        event->accept();
        qApp->quit();
    }
}
}
