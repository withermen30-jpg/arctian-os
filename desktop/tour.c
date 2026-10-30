#include "dsk.h"

static lv_obj_t *s_root;
static lv_obj_t *s_num;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static int s_idx;

static const char *T_T[4] = {
    "Masaustune hos geldiniz",
    "Baslat menusu",
    "Her sey Ayarlar'da",
    "Dosyalariniz"
};

static const char *T_B[4] = {
    "Simgelere cift tiklayarak uygulama acin. Alt+Tab pencereler arasinda gecis yapar.",
    "Soldaki kurt simgesi tum uygulamalari listeler; arama kutusu hepsini bulur.",
    "Ekran, ag, ses, guncelleme ve sistem araclari tek yerde toplandi.",
    "Dosya Yoneticisi ile C: ve D: (USB) gezinin; Ctrl+C / Ctrl+V ile kopyalayin."
};

static void tour_close(void) {
    if (s_root) { lv_obj_delete(s_root); s_root = 0; }
    kv_set("tour_done", "1");
}

static void tour_render(void) {
    if (!s_title) return;
    lv_label_set_text(s_title, T_T[s_idx]);
    lv_label_set_text(s_body, T_B[s_idx]);
    if (s_num) lv_label_set_text_fmt(s_num, "%d / 4", s_idx + 1);
}

static void tour_next_cb(lv_event_t *e) {
    (void)e;
    if (s_idx >= 3) { tour_close(); return; }
    s_idx++;
    tour_render();
}

static void tour_skip_cb(lv_event_t *e) {
    (void)e;
    tour_close();
}

