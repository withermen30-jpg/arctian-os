# libWolf — Arctian uygulama SDK'sı

`.wlf`, Arctian'ın uygulama biçimidir (Windows'ta `.exe` ne ise burada `.wlf`).
Uygulamalar **C** ile yazılır ve **libWolf** üzerinden işletim sistemiyle
konuşur. Uygulama kodu LVGL, ASI ya da çekirdek başlıklarını **görmez**;
aradaki katman budur:

```
   uygulama (main.c)
        │   wolf_label(), wolf_button(), wolf_fs_read(), ...
        ▼
   libWolf  (include/wolf.h + src/libwolf.c)   ← SDK
        │   wolf_api_t  (fonksiyon tablosu, çalışma anında doldurulur)
        ▼
   Arctian çalışma zamanı  (LVGL + dosya sistemi + ağ + kabuk)
```

Aynı `main.c` iki yolla derlenir:

| Yol | Araç | Çıktı | Not |
|---|---|---|---|
| Linux / macOS / WSL | `gcc` + `ld` + `wlfpack.py` | native `.wlf` | tam hız, makine kodu |
| Arctian içinde | `wf build <proje>` | bytecode `.wlf` | derleyici gerekmez |

## Hızlı başlangıç (Windows)

```bat
wolf new benim-uygulamam "Benim Uygulamam"
cd benim-uygulamam
wolf build
wolf run benim-uygulamam.wlf
```

Veya mevcut bir örnekten kopyalayın:

```bat
cd sdk\examples\merhaba
..\..\build.bat
```

Çıktı: `merhaba.wlf`. Arctian'da:

```
wf install merhaba.wlf
wf run merhaba
```

## Hızlı başlangıç (Arctian içinde)

```
wf new sayac
wf build sayac
wf run sayac
```

`wf new` şu dosyaları oluşturur:

```
C:/projler/sayac/main.c
C:/projler/sayac/wolf.json
C:/projler/sayac/README.txt
```

`main.c`'yi `editor` ile düzenleyin, `wf build` ile derleyin.

## wolf.json

```json
{
  "name": "sayac",
  "title": "Sayaç",
  "version": "1.0",
  "w": 520, "h": 400,
  "permissions": ["fs", "net", "exec"]
}
```

- `name` — kimlik (a-z, 0-9, `-`, `_`); dosya adı `<name>.wlf` olur
- `permissions` — verilmezse tam yetki. `fs`/`net`/`exec` daraltır.

### Uygulama rolü (0.7)

Her `.wlf` bir uygulamadır ama hepsi pencere açmaz. Rol `wolf.json` içinde
bildirilir ve paketin meta bloğuna yazılır:

```json
{
  "name": "pyrt",
  "title": "pyrt (çalışma zamanı)",
  "role": "runtime",
  "handles": ["py", "pyw"],
  "entry": "pyrt",
  "terminal": true,
  "permissions": ["fs"]
}
```

| Alan | Anlamı |
|---|---|
| `role` | `ui` (pencere), `cli` (terminalde çalışır), `runtime` (dosya ile çalışan yorumlayıcı/backend) |
| `handles` | Bu uygulamanın açtığı uzantılar; "Birlikte aç" ve çift tık bunu kullanır |
| `entry` | Kullanıcıya gösterilen giriş adı (örn. `python`) |
| `terminal` | Çalışırken terminal penceresi açık kalsın |

`role` verilmezse `ui` kabul edilir (eski paketler de böyle okunur).
`cli`/`runtime` uygulamaların çıktısı `wolf_log`, `wolf_notify` ve
`wolf_dialog` üzerinden terminale gider. Çalışma zamanına verilen dosya
`wolf_arg()` ile okunur.

```c
int wolf_main(const wolf_api_t *api) {
    wolf_bind(api);
    const char *dosya = wolf_arg();      /* "birlikte aç" ile gelen yol */
    ...
}
```

Arctian'da `.py` dosyasına çift tıklamak `handles=py` ilan eden `.wlf`
uygulamasını seçer; terminal açılır ve dosya argüman olarak verilir.

## API

Tam liste `include/wolf.h` içinde. Özet:

**Çizim** `wolf_root, wolf_panel, wolf_label, wolf_button, wolf_input,
wolf_listbox, wolf_image, wolf_progress, wolf_slider, wolf_separator,
wolf_checkbox`

**Özellik** `wolf_text, wolf_value, wolf_move, wolf_resize, wolf_color,
wolf_visible, wolf_enabled, wolf_clear, wolf_destroy, wolf_list_add,
wolf_list_clear, wolf_list_count, wolf_list_item, wolf_list_selected`

**Olay** `wolf_on_click(w, fn)`, `wolf_on_change(w, fn)`

**Stil (v2)** `wolf_style(w, prop, value, sel)`, `wolf_state_set(w, state, on)`,
`WOLF_SEL(durum, bölüm)` — renk `0xRRGGBB`, opaklık `0..100`; durumlar
`WOLF_SF_HOVER/PRESSED/FOCUSED/USER1/USER2`

**Geçiş ve animasyon (v2)** `wolf_transition(w, WOLF_TP_*, süre, gecikme, easing)`,
`wolf_anim_in(w, dy, süre, gecikme)`, `wolf_anim_pulse(w, süre, düşük_opak)`

`wolf_api_v2()` sıfırdan farklıysa ABI v2 hazır demektir; v2 olmadan derlenen
paketler eski işletim sisteminde aynen çalışır.

**Dosya** `wolf_fs_read/write/list/count/exists/remove/mkdir/rename`

**Sistem** `wolf_log, wolf_notify, wolf_dialog, wolf_exec, wolf_ticks_ms,
wolf_sleep_ms, wolf_now, wolf_os_version, wolf_open_app, wolf_clip_set/get,
wolf_fetch`

## Örnek

```c
#include "wolf.h"

int sayac = 0;
int bar = 0;

void artir(int w, void *user) {
    sayac = sayac + 1;
    wolf_value(bar, sayac * 10);
    if (sayac >= 10) wolf_notify("Sayaç", "10'a ulaştı!");
}

int wolf_main(const wolf_api_t *api) {
    wolf_bind(api);
    int root = wolf_root();
    wolf_win_title("Sayaç");
    wolf_label(root, 20, 16, "Sayaç uygulaması");
    int b = wolf_button(root, 20, 52, 200, 44, "Artır");
    bar = wolf_progress(root, 20, 110, 300, 18, 0);
    wolf_on_click(b, artir);
    return 0;
}
```

## Sınırlar (bilinçli)

- Çizim koordinat tabanlıdır (ölçek otomatik uygulanır).
- Bytecode derleyici (OS içi) C'nin bir alt kümesini destekler:
  `int/char`, aritmetik, karşılaştırma, `if/else`, `while`, `for`, `break`,
  `return`, fonksiyonlar (≤8 parametre), `i++`, `+=`, metin literalleri.
  `struct`, pointer, dizi, `float` **yoktur** — native SDK ile bunlar
  kullanılabilir (gcc sınırı).
- `wolf_exec` yalnızca bilgi komutlarını kabul eder (güvenlik).
- Aynı adres alanında çalışırlar; tam süreç izolasyonu yol haritasında.

## Dosyalar

```
sdk/
  include/wolf.h      public API (uygulama bunu include eder)
  src/libwolf.c       API'nin istemci tarafı (uygulamaya linklenir)
  link.ld             native .wlf yerleşimi (0x03000000)
  wlfpack.py          .wlf paketleyici + --inspect
  build.bat           Windows derleme
  build.sh            Linux/macOS/WSL derleme
  examples/merhaba/   örnek proje
```
