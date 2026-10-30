/**
 * libWolf — Arctian uygulama katmani (public SDK basligi)
 * ======================================================
 *
 * Bu baslik, .wlf uygulamalarinin isletim sistemiyle konusma bicimidir.
 * Uygulama kodu LVGL'i, ASI'yi, cekirdek basliklarini GORMEZ; yalnizca
 * buradaki `wolf_*` islevlerini cagirir. Aradaki katman (wolf_api_t) calisma
 * zamaninda isletim sistemi tarafindan doldurulur.
 *
 * Kullanim (C):
 *
 *     #include "wolf.h"
 *
 *     int wolf_main(const wolf_api_t *api) {
 *         wolf_bind(api);                       // ZORUNLU: ilk satir
 *         int root = wolf_root();
 *         wolf_label(root, 20, 20, "Merhaba Arctian!");
 *         int b = wolf_button(root, 20, 60, 160, 40, "Tikla");
 *         wolf_on_click(b, my_click, 0);
 *         return 0;
 *     }
 *
 * ABI: `wolf_api_t` alanlari YALNIZCA SONA EKLENIR; mevcut alanlarin sirasi
 * ve anlami degismez. `version` ve `size` alanlari bunu dogrulamak icindir.
 *
 * Lisans: MIT (Arctian SDK)
 */
#ifndef ARCTIAN_WOLF_H
#define ARCTIAN_WOLF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WOLF_ABI_VERSION  2u
#define WOLF_API_MAGIC    0x574F4C46u   /* "WOLF" */

/* ABI v2 notlari:
 *   - Tablo alanlari yalnizca SONA eklenir; eski uygulamalar yeni isletim
 *     sisteminde sorunsuz baglanir (HAS() korumasi zaten vardir).
 *   - Yeni alanlarin varligi `wolf_api_v2()` ya da dogrudan fonksiyon
 *     kontrolu ile sorgulanir; surum buyugu baglanmayi ENGELLEMEZ.
 *   - Stil/boyar API'si yalnizca native (.wlf C) uygulamalar icindir;
 *     bytecode VM v1 alt kumesini kullanmaya devam eder.
 */

/* ---- Dondurulen degerler ---- */
#define WOLF_OK        0
#define WOLF_ERR      (-1)

/* ---- Pencere bayraklari (wolf_api_t.app_info) ---- */
#define WOLF_WIN_RESIZABLE  1u
#define WOLF_WIN_FULLSCREEN 2u

/* ---- Uygulama izinleri (.wlf header'daki flags) ---- */
#define WOLF_PERM_FS    0x0001u
#define WOLF_PERM_NET   0x0002u
#define WOLF_PERM_EXEC  0x0004u
#define WOLF_PERM_ALL   0xFFFFu

/* Widget kimligi: 0 = gecersiz. */
typedef int wolf_id;

/**
 * Olay geri cagrisi.
 *   widget : olayin geldigi widget kimligi
 *   user   : wolf_on_click/on_change'e verilen isaretci
 */
typedef void (*wolf_event_fn)(wolf_id widget, void *user);

/**
 * Isletim sisteminin uygulamaya sundugu islev tablosu.
 * Tum alanlar isaretcidir; uygulama bunlari dogrudan cagirmaz,
 * libwolf sarmalayicilari cagirir.
 */
