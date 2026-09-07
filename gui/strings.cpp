#include "translations.h"

#include <QLocale>
#include <array>

namespace Mux {
struct Translation { const char *en; const char *tr; };
static constexpr std::array<Translation, static_cast<size_t>(Text::Count)> translations{{
    {"Graphics, in your control.", "Grafikler senin kontrolünde."},
    {"DEMO · No hardware changes", "DEMO · Donanım değiştirilmez"},
    {"CURRENT MODE", "GEÇERLİ MOD"},
    {"Unknown", "Bilinmiyor"},
    {"Hybrid", "Hibrit"},
    {"Discrete", "Ayrık"},
    {"Integrated", "Entegre"},
    {"Intel drives the display. NVIDIA handles demanding apps.", "Ekranı Intel yönetir. Zorlu uygulamaları NVIDIA çalıştırır."},
    {"NVIDIA drives the display directly. Maximum gaming performance.", "Ekranı doğrudan NVIDIA yönetir. En yüksek oyun performansı."},
    {"Intel graphics only. Experimental; not yet tested on this device.", "Yalnızca Intel grafik birimi. Deneysel; bu cihazda henüz test edilmedi."},
    {"CHOOSE A MODE", "MOD SEÇ"},
    {"Active", "Etkin"},
    {"Select", "Seç"},
    {"Internal display", "Dahili ekran"},
    {"BIOS", "BIOS"},
    {"Hardware", "Donanım"},
    {"AC power connected", "Adaptör bağlı"},
    {"On battery", "Pille çalışıyor"},
    {"Known hardware & BIOS", "Eşleşen donanım ve BIOS"},
    {"Unsupported configuration", "Desteklenmeyen yapılandırma"},
    {"Validated graphics modes", "Doğrulanmış grafik modları"},
    {"Hybrid ↔ Discrete verified on Vector 16 HX AI A2XWIG with BIOS E15M3IMS.116. Integrated remains untested. A full shutdown is required after applying a mode.", "Hibrit ↔ Ayrık, Vector 16 HX AI A2XWIG ve E15M3IMS.116 BIOS ile doğrulandı. Entegre henüz test edilmedi. Mod uygulandıktan sonra tam kapatma gerekir."},
    {"Ready for a full shutdown", "Tam kapatma bekleniyor"},
    {"Target: %1. Save your work, shut down completely, then power on to verify the new mode.", "Hedef: %1. İşlerini kaydet, bilgisayarı tamamen kapat ve yeni modu doğrulamak için yeniden aç."},
    {"Shut down…", "Bilgisayarı kapat…"},
    {"Refresh", "Yenile"},
    {"Refreshing…", "Yenileniyor…"},
    {"Open MSI MUX", "MSI MUX'u aç"},
    {"Quit", "Çıkış"},
    {"Language", "Dil"},
    {"Start at login", "Oturum açıldığında başlat"},
    {"Preferences", "Tercihler"},
    {"Ready", "Hazır"},
    {"Reading graphics state…", "Grafik durumu okunuyor…"},
    {"Backend not installed. Install the complete MSI MUX package to read hardware status.", "Arka uç kurulu değil. Donanım durumunu okumak için MSI MUX paketinin tamamını kur."},
    {"Could not refresh hardware status. Switching is disabled until a fresh reading succeeds.", "Donanım durumu yenilenemedi. Yeni okuma başarılı olana kadar geçiş devre dışı."},
    {"Firmware state is unavailable. Check that the MSI firmware interface is accessible.", "Firmware durumu okunamıyor. MSI firmware arayüzünün erişilebilir olduğunu kontrol et."},
    {"This hardware does not match the characterized model. Switching is disabled.", "Donanım, incelenen modelle eşleşmiyor. Mod geçişi devre dışı."},
    {"This BIOS version is not characterized. Switching is disabled.", "Bu BIOS sürümü incelenmiş değil. Mod geçişi devre dışı."},
    {"Firmware mode or layout is not recognized. Switching is disabled.", "Firmware modu veya veri yapısı tanınmıyor. Mod geçişi devre dışı."},
    {"Connect AC power before changing the graphics mode.", "Grafik modunu değiştirmeden önce adaptörü bağla."},
    {"A mode change is already pending. Fully shut down before another change.", "Bir mod değişikliği zaten bekliyor. Yeni değişiklikten önce tamamen kapat."},
    {"Applying the firmware change. Keep this app open and AC power connected.", "Firmware değişikliği uygulanıyor. Uygulamayı açık ve adaptörü bağlı tut."},
    {"Switch to %1?", "%1 moduna geçilsin mi?"},
    {"This changes persistent firmware settings to %1. Your current session will keep its present mode until a full shutdown and power-on.", "Bu işlem kalıcı firmware ayarını %1 olarak değiştirir. Tam kapatma ve yeniden açmaya kadar oturumun mevcut modda kalır."},
    {"A failed firmware transition can leave a black screen and may require service recovery. A BIOS reset is not a guaranteed recovery method.", "Başarısız bir firmware geçişi siyah ekrana ve servisle kurtarma ihtiyacına yol açabilir. BIOS sıfırlama kesin bir kurtarma yöntemi değildir."},
    {"Type %1 to confirm", "Onaylamak için %1 yaz"},
    {"Authenticate and apply", "Kimlik doğrula ve uygula"},
    {"Cancel", "Vazgeç"},
    {"Applying mode…", "Mod uygulanıyor…"},
    {"Mode staged successfully. Save your work and perform a full shutdown.", "Mod başarıyla hazırlandı. İşlerini kaydet ve bilgisayarı tamamen kapat."},
    {"The operation failed. Read the diagnostic details before trying again.", "İşlem başarısız oldu. Yeniden denemeden önce tanılama ayrıntılarını oku."},
    {"The operation did not report a verified result. Firmware may have changed. Inspect the details and refresh status before taking any further action.", "İşlem doğrulanmış bir sonuç bildirmedi. Firmware değişmiş olabilir. Yeni bir işlem yapmadan önce ayrıntıları incele ve durumu yenile."},
    {"Diagnostic details", "Tanılama ayrıntıları"},
    {"Close", "Kapat"},
    {"Shut down this computer?", "Bilgisayar kapatılsın mı?"},
    {"Save your work and close your applications first. This will completely power off the computer. Turn it on again to verify the graphics mode.", "Önce işlerini kaydet ve uygulamalarını kapat. Bilgisayar tamamen kapanacak. Grafik modunu doğrulamak için yeniden aç."},
    {"The desktop could not shut down the computer. Use KDE's normal shutdown menu after saving your work.", "Masaüstü bilgisayarı kapatamadı. İşlerini kaydettikten sonra KDE'nin normal kapatma menüsünü kullan."},
    {"Demo: mode staged in the preview only. No firmware or power operation was performed.", "Demo: mod yalnızca önizlemede hazırlandı. Firmware veya güç işlemi yapılmadı."},
    {"MSI MUX error", "MSI MUX hatası"},
    {"Could not update the login startup entry.", "Oturum açılışı kaydı güncellenemedi."},
    {"MSI MUX is already running in your system tray.", "MSI MUX zaten sistem tepsisinde çalışıyor."},
    {"Linux edition · %1", "Linux sürümü · %1"},
    {"Not detected", "Algılanmadı"},
    {"Unavailable", "Kullanılamıyor"},
    {"Authentication was cancelled or denied. No successful change was reported.", "Kimlik doğrulama iptal edildi veya reddedildi. Başarılı bir değişiklik bildirilmedi."},
    {"The backend returned an incompatible or invalid response.", "Arka uç uyumsuz veya geçersiz bir yanıt döndürdü."},
    {"The requested mode was already selected. No change was needed.", "İstenen mod zaten seçiliydi. Değişiklik gerekmedi."},
    {"The system tray is unavailable. Keep this window open to access MSI MUX.", "Sistem tepsisi kullanılamıyor. MSI MUX'a erişmek için pencereyi açık tut."},
    {"Show window", "Pencereyi göster"},
    {"A previous transaction needs recovery review. Switching is disabled; inspect the backend diagnostics.", "Önceki işlem için kurtarma incelemesi gerekiyor. Mod geçişi devre dışı; arka uç tanılamasını incele."},
    {"The MSI kernel interface is unavailable. Load the msi-wmi-platform module and refresh.", "MSI çekirdek arayüzü kullanılamıyor. msi-wmi-platform modülünü yükle ve durumu yenile."},
    {"Enable experimental Integrated mode", "Deneysel Entegre modunu etkinleştir"},
    {"Integrated · Experimental", "Entegre · Deneysel"},
    {"Untested. Enable in Preferences to select this mode.", "Test edilmedi. Seçmek için Tercihler'den etkinleştir."},
    {"Integrated mode has not been tested on this model and BIOS.", "Entegre modu bu model ve BIOS üzerinde test edilmedi."},
}};

QString tr(Text key, Language language) {
    const auto index = static_cast<size_t>(key);
    if (index >= translations.size()) return {};
    const auto value = language == Language::Turkish ? translations[index].tr : translations[index].en;
    return QString::fromUtf8(value);
}

QString modeName(Mode mode, Language language) {
    switch (mode) {
    case Mode::Hybrid: return tr(Text::Hybrid, language);
    case Mode::Discrete: return tr(Text::Discrete, language);
    case Mode::Integrated: return tr(Text::Integrated, language);
    case Mode::Unknown: return tr(Text::Unknown, language);
    }
    return {};
}

QString blockText(const Status &status, Language language) {
    if (!status.valid) return tr(Text::StatusFailed, language);
    if (!status.expectedHardware) return tr(Text::UnsupportedHardware, language);
    if (!status.firmwareAvailable) return tr(Text::FirmwareUnavailable, language);
    if (!status.firmwareValid || status.current == Mode::Unknown || status.target == Mode::Unknown)
        return tr(Text::UnknownMode, language);
    if (status.blockCode == QLatin1String("recovery_required")) return tr(Text::RecoveryRequired, language);
    if (status.pendingShutdown) return tr(Text::PendingBlock, language);
    if (!status.acPower) return tr(Text::AcRequired, language);
    if (status.bios != QLatin1String("E15M3IMS.116")) return tr(Text::UnsupportedBios, language);
    if (status.blockCode == QLatin1String("acpi_unavailable")) return tr(Text::AcpiUnavailable, language);
    if (!status.switchingSupported || !status.newSwitchSupported) return tr(Text::Unsupported, language);
    return {};
}

Language systemLanguage() {
    return QLocale::system().language() == QLocale::Turkish ? Language::Turkish : Language::English;
}
}
