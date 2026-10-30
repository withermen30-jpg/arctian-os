# `third_party/`

Bağımlılıklar **vendored** edilmiştir (git submodule değil): repoyu klonlayan biri ek
bir şey indirmeden derleyebilir. Alt ağaçlar mümkün olduğunca budanmıştır; derlemeye
girmeyen test/örnek/belge klasörleri ve üst seviye araçlar çıkarılmıştır.

| Kütüphane | Sürüm | Lisans | Boyut | Nerde kullanılıyor |
|---|---|---|---|---|
| [LVGL](https://github.com/lvgl/lvgl) | 9.6.0 | MIT (`lvgl/LICENCE.txt`) | ~25 MB | masaüstü kabuğu + tüm arayüzler |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | 3.6.2 (LTS) | Apache-2.0 (`mbedtls/LICENSE`) | ~8 MB | HTTPS / TLS (`kernel_plus`) |
| [QuickJS](https://bellard.org/quickjs/) | 2025-09-13 | MIT (`quickjs/LICENSE`) | ~2,3 MB | Wolf Engine tarayıcısının JS motoru |
| [Lexbor](https://github.com/lexbor/lexbor) | 3.1.0 | Apache-2.0 (`lexbor/LICENSE`) | ~41 MB | Lexbor tarayıcısı (HTML/CSS/selector) |

Her kütüphanenin kendi lisans metni alt klasöründe durur; dağıtım sırasında bu dosyalar
korunmalıdır (Apache-2.0 madde 4(c)).

## `lvgl/` — arayüz kiti (masaüstü kabuğu)

Tutulan kısım: `include/`, `src/`, `lvgl.h`, `lvgl_private.h`, `lv_version.h`,
`lv_conf.h`, `lv_conf_template.h`, `COPYRIGHTS.md`, `LICENCE.txt`.
Çıkarılanlar: `.git`, `tests/`, `examples/`, `demos/`, `docs/`, `scripts/`, `configs/`,
`framework/`, `component/` ve derlemeye girmeyen üst seviye dosyalar.

**Kaynak koduna dokunulmadı.** Davranış iki yerle ayarlanır:

- `lvgl/lv_conf.h` — Arctian yapılandırması: yerleşik stdlib (`LV_USE_STDLIB_*` = BUILTIN),
  `LV_MEM_SIZE` 4 MB, `LV_USE_OS = LV_OS_NONE`, yalnız yazılım çizici (`LV_USE_DRAW_SW 1`),
  log ve POSIX dosya sistemi kapalı.
- `Makefile` içindeki `LVGL_C` listesi — `src/` altında `find` ile dolaşır ve GPU/OS/platform
  çizicilerini, sürücüleri, harici kütüphane bağlarını (`src/libs/*`), FreeType ve
  `debugging/` altını hariç tutar.

Kabuk LVGL'yi `desktop/port/lv_port.c` üzerinden bağlar (çerçeve tamponuna çizim, fare/klavye
girdisi, tick).

## `mbedtls/` — TLS (HTTPS ve mağaza istemcisi)

Tutulan kısım: `library/`, `include/`, `3rdparty/everest`, `3rdparty/p256-m`, `LICENSE`,
`SECURITY.md`. Çıkarılanlar: `.git`, `tests/`, `programs/`, `configs/`, `docs/`,
`framework/`, `visualc/` ve `ChangeLog`/CI dosyaları.

**Tek kaynak değişikliği** `include/mbedtls/mbedtls_config.h` sonuna eklenen "Arctian
bare-metal override" bloğudur: libc/dosya/ağ/zaman bağımlılıkları kapatılır
(`MBEDTLS_FS_IO`, `MBEDTLS_NET_C`, `MBEDTLS_TIMING_C`, `MBEDTLS_HAVE_TIME*`, PSA dosya
depolaması, `MBEDTLS_SELF_TEST`, `MBEDTLS_DEBUG_C` kaldırılır), yerine donanım entropisi ve
statik bellek havuzu zorunlu kılınır (`MBEDTLS_NO_PLATFORM_ENTROPY`,
`MBEDTLS_ENTROPY_HARDWARE_ALT`, `MBEDTLS_PLATFORM_MEMORY`, `MBEDTLS_MEMORY_BUFFER_ALLOC_C`).

Platform bağlantısı repoya aittir: `lib/mbedtls_port.c` eksik libc fonksiyonlarını
(`memmove`, `strncmp`, …) ve `mbedtls_hardware_poll` entropi kaynağını sağlar. Tüm
`library/*.c` dosyaları `libmbedtls.a` olarak derlenip `kernel_plus` içine linklenir.

## `quickjs/` — JavaScript motoru

Bellard'ın QuickJS'inin çekirdek dosyaları olduğu gibi tutulur: `quickjs.c`, `cutils.c`,
`libregexp.c`, `libunicode.c`, `dtoa.c` + başlıkları ve opcode tabloları. Kaynak koduna
dokunulmadı.

Üstyapı repoya aittir:

- `quickjs_port.c` — platform köprüsü. `asi.h` üzerinden bellek/arena sağlar
  (`arctian_js_arena`), libc'te olmayan `pthread`/`time` çağrılarını karşılar.
- Makefile `QUICKJS_CORE` listesi bu beş dosyayı + port shim'ini derler ve
  `desktop.bin` içine linkler.

Kullanım yeri: Wolf Engine tarayıcı uygulaması (`desktop/apps/wolf_engine`). Motorun yığını,
kabuğun sağladığı 64 MB'lık arena içine kurulur (`arctian_js_arena`, `desktop/shell_api.c`).
`.wlf` paketlerinin `WLF_KIND_SCRIPT` türü QuickJS **değil**, `desktop/wlf_vm.c`'nin
çalıştırdığı Wolf bytecode'udur.

## `lexbor/` — HTML/CSS ayrıştırıcı

Tarayıcı uygulamasının (`desktop/apps/lexbor`) parser katmanı. İki parça derlemeye girer:

- `lexbor_single.c` — projenin kendi ürettiği tek dosyalık birleştirilmiş derleme (~20 MB);
  Makefile bunu `-O1 -w` ile derler.
- `lex_posix_stub.c` — `calloc` gibi POSIX bağımlılıklarını Arctian ayırıcısına bağlayan köprü.
- `source/` ağacı başlıklar için `-Ithird_party/lexbor/source` ile yola eklenir.
- `LICENSE` Apache-2.0 metnidir.

## Güncelleme yaparken

1. Yukarıdaki akış/boyut dengesini bozmadan taze bir kaynak kopyası yerleştir.
2. `lv_conf.h` ve `mbedtls_config.h` içindeki Arctian ayarlarının, `quickjs_port.c` ve
   `lex_posix_stub.c` köprülerinin korunduğunu kontrol et.
3. Tam bir temiz derleme yap (`build/obj-*` silinmişken `build.bat`) ve ISO'nun QEMU'da
   açıldığını doğrula — LVGL sürüm değişimi kabuğun çizimini doğrudan etkiler.
