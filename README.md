# Arctian OS(v0.2)

64-bit, modüler bir işletim sistemi. Önyükleyici, çekirdek, sürücü/ağ katmanı ve
LVGL tabanlı masaüstünden oluşur. Uygulamalar C, Zig veya Rust ile yazılabilir.

# Geliştirici Notu

Arctian şuanda 0.2 sürümünde olup daha henüz son kullanıcı için hazır değildir. 1.0 sürümü için tahmini tarih 1 Ocak 2027 dir.
Bu kaynak koddur, eğer derlenmiş ve kullanıma hazır versiyonunu istiyorusanız lütfen şuradan indirin: https://witherstudio.com.tr

## Gereksinimler

Linux (Debian/Ubuntu) veya Windows'ta WSL (Debian). Derleme için:

    apt install build-essential nasm python3 xorriso

İsteğe bağlı (uygulamaları Zig/Rust ile yazacaksanız):

    lld                      # Rust nesnelerinin linklenmesi için
    zig 0.13+                # Zig uygulamaları
    rustc + x86_64-unknown-none hedefi   # Rust uygulamaları
        rustup target add x86_64-unknown-none

Çalıştırmak/test etmek için QEMU: `qemu-system-x86_64`.

## Derleme

    make

Çıktılar:

- `build/arctian.img` — diske (HDD/SSD/USB) yazılabilir önyüklenebilir imaj
- `build/arctian.iso` — BIOS (El Torito) önyüklenebilir ISO

Windows'ta `build.bat` çalıştırın (WSL Debian üzerinden `make` çağırır).

Yardımcı:

    ./build_iso.sh          # temiz derleme + ISO
    ./build_iso.sh clean    # derleme çıktılarını temizle
    ./build_iso.sh qemu     # derleyip QEMU'da aç

## QEMU ile çalıştırma

Doğrudan disk imajı:

    make qemu

veya:

    qemu-system-x86_64 -drive file=build/arctian.img,format=raw -m 512M -vga std -serial stdio

ISO'dan (kalıcı kurulum için boş bir hedef disk ekleyin):

    qemu-img create -f raw build/testdisk.img 8G
    qemu-system-x86_64 -m 512M -vga std -serial stdio \
        -cdrom build/arctian.iso \
        -drive file=build/testdisk.img,format=raw

ISO'dan açıldığında `install` modülü hedef diske kalıcı kurulumu otomatik yapar;
sonraki açılışlar diski çıkarıp kurulu sistemden yapılabilir.

## Proje yapısı

    boot/            stage1 / stage2 önyükleyici
    main_kernel/     çekirdek bootstrap ve modül yükleyici
    kernel_plus/     sürücüler, ağ (TCP/IP, TLS 1.3) ve sistem servisleri (ASI)
    desktop/         LVGL masaüstü ve uygulamalar (desktop/apps/)
    lib/             ortak kütüphane (AFS, PNG/SVG, blok aygıtlar, ...)
    install/         kalıcı kurulum modülü (ISO -> disk)
    include/arctian/ ortak başlıklar (ASI, AMOD, DPK, app)
    third_party/     lvgl, mbedtls
    tools/           paketleyici ve kod üretici scriptler

Derleme akışı: `boot` -> `main_kernel` -> `kernel_plus` -> `desktop`.
Her modül 512 baytlık bir AMOD başlığı ile paketlenir (`tools/arctianpack.py`).

## Uygulama geliştirme

`desktop/apps/<ad>/` altına üç dosya koyup `make` çalıştırın:

    app.json     meta veri
    main.c       (veya main.zig / main.rs)
    icon.svg     40x40 SVG ikon

`app.json` örneği:

    {
      "name": "uygulamam",
      "title": "Uygulamam",
      "category": "Araçlar",
      "icon": "icon.svg",
      "w": 560,
      "h": 380
    }

Giriş noktası:

    void app_entry(lv_obj_t *win, asi_t *asi);

Uygulama derleme sırasında otomatik bulunur ve masaüstünde + Başlat menüsünde
görünür. Zig ve Rust kaynakları (`main.zig`, `main.rs`) da aynı şekilde derlenir.

## Lisans

CC BY-NC 4.0. Ticari kullanım yasaktır. Her kullanım, dağıtım veya türev
çalışmada "Arctian OS" ve

    https://github.com/withermen30-jpg/arctian-os

bağlantısı belirtilmelidir. Ayrıntılar için `LICENSE` ve `NOTICE`.
