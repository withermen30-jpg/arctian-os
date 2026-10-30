# Sürüm kayıtları

Sürüm numarasının tek kaynağı [`version.json`](version.json)'dur; Makefile derleme
anında okuyup `ARCTIAN_VERSION` olarak gömer.

## 0.7 — Koruma sınırı, güncelleme ve `.wlf` uygulama modeli

**Güvenlik / koruma sınırı**

- Her açılan uygulamaya bir **özne** (kimlik) kaydı: kaynak (gömülü / kutulu / güvenilir)
  + ad + yetenek maskesi. Kutulu paket varsayılan olarak `CAP_ALL` **almaz**; paket ne
  bildirdiyse o.
- **Karar deposu:** (özne, tür, kaynak) anahtarıyla *bir kez / her zaman / reddet*
  kararları; "bir kez" kararları çağrı bitince silinir.
- **Onay penceresi:** kullanıcıya "Bir kez / Her zaman / Reddet" seçenekleriyle ulaşan
  gerçek bir pencere; olay döngüsü gerçek zamanda ilerler (eskiden pencere kullanıcı
  görmeden kapanıyordu).
- **Dosya kapısı:** tüm dosya erişimi tek noktadan (`caps_fs_guard`) geçer; tutamac
  açıldığı yola bağlıdır, bu yüzden `read`/`write` çağrıları da kapıdan geçer.
- **Komut kapısı:** ağ ve komut çağrıları aynı politikaya bağlandı; uygulama başına tek
  ağ izni.
- **İmza:** `.wlf`/manifesto gövdesi RSA-2048 + SHA-256 ile imzalanır; imzasız paket ve
  imzasız manifesto kabul edilmez.

**Güncelleme ve A/B yuva**

- Ayarlar → Güncelleme sekmesi: otomatik kontrol + gecikme sınırı ayarı.
- İmzalı sürüm manifestosu (modül başına boyut + CRC32); imzasız manifesto reddedilir.
- Kurulum kapanışta/açılışta yapılır; yarım kalan kurulum açılışta tamamlanır.
- **A/B sistem yuvası:** kesik yuva algılanırsa önceki yuvaya otomatik geri dönüş;
  kullanıcı diski yuvalardan bağımsızdır.

**Uygulama modeli: yalnızca `.wlf`**

- Gömülü çekirdek uygulamalar dışında uygulama biçimi tek: `.wlf`. `.dpk` tarama, kurulum
  ve uzantısı kaldırıldı; `C:/apps` yalnızca `.wlf` taşır.
- Başlığa `role` alanı eklendi: `ui` (pencere), `cli` (terminalde koşar), `runtime`
  (yalnızca "birlikte aç" ile dosya alarak koşar).
- Meta bloğu (`"WLFM" + key=value`): `role`, `handles`, `entry`, `terminal`. Dosya
  eşleştirmesi `appviewer` kategorisi yerine `handles` üzerinden yapılır; tek aday varsa
  doğrudan açılır, birden fazlaysa seçim penceresi çıkar.
- `wf` komutları: `wf list/info` rol ve uzantı listesi gösterir, `wf run <ad> [dosya]`
  role göre çalıştırır, `wf build <ad> [ui|cli|runtime] [uzantılar]` meta bloğunu üretir.
- SDK: `wlfpack.py` rol/meta yazar ve `--inspect` ile gösterir; `wolf_api_t` sonuna
  `arg()` eklendi (çalışma zamanı dosya argümanı).
- Örnekler: `uidemo` (ui), `sayac` (cli), `pyrt` (runtime, `handles=py,pyw`).

**Masaüstü ve arayüz**

- Dock, arayüz şartnamesine göre yeniden yazıldı: 1600×900 referanslı `dk_s()` ölçek
  birimi, düz opak zemin, 44×44 düğmeler, durum göstergeleri, seviyeye göre çizilen pil
  ikonu, `IBM Plex Sans` saat/tarih.
- Yeni imlec: 32×32 SVG'den, piksel başına 8×8 alt örnekleme ile kenar yumuşatma; işaretçi
  ucu tam fare konumuna oturur.
- Varsayılan çözünürlük 1280×720; 720p'de arayüz taban ölçeği 0.7, masaüstü simgeleri ayrı
  ölçeklenir, satır aralıkları yazı yüksekliğinden hesaplanır.
- Asistan ayrı bir uygulama olarak da açılır (başlat menüsünde "Asistan", `run asistan`).
- Dosya yöneticisi: çizili 16px klasör/dosya ikonları (eksik TTF glyph'leri yüzünden boş
  kutu çiziyordu), uzantıya göre renk, ayırt edilebilir hover/seçim rengi, geri oku, arama
  kutusu, boyut sütunu ve detay paneli.

**Düzeltilen hatalar**

- **Sürüm 0.0 görünüyordu:** komut satırından `EXTRA_CFLAGS=...` verildiğinde GNU make,
  Makefile içindeki `EXTRA_CFLAGS +=` satırlarını yok sayıyordu; sürüm ayrı bir değişkende
  (`VERSION_CFLAGS`) tutuluyor.
- **Hedefsiz `make` hiçbir şey derlemeden başarılı dönüyordu:** `-include $(shell find build
  -name '*.d')` satırı `all:` hedefinden önce durduğu için GNU Make 4.x `.d` dosyasındaki ilk
  hedefi varsayılan hedef sayıyordu; imaj hiç yenilenmiyordu. Makefile başına
  `.DEFAULT_GOAL := all` eklendi, `-include` kapsamı `$(BUILD)` ile sınırlandı.
