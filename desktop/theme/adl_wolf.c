/**
 * ADL-Wolf tema: macOS esintili koyu, canli accent, Beyaz Kurt kimligi.
 * Arayuz fontu gomulu TTF'den (Tiny TTF) olusturulur; Turkce destekler.
 *
 * Erisilebilirlik: yaz olcegi (font boyutu), yuksek kontrast teması
 * (metin/zemin kontrasti artar) ve klavye gezinme odak halkasi buradan
 * yonetilir. Renk token'lari fonksiyon makrosudur; kontrast moduna gore
 * degerleri aninda degisir (yeni olusturulan nesnelere uygulanir).
 */
#include "adl_wolf.h"

extern const unsigned char font_ttf[];
extern const unsigned int  font_ttf_len;

static lv_font_t *s_font = 0;
static uint32_t s_accent = 0x3B82F6;
static uint32_t s_accent2 = 0x8B5CF6;

/* Erisilebilirlik durumu */
static int s_font_pct = 100;     /* yaz olcegi: 100/125/150 (%) */
static int s_contrast = 0;       /* 1 = yuksek kontrast teması */
static int s_light = 0;          /* 1 = acik tema */
static int s_ring_on = 1;        /* 1 = odak halkasi acik */

/* Odak halkasi stili + uzerine eklenen tema */
static lv_style_t s_ring_style;
static int        s_ring_ready = 0;
static lv_theme_t *s_base_theme = 0;

uint32_t adl_accent(void) { return s_accent; }
uint32_t adl_accent_2(void) { return s_accent2; }

void adl_set_accent(uint32_t accent, uint32_t accent2) {
    s_accent = accent;
    s_accent2 = accent2;
}

/* ---- Renk token'lari (kontrast / acik tema moduna gore) ---- */
uint32_t adl_bg(void) {
    if (s_contrast) return 0x000000;
    return s_light ? 0xEEF1F6 : 0x0E0E12;
}
uint32_t adl_surface(void) {
    if (s_contrast) return 0x0B0B0D;
    return s_light ? 0xFFFFFF : 0x16171C;
}
uint32_t adl_surface_2(void) {
    if (s_contrast) return 0x17171A;
    return s_light ? 0xF3F5F9 : 0x1E2027;
}
uint32_t adl_border(void) {
    if (s_contrast) return 0xA8A8AE;
    return s_light ? 0xD3D8E0 : 0x2C2F38;
}
uint32_t adl_text(void) {
    if (s_contrast) return 0xFFFFFF;
    return s_light ? 0x15181D : 0xF5F5F7;
}
uint32_t adl_text_dim(void) {
    if (s_contrast) return 0xDCDCE2;
    return s_light ? 0x5C6470 : 0x9AA0A6;
}
uint32_t adl_danger(void)    { return s_contrast ? 0xFF8A82 : 0xFF5F57; }
uint32_t adl_warn(void)      { return s_contrast ? 0xFFD65E : 0xFEBC2E; }
uint32_t adl_ok(void)        { return s_contrast ? 0x63E37A : 0x28C840; }

void adl_set_light(int on) { s_light = on ? 1 : 0; }
int  adl_light(void)       { return s_light; }

void adl_set_contrast(int on) { s_contrast = on ? 1 : 0; }
int  adl_contrast(void)       { return s_contrast; }
void adl_set_focus_ring(int on) { s_ring_on = on ? 1 : 0; }
int  adl_focus_ring(void)      { return s_ring_on; }

/* remove_style_all ile silinen odak halkasi stilini geri ekle.
 * Stil nesnesi statik ve tekil; tema yeniden kurulsa da ayni kalir. */
void adl_focus_ring_attach(lv_obj_t *obj) {
    if (!obj || !s_ring_on) return;
    if (!s_ring_ready) {
        lv_style_init(&s_ring_style);
        lv_style_set_outline_width(&s_ring_style, 3);
        lv_style_set_outline_pad(&s_ring_style, 2);
        lv_style_set_radius(&s_ring_style, 8);
        lv_style_set_outline_color(&s_ring_style, lv_color_hex(s_accent));
        s_ring_ready = 1;
    }
    lv_obj_add_style(obj, &s_ring_style, LV_STATE_FOCUSED);
}

/* ---- Font olcegi ---- */
void adl_set_font_scale(int percent) {
    if (percent < 80) percent = 80;
    if (percent > 200) percent = 200;
    s_font_pct = percent;
}

int adl_font_scale(void) { return s_font_pct; }

void adl_font_init(void) {
    int px = (15 * s_font_pct + 50) / 100;   /* 15 taban boyut */
    if (px < 10) px = 10;
    s_font = lv_tiny_ttf_create_data(font_ttf, font_ttf_len, (uint32_t)px);
}

lv_font_t *adl_font(void) {
    return s_font;
}

/* Olcek degisince: yeni boyutta font olustur, temayi tazele.
 * Eski font nesnesi bilincli olarak birakilir; acik pencerelerdeki eski
 * nesneler ona isaret etmeye devam eder (gui asmaz). Tum kabugun yeni
 * fontla kurulmasi icin shell_rebuild gerekir. */
void adl_font_apply_scale(void) {
    adl_font_init();
    adl_wolf_theme_init();
}

/* ---- Odak halkasi: klavye ile gezinirken odaktaki ogeler cizgiyle
 * belirir. Taban tema once uygulanir, halka sonradan eklenir. ---- */
static void ring_apply(lv_theme_t *th, lv_obj_t *obj) {
    (void)th;
    if (s_ring_on) lv_obj_add_style(obj, &s_ring_style, LV_STATE_FOCUSED);
}

void adl_wolf_theme_init(void) {
    lv_display_t *d = lv_display_get_default();

    if (!s_ring_ready) {
        lv_style_init(&s_ring_style);
        lv_style_set_outline_width(&s_ring_style, 3);
        lv_style_set_outline_pad(&s_ring_style, 2);
        lv_style_set_radius(&s_ring_style, 8);
        s_ring_ready = 1;
    }
    /* Vurgu rengi degisince halka rengini de tazele */
    lv_style_set_outline_color(&s_ring_style, lv_color_hex(s_accent));

    /* 0.6 duzeltmesi: burada 'true' SABIT verilmisti; acik tema secildiginde
     * bile LVGL taban bilesenleri (button/textarea/switch) koyu kaliyordu.
     * Artik tema kipini palet belirler. */
    s_base_theme = lv_theme_default_init(
        d,
        lv_color_hex(ADL_ACCENT),
        lv_color_hex(ADL_ACCENT_2),
        s_contrast ? true : !s_light,
        s_font ? s_font : LV_FONT_DEFAULT);

    /* Ustteki (halka) tema: parent'i taban; once parent, sonra bu uygulanir */
    lv_theme_t *ring = lv_theme_create();
    lv_theme_set_apply_cb(ring, ring_apply);
    lv_theme_set_parent(ring, s_base_theme);
    lv_display_set_theme(d, ring);
}
