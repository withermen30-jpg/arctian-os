/**
 * Arctian 0.7 - Onay penceresi
 *
 * Pencereyi yalnizca masaustu cizer; uygulamanin kendi kodu "izin verildi"
 * dondurmez. Karar ucludur: Bir kez / Her zaman / Reddet. Kapatmak ve Esc
 * Reddet sayilir.
 *
 * Bekleme dongusu LVGL'in kendi `lv_timer_handler` icine ic ice girmez
 * (o fonksiyon ayni anda tekrar cagrilamaz). Onun yerine girdi + yenileme
 * asamalarini elle calistiririz: indev okuma, lv_refr_now ve kirli bolgenin
 * sunulmasi. Uzun sure cevap gelmezse (dongu limiti) guvenlik acisindan
 * Reddet ile biter.
 *
 * Oz-testler icin `consent_set_hook` kurulur; kanca varken pencere hic
 * cizilmez, karar dogrudan kanca doner.
 */
#include "dsk.h"
#include "caps.h"

#define CONSENT_ITERS 3000      /* ~48 sn (adim basi 16 ms): guvenlik siniri */

static int  (*s_hook)(int type, const char *src);
static int   s_answer;
static int   s_open;
static lv_obj_t *s_overlay;
static lv_group_t *s_group;
static lv_group_t *s_prev_group;

void consent_set_hook(int (*fn)(int type, const char *src)) { s_hook = fn; }
int  consent_is_open(void) { return s_open; }
int  consent_answer_now(void) { return s_answer; }

static const char *title_of(void) {
    const char *t = caps_subject_title();
    return (t && t[0]) ? t : "Uygulama";
}

void consent_answer(int v) { if (s_open) s_answer = v; }

static void consent_destroy(void) {
    if (s_group) {
        lv_indev_t *kp = lv_port_get_keypad();
        if (kp && lv_indev_get_group(kp) == s_group)
            lv_indev_set_group(kp, s_prev_group);
        lv_group_remove_all_objs(s_group);
        lv_group_del(s_group);
        s_group = 0;
    }
    if (s_overlay) { lv_obj_delete(s_overlay); s_overlay = 0; }
    s_open = 0;
}

static void btn_cb(lv_event_t *e) {
    s_answer = (int)(intptr_t)lv_event_get_user_data(e);
}

/* Pencere acikken Esc = Reddet (panel ve katman uzerinde ayni isleyici). */
static void key_cb(lv_event_t *e) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC) s_answer = PERM_DENY;
}

static lv_obj_t *mk_button(lv_obj_t *panel, int x, const char *txt,
                           uint32_t col, int answer) {
    lv_obj_t *b = lv_button_create(panel);
    lv_obj_set_size(b, 132, 44);
    lv_obj_set_pos(b, x, 168);
    lv_obj_set_style_radius(b, 9, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_t *l = label(b, txt, answer == PERM_DENY ? ADL_TEXT : 0xFFFFFF);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)answer);
    if (s_group) lv_group_add_obj(s_group, b);
    return b;
}