typedef struct wolf_api {
    /* --- kimlik (uyumluluk denetimi) --- */
    uint32_t magic;              /* WOLF_API_MAGIC */
    uint32_t version;            /* WOLF_ABI_VERSION */
    uint32_t size;               /* sizeof(wolf_api_t) */

    /* --- pencere --- */
    wolf_id  (*root)(void);                              /* uygulama kok kapsayicisi */
    void     (*win_title)(const char *title);            /* pencere basligini degistir */
    void     (*win_size)(int w, int h);                  /* boyut istegi */

    /* --- cizim (parent = 0 ise kok) --- */
    wolf_id  (*panel)(wolf_id parent, int x, int y, int w, int h);
    wolf_id  (*label)(wolf_id parent, int x, int y, const char *text);
    wolf_id  (*button)(wolf_id parent, int x, int y, int w, int h, const char *text);
    wolf_id  (*input)(wolf_id parent, int x, int y, int w, int h, const char *placeholder);
    wolf_id  (*listbox)(wolf_id parent, int x, int y, int w, int h);
    wolf_id  (*image)(wolf_id parent, int x, int y, int w, int h, const char *path);
    wolf_id  (*progress)(wolf_id parent, int x, int y, int w, int h, int percent);
    wolf_id  (*slider)(wolf_id parent, int x, int y, int w, int h, int percent);
    wolf_id  (*separator)(wolf_id parent, int x, int y, int w);
    wolf_id  (*checkbox)(wolf_id parent, int x, int y, const char *text, int checked);

    /* --- widget ozellikleri --- */
    void     (*set_text)(wolf_id w, const char *text);
    int      (*get_text)(wolf_id w, char *out, int cap);
    void     (*set_value)(wolf_id w, int value);          /* progress/slider/checkbox */
    int      (*get_value)(wolf_id w);
    void     (*set_pos)(wolf_id w, int x, int y);
    void     (*set_size)(wolf_id w, int w2, int h2);
    void     (*set_color)(wolf_id w, uint32_t rgb);       /* 0xRRGGBB */
    void     (*set_visible)(wolf_id w, int visible);
    void     (*set_enabled)(wolf_id w, int enabled);
    void     (*clear)(wolf_id w);                         /* cocuklari sil */
    void     (*destroy)(wolf_id w);

    /* --- olaylar --- */
    void     (*on_click)(wolf_id w, wolf_event_fn cb, void *user);
    void     (*on_change)(wolf_id w, wolf_event_fn cb, void *user);

    /* --- liste --- */
    void     (*list_add)(wolf_id w, const char *item);
    void     (*list_clear)(wolf_id w);
    int      (*list_count)(wolf_id w);
    int      (*list_item)(wolf_id w, int index, char *out, int cap);
    int      (*list_selected)(wolf_id w);

    /* --- dosya sistemi (izin: FS) --- */
    int      (*fs_read)(const char *path, void *buf, int cap);
    int      (*fs_write)(const char *path, const void *buf, int len);
    int      (*fs_list)(const char *dir, int index, char *name, int cap, int *is_dir);
    int      (*fs_count)(const char *dir);
    int      (*fs_exists)(const char *path);
    int      (*fs_remove)(const char *path);
    int      (*fs_mkdir)(const char *path);
    int      (*fs_rename)(const char *from, const char *to);

    /* --- sistem --- */
    void     (*log)(const char *text);
    void     (*notify)(const char *title, const char *message);
    void     (*dialog)(const char *title, const char *message, const char *ok);
    int      (*exec)(const char *command, char *out, int cap);   /* kabuk alt kumesi */
    uint64_t (*ticks_ms)(void);
    void     (*sleep_ms)(uint32_t ms);
    int      (*now)(int *year, int *month, int *day, int *hour, int *min, int *sec);
    int      (*os_version)(char *out, int cap);                   /* "0.6" gibi */
    int      (*open_app)(const char *name_or_title);             /* baska uygulama ac */

    /* --- pano --- */
    void     (*clip_set)(const char *text);
    int      (*clip_get)(char *out, int cap);

    /* --- ag (izin: NET) --- */
    int      (*fetch)(const char *url, void *buf, int cap, int *len_out);

    /* --- 0.7: calisma zamani argumani (rol=cli/runtime) --- */
    const char *(*arg)(void);         /* birlikte acilan dosya; yoksa "" */

    /* ============ ABI v2 eklemeleri (yalnizca sona eklenir) ============ */

    /* --- stil motoru (CSS benzeri) ---------------------------------
     * prop = WOLF_SP_*  |  value = ozellige gore anlam (asagida)
     * flags = durum bitleri | ( kisim << 8 )  [ WOLF_SEL(durum, kisim) ]
     * Renkler 0xRRGGBB; opaklikler 0..100. */
    void     (*style)(wolf_id w, int prop, int value, int flags);

    /* --- poligon cizgisi (lv_line) --- */
    wolf_id  (*line)(wolf_id parent, int x, int y);
    void     (*line_reset)(wolf_id w);                 /* noktaları sil */
    int      (*line_to)(wolf_id w, int x, int y);      /* 0 OK, -1 tas tasma */
    /* renk/genislik stil ile: WOLF_SP_LINE_COLOR / LINE_WIDTH / LINE_OPA
     * / LINE_ROUNDED */

    /* --- yay / kadran (lv_arc) --- */
    wolf_id  (*arc)(wolf_id parent, int x, int y, int r);
    void     (*arc_set)(wolf_id w, int start_deg, int end_deg, int rot_deg);
    /* deger: set_value/get_value; kalinlik/renk: WOLF_SP_ARC_WIDTH/COLOR/ROUNDED
     * (MAIN = arka halka, INDICATOR = dolgu; BG_COLOR yaylari boyamaz!) */

    /* --- piksel tuvali (ARGB8888; argb = 0xAARRGGBB, alpha harmanlanir) --- */
    wolf_id   (*canvas)(wolf_id parent, int x, int y, int w, int h);
    void      (*canvas_px)(wolf_id w, int x, int y, uint32_t argb);
    void      (*canvas_rect)(wolf_id w, int x, int y, int cw, int ch,
                             uint32_t argb, int radius);
    void      (*canvas_line)(wolf_id w, int x1, int y1, int x2, int y2,
                             uint32_t argb, int width);
    void      (*canvas_circle)(wolf_id w, int cx, int cy, int r,
                               uint32_t argb, int fill);
    void      (*canvas_grad)(wolf_id w, int x, int y, int cw, int ch,
                             uint32_t c0, uint32_t c1, int dir); /* 0 dus 1 yatay */
    void      (*canvas_clear)(wolf_id w, uint32_t argb);
    uint32_t  (*canvas_getpx)(wolf_id w, int x, int y); /* -1 disari */

    /* --- ses (WAV PCM; 8/16-bit mono/stereo) --- */
    int       (*audio_ok)(void);                     /* 1 = surucu var */
    int       (*audio_play)(const char *path);       /* C:/... .wav oku+calar */
    int       (*audio_play_mem)(const void *data, int len); /* bellekten */
    void      (*audio_stop)(void);
    int       (*audio_playing)(void);
    int       (*audio_vol)(void);                    /* 0..100 */
    void      (*audio_set_vol)(int percent);

    /* --- zamanlayici: her period_ms'de cb(timer_id, user) --- */
    wolf_id   (*timer)(uint32_t period_ms, wolf_event_fn cb, void *user);
    void      (*timer_cancel)(wolf_id t);

    /* --- yigin sirasi --- */
    void      (*to_front)(wolf_id w);
    void      (*to_back)(wolf_id w);

    /* --- gecis animasyonu: props_mask = WOLF_TP_*, easing = WOLF_EASE_* ---
     * durum degisiminde (hover/pressed/WOLF_SF_USER1...) secili ozellikler
     * dur_ms boyunca canlanir; delay_ms CSS animation-delay karsiligidir. */
    void      (*transition)(wolf_id w, unsigned props_mask, int dur_ms,
                            int delay_ms, int easing);

    /* --- serbest durum ac/kapat: state_flags = WOLF_SF_USER1/2 bitleri.
     * Tema degistirme deseni: koyu renkleri WOLF_SEL(WOLF_SF_USER1, ...)
     * ile ver, toggle'da state_set(w, WOLF_SF_USER1, on). */
    void      (*state_set)(wolf_id w, int state_flags, int on);

    /* --- fadeUp girisi: opa 0->100 ve y dy->0 (CSS @keyframes fadeUp).
     * Cagride widget gizlenir; delay_ms sonra dur_ms icinde belirir. */
    void      (*anim_in)(wolf_id w, int dy, int dur_ms, int delay_ms);

    /* --- softPulse: opa low_opa..100 sonsuz salinim (canli gosterge). */
    void      (*anim_pulse)(wolf_id w, int period_ms, int low_opa);

    /* --- ileride: buraya yeni alanlar eklenir --- */
} wolf_api_t;

