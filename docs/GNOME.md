# GNOME panel integration

MSI MUX uses its existing Qt tray and menu on GNOME through **AppIndicator and
KStatusNotifierItem Support**. The adapter displays the same mode badge, current
mode, local draft or firmware-pending state, and mode menu used on KDE. It does
not gain permission to change firmware: selecting a mode opens MSI MUX's
local-draft flow. The
[adapter's upstream documentation](https://github.com/ubuntu/gnome-shell-extension-appindicator)
describes panel icons and their menus.
The normal location is GNOME's top bar; user-installed panel extensions may
place indicators elsewhere.

## Setup

1. Install MSI MUX and launch it from the applications menu as your normal user.
2. If the GNOME top bar has no MSI MUX icon, install or enable the compatible
   **AppIndicator and KStatusNotifierItem Support** extension through your
   distribution's extension manager or its
   [GNOME Extensions page](https://extensions.gnome.org/extension/615/appindicator-support/).
   Ubuntu may already provide its Ubuntu AppIndicators variant; use the enabled
   distribution adapter instead of installing a second one.
3. Keep MSI MUX running. When a tray becomes available, Qt registers the existing
   icon automatically. If GNOME requires a new login after extension installation,
   save your work and log out and in when convenient. MSI MUX does not change
   GNOME extension settings or trigger logout.
4. To keep the indicator after subsequent logins, enable MSI MUX's optional
   **Start at login** preference. Installing MSI MUX does not enable autostart.

Qt supports Linux desktops offering the StatusNotifierItem interface and
[automatically registers a visible tray icon when a host appears later](https://doc.qt.io/qt-6/qsystemtrayicon.html).
Without a tray host, MSI MUX keeps its normal window available. Opening MSI MUX
again brings the existing window forward.

In version 0.5.0, **Apply saves a local draft only**, just as in the application
window. It does not request administrator authorization or change firmware.
Selecting another mode replaces the draft; canceling it or selecting the current
mode clears it. Later sends no firmware or power request. Integrated remains an
ordinary choice alongside Hybrid and Discrete, based on the owner's report of a
successful hardware test.

To commit the latest draft, save your work and explicitly choose **Restart or
Power Off inside MSI MUX**. Fresh status and the backend's model, BIOS, power and
transaction checks must pass. The application requests helper authorization,
applies firmware, and requires verified success before sending the selected
normal desktop power request. A failed or uncertain apply sends no power request.
GNOME may then show its own confirmation or inhibitor dialog and allow
cancellation; one application click is not a guarantee of a restart. MSI MUX
never forces the action.

Restarting or shutting down from GNOME's own menu does **not** apply a local
draft. Reopening MSI MUX in the same boot restores a valid draft; after a new
boot the old draft is invalidated, without assuming it was applied. An already
committed firmware-pending target cannot be changed as a draft, and canceling a
GNOME power dialog does not undo it. Full shutdown and power-on is the method
in the three captured Hybrid/Discrete tests; warm-restart effectiveness remains
unverified.

The installed desktop launcher also offers Hybrid, Discrete and Integrated request actions
in launchers that support desktop actions. These open the local-draft flow in MSI MUX;
they do not apply a mode directly. Their fixed `--request-mode mshybrid`,
`--request-mode discrete` and `--request-mode integrated` arguments are handled by the unprivileged GUI, including
when an existing application instance receives the request. Saving a draft
through this route never calls the privileged helper or authorizes a later
commit/power action. The
[desktop action specification](https://specifications.freedesktop.org/desktop-entry/latest/extra-actions.html)
allows launchers to expose these additional entries. Launcher support varies.

## Compatibility and validation limits

As checked on **2026-09-07**, the GNOME Extensions page marks adapter version 64
active for GNOME Shell **45–50**. Its version 65 entry includes Shell 51 but is
marked rejected; this project does not treat that entry as validated Shell 51
support. Choose a currently approved version matching your installed Shell.
MSI MUX does not disable GNOME's extension version checks.

The integration uses the maintained adapter rather than a bundled Shell
extension. This avoids a second copy of status parsing, polling and mode
controls. The application remains responsible for bounded unprivileged status
queries and all confirmation and pending-state behavior.

GNOME Shell was unavailable on the development host. Desktop detection and
request handling can be tested with synthetic inputs, but a native GNOME
session, the adapter's rendered menu, and GNOME-specific notification behavior
have **not** been verified here. KDE validation and physical Hybrid/Discrete
validation do not establish GNOME session compatibility. Do not rely on a tray
tooltip or notification alone; open the menu or window to read the full status.

For a GNOME session check, record the Shell and adapter versions and confirm
that current mode, local draft and firmware-pending status match the application
window. Check that saving, replacing or canceling a draft and choosing Later
request neither authorization nor power. Verify the missing-adapter window
fallback and later adapter availability. These interface checks do not require
committing a draft or performing a firmware transition.

## Türkçe

GNOME üst panelinde MSI MUX simgesi ve menüsü için **AppIndicator and
KStatusNotifierItem Support** uyarlayıcısını kullanın. Uygulamayı normal
kullanıcınızla açın. Simge görünmüyorsa dağıtımınızın eklenti yöneticisinden veya
[GNOME Extensions sayfasından](https://extensions.gnome.org/extension/615/appindicator-support/)
GNOME sürümünüze uygun eklentiyi kurup etkinleştirin. Ubuntu'da Ubuntu
AppIndicators zaten etkin olabilir; ikinci bir uyarlayıcı kurmanız gerekmez.

MSI MUX açıkken panel desteği kullanılabilir olduğunda simge otomatik kaydolur.
Eklentinin kurulumu yeni oturum gerektiriyorsa işlerinizi kaydedip uygun zamanda
oturumu kapatıp açın. MSI MUX eklenti ayarlarınızı değiştirmez ve oturumu
kapatmaz. Sonraki oturumlarda otomatik çalışması için uygulamanın isteğe bağlı
**Oturum açıldığında başlat** tercihini etkinleştirin.

0.5.0 sürümünde panelden veya başlatıcı kısayolundan açılan **Uygula** işlemi
yalnız yerel taslağı kaydeder. Yönetici yetkisi istemez ve firmware'i değiştirmez.
Başka bir mod seçmek taslağı değiştirir; taslağı iptal etmek veya mevcut modu
seçmek temizler. **Daha Sonra** firmware veya güç işlemi yapmaz. Entegre, cihaz
sahibinin başarılı donanım testi bildirimine dayanarak Hibrit ve Ayrık yanında
normal seçenektir.

Son taslağı uygulamak için çalışmalarınızı kaydedip **MSI MUX içindeki Yeniden
Başlat veya Kapat** seçeneğini açıkça seçin. Güncel durum ve donanım kontrolleri
yenilenir; yönetici kimlik doğrulamasıyla firmware işlemi yapılır. Yalnız
doğrulanmış başarıdan sonra normal masaüstü güç isteği gönderilir. Firmware
işlemi başarısız veya belirsizse güç isteği gönderilmez. GNOME ayrıca onay veya
engelleyici durum penceresi gösterebilir ve iptale izin verebilir; uygulama güç
işlemini zorlamaz.

GNOME'un kendi menüsünden yeniden başlatmak veya kapatmak taslağı uygulamaz.
Aynı açılışta MSI MUX'u kapatıp açmak taslağı geri getirir; yeni açılışta eski
taslak geçersizleşir ve uygulanmış sayılmaz. Firmware'e zaten uygulanmış bekleyen
hedef taslak gibi değiştirilemez. GNOME güç penceresini iptal etmek de onu geri
almaz. Üç kayıtlı Hibrit/Ayrık testinde tam kapatma ve yeniden açma kullanılmıştır;
yalnız yeniden başlatmanın yeterli olduğu henüz doğrulanmamıştır.

Geliştirme bilgisayarında GNOME Shell bulunmadığından gerçek GNOME oturumu,
eklenti menüsü ve GNOME bildirimleri bu projede test edilmemiştir. 2026-09-07
tarihinde eklenti sayfasındaki etkin sürüm 64, GNOME Shell 45–50 aralığını
listeliyordu. GNOME 51 için etkin destek iddiasında bulunmuyoruz. Panel desteği
yoksa normal MSI MUX penceresini kullanabilirsiniz; durumu görmek için yalnız
bildirime veya simgenin araç ipucuna güvenmeyin.
