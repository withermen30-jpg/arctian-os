# Arctian OS — Uygulama Geliştirici Kılavuzu (C)

Bu belge, **Arctian** işletim sistemi için C dilinde uygulama geliştirmek
isteyen biri için tek kaynak referanstır. Dosya sistemi, medya (ses), ağ,
grafik, girdi, süreç, güvenlik, güç yönetimi ve mağaza (DuckStore) dahil
**tüm genel API'leri** ve çalışan **kod örneklerini** içerir.

- Sürüm: **Arctian 0.7** (`ARCTIAN_VERSION` — tek kaynak `version.json`)
- Arayüz kiti: **LVGL v9.6** + ASI (Arctian System Interface, sürüm `2`)
- Mimari: x86_64 (AMD64), 64-bit; uygulamalar `asi_t *asi` köprüsü üzerinden sisteme
  erişir. Uygulamalar kabukla aynı adres alanında çalışır — izinler bir *politika*
  sınırıdır, donanımsal izolasyon değildir (bkz. [mimari.md](mimari.md)).
- Ekran çıkışı: Bochs VBE / VESA çerçeve tamponu; varsayılan **1280×720**, Ayarlar →
  Ekran'dan çalışma anında **1600×900 / 1920×1080 / 2560×1440**'a değiştirilebilir.

> Uygulamalar libc **kullanmaz**. `printf`, `malloc`, `strlen`, `#include <stdio.h>`
> gibi hiçbir standart C kütüphanesi sembolü yoktur. Bellek için `asi->mem`,
> metin için `asi->console`, arayüz için LVGL kullanılır. `memcpy`/`memset`/`memmove`/
> `memcmp`/`strlen` işletim sistemi tarafından sağlanır ve doğrudan çağrılabilir.

---

## İçindekiler