/* ==================================================================== */
/* ABI v2 — stil sozlesmesi                                             */
/* ==================================================================== */

/* Durum bayraklari (flags dusuk byte) — CSS'in :hover/:active karsiligi. */
#define WOLF_SF_HOVER     0x01
#define WOLF_SF_PRESSED   0x02
#define WOLF_SF_CHECKED   0x04
#define WOLF_SF_FOCUSED   0x08
#define WOLF_SF_DISABLED  0x10
#define WOLF_SF_EDITED    0x20
#define WOLF_SF_USER1     0x40   /* bos durum: tema degistirme vb. */
#define WOLF_SF_USER2     0x80

/* Kisim (part) — flags yuksek byte: WOLF_SEL(durum, kisim) ile kullan. */
#define WOLF_PF_MAIN       0
#define WOLF_PF_SCROLLBAR  1
#define WOLF_PF_INDICATOR  2
#define WOLF_PF_KNOB       3
#define WOLF_PF_CURSOR     4
#define WOLF_PF_ITEMS      5
#define WOLF_SEL(state, part) ((int)(state) | (((int)(part)) << 8))

/* Stil ozellikleri (wolf_style prop kodlari; degerler yorumlarda). */
enum wolf_style_prop {
    WOLF_SP_BG_COLOR      = 1,   /* 0xRRGGBB */
    WOLF_SP_BG_OPA        = 2,   /* 0..100 */
    WOLF_SP_BG_GRAD_COLOR = 3,   /* 0xRRGGBB */
    WOLF_SP_BG_GRAD_DIR   = 4,   /* 0 yok | 1 dikey | 2 yatay */
    WOLF_SP_TEXT_COLOR    = 5,   /* 0xRRGGBB */
    WOLF_SP_TEXT_OPA      = 6,   /* 0..100 */
    WOLF_SP_TEXT_LETTER   = 7,   /* harf araligi px */
    WOLF_SP_TEXT_LINE     = 8,   /* satir araligi px */
    WOLF_SP_RADIUS        = 9,   /* px (buyuk = kapsul) */
    WOLF_SP_BORDER_WIDTH  = 10,  /* px */
    WOLF_SP_BORDER_COLOR  = 11,  /* 0xRRGGBB */
    WOLF_SP_BORDER_OPA    = 12,  /* 0..100 */
    WOLF_SP_BORDER_SIDE   = 13,  /* 1 ust 2 alt 4 sol 8 sag (15=tum) */
    WOLF_SP_PAD           = 14,  /* px (ic kenar, her yon) */
    WOLF_SP_PAD_TOP       = 15,
    WOLF_SP_PAD_BOTTOM    = 16,
    WOLF_SP_PAD_LEFT      = 17,
    WOLF_SP_PAD_RIGHT     = 18,
    WOLF_SP_PAD_ROW       = 19,  /* esnek dizide satir araligi */
    WOLF_SP_PAD_COLUMN    = 20,
    WOLF_SP_SHADOW_WIDTH  = 21,  /* px bulaniklik yari capi */
    WOLF_SP_SHADOW_COLOR  = 22,  /* 0xRRGGBB */
    WOLF_SP_SHADOW_OPA    = 23,  /* 0..100 */
    WOLF_SP_SHADOW_SPREAD = 24,  /* px genisleme */
    WOLF_SP_SHADOW_OFS_X  = 25,  /* px */
    WOLF_SP_SHADOW_OFS_Y  = 26,  /* px */
    WOLF_SP_OPA           = 27,  /* tum widget opakligi 0..100 */
    WOLF_SP_ROTATE        = 28,  /* 0.1 derece (450 = 45 der.) */
    WOLF_SP_SCALE         = 29,  /* % (100 normal, 200 iki kat) */
    WOLF_SP_SCALE_X       = 30,  /* % */
    WOLF_SP_SCALE_Y       = 31,  /* % */
    WOLF_SP_PIVOT_X       = 32,  /* px (donme merkezi) */
    WOLF_SP_PIVOT_Y       = 33,  /* px */
    WOLF_SP_LINE_WIDTH    = 34,  /* lv_line kalinligi px */
    WOLF_SP_LINE_COLOR    = 35,  /* 0xRRGGBB */
    WOLF_SP_LINE_OPA      = 36,  /* 0..100 */
    WOLF_SP_LINE_ROUNDED  = 37,  /* 1 = yuvarlak uc/birlesim */
    WOLF_SP_OUTLINE_WIDTH = 38,  /* px (odak halkasi) */
    WOLF_SP_OUTLINE_COLOR = 39,  /* 0xRRGGBB */
    WOLF_SP_OUTLINE_OPA   = 40,  /* 0..100 */
    WOLF_SP_OUTLINE_PAD   = 41,  /* px */
    WOLF_SP_ANIM_TIME     = 42,  /* ms (bar/slider animasyonu) */
    WOLF_SP_CLIP_CORNER   = 43,  /* 1 = kosede kirp */
    WOLF_SP_BG_GRAD_OPA   = 44,  /* 0..100 */
    WOLF_SP_SIZE          = 45,  /* px: parca kalinligi (knob, scrollbar) */
    WOLF_SP_ARC_WIDTH     = 46,  /* px: yay kalinligi (MAIN arka, INDICATOR dolgu) */
    WOLF_SP_ARC_COLOR     = 47,  /* 0xRRGGBB (yay icin BG_COLOR KULLANILMAZ!) */
    WOLF_SP_ARC_ROUNDED   = 48,  /* 1 = yuvarlak uc */
    WOLF_SP_TRANSLATE_X   = 49,  /* px: kaydirma (giris animasyonu / hover) */
    WOLF_SP_TRANSLATE_Y   = 50,  /* px */
    WOLF_SP_FONT_PX       = 51   /* gomulu arayuz fontu, istenen piksel boyutu
                                  * (orn. 24 = 24px). Sozluk boyutlari or:
                                  * 11/13/16/20/24/32. */
};

