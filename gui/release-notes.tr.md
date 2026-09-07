# Sürüm notları

## 0.5.0 · 7 Eylül 2026

### Güç işlemine kadar değiştirilebilir seçim

- **Uygula** artık seçimi uygulamada kaydeder; donanıma henüz yazmaz. Yanlışlıkla
  Entegre seçtiysen Hibrit'i seçip tekrar Uygula diyebilirsin.
- Mod adı yazma gereği kaldırıldı. Hedef büyük gösterilir; **İptal** varsayılandır.
- **Yeniden başlat / Bilgisayarı kapat** seçildiğinde son hedef yeni durum
  okumasıyla kontrol edilir, donanıma bir kez uygulanır ve sonuç doğrulanır.
  Yalnız başarıdan sonra seçtiğin güç işlemi masaüstüne iletilir.
- **Daha sonra** donanım veya güç işlemi yapmaz. Seçim aynı açılışta uygulamayı
  yeniden açınca korunur; seçimi iptal edebilir veya başka moda çevirebilirsin.
- Geçerli mod, yerel seçim ve donanıma uygulanmış bekleyen hedef ayrı gösterilir.
- Hakkında'daki desteklenen yapılandırma ve donanım testi paragrafı kaldırıldı.

**Masaüstünün kendi menüsünden yeniden başlatmak seçimi uygulamaz.** Seçimi
uygulamak için MSI MUX'un güç düğmelerini kullan. Yeni açılışta eski yerel seçim
uygulanmış sayılmaz; geçersizleştirilir.

Önceki sürümde donanıma zaten iletilmiş bekleyen istekler güç döngüsüne kadar
kilitli kalır. Yeni yerel seçim akışı bu eski isteği geri almaz. Firmware motoru,
işlem günlüğü ve yetkili yardımcı değiştirilmedi.

## 0.4.0 · 7 Eylül 2026

### Daha anlaşılır mod geçişleri

- **Ayrık kırmızı**, **Hibrit sarı**, **Entegre turkuaz** renklerle gösterilir.
- İstek başarıyla uygulanınca üst alan geçerli moddan hedef moda hareketli bir gradyana dönüşür. Örneğin **Ayrık → Hibrit** geçişi kırmızıdan sarıya akar.
- Gerçekte çalışan mod, istenen moddan ayrı kalır; güç döngüsü tamamlanmadan yeni mod etkinmiş gibi gösterilmez.
- Mod çizimleri, Ayrık mod için roket ve hareketi azaltma ayarı eklendi. Pencere gizliyken animasyon durur.

### Hakkında ve masaüstü

- Donanım doğrulama bilgileri ana ekrandan **Hakkında** bölümüne taşındı.
- GitHub · **hayatboj**, proje, sorun bildirimi ve özgün proje bağlantıları eklendi.
- Türkçe/İngilizce sürüm notları internet olmadan uygulama içinden okunabilir.
- Sistem özeti isteğe bağlı kopyalanabilir; ham firmware verisi veya seri numarası içermez.
- GNOME için AppIndicator panel kurulumu ve uygulama simgesinin sağ tık menüsünde mod onayı kısayolları eklendi.

### Güç seçenekleri ve Entegre mod

- Başarılı mod isteğinden sonra **Yeniden başlat**, **Tamamen kapat** ve **Daha sonra** seçenekleri gösterilir.
- Güç işlemi yalnızca düğmeye açıkça basıldığında masaüstüne iletilir. Zorla kapatma veya otomatik geri sayım yoktur; masaüstü ek onay gösterebilir.
- Normal yeniden başlatmanın MUX geçişini tamamladığı henüz doğrulanmadı; tam kapatma seçeneği korunur.
- Cihaz sahibinin başarılı donanım testi bildiriminden sonra **Entegre modun deneysel kısıtı kaldırıldı**. Donanım, BIOS, güç ve işlem güvenlik kontrolleri korunur.

GNOME paneli AppIndicator desteği gerektirir; bu KDE bilgisayarında gerçek GNOME oturumu testi yapılmadı.

## 0.3.0 · 7 Eylül 2026

- Hibrit → Ayrık → Hibrit → Ayrık dizisindeki üç tam kapatma/açma testi doğrulandı.
- Dahili ekran bağlantısı ve 2560×1600, 240 Hz her adımda kontrol edildi.
- Geç yüklenen KDE tepsisi desteği ve güvenli sürüm yükseltme testleri eklendi.
- O sürümde Entegre mod, henüz sınanmadığı için deneysel seçenek gerektiriyordu.

## 0.3.0-rc.1 · 7 Eylül 2026

- Türkçe/İngilizce Qt 6 tepsi uygulaması, Polkit yardımcı programı ve kurulum paketi.
- Salt okunur durum sorgusu, işlem kaydı ve belirsiz firmware sonucu yönetimi.

## 0.2.0 · 31 Ağustos 2026

- Linux durum okuma ve mod değiştirme desteği, Linux paketleri.

## 0.1.0 · 31 Ağustos 2026

- İlk Windows komut satırı aracı, donanım doğrulaması ve GPU MUX protokolü.