void tour_show(void) {
    if (s_root) return;

    int sw = g_asi->gfx->width();
    int sh = g_asi->gfx->height();

    s_root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_hex(ADL_BG), 0);
    lv_obj_set_style_bg_opa(s_root, 235, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(s_root);

    int w = ui_s(560), h = ui_s(300);
    lv_obj_t *box = lv_obj_create(s_root);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, w, h);
    lv_obj_center(box);
    lv_obj_set_style_radius(box, ui_s(16), 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, ui_s(1), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_shadow_width(box, ui_s(24), 0);
    lv_obj_set_style_shadow_color(box, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(box, 150, 0);

    s_num = lv_label_create(box);
    lv_obj_set_style_text_color(s_num, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_align(s_num, LV_ALIGN_TOP_RIGHT, -ui_s(20), ui_s(16));

    lv_obj_t *ic = make_icon(box, SVG_SETTINGS, ui_s(48));
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, ui_s(28));

    s_title = lv_label_create(box);
    lv_obj_set_style_text_color(s_title, lv_color_hex(ADL_TEXT), 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, ui_s(90));

    s_body = lv_label_create(box);
    lv_obj_set_style_text_color(s_body, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_set_width(s_body, w - ui_s(80));
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_body, LV_ALIGN_TOP_MID, 0, ui_s(132));

    lv_obj_t *skip = lv_button_create(box);
    lv_obj_set_size(skip, ui_s(110), ui_s(38));
    lv_obj_align(skip, LV_ALIGN_BOTTOM_LEFT, ui_s(24), -ui_s(20));
    lv_obj_set_style_radius(skip, ui_s(9), 0);
    lv_obj_set_style_bg_color(skip, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_t *sl = lv_label_create(skip);
    lv_label_set_text(sl, "Atla");
    lv_obj_center(sl);
    lv_obj_add_event_cb(skip, tour_skip_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *next = lv_button_create(box);
    lv_obj_set_size(next, ui_s(130), ui_s(38));
    lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, -ui_s(24), -ui_s(20));
    lv_obj_set_style_radius(next, ui_s(9), 0);
    lv_obj_set_style_bg_color(next, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_t *nl = lv_label_create(next);
    lv_label_set_text(nl, "Ileri");
    lv_obj_center(nl);
    lv_obj_add_event_cb(next, tour_next_cb, LV_EVENT_CLICKED, 0);

    s_idx = 0;
    tour_render();
}


/* ------------------------------------------------------------------ */
/* "Ne yapabilirim?" yardim paneli                                     */
/* Kisayollar + ornek isler tek ekranda. Ac/Kapa: Ctrl+Alt+H ya da     */
/* Baslat menusundeki "?" dugmesi.                                     */
/* ------------------------------------------------------------------ */

static lv_obj_t *h_root;

static const char *H_KEYS =
    "Klavye kısayolları\n"
    "  Alt+Tab           pencereler arası geçiş\n"
    "  Ctrl+Alt+T        terminal aç\n"
    "  Ctrl+Alt+H        bu yardımı aç/kapat\n"
    "  Ctrl+C / Ctrl+V   kopyala / yapıştır\n"
    "  Ctrl+X / Ctrl+Z   kes / geri al\n"
    "  Tab / Enter       klavye ile gezin / aç";

static const char *H_TASKS =
    "Örnek işler\n"
    "  Dosya kopyala: Dosya Yöneticisi'nde satır seç,\n"
    "    Ctrl+C, hedef klasöre git, Ctrl+V\n"
    "  Dosya aç: çift tıkla — uzantısına göre uygulama\n"
    "  Kısayol ekle: Başlat'ta uygulamaya sağ tıkla\n"
    "  Ekran görüntüsü: Hızlı Ayarlar → Ekran görüntüsü\n"
    "  Yazı büyüt: Ayarlar → Erişilebilirlik\n"
    "  Pencere yarıya: başlığı ekran kenarına sürükle";

void help_close(void) {
    if (h_root) { lv_obj_delete(h_root); h_root = 0; }
}

static void help_close_cb(lv_event_t *e) {
    (void)e;
    help_close();
}

void help_toggle(void) {
    if (h_root) { help_close(); return; }

    h_root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(h_root);
    lv_obj_set_size(h_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(h_root, lv_color_hex(ADL_BG), 0);
    lv_obj_set_style_bg_opa(h_root, 225, 0);
    lv_obj_add_flag(h_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(h_root);

    int w = ui_s(760), h = ui_s(430);
    lv_obj_t *box = lv_obj_create(h_root);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, w, h);
    lv_obj_center(box);
    lv_obj_set_style_radius(box, ui_s(16), 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, ui_s(1), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_shadow_width(box, ui_s(24), 0);
    lv_obj_set_style_shadow_color(box, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(box, 150, 0);

    lv_obj_t *t = lv_label_create(box);
    lv_label_set_text(t, "Ne yapabilirim?");
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_text_font(t, adl_font(), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, ui_s(20));

    /* solda kisayollar, sagda ornek isler */
    lv_obj_t *lk = lv_label_create(box);
    lv_label_set_text(lk, H_KEYS);
    lv_obj_set_style_text_color(lk, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_label_set_long_mode(lk, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lk, w / 2 - ui_s(40));
    lv_obj_align(lk, LV_ALIGN_TOP_LEFT, ui_s(30), ui_s(64));

    lv_obj_t *tk = lv_label_create(box);
    lv_label_set_text(tk, H_TASKS);
    lv_obj_set_style_text_color(tk, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_label_set_long_mode(tk, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(tk, w / 2 - ui_s(40));
    lv_obj_align(tk, LV_ALIGN_TOP_RIGHT, -ui_s(30), ui_s(64));

    lv_obj_t *sep = lv_obj_create(box);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, ui_s(1), h - ui_s(120));
    lv_obj_set_style_bg_color(sep, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, ui_s(60));

    lv_obj_t *close = lv_button_create(box);
    lv_obj_set_size(close, ui_s(120), ui_s(40));
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -ui_s(18));
    lv_obj_set_style_radius(close, ui_s(10), 0);
    lv_obj_set_style_bg_color(close, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_t *cl = lv_label_create(close);
    lv_label_set_text(cl, "Kapat");
    lv_obj_center(cl);
    lv_obj_add_event_cb(close, help_close_cb, LV_EVENT_CLICKED, 0);
}
