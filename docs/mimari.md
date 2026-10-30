# Arctian mimarisi

Bu belge sistemin parçalarını ve aralarındaki sözleşmeleri anlatır. Uygulama yazmak için
gereken API bilgisi burada değil: [uygulama-gelistirme.md](uygulama-gelistirme.md).

## 1. Genel şekil

Arctian "tek büyük çekirdek" (monolithic) çalışır, ama kod **bağımsız modüllere** bölünmüştür.
Her modül kendi `.bin` imajıdır, sabit bir fiziksel adrese yüklenir ve başında 512 baytlık
bir **AMOD** başlığı taşır. Modüller birbirini C sembolü üzerinden çağırır; hepsi aynı
adres alanında, aynı ayrıcalık halkasında (ring 0) çalışır.

```
 BIOS/UEFI(CSM)
   └─ stage1 (MBR 0x7C00)              boot/stage1.asm        diskten
   └─ stage1_iso (El Torito 0x7C00)    boot/stage1_iso.asm    CD'den
        └─ stage2 (0x10000)            boot/stage2.asm        uzun mod, sayfalama, VESA
             └─ main_kernel (0x100000) main_kernel/           GDT/IDT, yükleyici, syscall
                  ├─ install   (0x200000)  install/           kurulum sihirbazı
                  ├─ kernel_plus (0x300000) kernel_plus/      sürücüler + servisler (ASI)
                  └─ desktop   (0xC00000)  desktop/           LVGL kabuğu + uygulamalar
```

Adreslerin tek kaynağı [`include/arctian/memory_map.h`](../include/arctian/memory_map.h).
Bölgeler ve sınırları:

| Bölge | Taban | Azami |
|---|---|---|
| `main_kernel` | `0x00100000` | 1 MB |
| `install` | `0x00200000` | 1 MB |
| `kernel_plus` | `0x00300000` | 7 MB |
| `desktop` (imaj + bss) | `0x00C00000` | 8 MB |
| `.dpk` uygulama bölgesi | `0x01800000` | 40 MB (2 MB dilimler) |
| `.wlf` uygulama bölgesi | `0x03000000` | 8 MB |
| disk tamponu | `0x04000000` | 16 MB |
| blok/DMA geçici alan | `0x05000000` | 1 MB |
| backbuffer | `0x08000000` | 16 MB |
| yığın (heap) | `0x10000000` | 768 MB |

`kernel_plus` `.bss`'i eski masaüstü tabanına dayandığı için masaüstü `0xA00000`'dan
`0xC00000`'a taşındı; adreslere dokunurken bu tabloyu birlikte düşünmek gerekir.

Önbellek/idleme eşlemesi: 0–1 GB arası 2 MB'lik huge page'lerle kimlik-eslemeli
(`IDENTITY_MAP_*`), yani modüller yüklenirken sanal=fiziksel çalışır.

## 2. İmaj biçimi

### Disk imajı (`build/arctian.img`)

```
LBA 0..     : boot imaji (stage1 + stage2 + main_kernel)
LBA 2048    : superblock  (sihir "ARCT", sürüm 1)
LBA 2049    : modul tablosu
LBA 2050    : durum sektoru (kapanis/açilis kurulumu için)
LBA 4096..  : modul imajlari  (512B AMOD başlığı + binary, 512B'ye yastalanmış)
LBA 20000.. : AFS (kullanıcı diski C:/)
```

### ISO (`build/arctian.iso`)

El Torito, no-emulation, önyüklenebilir; `boot_iso.img` (stage1_iso + stage2 + main_kernel)
biçimli kaydıktan sonra modüller ISO9660 dosyaları olarak durur. Packer (`tools/arctianpack.py`)
`xorriso` üretir; hata olursa derlemeyi durdurur ve çıktı `.part` üzerinden atomik taşınır.

### AMOD başlığı

[`include/arctian/amod.h`](../include/arctian/amod.h) — 512 bayt: sihir (`AMOD`), ABI sürümü,
tür (kernel / install / driver_svc / desktop / app), tercih edilen yükleme adresi, imaj boyutu,
`.bss` boyutu, giriş offseti, basit toplama sağlama toplamı, ad ve sürüm. `.bss` yükleyici
tarafından sıfırlanır; `entry_off` adrese eklenip atılır.

## 3. Modüller