- **ISO sessizce atlanıyordu:** paketleyici `xorriso` hatasını yutup RC=0 dönüyordu. Artık
  hata derlemeyi durdurur; `.img` ve `.iso` `.part` üzerinden atomik taşınır + boyut
  doğrulaması yapılır. `build.bat` da RC=0'a güvenmez, ISO'nun bu derlemeden sonra
  yenilendiğini işaret dosyasıyla doğrular.
- **Saat yanlış saat dilimindeydi:** RTC/NTP UTC tutarken masaüstü UTC gösteriyordu;
  `clock_set_tz_offset()` eklendi (varsayılan `Europe/Istanbul`), saat artık
  saat/dakika/saniyeyi birlikte karşılaştırıyor.
- **Masaüstü ikonları kayboluyordu:** ikon katmanı sabit indekse taşınıyordu; perde
  nesneleri arka plana alınınca katman duvar kâğıdının altına düşüyordu. Artık katman her
  zaman duvar kâğıdının hemen üstüne taşınır.
- **Parlaklık/gece modu hiç görünmüyordu:** perdeler arka plana alınıyordu; etkin perdeler
  en öne alınır, ayar yazıldıktan hemen sonra uygulanır.
- **Hızlı ayarlar/bildirim listesi tek satıra yığılıyordu:** LVGL'de varsayılan layout
  olmadığı için flex flow gerekiyordu.
- **`ls C:/` alt klasör dosyalarını gösteriyordu:** AFS'de torun yollar kök listesine
  sızıyordu; yalnızca gerçek doğrudan çocuklar listelenir.
- **Türkçe metinler bozuk görünüyordu:** birkaç kaynak dosyada UTF-8 → CP1252 → UTF-8
  zinciriyle iki kez kodlanmış 68 metin geri çevrildi; kurulum ve hesap ekranı doğru
  çiziliyor.
- **Uygulama ikonları testere kenarlıydı:** en yakın komşu ile küçültme yerine alan
  ortalaması (`scale_hq`) kullanılıyor.
- **Asistan yanıt üretmiyordu:** giriş metni temizlendikten *sonra* okunuyordu; artık önce
  kopyalanıyor.
- Terminal yol çözümlemesi (`path_resolve` / `path_normalize`) sürücü harfli mutlak yol,
  ters bölü ve `..` için sağlamlaştırıldı.

**Yapılmayanlar (bilerek açık)**

- Ring 3 + süreç başına ayrı adres alanı ve çizim mesaj kuyruğu bu sürümde açılmadı: mevcut
  uygulama modeli tüm kodu masaüstüyle aynı adres alanında çalıştırıyor ve bu, çekirdek +
  çekirdek artısı + kabuğun birlikte yeniden yazılmasını gerektiriyor. Yarım bir izolasyon
  güvenlik vaadi vermez. Sıradaki adım: syscall ABI + çizim kuyruğu, sonra süreç başına
  adres alanı.

## 0.6 — Ağdan kurulum, ABI koruması, arayüz cila

- Ağdan `.wlf`/`.dpk` kurulumu, depo feed iyileştirmeleri.
- **ABI imzası:** paketler masaüstü sembol adreslerine mutlak linklendiği için, masaüstü her
  derlendiğinde ABI imzası değişir ve paketler yeniden üretilir; aksi hâlde eski paketler
  sayfa hatası veriyordu.
- `.wlf` native uygulamalar için `APP_BASE`'den ayrı bir yükleme bölgesi: üçüncü paket ile
  adres çakışması giderildi.
- Dock arama kutusu gerçek arama yapıyor (menüyü açıyor ama klavye odağını çalmıyor).
- Açık tema paleti; LVGL taban teması da doğru değiştiriliyor.
- Tarayıcı: yer imleri, indirme yöneticisi, ileri yığını, `fetch`/`XMLHttpRequest` köprüsü.
- AFS'de yerel yeniden adlandırma; dosya yeniden adlandırma/taşıma akışı.
- Çalışma alanı göstergesi, pencereyi çalışma alanına taşıma (Ctrl+Alt+Shift+,/.), kare hızı
  HUD'u, klavye düzeni seçimi, Özellikler penceresi, 0.6 öz-testleri.

## 0.5 — Kullanıcı deneyimi

- Kurulum sihirbazı (Kur / Dene, dil-klavye-saat seçimi, disk seçimi, kalıcı AFS).
- Keşfedilebilirlik: "Tümü" uygulamalar, kategoriler, sağ tık menüleri.
- Dosya deneyimi: uzantı ilişkilendirme, kopyala/taşı, sürükle-bırak, geri al, arama.
- Hızlı Ayarlar + OSD + bildirim merkezi.
- Pencere yönetimi: snapping reflow, Alt+Tab önizleme, boyut hatırlama.
- Pano ve metin düzenleme kısayolları (Ctrl+C/V/X/A, geri al).
- Sistem özelliklerini arayüze taşıma (NTP, yazdırma, dmesg, sürücüler).
- Erişilebilirlik: yazı ölçeği, yüksek kontrast, odak halkası, büyük simge.
- İlk kullanım: hoş geldin turu, yardım paneli, boş durum ekranları.
- Sürücü çalışması: Atheros ath9k (AR928x) uçtan uca (EEPROM/reset/halka/kanal/tarama/
  auth-assoc/WPA2 el sıkışma/CCMP); Intel AX201 firmware + tarama-bağlantı iskeleti.

## 0.4 ve öncesi — çekirdek işlevler

Önyükleyici (MBR/El Torito), uzun mod + sayfalama, modüler imaj biçimi, temel sürücüler
(PCI, VESA, blok aygıtları, klavye-fare), AFS, LVGL tabanlı kabuk, ASI arayüzü, ses,
ağ yığını ve kurulum akışının çekirdeği.
