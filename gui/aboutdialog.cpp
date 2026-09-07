#include "aboutdialog.h"
#include "desktopintegration.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>

namespace Mux {
namespace {
QString localized(Language language, const char *en, const char *tr) {
    return QString::fromUtf8(language == Language::Turkish ? tr : en);
}
QLabel *paragraph(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
void openLink(QWidget *parent, Language language, const QUrl &url) {
    if (url.scheme() != QLatin1String("https")) return;
    if (!QDesktopServices::openUrl(url))
        QMessageBox::information(parent, QStringLiteral("MSI MUX"),
            localized(language, "Could not open your browser. Address: ", "Tarayıcı açılamadı. Adres: ") + url.toString());
}
QPushButton *linkButton(QWidget *parent, Language language, const QString &label, const char *url) {
    auto *button = new QPushButton(label, parent);
    QObject::connect(button, &QPushButton::clicked, parent, [parent, language, url] {
        openLink(parent, language, QUrl(QString::fromLatin1(url)));
    });
    return button;
}
}

QString AboutDialog::diagnosticsText(Language language, const Status &status) {
    QStringList lines{QStringLiteral("MSI MUX %1").arg(QCoreApplication::applicationVersion()),
        localized(language, "Model: ", "Model: ") + status.model,
        localized(language, "Board: ", "Anakart: ") + status.board,
        QStringLiteral("BIOS: ") + status.bios,
        localized(language, "Current: ", "Geçerli: ") + modeName(status.current, language),
        localized(language, "Requested: ", "İstenen: ") + modeName(status.target, language),
        localized(language, "Power cycle pending: ", "Güç döngüsü bekleniyor: ") +
            localized(language, status.pendingShutdown ? "yes" : "no", status.pendingShutdown ? "evet" : "hayır"),
        localized(language, "Status available: ", "Durum okunabildi: ") +
            localized(language, status.valid ? "yes" : "no", status.valid ? "evet" : "hayır")};
    for (const auto &display : status.displays) {
        if (display.connected && display.enabled)
            lines << localized(language, "Internal display: ", "Dahili ekran: ") +
                display.vendor + QStringLiteral(" / ") + display.driver + QStringLiteral(" / ") + display.connector;
    }
    if (!status.blockCode.isEmpty()) lines << QStringLiteral("Status code: ") + status.blockCode;
    return lines.join(QLatin1Char('\n'));
}

AboutDialog::AboutDialog(Language language, const Status &status, QWidget *parent, bool showChanges)
    : QDialog(parent) {
    const auto t = [language](const char *en, const char *tr) { return localized(language, en, tr); };
    setObjectName(QStringLiteral("aboutDialog"));
    setWindowTitle(t("About MSI MUX", "MSI MUX Hakkında"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/msi-mux.svg")));
    resize(680, 650);
    setMinimumSize(500, 420);
    if (auto *screen = QGuiApplication::primaryScreen())
        resize(width(), qMin(height(), screen->availableGeometry().height() - 80));
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QString background = dark ? QStringLiteral("#1d2328") : QStringLiteral("#f4f6f7");
    const QString card = dark ? QStringLiteral("#272f35") : QStringLiteral("#ffffff");
    const QString ink = dark ? QStringLiteral("#f1f6f5") : QStringLiteral("#182d32");
    const QString line = dark ? QStringLiteral("#3a464c") : QStringLiteral("#dce4e7");
    setStyleSheet(QStringLiteral(
        "QDialog#aboutDialog { background:%1; color:%3; }"
        "QTabWidget::pane { border:1px solid %4; border-radius:12px; background:%2; top:8px; }"
        "QTabBar::tab { background:%1; color:%3; border:1px solid %4; border-radius:8px; padding:9px 18px; margin-right:6px; font-weight:600; }"
        "QTabBar::tab:selected { background:#167d69; color:white; border-color:#167d69; }"
        "QTextBrowser, QPlainTextEdit { background:%2; color:%3; border:0; border-radius:10px; padding:8px; }"
    ).arg(background, card, ink, line));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 22, 22, 18);
    layout->setSpacing(16);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("aboutTabs"));
    layout->addWidget(tabs);

    auto *about = new QWidget(tabs);
    auto *aboutLayout = new QVBoxLayout(about);
    aboutLayout->setContentsMargins(16, 20, 16, 14);
    aboutLayout->setSpacing(14);
    auto *brand = new QHBoxLayout;
    brand->setSpacing(15);
    auto *icon = new QLabel(about);
    icon->setPixmap(QIcon(QStringLiteral(":/icons/msi-mux.svg")).pixmap(56, 56));
    icon->setFixedSize(56, 56);
    brand->addWidget(icon);
    auto *brandText = new QVBoxLayout;
    auto *title = paragraph(about, QStringLiteral("MSI MUX  %1").arg(QCoreApplication::applicationVersion()));
    QFont titleFont = title->font();
    titleFont.setPointSize(23);
    titleFont.setBold(true);
    title->setFont(titleFont);
    brandText->addWidget(title);
    brandText->addWidget(paragraph(about, t("Your graphics mode, clearly in view.", "Grafik modun, bir bakışta.")));
    brand->addLayout(brandText, 1);
    aboutLayout->addLayout(brand);
    aboutLayout->addWidget(paragraph(about, t(
        "Developed by hayatboj. A native Linux interface for MSI GPU MUX control, with KDE and GNOME tray integration.",
        "hayatboj tarafından geliştirildi. KDE ve GNOME tepsi desteğiyle MSI GPU MUX kontrolü için yerel Linux arayüzü.")));
    auto *links = new QHBoxLayout;
    links->addWidget(linkButton(about, language, QStringLiteral("GitHub · hayatboj"), "https://github.com/hayatboj"));
    links->addWidget(linkButton(about, language, t("Project", "Proje"), "https://github.com/hayatboj/msi-gpu-mux-switch"));
    links->addWidget(linkButton(about, language, t("Report an issue", "Sorun bildir"), "https://github.com/hayatboj/msi-gpu-mux-switch/issues"));
    aboutLayout->addLayout(links);
    aboutLayout->addSpacing(14);
    aboutLayout->addWidget(paragraph(about, t(
        "Supported configuration: Vector 16 HX AI A2XWIG · MS-15M3 · BIOS E15M3IMS.116. Hybrid/Discrete were verified through three full power cycles; the device owner also reports a successful Integrated test.",
        "Desteklenen yapılandırma: Vector 16 HX AI A2XWIG · MS-15M3 · BIOS E15M3IMS.116. Hibrit/Ayrık üç tam güç döngüsüyle doğrulandı; cihaz sahibi Entegre testinin de başarılı olduğunu bildirdi.")));
    aboutLayout->addWidget(paragraph(about, t(
        "Based on steelbrain/msi-gpu-mux-switch. MIT license. Independent community project; not affiliated with MSI or NVIDIA.",
        "steelbrain/msi-gpu-mux-switch temel alınmıştır. MIT lisansı. Bağımsız topluluk projesidir; MSI veya NVIDIA ile bağlantılı değildir.")));
    auto *credits = new QHBoxLayout;
    credits->addWidget(linkButton(about, language, t("Upstream project", "Özgün proje"), "https://github.com/steelbrain/msi-gpu-mux-switch"));
    credits->addWidget(linkButton(about, language, t("License", "Lisans"), "https://github.com/hayatboj/msi-gpu-mux-switch/blob/linux-kde/LICENSE"));
    credits->addWidget(linkButton(about, language, t("Recovery guide", "Kurtarma rehberi"), "https://github.com/hayatboj/msi-gpu-mux-switch/blob/linux-kde/docs/RECOVERY.md"));
    aboutLayout->addLayout(credits);
    aboutLayout->addStretch();
    tabs->addTab(about, t("About", "Hakkında"));

    auto *changes = new QTextBrowser(tabs);
    changes->setObjectName(QStringLiteral("releaseNotes"));
    changes->setOpenLinks(false);
    changes->setOpenExternalLinks(false);
    changes->document()->setDocumentMargin(14);
    QFont notesFont = changes->font();
    notesFont.setPointSizeF(10.5);
    changes->setFont(notesFont);
    QFile notes(language == Language::Turkish ? QStringLiteral(":/docs/release-notes.tr.md") : QStringLiteral(":/docs/CHANGELOG.md"));
    if (notes.open(QIODevice::ReadOnly)) changes->setMarkdown(QString::fromUtf8(notes.readAll()));
    else changes->setPlainText(t("Release notes are unavailable.", "Sürüm notları okunamadı."));
    connect(changes, &QTextBrowser::anchorClicked, this, [this, language](const QUrl &url) { openLink(this, language, url); });
    tabs->addTab(changes, t("What's new", "Yenilikler"));

    auto *system = new QWidget(tabs);
    auto *systemLayout = new QVBoxLayout(system);
    systemLayout->setContentsMargins(16, 18, 16, 14);
    systemLayout->setSpacing(12);
    systemLayout->addWidget(paragraph(system, t("A read-only snapshot for troubleshooting. No serial number or raw firmware data is included.",
        "Sorun giderme için salt okunur durum özeti. Seri numarası veya ham firmware verisi içermez.")));
    auto *snapshot = new QPlainTextEdit(diagnosticsText(language, status), system);
    snapshot->setReadOnly(true);
    snapshot->setObjectName(QStringLiteral("diagnosticsSnapshot"));
    systemLayout->addWidget(snapshot, 1);
    auto *copy = new QPushButton(t("Copy system summary", "Sistem özetini kopyala"), system);
    connect(copy, &QPushButton::clicked, this, [copy, snapshot, language] {
        QGuiApplication::clipboard()->setText(snapshot->toPlainText());
        copy->setText(localized(language, "Copied", "Kopyalandı"));
    });
    systemLayout->addWidget(copy, 0, Qt::AlignLeft);
    systemLayout->addWidget(paragraph(system, t("GNOME panel menus use the AppIndicator and KStatusNotifierItem Support extension. Without a tray provider, the full application window remains available.",
        "GNOME panel menüleri AppIndicator and KStatusNotifierItem Support eklentisini kullanır. Tepsi desteği yoksa uygulamanın tam penceresi kullanılabilir.")));
    systemLayout->addWidget(linkButton(system, language, t("GNOME panel setup", "GNOME panel kurulumu"),
        "https://extensions.gnome.org/extension/615/appindicator-support/"), 0, Qt::AlignLeft);
    tabs->addTab(system, t("System", "Sistem"));
    tabs->setCurrentIndex(showChanges ? 1 : 0);
    auto *buttons = new QDialogButtonBox(this);
    auto *close = buttons->addButton(t("Close", "Kapat"), QDialogButtonBox::RejectRole);
    close->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
}