/* Pencereyi cizer, s_answer'i -1'de birakir. 0 = basarisiz. */
static int consent_show(int type, const char *src) {
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return 0;

    s_overlay = lv_obj_create(scr);
    base_obj(s_overlay, 0x000000, 0, 0x000000);
    lv_obj_set_style_bg_opa(s_overlay, 140, 0);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(s_overlay);
    base_obj(panel, ADL_SURFACE, 16, ADL_BORDER);
    lv_obj_set_style_shadow_width(panel, 30, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(panel, 190, 0);
    lv_obj_set_size(panel, 560, 240);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(panel, 22, 0);

    lv_obj_t *ti = label(panel, "Izin gerekiyor", ADL_ACCENT);
    lv_obj_align(ti, LV_ALIGN_TOP_LEFT, 0, 0);

    char msg[400];
    int n = 0;
    const char *t = title_of();
    const char *p = t;
    while (*p && n < (int)sizeof(msg) - 1) msg[n++] = *p++;
    msg[n++] = ' ';

    if (type == PERM_T_EXEC) {
        const char *q = "su komutu calistirmak istiyor:  ";
        while (*q && n < (int)sizeof(msg) - 2) msg[n++] = *q++;
        msg[n++] = '"';
        for (p = src; p && *p && n < (int)sizeof(msg) - 2; p++) msg[n++] = *p;
        msg[n++] = '"';
    } else if (type == PERM_T_NET) {
        const char *q = "aga cikmak istiyor.";
        while (*q && n < (int)sizeof(msg) - 2) msg[n++] = *q++;
    } else {
        const char *q = "su dosyaya erismek istiyor:  ";
        while (*q && n < (int)sizeof(msg) - 2) msg[n++] = *q++;
        msg[n++] = '"';
        for (p = src; p && *p && n < (int)sizeof(msg) - 2; p++) msg[n++] = *p;
        msg[n++] = '"';
    }
    msg[n] = 0;

    lv_obj_t *ml = label(panel, msg, ADL_TEXT_DIM);
    lv_label_set_long_mode(ml, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ml, 516);
    lv_obj_set_style_text_align(ml, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(ml, LV_ALIGN_TOP_LEFT, 0, 44);

    s_group = lv_group_create();
    mk_button(panel, 0,   "Bir kez",   ADL_ACCENT,   PERM_ONCE);
    mk_button(panel, 148, "Her zaman", 0x2E7D4F,     PERM_ALWAYS);
    mk_button(panel, 296, "Reddet",    0x9E3B3B,     PERM_DENY);

    lv_obj_add_event_cb(panel, key_cb, LV_EVENT_KEY, 0);
    lv_obj_add_event_cb(s_overlay, key_cb, LV_EVENT_KEY, 0);

    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) {
        s_prev_group = lv_indev_get_group(kp);
        lv_indev_set_group(kp, s_group);
    }
    s_open = 1;
    return 1;
}

/* Girisi kendi dongumuzde isleriz (LVGL timer kilitli oldugu icin). */
static int consent_pump(void) {
    for (int i = 0; i < CONSENT_ITERS && s_answer < 0; i++) {
        lv_port_tick_advance(16);

        lv_indev_t *m = lv_port_get_indev();
        if (m) lv_indev_read(m);
        lv_indev_t *k = lv_port_get_keypad();
        if (k) lv_indev_read(k);

        lv_refr_now(lv_display_get_default());

        int dx = 0, dy = 0, dw = 0, dh = 0;
        if (lv_port_take_dirty(&dx, &dy, &dw, &dh) && dw > 0 && dh > 0) {
            if (g_asi && g_asi->gfx) {
                if (g_asi->gfx->present_rect) g_asi->gfx->present_rect(dx, dy, dw, dh);
                else if (g_asi->gfx->present) g_asi->gfx->present();
            }
        }

        /* Dongu gercek zamanda ilerler: her tur 16 ms uyur, boylece pencere
         * CONSENT_ITERS*16 ms sureyle acik kalir. Yalnizca sahte zaman
         * ilerletilirse 3000 tur birkac saniyede tukenip pencere kullanici
         * gormeden kapanir. */
        if (g_asi && g_asi->time && g_asi->time->sleep_ms)
            g_asi->time->sleep_ms(16);
    }
    return s_answer;
}

int consent_ask(int type, const char *src) {
    /* Oz-test / otomatik mod: pencere cizilmez. */
    if (s_hook) return s_hook(type, src);

    /* Bekleme dongusu olaylari islerken ikinci bir soru sorulursa eski
     * diyaloğun üstüne yenisi biner, karar iki soru arasinda karisirdi.
     * Ic ice soruyu basindan reddediyoruz (fail-closed). */
    if (s_open) return PERM_DENY;

    s_answer = -1;
    if (!consent_show(type, src)) return PERM_DENY;
    int v = consent_pump();
    consent_destroy();
    if (v < 0) v = PERM_DENY;             /* sure doldu -> inkar */
    return v;
}

/* ---- Oz-test yardimcilari ---- */
/* Gercek pencereyi ciz, karari bekleme dongusu olmadan al. */
int consent_test_show(int type, const char *src) {
    s_hook = 0;
    s_answer = -1;
    if (consent_show(type, src) != 1) return -1;
    return 0;
}

void consent_test_end(void) {
    consent_destroy();
    s_answer = -1;
}

/* Esc yolunu sinar: olay LVGL uzerinden ayni isleyiciye duser. */
void consent_send_esc(void) {
    if (!s_open) return;
    uint32_t k = LV_KEY_ESC;
    if (s_overlay) lv_obj_send_event(s_overlay, LV_EVENT_KEY, &k);
    else s_answer = PERM_DENY;
}