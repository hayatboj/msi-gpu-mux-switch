# Sürüm notları

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