/* ---- Gecis (CSS `transition` karsiligi) ----------------------------
 * wolf_transition() ile bir widget'a kural tanimla; hedef durumdaki
 * degerleri wolf_style(WOLF_SEL(durum,...)) ile ver. Durum degisince
 * (hover/pressed/user1...) secilen ozellikler eski->yeni deger arasinda
 * canlanir. Yalniz MAIN kisimda calisir. */
#define WOLF_TP_BG_COLOR      (1u << 0)
#define WOLF_TP_BG_OPA        (1u << 1)
#define WOLF_TP_TEXT_COLOR    (1u << 2)
#define WOLF_TP_TEXT_OPA      (1u << 3)
#define WOLF_TP_BORDER_COLOR  (1u << 4)
#define WOLF_TP_BORDER_WIDTH  (1u << 5)
#define WOLF_TP_RADIUS        (1u << 6)
#define WOLF_TP_SHADOW        (1u << 7)   /* genislik+renk+opa+yayilim+kayma */
#define WOLF_TP_TRANSLATE_X   (1u << 8)
#define WOLF_TP_TRANSLATE_Y   (1u << 9)
#define WOLF_TP_SCALE         (1u << 10)  /* x + y */
#define WOLF_TP_OPA           (1u << 11)  /* tum widget opakligi */
#define WOLF_TP_OUTLINE       (1u << 12)  /* odak halkasi (odak=3px halka) */
#define WOLF_TP_ARC           (1u << 13)  /* kalinlik + renk + opa */
#define WOLF_TP_ALL \
    (WOLF_TP_BG_COLOR | WOLF_TP_BG_OPA | WOLF_TP_TEXT_COLOR | \
     WOLF_TP_BORDER_COLOR | WOLF_TP_RADIUS | WOLF_TP_SHADOW | \
     WOLF_TP_TRANSLATE_Y | WOLF_TP_SCALE | WOLF_TP_OPA | WOLF_TP_OUTLINE)

