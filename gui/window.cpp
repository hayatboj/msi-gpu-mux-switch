#include "window.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
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
    painter.setBrush(QColor(mode == Mode::Discrete ? QStringLiteral("#246aa0") :
        mode == Mode::Integrated ? QStringLiteral("#527b32") :
        mode == Mode::Hybrid ? QStringLiteral("#167d69") : QStringLiteral("#58656b")));
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
    : QMainWindow(parent), m_backend(demo, this), m_language(language), m_settings(), m_tray(this), m_trayEnabled(trayEnabled) {
    setWindowTitle(QStringLiteral("MSI MUX"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/msi-mux.svg")));
    m_backend.setExperimentalIntegratedEnabled(!demo &&
        m_settings.value(QStringLiteral("experimentalIntegratedEnabled"), false).toBool());
    setMinimumSize(590, 640);
    const int availableHeight = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry().height() : 980;
    resize(680, qBound(640, availableHeight - 80, 900));
    buildUi();
    buildMenus();
    connect(&m_backend, &Backend::statusChanged, this, [this] {
        if (m_backend.status().valid) m_statusError.clear();
        updateUi();
    });
    connect(&m_backend, &Backend::refreshingChanged, this, [this](bool) { updateUi(); });
    connect(&m_backend, &Backend::busyChanged, this, [this](bool busy) {
        setWindowFlag(Qt::WindowCloseButtonHint, !busy);
        if (isVisible() || busy) showPanel();
        updateUi();
    });
    connect(&m_backend, &Backend::statusError, this, [this](const QString &code, const QString &details) {
        m_statusError = code;
        m_lastDetails = details;
        updateUi();
    });
    connect(&m_backend, &Backend::applyFinished, this, [this](bool success, bool cancelled, const QString &details) {
        m_lastDetails = details;
        m_successfulApply = success;
        updateUi();
        showPanel();
        const auto message = success ? t(m_backend.demo() ? Text::DemoApply : Text::ApplySuccess) :
            t(cancelled ? Text::PermissionDenied : Text::ApplyUncertain);
        if (success) {
            m_tray.showMessage(QStringLiteral("MSI MUX"), message, QSystemTrayIcon::Information, 10000);
        } else {
            showDetails(t(Text::ApplyFailed), message, details);
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
        "QFrame#hero { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #133f40,stop:1 #112c38); border-radius:18px; }"
        "QFrame#hero QLabel { color:#f0fffc; }"
        "QFrame#hero QLabel#heroCaption { color:#91beba; font-size:10px; font-weight:700; letter-spacing:1.8px; }"
        "QFrame#hero QLabel#heroMode { font-size:38px; font-weight:700; letter-spacing:-1px; }"
        "QFrame#hero QLabel#heroDescription { color:#b6cfcd; font-size:12px; }"
        "QFrame#hero QLabel#heroValue { font-size:13px; font-weight:600; }"
        "QLabel#power { color:#a4eed5; background:#254e4d; padding:6px 10px; border-radius:9px; font-size:11px; }"
        "QFrame#modeCard, QFrame#deviceCard { background:%2; border:1px solid %5; border-radius:12px; }"
        "QFrame#modeCard[active=true] { border:1px solid #269d87; }"
        "QLabel#modeBadge { color:#248c79; background:%1; border-radius:11px; font-size:16px; font-weight:700; }"
        "QFrame#pending { background:%2; border:1px solid #269d87; border-radius:12px; }"
        "QFrame#notice { background:%2; border:1px solid %5; border-radius:12px; }"
        "QLabel#noticeTitle { font-weight:650; font-size:12px; }"
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

    auto *hero = new QFrame(body);
    hero->setObjectName(QStringLiteral("hero"));
    auto *heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(25, 21, 25, 22);
    heroLayout->setSpacing(7);
    auto *heroTop = new QHBoxLayout;
    m_currentCaption = label(hero, QStringLiteral("heroCaption"));
    heroTop->addWidget(m_currentCaption);
    heroTop->addStretch();
    m_powerLabel = label(hero, QStringLiteral("power"));
    heroTop->addWidget(m_powerLabel);
    heroLayout->addLayout(heroTop);
    m_modeLabel = label(hero, QStringLiteral("heroMode"));
    heroLayout->addWidget(m_modeLabel);
    m_modeDescription = label(hero, QStringLiteral("heroDescription"), true);
    heroLayout->addWidget(m_modeDescription);
    heroLayout->addSpacing(11);
    auto *panel = new QHBoxLayout;
    m_panelCaption = label(hero, QStringLiteral("heroCaption"));
    m_panelValue = label(hero, QStringLiteral("heroValue"));
    panel->addWidget(m_panelCaption);
    panel->addStretch();
    panel->addWidget(m_panelValue);
    heroLayout->addLayout(panel);
    layout->addWidget(hero);

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
    pendingLayout->addWidget(m_shutdownButton, 0, Qt::AlignRight);
    connect(m_shutdownButton, &QPushButton::clicked, this, &Window::shutdown);
    layout->addWidget(m_pendingCard);

    m_chooseLabel = label(body, QStringLiteral("caption"));
    layout->addWidget(m_chooseLabel);
    auto *modeLayout = new QVBoxLayout;
    modeLayout->setSpacing(8);
    for (size_t index = 0; index < modes.size(); ++index) {
        auto *modeCard = new QFrame(body);
        modeCard->setObjectName(QStringLiteral("modeCard"));
        auto *row = new QHBoxLayout(modeCard);
        row->setContentsMargins(15, 13, 15, 13);
        row->setSpacing(13);
        auto *badge = label(modeCard, QStringLiteral("modeBadge"));
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
        connect(m_modeButtons[index], &QPushButton::clicked, this, [this, index] { applyMode(modes[index]); });
        m_modeCards[index] = modeCard;
        modeLayout->addWidget(modeCard);
    }
    layout->addLayout(modeLayout);

    auto *device = new QFrame(body);
    device->setObjectName(QStringLiteral("deviceCard"));
    auto *deviceLayout = new QVBoxLayout(device);
    deviceLayout->setContentsMargins(17, 14, 17, 14);
    auto *deviceRow = new QHBoxLayout;
    auto *model = new QVBoxLayout;
    m_deviceCaption = label(device, QStringLiteral("caption"));
    m_deviceValue = label(device, QStringLiteral("muted"), true);
    model->addWidget(m_deviceCaption);
    model->addWidget(m_deviceValue);
    deviceRow->addLayout(model, 1);
    auto *bios = new QVBoxLayout;
    m_biosCaption = label(device, QStringLiteral("caption"));
    m_biosValue = label(device, QStringLiteral("muted"));
    bios->addWidget(m_biosCaption);
    bios->addWidget(m_biosValue);
    deviceRow->addLayout(bios);
    deviceLayout->addLayout(deviceRow);
    m_compatibilityLabel = label(device, QStringLiteral("muted"));
    deviceLayout->addWidget(m_compatibilityLabel);
    layout->addWidget(device);

    auto *notice = new QFrame(body);
    notice->setObjectName(QStringLiteral("notice"));
    auto *noticeLayout = new QVBoxLayout(notice);
    noticeLayout->setContentsMargins(17, 13, 17, 13);
    noticeLayout->setSpacing(5);
    m_experimentalTitle = label(notice, QStringLiteral("noticeTitle"));
    m_experimentalDetail = label(notice, QStringLiteral("muted"), true);
    noticeLayout->addWidget(m_experimentalTitle);
    noticeLayout->addWidget(m_experimentalDetail);
    layout->addWidget(notice);

    auto *footer = new QHBoxLayout;
    m_version = label(body, QStringLiteral("version"));
    footer->addWidget(m_version, 1);
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
        connect(m_modeActions[index], &QAction::triggered, this, [this, index] { applyMode(modes[index]); });
    }
    m_shutdownAction = m_trayMenu->addAction(QString(), this, &Window::shutdown);
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
    m_experimentalIntegratedAction = m_preferencesMenu->addAction(QString());
    m_experimentalIntegratedAction->setCheckable(true);
    m_experimentalIntegratedAction->setChecked(m_backend.experimentalIntegratedEnabled());
    connect(m_experimentalIntegratedAction, &QAction::triggered, this, &Window::setExperimentalIntegrated);
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
        values.append(QStringLiteral("%1 · %2").arg(vendor.isEmpty() ? t(Text::Unknown) : vendor, display.connector));
    }
    return values.isEmpty() ? t(Text::NoPanel) : values.join(QStringLiteral(", "));
}

void Window::updateUi() {
    const auto &status = m_backend.status();
    const bool busy = m_backend.busy();
    const bool refreshing = m_backend.refreshing();
    m_subtitle->setText(t(Text::Subtitle));
    m_demoLabel->setText(t(Text::Demo));
    m_currentCaption->setText(t(Text::CurrentMode));
    m_modeLabel->setText(modeName(status.current, m_language));
    Text description = Text::Reading;
    for (size_t index = 0; index < modes.size(); ++index)
        if (modes[index] == status.current) description = descriptions[index];
    m_modeDescription->setText(status.valid ? t(description) : t(m_statusError.isEmpty() ? Text::Reading : Text::Disabled));
    m_panelCaption->setText(t(Text::InternalPanel).toUpper());
    m_panelValue->setText(panelLabel());
    m_powerLabel->setText(!status.valid ? t(Text::Unknown) : t(status.acPower ? Text::AcConnected : Text::Battery));
    m_chooseLabel->setText(t(Text::SelectMode));
    QString reason = blockText(status, m_language);
    if (busy) reason = t(Text::Busy);
    else if (m_statusError == QLatin1String("backend_missing")) reason = t(Text::BackendMissing);
    else if (!m_statusError.isEmpty()) reason = t(Text::StatusFailed);
    else if (!status.valid && refreshing) reason = t(Text::Reading);
    m_statusLabel->setText(reason);
    m_statusLabel->setVisible(!reason.isEmpty() && (!status.pendingShutdown || status.blockCode == QLatin1String("recovery_required")));
    m_statusLabel->setToolTip(m_lastDetails);
    m_pendingCard->setVisible(status.pendingShutdown);
    m_pendingTitle->setText(t(Text::PendingTitle));
    m_pendingDescription->setText(t(Text::PendingDetail).arg(modeName(status.target, m_language)));
    m_shutdownButton->setText(t(Text::Shutdown));
    m_shutdownButton->setVisible(m_successfulApply);
    m_shutdownButton->setEnabled(!busy);
    for (size_t index = 0; index < modes.size(); ++index) {
        const auto mode = modes[index];
        const bool active = status.current == mode;
        const bool enabled = m_backend.canApplyMode(mode);
        const auto name = mode == Mode::Integrated ? t(Text::IntegratedExperimental) : modeName(mode, m_language);
        m_modeNames[index]->setText(name);
        m_modeDescriptions[index]->setText(t(mode == Mode::Integrated && !m_backend.experimentalIntegratedEnabled() ?
            Text::IntegratedOptInRequired : descriptions[index]));
        m_modeButtons[index]->setText(t(active ? Text::Active : Text::Select));
        m_modeButtons[index]->setEnabled(enabled);
        m_modeButtons[index]->setAccessibleName(t(Text::ApplyTitle).arg(modeName(mode, m_language)));
        m_modeCards[index]->setProperty("active", active);
        m_modeCards[index]->style()->unpolish(m_modeCards[index]);
        m_modeCards[index]->style()->polish(m_modeCards[index]);
        m_modeActions[index]->setText(name);
        m_modeActions[index]->setChecked(active);
        m_modeActions[index]->setEnabled(enabled);
    }
    m_deviceCaption->setText(t(Text::Hardware).toUpper());
    m_deviceValue->setText(status.model.isEmpty() ? t(Text::Unknown) : status.model);
    m_deviceValue->setToolTip(status.board);
    m_biosCaption->setText(t(Text::Firmware));
    m_biosValue->setText(status.bios.isEmpty() ? t(Text::Unknown) : status.bios);
    m_compatibilityLabel->setText(t(status.expectedHardware && status.bios == QLatin1String("E15M3IMS.116") ? Text::Compatible : Text::Unsupported));
    m_experimentalTitle->setText(t(Text::Experimental));
    m_experimentalDetail->setText(t(Text::ExperimentalDetail));
    m_version->setText(t(Text::Version).arg(QStringLiteral("0.3.0")));
    m_refreshButton->setText(t(refreshing ? Text::Refreshing : Text::Refresh));
    m_refreshButton->setEnabled(!busy && !refreshing);
    m_preferencesButton->setToolTip(t(Text::Settings));
    m_preferencesButton->setAccessibleName(t(Text::Settings));
    m_preferencesMenu->setTitle(t(Text::Settings));
    m_currentAction->setText(t(Text::CurrentMode) + QStringLiteral(": ") + modeName(status.current, m_language));
    m_pendingAction->setText(t(Text::PendingTitle) + QStringLiteral(" · ") + modeName(status.target, m_language));
    m_pendingAction->setVisible(status.pendingShutdown);
    m_openAction->setText(t(Text::Open));
    m_refreshAction->setText(t(Text::Refresh));
    m_refreshAction->setEnabled(!busy && !refreshing);
    m_languageMenu->setTitle(t(Text::LanguageMenu));
    m_autostartAction->setText(t(Text::Autostart));
    m_experimentalIntegratedAction->setText(t(Text::EnableExperimentalIntegrated));
    m_experimentalIntegratedAction->setChecked(m_backend.experimentalIntegratedEnabled());
    m_experimentalIntegratedAction->setEnabled(!busy);
    m_quitAction->setText(t(Text::Quit));
    m_quitAction->setEnabled(!busy);
    m_shutdownAction->setText(t(Text::Shutdown));
    m_shutdownAction->setVisible(status.pendingShutdown && m_successfulApply);
    m_shutdownAction->setEnabled(!busy);
    m_englishAction->setChecked(m_language == Language::English);
    m_turkishAction->setChecked(m_language == Language::Turkish);
    m_trButton->setChecked(m_language == Language::Turkish);
    m_enButton->setChecked(m_language == Language::English);
    m_tray.setToolTip(QStringLiteral("MSI MUX · %1%2").arg(modeName(status.current, m_language),
        status.pendingShutdown ? QStringLiteral(" → ") + modeName(status.target, m_language) : QString()));
    m_tray.setIcon(modeTrayIcon(windowIcon(), status.current, status.pendingShutdown));
}

void Window::applyMode(Mode mode) {
    if (!m_backend.canApplyMode(mode)) return;
    showPanel();
    QDialog dialog(this);
    dialog.setWindowTitle(t(Text::ApplyTitle).arg(modeName(mode, m_language)));
    dialog.setMinimumWidth(490);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(15);
    auto *description = label(&dialog, {}, true);
    description->setText(t(Text::ApplyDescription).arg(modeName(mode, m_language)));
    layout->addWidget(description);
    auto *risk = label(&dialog, QStringLiteral("muted"), true);
    risk->setText(m_backend.demo() ? t(Text::Demo) :
        (mode == Mode::Integrated ? t(Text::IntegratedRisk) + QLatin1Char('\n') : QString()) + t(Text::ApplyRisk));
    layout->addWidget(risk);
    auto *tokenLabel = label(&dialog);
    tokenLabel->setText(t(Text::TypeToken).arg(modeToken(mode)));
    auto *input = new QLineEdit(&dialog);
    input->setAccessibleName(tokenLabel->text());
    input->setPlaceholderText(modeToken(mode));
    tokenLabel->setBuddy(input);
    layout->addWidget(tokenLabel);
    layout->addWidget(input);
    auto *buttons = new QDialogButtonBox(&dialog);
    auto *cancel = buttons->addButton(t(Text::Cancel), QDialogButtonBox::RejectRole);
    auto *apply = buttons->addButton(t(Text::ConfirmApply), QDialogButtonBox::AcceptRole);
    apply->setObjectName(QStringLiteral("primary"));
    apply->setEnabled(false);
    cancel->setDefault(true);
    layout->addWidget(buttons);
    connect(input, &QLineEdit::textChanged, &dialog, [apply, mode](const QString &value) {
        apply->setEnabled(value == modeToken(mode));
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    input->setFocus();
    m_refreshTimer.stop();
    const bool accepted = dialog.exec() == QDialog::Accepted && input->text() == modeToken(mode);
    m_refreshTimer.start();
    if (accepted) m_backend.apply(mode);
}

void Window::shutdown() {
    if (!m_successfulApply || !m_backend.status().pendingShutdown || m_backend.busy()) return;
    QMessageBox box(QMessageBox::Question, t(Text::ShutdownTitle), t(Text::ShutdownConfirm), QMessageBox::NoButton, this);
    auto *cancel = box.addButton(t(Text::Cancel), QMessageBox::RejectRole);
    auto *confirm = box.addButton(t(Text::Shutdown), QMessageBox::AcceptRole);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() != confirm) return;
    if (m_backend.demo()) { showDetails(QStringLiteral("MSI MUX"), t(Text::DemoApply), {}); return; }
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"), QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PowerOff"));
    message << false;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError()) showDetails(t(Text::Error), t(Text::ShutdownFailed), reply.error().message());
        watcher->deleteLater();
    });
}

void Window::showDetails(const QString &title, const QString &message, const QString &details) {
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

void Window::setExperimentalIntegrated(bool enabled) {
    if (m_backend.busy()) return;
    m_backend.setExperimentalIntegratedEnabled(enabled);
    if (!m_backend.demo()) m_settings.setValue(QStringLiteral("experimentalIntegratedEnabled"), enabled);
    updateUi();
}

void Window::showPanel() {
    showNormal();
    raise();
    activateWindow();
}

void Window::requestQuit() {
    if (m_backend.busy()) { showPanel(); return; }
    m_quitting = true;
    qApp->quit();
}

void Window::closeEvent(QCloseEvent *event) {
    if (m_backend.busy()) { event->ignore(); return; }
    if (hasTray() && !m_quitting) {
        hide();
        event->ignore();
    } else {
        event->accept();
        qApp->quit();
    }
}
}