| Modül | İçerik |
|---|---|
| `boot/` | NASM ile yazılmış üç aşama: disk/ISO okuma, A20, uzun mod, sayfama, VESA modu seçimi, VBE info bloğu |
| `main_kernel/` | GDT/IDT, ISR'ler, PIC, sayfalama ve VMM, modül yükleyici, syscall, ring3 iskeleti (`ring3.c` — henüz tam izolasyon sağlamaz) |
| `install/` | Kurulum sihirbazı: bölümleme, AFS biçimlendirme, modül kopyalama, önyükleme kaydı; kalıp/görseller `design/setup/` HTML'inden üretilir |
| `kernel_plus/` | Sürücüler (`drivers/`), ağ yığını (`net/`), zamanlayıcı/işlem/iplik/senkronizasyon, yetenek kapıları (`system/caps.c`), disk şifreleme, sanal FS, TLS |
| `desktop/` | LVGL ile masaüstü kabuğu (dock, pencereler, paneller), gömülü uygulamalar, `.wlf` yükleyici/VM, tarayıcı motorları (Wolf Engine + QuickJS, Lexbor), tema (ADL-Wolf), kurulum sonrası ekranlar |
| `lib/` | AFS, FAT32, ISO9660, blok katmanı (AHCI/ATA/NVMe), ses/medya kodekleri, PNG/SVG, malloc, `printf`, seri port/klog |
| `include/arctian/` | Kamu başlıkları: `asi.h`, `app.h`, `wlf.h`, `amod.h`, `dpk.h`, `update.h`, `sig.h`, `slot.h`, `memory_map.h`, ... |

| `third_party/` | Vendored kütüphaneler: LVGL (arayüz), mbedTLS (TLS), QuickJS (Wolf Engine JS), Lexbor (HTML/CSS). bkz. `third_party/README.md` |

Uygulamalar `lib/` ve çekirdek sembollerini **bağlanma zamanında** değil, paket yüklenirken
çözümlemez: gömülü uygulamalar `desktop.bin` içine linklenir, `.wlf` paketleri yalnızca
`wolf_api_t` fonksiyon tablosu üzerinden sisteme dokunur (ABI sözleşmesi başlığın sonuna
ekleme yapılarak sürülür).

## 4. ASI — uygulama arayüzü

`asi_t` ([`include/arctian/asi.h`](../include/arctian/asi.h)) uygulama ile sistemin tek temas
noktasıdır; her uygulama `entry(asi_t *asi, ...)` içinde bu köprüyü alır. Gruplar:

`console`, `mem`, `storage`, `time`, `gfx`, `input`, `system`, `fs`, `net`, `proc`, `lab`,
`sandbox`, `audio`, `crypt`, `firewall`, `power`.

libc yoktur: `printf`, `malloc`, `strlen` gibi semboller yoktur; `memcpy`/`memset`/`memmove`/
`memcmp`/`strlen` işletim sistemi tarafından sağlanır. Bellek için `asi->mem`, arayüz için LVGL
kullanılır.

## 5. Uygulama modeli: `.wlf`

0.7'de gömülü çekirdek uygulamalar dışında **her uygulama bir `.wlf`'tir**; `C:/apps` yalnızca
`.wlf` taşır. Biçim ([`include/arctian/wlf.h`](../include/arctian/wlf.h)):

```
[wlf_header_t] [meta: "WLFM" + u16 uzunluk + key=value satırları]
               [ikon (PNG/SVG, isteğe bağlı)] [payload]
```