/* Yavaslatma egileri (CSS timing-function karsiligi). */
#define WOLF_EASE_LINEAR   0
#define WOLF_EASE_IN       1
#define WOLF_EASE_OUT      2
#define WOLF_EASE_IN_OUT   3   /* CSS "ease"/cubic-bezier(.42,0,.58,1) */

/* Tablo ABI v2 alanlarini iceriyor mu? (binding sonrasi sorulur) */
int wolf_api_v2(void);

/* ==================================================================== */
/* Istemci tarafi (libwolf)                                             */
/* ==================================================================== */

/** Isletim sisteminin verdigi tabloyu baglar. Uygulama GIRISTE cagirmali. */
void              wolf_bind(const wolf_api_t *api);
const wolf_api_t *wolf_bound(void);
int               wolf_ready(void);

/* Pencere */
wolf_id wolf_root(void);
void    wolf_win_title(const char *title);
void    wolf_win_size(int w, int h);

/* Cizim */
wolf_id wolf_panel(wolf_id parent, int x, int y, int w, int h);
wolf_id wolf_label(wolf_id parent, int x, int y, const char *text);
wolf_id wolf_button(wolf_id parent, int x, int y, int w, int h, const char *text);
wolf_id wolf_input(wolf_id parent, int x, int y, int w, int h, const char *placeholder);
wolf_id wolf_listbox(wolf_id parent, int x, int y, int w, int h);
wolf_id wolf_image(wolf_id parent, int x, int y, int w, int h, const char *path);
wolf_id wolf_progress(wolf_id parent, int x, int y, int w, int h, int percent);
wolf_id wolf_slider(wolf_id parent, int x, int y, int w, int h, int percent);
wolf_id wolf_separator(wolf_id parent, int x, int y, int w);
wolf_id wolf_checkbox(wolf_id parent, int x, int y, const char *text, int checked);

