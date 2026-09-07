# Linux masaüstü için MSI MUX

[English](README.md) · [Sürümler](https://github.com/hayatboj/msi-gpu-mux-switch/releases) · [GNOME kurulumu](https://github.com/hayatboj/msi-gpu-mux-switch/blob/linux-kde/docs/GNOME.md) · [Kurtarma bilgileri](docs/RECOVERY.md)

Desteklenen MSI dizüstünde fiziksel GPU MUX modunu yöneten, Qt 6 ile geliştirilmiş masaüstü uygulaması. Geçerli modu görün, tepsi menüsünden başka bir mod seçin ve uygulamayı kapatmadan Türkçe/İngilizce arasında geçin. KDE kendi sistem tepsisini, GNOME ise aynı simge ve menü için AppIndicator desteğini kullanır.

**0.4.0 sürümü, aşağıdaki tam yapılandırmada Hibrit, Ayrık ve Entegre modlarını normal seçenekler olarak sunar.** Linux üzerinde kaydı alınan **Hibrit → Ayrık → Hibrit → Ayrık** dizisindeki üç geçiş 7 Eylül 2026’da başarılı oldu. Her geçişten sonra elle tam kapatma ve yeniden açma yapıldı; **2560×1600, 240 Hz** korundu. Cihaz sahibi daha sonra Entegre donanım testinin de başarılı olduğunu bildirdi. Bu bildirim üç kayıtlı açılış gözleminden ayrıdır; Entegre testinin geçiş yönü ve açılış yöntemi kaydedilmemiştir. [Kanıtlar ve sınırları](docs/VALIDATION.md) doğrulama kaydında bulunuyor.

| Desteklenen yapılandırma | Değer |
|---|---|
| Model | MSI Vector 16 HX AI A2XWIG |
| Anakart | MS-15M3 |
| BIOS | E15M3IMS.116 |
| Sistem | Linux x86-64, UEFI |
| Masaüstü | KDE Plasma; AppIndicator/StatusNotifierItem uyarlayıcısıyla GNOME |

![Türkçe KDE arayüzü; sentetik demo verisi kullanılmıştır](docs/images/kde-tr.png)

## Kullanım

Uygulama menüsünden **MSI MUX**’u açın. Tepsi simgesinin menüsünden geçerli modu ve kullanılabilir seçenekleri görün:

- **Hibrit:** Intel ve NVIDIA birlikte kullanılabilir; bu cihazda geçişi doğrulandı.
- **Ayrık:** Bu cihazda NVIDIA’yı ve dahili ekranın doğrudan NVIDIA bağlantısını seçer; geçişi doğrulandı.
- **Entegre:** Intel grafik birimini seçer; cihaz sahibi başarılı donanım testi bildirdi. Diğer modlarla aynı onay ve güvenlik kontrollerini kullanır.

KDE'de sağ tıklamak menüyü, sol tıklamak durum penceresini açar. Ayrık kırmızı, Hibrit kehribar, Entegre turkuaz tonlarıyla gösterilir. Bekleyen değişiklik animasyonu, geçerli mod ile istenen hedefi ayırır; hedefin seçilmesi ekran bağlantısının değiştiği anlamına gelmez. **Hakkında** bölümünde **Yenilikler**, sistem ayrıntıları ve dahili ekranı süren GPU bulunur.

Dil menüsünden **Türkçe** veya **English** seçilebilir. Oturum açıldığında başlatma ve hareketi azaltma tercihleri isteğe bağlıdır. Uygulama, tepsinin sonradan hazır olması dahil tepsi kullanılabilirliğini takip eder.

Mod değiştirmek için adaptör bağlı olmalı; cihaz/BIOS eşleşmeli ve önceki işlem çözümlenmemiş durumda kalmamalı. Moda özel yazılı onayın ardından masaüstü yönetici kimlik doğrulaması ister. Arayüz yönetici olarak çalışmaz.

Başarılı isteğin ardından **Yeniden Başlat**, **Kapat** veya **Daha Sonra** seçenekleri sunulur. Güç işlemi yalnız açıkça seçtiğinizde, etkin masaüstünün normal oturum arayüzünden istenir. Masaüstü ayrıca onay sorabilir, işlemi engelleyen bir durum bildirebilir veya iptale izin verebilir; uygulama bunları atlayarak zorla kapatmaz.

Önce çalışmalarınızı kaydedin. **Üç kayıtlı testte doğrulanan yöntem tam kapatma ve yeniden açmadır; yalnız yeniden başlatmanın MUX değişikliğine yeterli olduğu henüz doğrulanmamıştır.** Yeniden açıldıktan sonra mevcut modu ve dahili panelin GPU’sunu kontrol edin.

GNOME'da aynı simge ve menüyü standart üst panelde görmek için AppIndicator uyarlayıcısını etkinleştirin; başka panel eklentileri konumu değiştirebilir. Uygulama eksik panel desteği için kurulum bağlantısı sunar ve normal penceresini kullanılabilir tutar. Başlatıcının üç moda yönelik kısayolları da uygulamanın onay akışını açar. [GNOME kurulumu ve uyumluluk](https://github.com/hayatboj/msi-gpu-mux-switch/blob/linux-kde/docs/GNOME.md) rehberine bakın. Geliştirme bilgisayarında gerçek GNOME oturumu test edilmemiştir.

Bu modelde Ayrık mod, Intel’e bağlı Thunderbolt görüntü çıkışlarını devre dışı bırakır. HDMI doğrudan NVIDIA’ya bağlıdır.

## Kurulum ve derleme

[Sürümler sayfasındaki](https://github.com/hayatboj/msi-gpu-mux-switch/releases) Linux paketini ve SHA-256 dosyasını kullanın. Sürümün donanım testi notlarını okuyun. Arşivi açtıktan sonra:

```sh
python3 install.py --check
sudo python3 install.py
```

Arşiv kurulumu `uninstall.py` ile kaldırılır. Arşiv kurulumu ve Arch paketini aynı anda kullanmayın. Kurulum otomatik başlatmayı kendiliğinden açmaz.

Kaynak derlemesi için Rust 1.88+, CMake, C++20 derleyicisi, Python 3 ve Qt 6 gerekir. Arch/CachyOS tarafında `base-devel`, `cmake`, `ninja`, `rust`, `python`, `qt6-base`, `qt6-svg`; yetkilendirme için `polkit` ve masaüstü kimlik doğrulama aracı kullanılır.

```sh
./scripts/build-linux.sh
./scripts/package-linux.sh 0.4.0
```

Yayın paketleri yalnız `v*` etiketi gönderildiğinde veya yayın iş akışı elle başlatıldığında derlenir. Yayımlanması için sürüm etiketi eşleşmeli ve Linux ile Windows doğrulama/derleme işleri başarıyla tamamlanmalıdır.

## Tanılama

Yalnız durum okur; firmware değişikliği veya MSI ACPI çağrısı yapmaz:

```sh
msi-mux-switch --status --json
```

Ayrıntılı tanılama, izin varsa MSI WMI durumunu da okur:

```sh
msi-mux-switch --debug --json
```

Uygulama sıradan durum kontrollerinde `nvidia-smi` çalıştırmaz. Değişiklik gerektiğinde yalnız sınırlı işlemleri kabul eden Polkit yardımcısını çağırır.

Üç modun etkileşimli komut satırı geçişinde de moda özel onay metnini aynen yazmak gerekir. Betikler için `--json` yalnız bu yazılı onayı atlar; diğer güvenlik kontrollerini kaldırmaz ve cihaz/BIOS denetimini aşan bir seçenek sunmaz. Masaüstü `--request-mode` kısayolları normal arayüz onayını ister; betik geçiş yolunu kullanmaz.

## Sınırlar

Fiziksel MUX geçişi kalıcı UEFI durumunu değiştirir. Firmware çağrısından sonraki hata, sonucu belirsiz bırakabilir. Önceki hedefin geri yazılması bütün donanım durumunun geri alındığı anlamına gelmez. BIOS varsayılanlarının veya EC resetinin her durumu kurtardığı doğrulanmamıştır. Kayıtlı Hibrit/Ayrık testleri ve cihaz sahibinin Entegre başarısı bildirimi yalnız belirtilen yapılandırmayla ilgilidir; her hatanın kurtarılabileceğini veya yeniden başlatmanın yeterli olduğunu göstermez. Hata yolları, gerçek firmware’i bilerek bozmak yerine sentetik testlerle sınanır. İlk kullanımdan önce [kurtarma notlarını](docs/RECOVERY.md) okuyun.

Bu uygulama `prime-run` gibi yalnız uygulamanın çizim GPU’sunu seçmez. Desteklenmeyen BIOS ve cihazlarda arayüz geçişi açmaz.

MIT lisanslıdır. Temel proje: [steelbrain/msi-gpu-mux-switch](https://github.com/steelbrain/msi-gpu-mux-switch). MSI veya NVIDIA ile resmi bağlantısı yoktur.