- **Tür:** `WLF_KIND_NATIVE` (x86-64 freestanding kod, libWolf'a linklenmiş) veya
  `WLF_KIND_SCRIPT` (derli Wolf bytecode; derleyici gerekmediği için işletim sistemi içinde
  `wf build` ile üretilebilir).
- **Rol** (`header.role`, eski `reserved0` alanının yerini aldı):
  - `ui` → pencere açılır,
  - `cli` → terminal açılır ve program terminalde koşar,
  - `runtime` → yalnızca "birlikte aç" ile dosya alarak koşar (örneğin portlanmış bir dil).
- **Meta anahtarları:** `role`, `handles=py,pyw`, `entry=python`, `terminal=1`.
  Dosya eşleştirmesi eski `appviewer` kategorisi yerine `handles` üzerinden yapılır.
- Sağlama toplamı ikon + payload üzerindedir; meta bloğu bunun dışındadır ama **imza
  kapsamındadır**. Başlığa alan eklerken kural: alanlar yalnızca sona eklenir.

Native `.wlf` kendi bölgesine (`WLF_BASE`) yüklenir, `.bss` sıfırlanır ve
`int wolf_main(const wolf_api_t *api)` çağrılır.

## 6. Koruma sınırı ve imza

Bu sürümdeki sınır **politika** düzeyindedir; donanımsal izolasyon (ring 3 + süreç başına
adres alanı) henüz uygulanmadı.

Akış:

1. Uygulama açılınca bir **özne** kaydı oluşur: kaynak (gömülü / kutulu / güvenilir) + ad +
   yetenek maskesi. Kutulu paket varsayılan olarak tüm yetenekleri almaz.
2. Dosya erişimi tek noktadan (`caps_fs_guard`) geçer; açılan tutamac yoluyla bağlantılıdır,
   bu yüzden `read`/`write` de kapıdan geçer.
3. Ağ ve komut çağrıları kendi kapılarından geçer; izin uygulama başınadır.
4. Karar gerekirse **onay penceresi** açılır (Bir kez / Her zaman / Reddet). Kararlar
   **karar deposu**na yazılır ("bir kez" çağrı bitince silinir).
5. Paket ve güncelleme manifestosu **RSA-2048 + SHA-256** ile imzalanır; imzasız olan reddedilir.
   Geliştirme anahtarı `tools/keys/dev_rsa2048.txt`; yenisi `tools/sig_keygen.py`.

Açık kalan iş (bilerek bu sürüme alınmadı): ayrı PML4 + kullanıcı yığını + `iret` ile ring 3,
ince syscall ABI'si, compositten geçen çizim kuyruğu. Sıra: önce syscall ABI + çizim kuyruğu,
sonra süreç başına adres alanı.

## 7. Güncelleme ve A/B yuva

- Sunucu imzalı bir satır-tabanlı manifesto yayınlar (`#arctian-update 1`): her modül için boyut
  + CRC32. İstemci indirdikten sonra doğrular.
- Ayarlar → Güncelleme: otomatik kontrol ve gecikme günü ayarı.
- Kurulum **kapanışta** başlar, bitmezse **açılışta** tamamlanır (durum sektörü izler).
- Sistem iki yuvada (A/B) durur; yarıda kesilmiş bir güncellemede açılış önceki yuvaya döner.
  Kullanıcı diski (AFS) yuvalardan bağımsızdır.

## 8. Dosya sistemi

- **AFS v4** kullanıcı diskidir (`C:/`), diskte LBA 20000'den başlar, 64 bayta kadar ad,
  klasör/ekleme/silme, boyut metadata'sı ve bütünlük denetimi (fsck) vardır.
  Biçim sürümü uymazsa disk yeniden biçimlendirilir.
- Sistem dosyaları (ayarlar, kurulu paketler) ham kökte durur ve masaüstünde gizlidir;
  `C:/` kullanıcıya aittir: `C:/apps`, `C:/belgeler`, `C:/indirilenler`, `C:/masaustu`, `C:/logs`.
- Ayrıca **FAT32** (USB/taşınabilir) ve **ISO9660** (CD'den açılış) okunur; **USB MSD**
  üzerinden takılan diskler sanal FS katmanında görünür.
- İsteğe bağlı **AES-XTS disk şifreleme** (`kernel_plus/system/diskcrypt.c`).

## 9. Ağ

`kernel_plus/net/` sıfırdan yazılmış bir yığındır: ARP, IPv4, TCP, UDP, DHCP, DNS, NTP,
HTTP/HTTPS (mbedTLS ile TLS), artı uygulama seviyesi güvenlik duvarı. Sürücüler: e1000,
PCnet, RTL8139, RTL8169, Atheros ath9k (uçtan uca: EEPROM/reset/halka/kanal/tarama/
auth-assoc/WPA2/CCMP), Intel AX201 (iskelet). WPA2 katmanı `kernel_plus/drivers/wpa2.c`.

## 10. Masaüstü kabuğu

- `desktop/main.c` + `dsk_state.c` + `taskbar.c`: kabuk kurulumu ve dock. Dock 1600×900
  mantıksal referanstan ölçeklenir (`dk_s()`); genişlik `596 + 48n - 4`.
- `desktop/windows.c`: pencere yöneticisi (taşıma, yakınlaştırma, snap, boyut hatırlama,
  çalışma alanı taşıma).
- `desktop/quickctl.c`: hızlı ayarlar, OSD, bildirim merkezi, parlaklık/gece modu perdeleri.
- `desktop/wlf*.c`: `.wlf` yükleme, rol yönlendirme, terminal köprüsü, paket başlığı okuma.
- `desktop/perm.c` + `consent.c`: özne/karar deposu ve onay penceresi.
- `desktop/theme/adl_wolf.c`: tema (renk paleti, ölçek, font).
- Ölçek iki katmanlıdır: kabuk `ui_s` (720p'de taban 0.7), masaüstü simgeleri `ic_s`
  (720p'de 0.55); 1080p'de ikisi de 0.75 olduğu için görünüm aynıdır.

## 11. Öz-testler

Ayrı derleme bayraklarıyla gelen gerçek test imajları vardır; normal derlemede kode dahil
değildir:

```sh
make pack DEBUG=1 EXTRA_CFLAGS=-DARCTIAN_07_TEST
```

Seri porta satır satır `OK`/`HATA` yazarlar (`desktop/t06test.c`, `desktop/t07test.c`,
`desktop/selftest.c`). Ayrıca `make debug` kurulum/hesap ekranlarını atlayan geliştirme
imajını, `make setuptest` kurulum özelleştirme ekranını doğrudan açan imajı üretir.