/* Widget ozellikleri */
void wolf_text(wolf_id w, const char *text);
int  wolf_text_of(wolf_id w, char *out, int cap);
void wolf_value(wolf_id w, int value);
int  wolf_value_of(wolf_id w);
void wolf_move(wolf_id w, int x, int y);
void wolf_resize(wolf_id w, int width, int height);
void wolf_color(wolf_id w, uint32_t rgb);
void wolf_visible(wolf_id w, int visible);
void wolf_enabled(wolf_id w, int enabled);
void wolf_clear(wolf_id w);
void wolf_destroy(wolf_id w);

/* Olaylar */
void wolf_on_click(wolf_id w, wolf_event_fn cb);
void wolf_on_click_ex(wolf_id w, wolf_event_fn cb, void *user);
void wolf_on_change(wolf_id w, wolf_event_fn cb);
void wolf_on_change_ex(wolf_id w, wolf_event_fn cb, void *user);

/* Liste */
void wolf_list_add(wolf_id w, const char *item);
void wolf_list_clear(wolf_id w);
int  wolf_list_count(wolf_id w);
int  wolf_list_item(wolf_id w, int index, char *out, int cap);
int  wolf_list_selected(wolf_id w);

/* Dosya sistemi */
int wolf_fs_read(const char *path, void *buf, int cap);
int wolf_fs_write(const char *path, const void *buf, int len);
int wolf_fs_list(const char *dir, int index, char *name, int cap, int *is_dir);
int wolf_fs_count(const char *dir);
int wolf_fs_exists(const char *path);
int wolf_fs_remove(const char *path);
int wolf_fs_mkdir(const char *path);
int wolf_fs_rename(const char *from, const char *to);