1. [Kavramlar ve çalışma modeli](#1-kavramlar-ve-çalışma-modeli)
2. [İlk uygulama — 5 dakikada](#2-ilk-uygulama--5-dakikada)
3. [Uygulama iskeleti: `app_entry` + `app.json` + `icon.svg`](#3-uygulama-iskeleti)
4. [Derleme ve paketleme (gömülü + `.wlf`)](#4-derleme-ve-paketleme)
5. [Arayüz: LVGL v9.6 hızlı referans](#5-arayüz-lvgl-v96-hızlı-referans)
6. [Tema (ADL-Wolf) ve font](#6-tema-adl-wolf-ve-font)
7. [ASI genel bakış](#7-asi-genel-bakış)
8. [console, mem, storage, time](#8-console-mem-storage-time)
9. [gfx — grafik](#9-gfx--grafik)
10. [input — fare ve klavye](#10-input--fare-ve-klavye)
11. [system — güç, çökme koruma, writeback](#11-system--güç-çökme-koruma-writeback)
12. [fs — dosya sistemi (AFS + USB)](#12-fs--dosya-sistemi-afs--usb)
13. [net — ağ](#13-net--ağ)
14. [proc, lab, sandbox](#14-proc-lab-sandbox)
15. [audio — medya/ses](#15-audio--medyases)
16. [crypt, firewall, power](#16-crypt-firewall-power)
17. [DuckStore mağaza API'si (`arctian_store`)](#17-duckstore-mağaza-apisi-arctian_store)
18. [Ayarlar API'si (`arctian_settings`)](#18-ayarlar-apisi-arctian_settings)
19. [Kimlik doğrulama (`arctian_auth_*`)](#19-kimlik-doğrulama-arctian_auth_)
20. [Kabuk köprüsü (`arctian_shell_*`)](#20-kabuk-köprüsü-arctian_shell_)
21. [Güncelleme API'si (`update.h`)](#21-güncelleme-apisi-updateh)
22. [Dosya açma (uzantı eşleme)](#22-dosya-açma-uzantı-eşleme)
23. [Yetenekler (izinler / sandbox)](#23-yetenekler-izinler--sandbox)
24. [Tam örnekler](#24-tam-örnekler)
25. [İpuçları ve sık yapılan hatalar](#25-i̇puçları-ve-sık-yapılan-hatalar)
26. [Ek: iç formatlar (WLF, DPK, AMOD, recovery)](#26-ek-i̇ç-formatlar)

---

## 1. Kavramlar ve çalışma modeli

Arctian'da iki tür uygulama vardır:

| Tür | Yeri | Ne zaman | Örnek |
|-----|------|----------|-------|
| **Gömülü uygulama** | `desktop/apps/<ad>/` | `make` ile derler, OS'un içine gömülür | `terminal`, `settings`, `filemanager` |
| **`.wlf` uygulaması** | `C:/apps/<ad>.wlf` | SDK ile derlenir, mağazadan indirilir veya `wf install` ile kurulur | `uidemo`, `sayac`, `pyrt` |

İki türün kaynak arayüzü farklıdır:

- **Gömülü uygulama:** `app_entry(lv_obj_t *win, asi_t *asi)` + `app.json`. LVGL ve ASI
  başlıklarını doğrudan kullanır; desktop modülüne linklenir. Bu belgenin geri kalanı bu
  arayüzü anlatır.
- **`.wlf` uygulaması:** başlık + meta bloğu (`role`, `handles`, …) + `wolf_main(const
  wolf_api_t *api)`. Arayüzü yalnızca libWolf'tur (`include/wolf.h`); `WLF_BASE`
  (0x03000000) bölgesine yüklenir. Ayrıntı: [sdk/README.md](../sdk/README.md).

**Önemli mimari gerçekler:**

- Giriş noktası `app_entry`'dir; `main()` **yazılmaz**.
- Uygulama, masaüstü compositor iş parçacığında çalışır. Bu yüzden `app_entry`
  içinde **sonsuz döngü, `sleep`, bloklayıcı ağ beklemesi yapılmamalıdır**; UI
  donar. Periyodik iş için `lv_timer_create`, uzun ağ işleri için `asi->net->submit`
  (arka plan fiberi) kullanılır.
- Masaüstü, uygulamayı çağırmadan önce otomatik olarak bir **çökme kurtarma
  noktası** (setjmp) kurar ve 5000 ms zaman aşımı verir. Uygulamanız page fault /
  GPF / geçersiz opcode alırsa ya da yükleme sırasında takılırsa **sistem panik
  yapmaz**; masaüstü "Uygulama çöktü" diyaloğu gösterip pencereyi kapatır.
  Yani ekstra bir şey yapmanıza gerek yok.

---

## 2. İlk uygulama — 5 dakikada

`desktop/apps/selam/app.json`:

```json
{ "name": "selam", "title": "Selam", "category": "Araçlar", "icon": "icon.svg", "w": 420, "h": 260 }
```

`desktop/apps/selam/main.c`:

```c
#include "app.h"           /* lvgl.h + asi.h + arctian_app_t + arctian_open_target */

void app_entry(lv_obj_t *win, asi_t *asi) {
    (void)asi;                                   /* kullanılmıyorsa sustur */

    lv_obj_t *l = lv_label_create(win);
    lv_label_set_text(l, "Merhaba Arctian!");
    lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
    lv_obj_center(l);
}
```

`desktop/apps/selam/icon.svg`:

```svg
<svg viewBox="0 0 40 40">
  <rect x="0" y="0" width="40" height="40" rx="10" fill="#1F2937" stroke="#D8A15A" stroke-width="1.5"/>
  <circle cx="20" cy="20" r="9" fill="none" stroke="#F5F5F7" stroke-width="2.5"/>
</svg>
```

Derle:

```
make -j4            # masaüstüne gömülür, Başlat menüsünde ve masaüstünde görünür
```

Bu kadar. `tools/gen_apps.py` her `make`'te `desktop/apps/*` klasörlerini tarar
ve kayıt tablosunu üretir.

---

## 3. Uygulama iskeleti

### 3.1 Giriş noktası

```c
void app_entry(lv_obj_t *win, asi_t *asi);
```

- `win`: size verilen **pencere içerik alanıdır**. Kendi başlığı / kapat /
  küçült / büyüt düğmeleri vardır; tekrar pencere oluşturmayın.
- `asi`: sistem servisleri köprüsü (`asi_t`). Her alt servis **NULL olabilir**;
  kullanmadan önce kontrol edin.
- Pencere gerçek boyutu: `lv_obj_get_width(win)` / `lv_obj_get_height(win)`.

### 3.2 `app.json` alanları

| Alan | Zorunlu | Açıklama |
|------|:---:|----------|
| `name` | ✅ | Klasör adıyla aynı; `a-z 0-9 _`, 1–26 karakter. Giriş sembolü `app_<name>_entry` olur. |
| `title` | ✅ | Başlıkta / Başlat menüsünde görünen ad (Türkçe olabilir). |
| `category` | ➖ | Serbest metin; Başlat menüsünde gruplama için kullanılır. |
| `icon` | ➖ | İkon dosyası, genelde `icon.svg` (`viewBox="0 0 40 40"`). `icon.png` de olur. |
| `version` | ➖ | DuckStore'da sürüm (yerel uygulamada gösterilir). |
| `publisher` | ➖ | Yayıncı. |
| `description` | ➖ | Kısa açıklama. |
| `w` / `h` | ➖ | Tercih edilen pencere piksel boyutu. Verilmezse 560×380. |
| `permissions` | ➖ | `"net,fs,dev"` veya liste. Bkz. §23. Verilmezse **tam yetki** (`CAP_ALL`). |
| `handles` | ➖ | Bu uygulamanın açabildiği uzantılar: `"ue,py"` veya `["ue","py"]` (bkz. §22). |
| `flags` | ➖ | `"no_desktop"` (masaüstünde simge gizle) ve/veya `"engine"` (arka plan motoru, açılışta terminal açar). |

Örnek:

```json
{
  "name": "notdefteri",
  "title": "Not Defteri",
  "category": "Araçlar",
  "version": "1.0",
  "publisher": "Arctian",
  "description": "Basit metin editörü.",
  "icon": "icon.svg",
  "w": 720,
  "h": 520,
  "permissions": ["fs"]
}
```

---

## 4. Derleme ve paketleme

### 4.1 Gömülü uygulama

Klasörü `desktop/apps/<ad>/` içine koyup:

```
make -j4
```

Makefile, `main.c` varsa C; `main.zig` varsa Zig; `main.rs` varsa Rust olarak
otomatik derler (aşağıya bakın). C için kullanılan bayraklar:

```
-std=gnu11 -m64 -ffreestanding -nostdlib -nostartfiles -nodefaultlibs \
-Wall -Wextra -O2 -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
-mno-red-zone -fno-stack-protector -DNDEBUG \
-Dapp_entry=app_<ad>_entry
```

### 4.2 Dağıtılabilir uygulama (`.wlf`)

0.7'de gömülü çekirdek uygulamalar dışında dağıtılan her şey `.wlf`'tir; eski `.dpk`
biçimi kaldırıldı. Bir `.wlf`'i host'ta (Linux / macOS / WSL) SDK ile üretirsin:

```sh
cd sdk/examples/uidemo
sh ../../build.sh            # -> uidemo.wlf
python3 ../../wlfpack.py --inspect uidemo.wlf
```

İşletim sistemi içinde (derleyici gerekmez, `wf` bytecode üretir):

```
wf new sayac
wf build sayac ui            # roller: ui | cli | runtime
wf run sayac
wf install sayac.wlf
```

Paketin davranışı başlık + meta bloğu ile belirlenir:

| Anahtar | Anlamı |
|---|---|
| `role` | `ui` (pencere), `cli` (terminalde koşar), `runtime` (yalnızca dosya verilerek koşar) |
| `handles` | bu çalıştırıcının işlediği uzantılar, örn. `py,pyw` |
| `entry` | kullanıcıya gösterilen giriş adı, örn. `python` |
| `terminal` | `1` ise çalışırken terminal penceresi görünür |

`.wlf` kurulumu imza ister: imzasız paket reddedilir. Geliştirme anahtarı
`tools/keys/dev_rsa2048.txt`'tur ve `tools/pkg_sign.py` imzayı ekler.

Biçimin tamamı: `include/arctian/wlf.h` ve [mimari.md](mimari.md#5-uygulama-modeli-wlf).

### 4.3 Zig ve Rust

Gömülü uygulama olarak derlemek için `desktop/apps/<ad>/` içine `main.zig` veya
`main.rs` koy; Makefile uygun kuralı uygular ve `app_entry` sembolü otomatik olarak
`app_<ad>_entry`'ye yeniden adlandırılır. Zig için `zig` , Rust için `rustc` gerekir
(`tools/rust_compile_stub.sh` eksik `core` sembolleri için küçük stub'lar ekler).
Rust `#![no_std]` ve `#[panic_handler]` gerektirir.

```zig
export fn app_entry(win: ?*anyopaque, asi: ?*anyopaque) void { }
```

```rust
#![no_std]
#[no_mangle]
pub extern "C" fn app_entry(win: *mut core::ffi::c_void, asi: *mut core::ffi::c_void) { }
```

---

## 5. Arayüz: LVGL v9.6 hızlı referans

Tam LVGL belgesi: `third_party/lvgl/docs`. Burada en çok kullanılan kısım var.

### 5.1 Widget oluşturma (v9 isimleri)

```c
lv_obj_t *c   = lv_obj_create(win);          /* kapsayıcı/panel  */
lv_obj_t *l   = lv_label_create(win);        /* etiket           */
lv_obj_t *b   = lv_button_create(win);       /* buton  (v8: lv_btn_create DEĞİL) */
lv_obj_t *sw  = lv_switch_create(win);
lv_obj_t *sl  = lv_slider_create(win);
lv_obj_t *bar = lv_bar_create(win);
lv_obj_t *cb  = lv_checkbox_create(win);
lv_obj_t *dd  = lv_dropdown_create(win);
lv_obj_t *ta  = lv_textarea_create(win);
lv_obj_t *arc = lv_arc_create(win);
lv_obj_t *sp  = lv_spinner_create(win);
lv_obj_t *lst = lv_list_create(win);
lv_obj_t *li  = lv_list_add_button(lst, NULL, "Öğe");   /* v9 */
lv_obj_t *tbl = lv_table_create(win);
lv_obj_t *mb  = lv_msgbox_create(win);       /* v9: parent ister */
```

### 5.2 Metin ve ölçek

```c
lv_label_set_text(l, "Merhaba");
lv_label_set_text_fmt(l, "Değer: %d / %s", x, s);
lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);   /* DOT / SCROLL / CLIP */
lv_obj_set_width(l, LV_PCT(100));
lv_obj_set_size(o, 200, 60);
lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
```

### 5.3 Stil (selector her zaman `0`)

```c
lv_obj_set_style_bg_color(o, lv_color_hex(0x1E2027), 0);
lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);          /* LV_OPA_50, LV_OPA_TRANSP */
lv_obj_set_style_text_color(o, lv_color_hex(0xF5F5F7), 0);
lv_obj_set_style_radius(o, 10, 0);
lv_obj_set_style_border_width(o, 1, 0);
lv_obj_set_style_border_color(o, lv_color_hex(0x2C2F38), 0);
lv_obj_set_style_pad_all(o, 12, 0);
lv_obj_set_style_pad_row(o, 8, 0);
lv_obj_set_style_shadow_width(o, 16, 0);
lv_obj_remove_style_all(o);                           /* tamamen çıplak nesne */
/* bar/slider parçaları: */
lv_obj_set_style_bg_color(bar, lv_color_hex(0xD8A15A), LV_PART_INDICATOR);
```

### 5.4 Hizalama, yerleşim

```c
lv_obj_align(o, LV_ALIGN_TOP_MID, 0, 16);
lv_obj_center(o);
lv_obj_align_to(o, base, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
lv_obj_set_pos(o, 20, 20);

/* Flex */
lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);       /* ROW / COLUMN / WRAP */
lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_set_flex_grow(child, 1);                        /* kalan alanı doldur */

/* Grid */
static const int32_t cols[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
static const int32_t rows[] = { LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
lv_obj_set_grid_dsc_array(box, cols, rows);
lv_obj_set_grid_cell(child, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
```

### 5.5 Olaylar

```c
static void on_click(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    (void)btn; (void)idx;
}
lv_obj_add_event_cb(b, on_click, LV_EVENT_CLICKED, (void *)(intptr_t)42);

/* Sık olaylar: LV_EVENT_CLICKED, LV_EVENT_VALUE_CHANGED, LV_EVENT_PRESSED,
   LV_EVENT_RELEASED, LV_EVENT_READY, LV_EVENT_CANCEL, LV_EVENT_DELETE */
```

### 5.6 Zamanlayıcı (bloklamadan periyodik iş)

```c
static void tick(lv_timer_t *t) { (void)t; /* güncelle */ }
lv_timer_t *tm = lv_timer_create(tick, 500, NULL);  /* 500 ms */
/* pencere kapanırken: lv_timer_delete(tm); */
```

`lv_tick_get()` monoton milisaniye sayacı verir (kronometre için idealdir).

### 5.7 Renk / font

```c
lv_color_t c = lv_color_hex(0x3B82F6);
lv_color_t d = lv_color_make(0x3B, 0x82, 0xF6);
/* Font: ekran zaten ADL fontunu kullanır; özel font gerekmiyorsa dokunmayın.
   Gerekirse: lv_obj_set_style_text_font(o, adl_font(), 0); */
```

---

## 6. Tema (ADL-Wolf) ve font

`#include "adl_wolf.h"` (`-Idesktop/theme` zaten açık).

| Token | Değer | Kullanım |
|-------|-------|----------|
| `ADL_BG` | `0x0E0E12` | Arka plan |
| `ADL_SURFACE` | `0x16171C` | Kart/yüzey |
| `ADL_SURFACE_2` | `0x1E2027` | İkincil yüzey |
| `ADL_BORDER` | `0x2C2F38` | Çizgi/kenarlık |
| `ADL_TEXT` | `0xF5F5F7` | Ana metin |
| `ADL_TEXT_DIM` | `0x9AA0A6` | Soluk metin |
| `ADL_ACCENT` | `adl_accent()` | Vurgu (çalışma anında değişir, varsayılan mavi `0x3B82F6`) |
| `ADL_ACCENT_2` | `adl_accent_2()` | İkincil vurgu (mor) |
| `ADL_DANGER` | `0xFF5F57` | Kırmızı |
| `ADL_WARN` | `0xFEBC2E` | Sarı |
| `ADL_OK` | `0x28C840` | Yeşil |

```c
uint32_t    adl_accent(void);       /* aktif vurgu rengi */
uint32_t    adl_accent_2(void);
void        adl_set_accent(uint32_t accent, uint32_t accent2);
lv_font_t  *adl_font(void);         /* Türkçe destekli gömülü Inter TTF */
```

`ADL_ACCENT` bir makro olduğundan **her kullanımda** güncel rengi verir; kullanıcı
Ayarlar'dan vurgu rengini değiştirince otomatik uyar.

---

## 7. ASI genel bakış

`asi` parametresi, `include/arctian/asi.h` içindeki `asi_t` yapısıdır:

```c
typedef struct {
    uint32_t        version;   /* ASI_VERSION (=2) */
    asi_console_t  *console;
    asi_mem_t      *mem;
    asi_storage_t  *storage;
    asi_time_t     *time;
    asi_gfx_t      *gfx;
    asi_input_t    *input;
    asi_system_t   *system;
    asi_fs_t       *fs;
    asi_net_t      *net;
    asi_proc_t     *proc;
    asi_lab_t      *lab;
    asi_sandbox_t  *sandbox;
    asi_audio_t    *audio;
    asi_crypt_t    *crypt;
    asi_firewall_t *firewall;
    asi_power_t    *power;
} asi_t;

extern asi_t *g_asi;   /* global erişim de mümkündür */
```

**Kural:** her alt servis `NULL` olabilir. Kullanmadan önce mutlaka kontrol edin:

```c
if (asi && asi->net && asi->net->http_get) { /* ... */ }
```

---

## 8. console, mem, storage, time

### 8.1 `asi->console` — metin konsolu / log

```c
void (*clear)(void);
void (*putc)(char c);
void (*puts)(const char *s);
void (*printf)(const char *fmt, ...);
```

```c
asi->console->printf("deger = %d, ad = %s\n", 42, "deneme");
```

> `printf` biçim dizgisi sınırlıdır (libc'deki kadar zengin değil): `%d %u %x %s %c %%`
> gibi temel belirteçler. Kayan nokta (`%f`) kullanmayın.

### 8.2 `asi->mem` — heap bellek

```c
void *(*alloc)(size_t size);              /* NULL = başarısız */
void  (*free)(void *ptr);
void  (*memset)(void *dst, int c, size_t n);
void  (*memcpy)(void *dst, const void *src, size_t n);
void  (*info)(asi_meminfo_t *out);
```

`asi_meminfo_t`: `total, used, free, heap_total, heap_used`.

```c
char *buf = asi->mem->alloc(256);
if (buf) {
    asi->mem->memset(buf, 0, 256);
    /* ... kullan ... */
    asi->mem->free(buf);
}
```

> **Not:** `kmalloc`/`kfree` serbest-listeli bir ayırıcıdır; `free` edilen bloklar
> birleştirilip yeniden kullanılır. Yine de her `alloc` için `free` çağırmak
> alışkanlık olsun.

### 8.3 `asi->storage` — ham disk sektörleri

```c
int (*read_sectors)(uint32_t lba, uint32_t count, void *buf);
int (*write_sectors)(uint32_t lba, uint32_t count, const void *buf);
int (*present)(void);
```

> Çok düşük seviyeli. Normalde dosyalar için `asi->fs` kullanın; bu API yalnızca
> ham sektör erişimi gerekirse.

### 8.4 `asi->time` — zaman

```c
uint64_t (*ticks_ms)(void);                 /* açılıştan beri ms */
void     (*sleep_ms)(uint32_t ms);          /* UI'yı BLOKLAR, dikkatli kullanın */
int      (*now)(asi_datetime_t *out);       /* RTC; 0 = başarılı */
```

```c
typedef struct {
    int year, month, day, hour, minute, second, weekday;  /* weekday: 0=Pazar..6=Cumartesi */
} asi_datetime_t;
```

```c
asi_datetime_t dt;
if (asi->time->now(&dt) == 0)
    asi->console->printf("Saat: %02d:%02d\n", dt.hour, dt.minute);
```

> `sleep_ms` compositor iş parçacığını bloklar. Periyodik iş için `ticks_ms` farkı
> + `lv_timer` tercih edilir.

---

## 9. gfx — grafik

```c
int        (*width)(void);
int        (*height)(void);
uint32_t  *(*framebuffer)(void);            /* 32-bit ARGB/ova formatlı */
uint32_t   (*rgb)(uint8_t r, uint8_t g, uint8_t b);   /* renk paketle */
void       (*present)(void);                /* değişiklikleri ekrana yansıt */
int        (*set_mode)(int width, int height);        /* yeni mod; 0 = başarılı */
```

`set_mode` Bochs VBE üzerinden çalışma-anında görüntü modunu değiştirir. Donanım
desteklemiyorsa mod değişmez ve 0 dışı döner; arayüz gerçekleşen `width`/`height`
değerini yansıtmalıdır. Yeni mod kurulduğunda LVGL display'i yeniden boyutlandırın
(`lv_port_set_resolution`) ve masaüstü kabuğunu yeniden kurun.

`gfx` **düşük seviyeli** framebuffer'dır; günlük arayüz için LVGL kullanın.
Doğrudan çizmeyi yalnızca özel efektlerde tercih edin.

```c
int w = asi->gfx->width(), h = asi->gfx->height();
uint32_t *fb = asi->gfx->framebuffer();
uint32_t col = asi->gfx->rgb(0xD8, 0xA1, 0x5A);
if (fb) for (int x = 0; x < w; x++) fb[x] = col;   /* üst satırı doldur */
asi->gfx->present();
```

> Not: LVGL kendi çizimini yönetir; elle `present()` çağırmak genelde gerekmez.

```c
/* Çalışma-anında çözünürlük değiştir (örn. 1920×1080 -> 2560×1440) */
if (asi->gfx->set_mode(2560, 1440) == 0) {
    lv_port_set_resolution(2560, 1440);
    /* masaüstü kabuğunu yeniden kur ... */
}
```

---

## 10. input — fare ve klavye

```c
int (*mouse_x)(void);
int (*mouse_y)(void);
int (*mouse_buttons)(void);     /* bit0 = sol, bit1 = sağ */
int (*mouse_scroll)(void);
int (*key_avail)(void);         /* 1 = kuyrukta tuş var */
int (*key_pop)(void);           /* kuyruktan sonraki tuş kodu/karakter */
```

> Çoğu uygulama LVGL olaylarını (`LV_EVENT_CLICKED`, `LV_EVENT_KEY`, textarea)
> kullanır; ham `input` yalnızca LVGL dışı özel kullanım içindir.

---

## 11. system — güç, çökme koruma, writeback

```c
void (*shutdown)(void);                       /* gerçek ACPI S5 kapanma */
void (*reboot)(void);
void (*writeback)(void);                      /* disk önbelleğini diske indir */
void (*recover_begin)(arctian_recover_t *ctx, uint32_t timeout_ms);
void (*recover_end)(void);
```

- `shutdown` / `reboot`: güç diyaloğu düğmeleriyle aynı işi yapar.
- `writeback`: masaüstü döngüsünden periyodik çağrılır; uygulamanın çağırması
  genelde gerekmez.
- **Çökme koruma**: Masaüstü, uygulamanızı çağırmadan önce otomatik olarak
  `recover_begin` yapar (5000 ms). Elle kullanmak isterseniz (örneğin kendi
  uzun işiniz için daha uzun süre) `recover.h`:

```c
arctian_recover_t ctx;
int rc = arctian_setjmp(&ctx);
if (rc) {
    /* istisna (rc=1) veya zaman aşımı (rc=2); ctx.reason okunabilir */
}
/* ... korumalı iş ... */
arctian_setjmp(&ctx);   /* not: imza arctian_setjmp(ctx) -> 0 */
```

`arctian_recover_t` alanları: yazmaç yedeği + `active`, `deadline_ms`, `code`,
`reason[160]`. `lib/recover.asm` ile birebir hizalıdır.

---

## 12. fs — dosya sistemi (AFS + USB)

`asi->fs`, **iki erişim yolu** sunar:

1. **Klasör/yol tabanlı API** (önerilen): `"C:/belgeler/not.txt"` gibi.
2. **Eski handle API'si**: `root()/create()/find()`.

Disk birimleri:

- `C:` — **AFS** (kalıcı yerel disk; yazılabilir; journaling + CRC32 + izinler).
  Açılışta otomatik oluşan klasörler: `apps`, `indirilenler`, `belgeler`,
  `masaustu`, `logs`; ayrıca `etc`, `Users/<ad>`, `cop` (çöp kovası).
- `D:` — **FAT32 (USB bellek)**, **salt-okunur**.

### 12.1 Yapı

```c
typedef struct {
    uint32_t uid, gid, mode, is_dir, size;
} asi_stat_t;

/* Handle API (tek seviyeli AFS) */
void    *(*root)(void);
void    *(*create)(void *dir, const char *name, int is_dir);
void    *(*find)(void *dir, const char *name);
int      (*write)(void *file, const void *data, uint32_t size);
uint32_t (*read)(void *file, void *buf, uint32_t size);
int         (*count)(void);
const char *(*name)(int idx);
uint32_t    (*size)(int idx);
int         (*remove)(const char *name);

/* Yol API (klasör destekli) */
int      (*mkdir)(const char *path);                          /* 0 = hazır */
void    *(*create_in)(const char *path, const char *name, int is_dir);
void    *(*find_in)(const char *path, const char *name);
uint32_t (*read_path)(const char *path, void *buf, uint32_t size);
int      (*count_dir)(const char *path);
int      (*list_dir)(const char *path, int idx, char *out, int cap, int *is_dir);

/* Metadata / izinler */
int  (*stat)(const char *path, asi_stat_t *out);
int  (*stat_handle)(void *file, asi_stat_t *out);
int  (*chmod)(const char *path, uint32_t mode);
int  (*chown)(const char *path, int uid, int gid);
void (*set_subject)(int uid, int gid);

/* Bütünlük */
int  (*fsck)(int fix, int *problems, int *fixed);
```

### 12.2 Dosya yazma / okuma

```c
/* Yaz (yoksa oluşturur) */
if (asi->fs && asi->fs->create_in) {
    void *h = asi->fs->create_in("C:/belgeler", "not.txt", 0);
    if (h) asi->fs->write(h, "Merhaba", 7);
}

/* Basitçe oku (yol API) */
char buf[512];
uint32_t n = asi->fs->read_path("C:/belgeler/not.txt", buf, sizeof(buf));
```

Var olan bir dosyaya ek yazmak için önce `find_in`/`find` ile handle alın, sonra
`write` çağırın (handle içindeki konum sonda kalır).

### 12.3 Klasörü listeleme

```c
int n = asi->fs->count_dir("C:/indirilenler");
for (int i = 0; i < n; i++) {
    char nm[128]; int isdir = 0;
    if (asi->fs->list_dir("C:/indirilenler", i, nm, sizeof(nm), &isdir) != 0) continue;
    asi->console->printf("%s%s\n", nm, isdir ? "/" : "");
}
```

### 12.4 Metadata ve izinler

```c
asi_stat_t st;
if (asi->fs->stat("C:/belgeler/not.txt", &st) == 0) {
    /* st.size, st.is_dir, st.mode, st.uid, st.gid */
}
asi->fs->chmod("C:/belgeler/not.txt", 0640);   /* sahibi/root */
asi->fs->chown("C:/belgeler/not.txt", 1000, 1000);  /* yalnız root */
```

AFS erişim denetimi POSIX benzeridir: root bypass, sahip/grup/diğer bitleri.
Etkin özne (`set_subject`) uygulamalara genelde kapalıdır; süreç uid/gid'i
çekirdek tarafından uygulanır.

### 12.5 Bütünlük (fsck)

```c
int problems = 0, fixed = 0;
asi->fs->fsck(1 /* onar */, &problems, &fixed);
```

### 12.6 Silme / çöp kovası deseni

```c
asi->fs->remove("C:/indirilenler/eski.txt");
```

Çöp kovası uygulaması (`desktop/apps/trash`) deseni: dosyayı `C:/cop` altına
kopyala, sonra kaynağı sil; geri yükleme `C:/belgeler`'e taşımaktır.

---

## 13. net — ağ

`asi->net`, ham paketten TLS'li HTTP indirmeye ve arka plan işlerine kadar geniş
bir yüzey sunar. **Tüm ağ çağrılarından önce `link_type()` ile bağlantı kontrol
edilmelidir.**

```c
#define ASI_LINK_NONE     0
#define ASI_LINK_ETHERNET 1
#define ASI_LINK_WIFI     2

#define ASI_WIFI_OFF        0
#define ASI_WIFI_CONNECTING 1
#define ASI_WIFI_CONNECTED  2
#define ASI_WIFI_FAILED     3
```

### 13.1 Ham paket

```c
int  (*present)(void);
int  (*send)(const void *data, uint32_t len);
int  (*recv)(void *buf, uint32_t maxlen);
int  (*has)(void);
void (*mac)(uint8_t out[6]);
```

### 13.2 Bağlantı (netlink)

```c
int  (*link_type)(void);        /* ASI_LINK_* */
int  (*link_up)(void);
void (*ip)(uint8_t out[4]);
void (*netmask)(uint8_t out[4]);
void (*gateway)(uint8_t out[4]);
void (*dns)(uint8_t out[4]);
int  (*configured)(void);
int  (*autoconfig)(void);       /* DHCP benzeri IP atama (ethernet) */
```

```c
if (asi->net->link_type() == ASI_LINK_NONE)
    return;                          /* önce bağlan */
uint8_t ip[4]; asi->net->ip(ip);
asi->console->printf("IP: %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
```

### 13.3 Wi-Fi

```c
typedef struct {
    char ssid[33];
    int  rssi;    /* -30 güçlü .. -90 zayıf */
    int  secure;  /* 1 = şifre gerekli */
} asi_wifi_net_t;

int  (*wifi_hw)(void);                              /* 1 = fiziksel 802.11 kartı */
int  (*wifi_scan)(asi_wifi_net_t *out, int max);    /* gerçek beacon taraması */
int  (*wifi_connect)(const char *ssid, const char *pass);
int  (*wifi_disconnect)(void);
int  (*wifi_state)(void);                           /* ASI_WIFI_* */
const char *(*wifi_ssid)(void);                     /* bağlı SSID veya "" */
```

> **Not:** Wi-Fi katmanı sahte ağ üretmez. Kayıtlı gerçek 802.11 sürücüsü yoksa
> `wifi_scan` 0 döner ve `wifi_connect` hata verir. Ethernet tarafında gerçek
> sürücüler (`e1000`, `rtl8139`, `pcnet`) vardır.

### 13.4 DNS + TCP (istemci)

```c
void (*poll)(void);                                       /* RX işlemesini sürer */
int  (*dns_resolve)(const char *host, uint8_t out_ip[4]); /* 0 = başarılı */
int  (*tcp_connect)(const uint8_t ip[4], uint16_t port);  /* bağlantı id, <0 hata */
int  (*tcp_send)(int conn, const void *data, uint32_t len);
int  (*tcp_recv)(int conn, void *buf, uint32_t maxlen);   /* 0=veri yok, <0=kapandı */
int  (*tcp_close)(int conn);
int  (*tcp_state)(int conn);
```

Ham TCP istemcisi örneği:

```c
uint8_t ip[4];
if (asi->net->dns_resolve("example.com", ip) != 0) { /* hata */ }
int c = asi->net->tcp_connect(ip, 80);
if (c >= 0) {
    const char *req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    asi->net->tcp_send(c, req, strlen(req));
    char buf[1024]; int n;
    while ((n = asi->net->tcp_recv(c, buf, sizeof(buf))) > 0) { /* buf[0..n) */ }
    asi->net->tcp_close(c);
}
```

### 13.5 HTTP / HTTPS

```c
const char *(*http_get)(const char *url, uint32_t *len_out);   /* gövde veya NULL */
const char *(*https_get)(const char *url, uint32_t *len_out);  /* TLS 1.3 dahil */
const char *(*last_url)(void);                                 /* yönlendirme sonrası URL */
```

- Her iki fonksiyon da `http://` ve `https://` şemalarını işler, 3xx
  yönlendirmelerini (en fazla 6) takip eder ve yalnızca **gövdeyi** döndürür.
- Dönen işaretçi **bir sonraki çağrıya kadar** geçerlidir; hemen kopyalayın.
- Cevap tamponu ~192 KB; daha büyük gövde için `download` kullanın.
- URL'de doğrudan IP de kullanılabilir (`http://45.143.99.75:8090/...`) → DNS yapılmaz.

```c
if (asi->net->link_type() == ASI_LINK_NONE) { /* önce bağlan */ }
uint32_t n = 0;
const char *body = asi->net->http_get("http://example.com/", &n);
if (body) { /* body, n bayt */ }
```

### 13.6 Büyük dosya indirme (akışlı)

```c
/* Gövde doğrudan dst'ye yazılır; cap = başlıklar + gövde için ayrılan alan. */
int (*download)(const char *url, void *dst, uint32_t cap, uint32_t *len_out);
```

```c
#define MAXSZ (4u * 1024u * 1024u)
uint8_t *buf = asi->mem->alloc(MAXSZ);
if (buf) {
    uint32_t len = 0;
    if (asi->net->download("http://sunucu/dosya.bin", buf, MAXSZ, &len) == 0) {
        /* buf[0..len) */
    }
}
```

### 13.7 Arka plan (fiber) ağ işleri — UI'yı bloklamaz

```c
typedef void (*asi_net_done_t)(int status, void *user);

int  (*submit)(void (*fn)(void *), asi_net_done_t done, void *user);  /* 0=kabul, -1=meşgul */
int  (*busy)(void);
void (*service)(void);      /* her karede çağrılır (masaüstü zaten yapar) */
```

- `fn` kendi yığınında çalışır; içindeki ağ çağrıları `ipv4_poll` içinde gönüllü
  olarak `yield` eder. `done`, UI karesi içinde çağrılır.
- **Aynı anda tek iş** çalışır; `busy()` ile kontrol edin.

```c
static void work(void *u)  { g_body = asi->net->http_get((const char *)u, &g_len); }
static void done(int st, void *u) { (void)st; (void)u; /* UI güncelle */ }

if (!asi->net->busy())
    asi->net->submit(work, done, "http://sunucu/veri");
```

### 13.8 Çerez

```c
/* JS document.cookie tarafından yazılan çerezi çekirdek deposuna ekler */
void (*cookie_set)(const char *host, const char *name, const char *value);
```

Tarayıcı motoru kullanır; normal uygulamalar için gerekmez.

---

## 14. proc, lab, sandbox

### 14.1 `asi->proc` — süreçler

```c
int  (*spawn)(const char *name, void (*entry)(void *), void *arg);
int  (*fork)(const char *name, void (*entry)(void *), void *arg);
int  (*exec)(void (*entry)(void *), void *arg);
void (*exit)(int code);
int  (*wait)(int pid, int *code);
int  (*kill)(int pid);
int  (*getpid)(void);
int  (*getppid)(void);
int  (*count)(void);
int  (*info)(int i, int *pid, char *name, int cap, int *state, int *ppid, int *code);
int  (*app_open)(const char *title);                 /* GUI penceresi → PID */
void (*app_close)(int pid, int code);
int  (*reap)(int *code_out);
int  (*app_open_caps)(const char *title, uint32_t caps);
uint32_t (*caps_of)(int pid);
```

```c
asi->console->printf("pid=%d ebeveyn=%d\n", asi->proc->getpid(), asi->proc->getppid());
```

### 14.2 `asi->lab` — çekirdek selftest / sayaçlar

```c
int (*selftest)(void);
int (*thread_count)(void);
int (*shm_count)(void);
int (*pipe_test)(void);
```

### 14.3 `asi->sandbox` — yetenek bağlamı

```c
void     (*enter)(int pid);       /* süreç PID'sinin yetenek/uid bağlamını uygula */
void     (*leave)(void);
uint32_t (*current)(void);        /* aktif yetenek maskesi */
int      (*check)(uint32_t cap);  /* 1 = izinli */
int      (*last_error)(void);
int      (*selftest)(void);
void     (*set_uid)(int uid, int gid);
uint32_t (*uid)(void);
uint32_t (*gid)(void);
```

Bkz. §23.

---

## 15. audio — medya/ses

```c
int  (*present)(void);                            /* 1 = ses donanımı var */
int  (*play_wav)(const void *data, uint32_t len); /* WAV baytlarını çal */
void (*stop)(void);
int  (*is_playing)(void);
int  (*set_volume)(int percent);                  /* 0..100 */
int  (*volume)(void);
void (*service)(void);                            /* masaüstü döngüsünden sürülür */
const char *(*last_error)(void);
uint32_t (*sample_rate)(void);                    /* aktif örnekleme hızı */
```

Desteklenen WAV: PCM format 1 ve 0xFFFE, 8/16-bit, mono/stereo; çalma anında
16-bit stereo'ya dönüştürülür.

Tam örnek (Medya Oynatıcı deseninden):

```c
#include "app.h"
#include "adl_wolf.h"

static asi_t   *A;
static uint8_t *g_buf;    /* çalan tampon; bırakılana kadar geçerli olmalı */
static uint32_t g_len;

static void cal_cb(lv_event_t *e) {
    (void)e;
    if (!A->audio) return;

    if (g_buf) { A->audio->stop(); A->mem->free(g_buf); g_buf = 0; }

    asi_stat_t st;
    if (A->fs->stat("C:/belgeler/ses.wav", &st) != 0 || st.is_dir || st.size == 0) return;

    uint8_t *buf = A->mem->alloc(st.size);
    if (!buf) return;

    uint32_t rd = A->fs->read_path("C:/belgeler/ses.wav", buf, st.size);
    if (rd == 0) { A->mem->free(buf); return; }

    if (A->audio->play_wav(buf, rd) != 0) { A->mem->free(buf); return; }

    g_buf = buf;          /* çalarken serbest bırakma! */
    g_len = rd;
}

static void win_del(lv_event_t *e) {
    (void)e;
    if (A->audio) A->audio->stop();
    if (g_buf) { A->mem->free(g_buf); g_buf = 0; }
}
```

> **Kritik:** `play_wav` tamponu **referans alır** (kopyalamaz). Çalma bitene kadar
> `g_buf`'u serbest bırakmayın; pencere kapanışında (`LV_EVENT_DELETE`) `stop()`
> çağırıp `free` edin.

Çalma durumu ve ilerleme için periyodik `lv_timer`:

```c
static void tick(lv_timer_t *t) {
    (void)t;
    if (A->audio->is_playing && !A->audio->is_playing()) {
        /* çalma bitti: tamponu serbest bırak */
        if (g_buf) { A->mem->free(g_buf); g_buf = 0; }
    }
}
```

---

## 16. crypt, firewall, power

### 16.1 `asi->crypt` — disk şifreleme (AES-XTS)

```c
int  (*present)(void);                 /* 1 = şifreleme başlığı var */
int  (*unlocked)(void);
int  (*unlock)(const char *pass);
int  (*setup)(const char *pass);       /* disk şifrelemeyi başlat */
int  (*disable)(const char *pass);     /* kaldır */
const char *(*status)(void);
```

Genelde açılışta kilit ekranı ve Ayarlar → Sistem kullanır.

### 16.2 `asi->firewall` — güvenlik duvarı

Kural yapısı:

```c
typedef struct {
    char     name[24];
    uint8_t  dir;      /* ASI_FW_DIR_IN / OUT / ANY */
    uint8_t  proto;    /* ASI_FW_PROTO_ANY / TCP(6) / UDP(17) */
    uint8_t  action;   /* ASI_FW_DENY(0) / ASI_FW_ALLOW(1) */
    uint8_t  enabled;
    uint8_t  ip[4];    /* 0.0.0.0 = hepsi */
    uint8_t  mask[4];  /* hepsi sıfır = hepsi */
    uint16_t port_lo;  /* 0 = hepsi */
    uint16_t port_hi;  /* tek port için port_lo'ya eşit */
    uint32_t hits;     /* eşleşme sayacı (çekirdek günceller) */
} asi_fw_rule_t;

int (*enabled)(void);
int (*set_enabled)(int on);
int (*count)(void);
int (*rule)(int idx, asi_fw_rule_t *out);     /* 0 = ok, -1 = yok */
int (*add)(const asi_fw_rule_t *r);           /* >=0 = yeni kuralın sırası */
int (*remove)(int idx);
int (*clear)(void);
int (*save)(void);
int (*load)(void);
int (*selftest)(void);
```

**Varsayılan izin'dir**; yalnızca açıkça tanımlı kurallar uygulanır. Amaç
istenmeyen portları açıkça kapatmaktır.

```c
asi_fw_rule_t r;
memset(&r, 0, sizeof(r));
r.name[0]='w'; r.enabled = 1;
r.dir = ASI_FW_DIR_OUT; r.proto = ASI_FW_PROTO_TCP; r.action = ASI_FW_DENY;
r.port_lo = 9999; r.port_hi = 9999;
if (asi->firewall->add(&r) >= 0) asi->firewall->save();
```

### 16.3 `asi->power` — güç / pil

```c
#define ASI_POWER_AC      0
#define ASI_POWER_BATTERY 1

int  (*source)(void);
int  (*percent)(void);       /* 0..100, -1 bilinmiyor */
int  (*present)(void);       /* 1 = pil takılı */
int  (*charging)(void);
int  (*minutes)(void);       /* kalan süre (dk), -1 bilinmiyor */
int  (*acpi)(void);          /* 1 = ACPI tabloları bulundu */
void (*sleep)(void);         /* uyku (tuş/fare ile uyanır) */
```

```c
asi->console->printf("Kaynak: %s, pil=%d%%\n",
    asi->power->source() == ASI_POWER_BATTERY ? "Pil" : "AC",
    asi->power->percent());
```

---

## 17. DuckStore mağaza API'si (`arctian_store`)

`app.h` içinde global bir köprü: `extern arctian_store_api_t *arctian_store;`.
DuckStore uygulaması ve Ayarlar bunu kullanır; normal uygulamalar da kullanabilir.

```c
typedef struct {
    /* 0.7: bayıtlar bir .wlf olmalıdır; eski .dpk biçimi reddedilir. */
    int         (*install)(const void *wlf, uint32_t size, char *err, int errcap);
    int         (*uninstall)(const char *id);
    int         (*launch)(const char *id);
    int         (*installed)(const char *id);            /* 1/0 */
    const char *(*installed_version)(const char *id);    /* "" yoksa */
    int         (*count_installed)(void);
    const char *(*installed_id)(int i);
    const char *(*installed_title)(int i);
    const char *(*installed_version_at)(int i);
    void        (*refresh)(void);

    /* Masaüstü arka planı (AFS'teki .wp dosyaları) */
    int         (*set_wallpaper)(const char *afs_name);  /* NULL/"" = varsayılan */
    int         (*wallpaper_active)(void);
    const char *(*wallpaper_name)(void);
} arctian_store_api_t;
```

Kurulu uygulamaları listeleme:

```c
if (arctian_store && arctian_store->count_installed) {
    int n = arctian_store->count_installed();
    for (int i = 0; i < n; i++)
        asi->console->printf("%s v%s\n",
            arctian_store->installed_title(i),
            arctian_store->installed_version_at(i));
}
```

Duvar kağıdı değiştirme:

```c
if (arctian_store && arctian_store->set_wallpaper)
    arctian_store->set_wallpaper("manken.wp");    /* AFS'teki .wp dosyası */
```

---

## 18. Ayarlar API'si (`arctian_settings`)

`extern arctian_settings_api_t *arctian_settings;`

```c
typedef struct {
    int         (*accent_count)(void);
    const char *(*accent_name)(int i);
    uint32_t    (*accent_color)(int i);
    uint32_t    (*accent_color_2)(int i);
    int         (*accent_current)(void);
    void        (*accent_set)(int i);

    int  (*get_int)(const char *key, int def);   /* anahtar-değer ayar deposu */
    void (*set_int)(const char *key, int val);

    int         (*resolution_count)(void);       /* desteklenen mod sayısı */
    const char *(*resolution_name)(int i);       /* örn. "2560 x 1440  (QHD / 2K)" */
    int         (*resolution_current)(void);     /* geçerli mod indeksi veya -1 */
    int         (*resolution_set)(int i);        /* seç; kalıcı yazar, 0 = başarılı */
} arctian_settings_api_t;
```

```c
int tema = arctian_settings->get_int("koyu_tema", 1);
arctian_settings->set_int("koyu_tema", 0);
arctian_settings->accent_set(2);                  /* 3. vurgu rengi */

/* Ekran çözünürlüğü: seçim anında uygulanır ve settings.cfg'ye yazılır */
for (int i = 0; i < arctian_settings->resolution_count(); i++)
    asi->console->printf("%d: %s\n", i, arctian_settings->resolution_name(i));
if (arctian_settings->resolution_current() != 3)
    arctian_settings->resolution_set(3);          /* 2560x1440 (2K) */
```

> Modlar 16:9'dur: 1280×720 (varsayılan), 1600×900, 1920×1080, 2560×1440 (2K).
> Seçim `settings.cfg` içindeki `res` anahtarına indeks olarak yazılır ve bir
> sonraki açılışta kabuk kurulmadan önce uygulanır. Geçiş anında açık pencereler
> kapanır ve masaüstü yeniden kurulur.

---

## 19. Kimlik doğrulama (`arctian_auth_*`)

`desktop/auth.c` sağlar. Çoklu hesap; `C:/etc/users` altında saklanır; parola
salt'lı yinelenen SHA-256 ile korunur. Her hesabın uid'si (1000+) ve
`C:/Users/<ad>` ev klasörü vardır.

```c
int         arctian_auth_user_count(void);
const char *arctian_auth_user_name(int i);
int         arctian_auth_user_has_password(int i);
uint32_t    arctian_auth_user_uid(int i);
int         arctian_auth_find(const char *name);          /* indeks veya -1 */
const char *arctian_auth_home(const char *name);          /* "C:/Users/<ad>" */

int         arctian_auth_create_user(const char *name, const char *pw_or_null);
int         arctian_auth_remove_user(const char *name);
int         arctian_auth_verify_user(const char *name, const char *pw);      /* 1 = doğru */
int         arctian_auth_set_password_for(const char *name, const char *pw, const char *pw2);

void        arctian_auth_set_current(const char *name);
const char *arctian_auth_current(void);
uint32_t    arctian_auth_current_uid(void);

/* Tek-kullanıcı yardımcıları (oturum kullanıcısı üzerinde) */
int         arctian_auth_has_user(void);
const char *arctian_auth_username(void);
int         arctian_auth_has_password(void);
int         arctian_auth_set_password(const char *pw, const char *pw2);
int         arctian_auth_verify(const char *pw);
```

```c
const char *u = arctian_auth_current();       /* oturumdaki kullanıcı */
const char *home = arctian_auth_home(u);      /* "C:/Users/<ad>" */
```

---

## 20. Kabuk köprüsü (`arctian_shell_*`)

Masaüstü kabuğuna (Başlat menüsü, pencereler) erişim. `app.h`:

```c
int  arctian_shell_app_count(void);
int  arctian_shell_app_info(int i, char *title, int tcap,
                            char *name, int ncap, char *cat, int ccap);
int  arctian_shell_launch(const char *name_or_title);
int  arctian_shell_close(const char *name_or_title);
int  arctian_shell_window_count(void);
int  arctian_shell_window_info(int i, char *title, int tcap, int *visible);

void arctian_shell_network_panel(void);   /* Wi-Fi/ağ panelini aç */
void arctian_shell_power_dialog(void);    /* Güç diyaloğu */
void arctian_shell_lock_screen(void);     /* Oturumu kilitle / kullanıcı değiştir */
void arctian_shell_editor(const char *path);  /* Dosyayı editörde aç */
```

```c
arctian_shell_launch("Terminal");          /* ada veya başlığa göre başlat */
int n = arctian_shell_window_count();      /* açık pencereler */
```

### 20.1 Masaüstü çekirdeğinin modül yapısı

Masaüstü tek bir çevrim birimi yerine `desktop/` altında birbirinden ayrı
modüllere bölünmüştür. Ortak semboller `desktop/dsk.h`'de toplanır:

| Modül | Sorumluluk |
|---|---|
| `main.c` | Giriş noktası, kilit/çökme kurtarma, ana döngü |
| `dsk_state.c` | Küresel durum (`g_taskbar`, `g_wallpaper`, `g_tb[]`, çözünürlük tercihleri) |
| `dsk_util.c` | `make_icon` / `make_logo`, canvas serbest bırakma, ölçek yardımcıları |
| `taskbar.c` | Cam taskbar, sabitlenmiş ikonlar, arama kutusu, saat, bağlam menüleri |
| `desktop_icons.c` | Masaüstü simgeleri, ızgara yerleşimi, sürükle/bırak, seçim |
| `startmenu.c` | Başlat menüsü (arama, sabitlenmiş karolar, önerilenler) |
| `wallpaper.c` | Arka plan ölçekleme (`scale_hq`) ve fare imleci (`build_cursor`) |
| `settings.c` | `shell_rebuild`, `shell_apply_resolution`, `boot_apply_saved_resolution` |
| `windows.c`, `launch.c`, `power.c`, `clock.c`, `network_panel.c` | Pencere yönetimi ve görev çubuğu panelleri |
| `lock.c`, `auth.c` | Kilit ekranı, hesap oluşturma/giriş |
| `port/lv_port.c` | LVGL ↔ ASI köprüsü (flush, indev, tick) |
| `theme/adl_wolf.c` | ADL-Wolf tema ve font kurulumu |

**Çözünürlük bağımsız ölçek.** Bütün arayüz ölçüleri **2560 x 1440 (2K)**
referans alınarak tanımlanır ve çalışma anında `ui_s()` ile ölçeklenir.
`ui_scale_refresh()` ekran yüksekliğini `UI_REF_H` (1440) oranına göre
`g_ui_s` değerine çevirir; `shell_rebuild()` her ölçüm değişiminden önce bunu
çağırır. Dolayısıyla taskbar, masaüstü simgeleri, başlat menüsü ve paneller her
çözünürlükte aynı **oransal** görünümü korur — taskbar örneğin her çözünürlükte
ekran yüksekliğinin ~%8,3'ü kadardır.

`shell_rebuild()` çözünürlük değişiminde açık pencereleri kapatır, ekran
çocuklarını temizler ve kabuğu yeni boyuta göre yeniden kurar. Fare imleci
`lv_indev_set_cursor()` tarafından **sys katmanına** taşındığı için
`lv_obj_clean(lv_screen_active())` onu silmez; bu yüzden `build_cursor()`
tek seferlik yaratılır ve sonraki yeniden kurulumlarda yeniden oluşturulmaz
(eski imleç aynen kalmaya devam eder).

---

## 21. Güncelleme API'si (`update.h`)

Sunucudan satır tabanlı bir sürüm manifesti çeker, listelenen modül paketlerini
indirir, boyut + CRC32 ile doğrular ve DuckStore kurulum hattından geçirir.

```c
/* Başlatma (masaüstü main.c çağırır) */
void        update_init(asi_t *asi);
void        update_set_base_url(const char *url);
const char *update_base_url(void);

const char *update_current_version(void);   /* derleme sürümü (ARCTIAN_VERSION) */
const char *update_applied_version(void);   /* kalıcı kurulmuş sürüm */
const update_status_t   *update_status(void);
const update_manifest_t *update_manifest(void);
int         update_has_update(void);

/* Bloklayıcı (ağ üzerinde çalışır): 0 = başarılı, <0 hata, >0 yeni sürüm */
int         update_check(void);
int         update_apply(void);

/* Geri çağrılar (masaüstü bağlar) */
void        update_set_installer(int (*fn)(const void *wlf, uint32_t size, char *err, int errcap));
void        update_set_notify(void (*fn)(void));
```

Durum makinesi ve durum yapısı:

```c
enum { UPDATE_IDLE, UPDATE_CHECKING, UPDATE_AVAILABLE, UPDATE_UPTODATE,
       UPDATE_DOWNLOADING, UPDATE_DONE, UPDATE_ERROR };

typedef struct {
    int  state;
    int  progress;         /* 0..100 */
    int  cur, total;
    char version[24];
    char message[96];
} update_status_t;
```

Desen (Ayarlar ekranından): arka planda çalıştır, `lv_timer` ile durumu izle:

```c
static void upd_work(void *u) { if ((intptr_t)u) update_apply(); else update_check(); }
static void upd_done(int st, void *u) { (void)st; (void)u; }

if (!asi->net->busy())
    asi->net->submit(upd_work, upd_done, (void *)(intptr_t)0);

/* periyodik: */
const update_status_t *st = update_status();
lv_bar_set_value(bar, st->progress, LV_ANIM_OFF);
lv_label_set_text(lbl, st->message);
```

Manifest formatı (sunucu tarafı):

```
#arctian-update 1
version=0.7.1
notes=<kısa açıklama>
@file
id=<uygulama kimliği>
version=<modül sürümü>
size=<bayt>
crc=<8 haneli CRC32 (IEEE)>
package=/packages/<id>.wlf
sig=<govdenin RSA-2048 imzası (hex)>
```

Gövde (`sig=` satırı hariç) mağaza anahtarıyla imzalanır; imzasız manifesto kabul
edilmez. Görsel örnek: `store/server/duckstore_server.py`.

---

## 22. Dosya açma (uzantı eşleme)

Bir dosyaya çift tıkladığında veya "Birlikte aç"ı seçtiğinde masaüstü, uzantıyı
`handles` alanında ilan eden uygulamaları toplar:

1. Önce kurulu `.wlf` uygulamaları bakılır — meta bloğundaki `handles=py,pyw`
   uzantıyı içeriyorsa adaydır.
2. Sonra gömülü uygulamaların `app.json` içindeki `handles` alanına bakılır.

Tek aday varsa doğrudan açılır; birden fazlaysa **"Hangisi ile açmak istersin?"**
diyaloğu çıkar. Seçilen uygulamaya açılacak dosyanın yolu `arctian_open_target`
global'ine yazılır.

Gömülü görüntüleyici uygulaması:

`app.json`:

```json
{ "name": "ueviewer", "title": "UE Görüntüleyici",
  "category": "Araclar", "handles": "ue", "icon": "icon.svg" }
```

`main.c`:

```c
#include "app.h"

void app_entry(lv_obj_t *win, asi_t *asi) {
    const char *dosya = (arctian_open_target && arctian_open_target[0])
                        ? arctian_open_target : "(dosya yok)";

    lv_obj_t *t = lv_label_create(win);
    lv_label_set_text_fmt(t, "Açılan dosya: %s", dosya);
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 20);

    /* dosyayı asi->fs->read_path(dosya, ...) ile okuyup içeriği gösterin */
}
```

Bir **çalışma zamanı** (`.py` gibi) olmak için `role=runtime` kullanılır: terminal
açılır, dosya argümanı `wolf_api_t` içindeki `arg()` ile gelir, dosya verilmezse
kullanım bilgisi yazılır. Çalışan örnek: `sdk/examples/pyrt`.

---

## 23. Yetenekler (izinler / sandbox)

`app.json` içindeki `permissions`, çalışma anında süreç yetenek maskesine çevrilir:

| Değer | Makro | Açıklama |
|-------|-------|----------|
| `net` | `CAP_NET` | Ağ erişimi |
| `fs`  | `CAP_FS`  | Dosya sistemi erişimi |
| `dev` | `CAP_DEV` | Cihaz/raw erişim |
| `all` | `CAP_ALL` | Hepsi (varsayılan) |

```c
#define CAP_NET (1u << 0)
#define CAP_FS  (1u << 1)
#define CAP_DEV (1u << 2)
#define CAP_ALL 0xFFFFFFFFu
```

`permissions` **verilmezse `CAP_ALL`** (geriye uyumluluk). Kısıtlanan bir uygulama
yetkisiz işlem denerse `EPERM` (`SANDBOX_EPERM == 1`) alır:

- Ağ çağrıları `CAP_NET` denetimlidir.
- Tüm dosya okuma/yazma `CAP_FS` denetimlidir.

Kendi bağlamınızı sorgulamak için:

```c
if (asi->sandbox && !asi->sandbox->check(CAP_NET))
    asi->console->puts("Bu uygulamanın ağ yetkisi yok\n");
```

DuckStore, kurulum öncesi izin listesini kullanıcıya gösterir.

---

## 24. Tam örnekler

### 24.1 Not defteri (fs + textarea + kaydet)

```json
{ "name": "notdefteri", "title": "Not Defteri", "category": "Araçlar",
  "icon": "icon.svg", "w": 720, "h": 520, "permissions": ["fs"] }
```

```c
#include "app.h"
#include "adl_wolf.h"

static asi_t *A;
static lv_obj_t *ta;

#define DOSYA "C:/belgeler/notlar.txt"
#define MAXN  (64u * 1024u)

static void yukle(void) {
    if (!A->fs || !A->fs->read_path) return;
    char *buf = A->mem->alloc(MAXN);
    if (!buf) return;
    uint32_t n = A->fs->read_path(DOSYA, buf, MAXN - 1);
    buf[n] = 0;
    if (n) lv_textarea_set_text(ta, buf);
    A->mem->free(buf);
}

static void kaydet_cb(lv_event_t *e) {
    (void)e;
    const char *txt = lv_textarea_get_text(ta);
    /* önce var olanı sil, sonra yeniden oluştur (basit yaklaşım) */
    if (A->fs->remove) A->fs->remove(DOSYA);
    void *h = A->fs->create_in("C:/belgeler", "notlar.txt", 0);
    if (h) {
        uint32_t len = 0; while (txt[len]) len++;
        A->fs->write(h, txt, len);
    }
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    A = asi;

    ta = lv_textarea_create(win);
    lv_textarea_set_one_line(ta, false);
    lv_obj_set_size(ta, LV_PCT(94), LV_PCT(78));
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *b = lv_button_create(win);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(b, kaydet_cb, LV_EVENT_CLICKED, NULL);
    lv_label_create(b); lv_label_set_text(lv_obj_get_child(b, 0), "Kaydet");

    yukle();
}
```

### 24.2 HTTP istemcisi (arka planda indirme)

```c
#include "app.h"
#include "adl_wolf.h"

static asi_t *A;
static lv_obj_t *lbl, *bar;

static char   g_body[4096];
static uint32_t g_len;
static int    g_rc;

static void work(void *u) {
    (void)u;
    const char *b = A->net->http_get("http://example.com/", &g_len);
    if (b && g_len < sizeof(g_body)) { memcpy(g_body, b, g_len); g_body[g_len] = 0; g_rc = 0; }
    else g_rc = -1;
}
static void done(int st, void *u) {
    (void)st; (void)u;
    lv_label_set_text(lbl, g_rc == 0 ? g_body : "Bağlantı hatası");
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
}

static void git_cb(lv_event_t *e) {
    (void)e;
    if (!A->net || !A->net->submit) return;
    if (A->net->link_type() == ASI_LINK_NONE) { lv_label_set_text(lbl, "Ağ yok"); return; }
    if (A->net->busy && A->net->busy()) { lv_label_set_text(lbl, "Meşgul..."); return; }
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
    A->net->submit(work, done, NULL);
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    A = asi;

    lbl = lv_label_create(win);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(90));
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 16);

    bar = lv_spinner_create(win);   /* veya lv_bar_create */
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *b = lv_button_create(win);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(b, git_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(b); lv_label_set_text(bl, "Getir");
}
```

### 24.3 Sistem bilgisi paneli (time + mem + power + net)

```c
#include "app.h"
#include "adl_wolf.h"

static asi_t *A;
static lv_obj_t *info;

static void refresh(lv_timer_t *t) {
    (void)t;
    asi_datetime_t dt; char line[256]; int n = 0;
    if (A->time->now(&dt) == 0)
        n += lv_snprintf(line + n, sizeof(line) - n, "Saat: %02d:%02d:%02d\n",
                         dt.hour, dt.minute, dt.second);
    asi_meminfo_t mi;
    if (A->mem->info) { A->mem->info(&mi);
        n += lv_snprintf(line + n, sizeof(line) - n, "Bellek: %llu/%llu KB\n",
                         (unsigned long long)(mi.heap_used / 1024),
                         (unsigned long long)(mi.heap_total / 1024)); }
    if (A->power && A->power->present())
        n += lv_snprintf(line + n, sizeof(line) - n, "Pil: %d%%\n", A->power->percent());
    if (A->net && A->net->link_type) {
        int lt = A->net->link_type();
        const char *s = lt == ASI_LINK_WIFI ? "Wi-Fi" : lt == ASI_LINK_ETHERNET ? "Ethernet" : "Yok";
        n += lv_snprintf(line + n, sizeof(line) - n, "Ağ: %s\n", s);
    }
    lv_label_set_text(info, line);
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    A = asi;
    info = lv_label_create(win);
    lv_obj_set_style_text_color(info, lv_color_hex(ADL_TEXT), 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 16, 16);
    refresh(NULL);
    lv_timer_create(refresh, 1000, NULL);
}
```

---

## 25. İpuçları ve sık yapılan hatalar

| Hata | Doğrusu |
|------|---------|
| `main()` yazmak | Sadece `app_entry(lv_obj_t*, asi_t*)` |
| `#include <stdio.h>` / `printf` / `malloc` | `asi->console->printf`, `asi->mem->alloc` |
| `lv_btn_create` | v9'da `lv_button_create` |
| `lv_obj_set_event_cb` | v9'da `lv_obj_add_event_cb` |
| `lv_msgbox_create()` (parent'sız) | v9'da `lv_msgbox_create(parent)` |
| Girişte sonsuz döngü / `sleep_ms` | `lv_timer_create` |
| `asi->net` doğrudan kullanmak | önce `asi->net && asi->net->link_type() != ASI_LINK_NONE` |
| Ağ isteğini UI iş parçacığında bloklamak | `asi->net->submit(...)` |
| Handle'ı `free` etmeden bırakmak | `asi->mem->alloc`/`free` dengesi |
| `%f` biçim belirteci | desteklenmez; tam sayı/%s kullan |
| Fontu elle ayarlamak | gerekmedikçe dokunma (ekran ADL fontunu miras verir) |
| Çöken uygulama çekirdeği düşürür sanmak | masaüstü otomatik setjmp ile korur (§11) |

**Performans notları:**
- Büyük dosya okuma/yazma için tek seferde `read_path`/`write` yerine blok blok
  çalışmak isterseniz `asi->storage` çok düşük seviyededir; genelde `fs` yeterlidir.
- Ekran varsayılan olarak 1280×720'dir (kullanıcı 2560×1440'a da çıkarabilir); sabit
  piksel yerine `LV_PCT`/`flex`/`grid` kullanın ki her çözünürlükte doğru görünsün.
- Uzun listelerde satır başına `lv_obj_create` yerine LVGL'nin dahili kaydırmasını
  kullanın (`lv_obj_set_scroll_dir`, `LV_OBJ_FLAG_SCROLLABLE`).

---

## 26. Ek: iç formatlar

Bunlar normal uygulama geliştirirken gerekmez; paketleyici/sistem araçları için.

### 26.1 WLF1 — uygulama paketi (`include/arctian/wlf.h`)

0.7'nin tek dağıtılabilir uygulama biçimi:

```
[wlf_header_t] [meta: "WLFM" + u16 + key=value satırları] [ikon][payload]
```

- `magic = "WLF1" (0x31464C57)`, `version = 1`; `kind`: `native` (x86-64 kod) veya
  `script` (Wolf bytecode). Alanlar yalnızca **sona** eklenir.
- `role` (`ui` / `cli` / `runtime`) ve meta anahtarları (`role`, `handles`, `entry`,
  `terminal`) §4.2 ve §22'de anlatılmıştır.
- `checksum = wlf_checksum(ikon + payload)`; meta bunun dışındadır ama imza
  kapsamındadır. Kurulum için RSA-2048/SHA-256 imza zorunludur.

### 26.2 DPK1 — eski DuckStore paketi (`include/arctian/dpk.h`)

> **0.7'de kurulmaz.** `store_install()` `.dpk` baytlarını açık bir hatayla reddeder ve
> 0.7 ile birlikte hiçbir paketleyici bu biçimi üretmez. Başlık iki nedenle durur:
> biçim tarihçesi ve yerleşim sabitleri (`APP_BASE` bölgesi, `DPK_MAX_PACKAGE` — güncelleme
> tamponunun boyutu buradan hesaplanır). Yeni uygulama yazan biri §26.1'e baksın.

```
[dpk_header_t] [meta][ikon SVG][kod image]
```

- `magic = "DPK1" (0x314B5044)`, `version = 1`, `abi = "arctian-1"`.
- `meta`: satır tabanlı `key=value` (percent-encoded). Anahtarlar: `title`,
  `category`, `version`, `publisher`, `description`, `window` (`"w,h"`),
  `permissions` (`"net,fs,dev"`).
- Kod `load_addr`e yüklenir, `bss_size` sıfırlanır, `entry_off`'taki fonksiyon
  `(win, asi)` ile çağrılır.
- `checksum = dpk_checksum(meta+ikon+kod)`.

### 26.3 AMOD — modül başlığı (`include/arctian/amod.h`)

Sistem modülleri (kernel/install/driver/desktop/app) için düz `.bin` başlığı:
`magic = "AMOD"`, `abi_version = 2`, `type`, `load_addr`, `image_size`,
`bss_size`, `entry_off`, `checksum`, `name`, `version`, `flags`.

### 26.4 Crash recovery (`include/arctian/recover.h`)

`arctian_recover_t` yapısı `lib/recover.asm` ile birebir hizalıdır; ayrıntı §11.

### 26.5 Bellek haritası (`include/arctian/memory_map.h`)

Uygulamayı ilgilendiren satırlar: `.wlf` uygulama bölgesi `WLF_BASE = 0x03000000`
(boyut `WLF_MAX = 8 MB`), eski `.dpk` bölgesi `APP_BASE = 0x01800000` (40 MB).

### 26.6 Wolf Engine JS köprüsü (`include/arctian/wolf.h`)

Tarayıcı motoru (Rust) için C ABI; DOM erişimi ve `wolf_engine_eval` vb.
Yalnızca tarayıcı uygulamasını ilgilendirir.

### 26.7 Önyükleme imajı (El Torito / MBR)

- ISO önyükleme imajı (`boot_iso.img`) ve disk önyükleme imajı
  `stage1 + stage2 + main_kernel` birleşiminden oluşur; `stage2`, kendi içindeki
  `kernel_size_bytes` alanıyla `main_kernel`'i `0x100000`'e taşır.
- `main_kernel`'in `.data` bölümü (örneğin ayırıcının `heap_ptr` değişkeni) imajın
  **kuyruğunda** yer alır. ISO'daki El Torito yükleme boyutu (**2048 baytlık CD
  sektörüne hizalı**) olmalıdır; hizasız olursa bazı BIOS'lar (VirtualBox) yalnızca
  tam CD sektörlerini okur ve imajın kuyruğunu (`.data`) yüklemeyi atlar. Bu durumda
  `heap_ptr` sıfır kalır, ayırmalar düşük belleğe (0–1 MB) taşar ve çekirdek yapıları
  bozulur (zamanlayıcı kesmesi tesliminde `#GP`/üçlü hata). `tools/arctianpack.py`
  bu nedenle ISO önyükleme imajını 2048'e hizalar.

---

*Bu belge Arctian 0.7 API yüzeyini (`include/arctian/*.h`, `desktop/*`, LVGL v9.6)
esas alır. Yeni başlık eklenirse ilgili bölüm güncellenmelidir.*