/* Sistem */
void     wolf_log(const char *text);
void     wolf_notify(const char *title, const char *message);
void     wolf_dialog(const char *title, const char *message, const char *ok);
int      wolf_exec(const char *command, char *out, int cap);
uint64_t wolf_ticks_ms(void);
void     wolf_sleep_ms(uint32_t ms);
int      wolf_now(int *year, int *month, int *day, int *hour, int *min, int *sec);
int      wolf_version(char *out, int cap);
int      wolf_open_app(const char *name_or_title);
void     wolf_clip_set(const char *text);
int      wolf_clip_get(char *out, int cap);

/* Ag */
int wolf_fetch(const char *url, void *buf, int cap, int *len_out);

/* Calisma zamani argumani (rol=cli/runtime); dosya yoksa bos dizge. */
const char *wolf_arg(void);

/* ---- ABI v2: stil ---- */
void wolf_style(wolf_id w, int prop, int value, int flags);
/* Kisayol: normal durum + MAIN kisim */
#define wolf_style_simple(w, prop, val) wolf_style((w), (prop), (val), 0)

/* ---- ABI v2: cizgi ve yay ---- */
wolf_id wolf_line(wolf_id parent, int x, int y);
void    wolf_line_reset(wolf_id w);
int     wolf_line_to(wolf_id w, int x, int y);
wolf_id wolf_arc(wolf_id parent, int x, int y, int r);
void    wolf_arc_set(wolf_id w, int start_deg, int end_deg, int rot_deg);

/* ---- ABI v2: piksel tuvali (argb = 0xAARRGGBB) ---- */
wolf_id  wolf_canvas(wolf_id parent, int x, int y, int w, int h);
void     wolf_canvas_px(wolf_id w, int x, int y, uint32_t argb);
void     wolf_canvas_rect(wolf_id w, int x, int y, int cw, int ch,
                          uint32_t argb, int radius);
void     wolf_canvas_line(wolf_id w, int x1, int y1, int x2, int y2,
                          uint32_t argb, int width);
void     wolf_canvas_circle(wolf_id w, int cx, int cy, int r,
                            uint32_t argb, int fill);
void     wolf_canvas_grad(wolf_id w, int x, int y, int cw, int ch,
                          uint32_t c0, uint32_t c1, int dir);
void     wolf_canvas_clear(wolf_id w, uint32_t argb);
uint32_t wolf_canvas_getpx(wolf_id w, int x, int y);

/* ---- ABI v2: ses ---- */
int  wolf_audio_ok(void);
int  wolf_audio_play(const char *path);
int  wolf_audio_play_mem(const void *data, int len);
void wolf_audio_stop(void);
int  wolf_audio_playing(void);
int  wolf_audio_vol(void);
void wolf_audio_set_vol(int percent);

/* ---- ABI v2: zamanlayici ---- */
wolf_id wolf_timer(uint32_t period_ms, wolf_event_fn cb, void *user);
void    wolf_timer_cancel(wolf_id t);

/* ---- ABI v2: yigin sirasi ---- */
void wolf_to_front(wolf_id w);
void wolf_to_back(wolf_id w);

/* ---- ABI v2: gecis ve animasyon ---- */
void wolf_transition(wolf_id w, unsigned props_mask, int dur_ms,
                     int delay_ms, int easing);
void wolf_state_set(wolf_id w, int state_flags, int on);
void wolf_anim_in(wolf_id w, int dy, int dur_ms, int delay_ms);
void wolf_anim_pulse(wolf_id w, int period_ms, int low_opa);

/* ---- kucuk yardimcilar (libwolf icinde, freestanding uyumlu) ---- */
int  wolf_strlen(const char *s);
void wolf_strcpy(char *dst, int cap, const char *src);
void wolf_strcat(char *dst, int cap, const char *src);
int  wolf_streq(const char *a, const char *b);
/** Basit sayi -> metin (snprintf yok; freestanding). */
void wolf_itoa(int value, char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* ARCTIAN_WOLF_H */

