#include "dsk.h"

#define SW_W 1440
#define SW_H 810

#define C_CREAM   0xFCF0DD
#define C_AMBER   0xFFBD59
#define C_ORANGE  0xFF751F
#define C_BLUE    0x1800AD
#define C_DARK    0x2C2C35
#define C_PURPLE  0x5E17EB
#define C_BLACK   0x000000

static lv_obj_t *s_root;
static lv_obj_t *s_stage;
static lv_obj_t *s_slides[5];
static int s_cur;
static int s_s;
static int s_ox, s_oy;
static int s_sw, s_sh;
static int s_theme;
static lv_obj_t *s_border;
static lv_obj_t *s_kbd_lbl;
static int s_kbd;
static lv_group_t *s_group;

static lv_font_t *F_ANTON_L, *F_ANTON_XL;
static lv_font_t *F_ABRIL_S, *F_ABRIL_M, *F_ABRIL_B;
static lv_font_t *F_ARIMO_S, *F_ARIMO_M, *F_ARIMO_K, *F_ARIMO_L;
static lv_font_t *F_SERIF;

static uint32_t *s_svg_buf[5];
static lv_obj_t *s_svg_can[5];

static int P(int v) { return (int)((int64_t)v * s_s / 1000); }

static void ensure_svg(int i) {
    if (s_svg_buf[i]) return;
    uint32_t *native = 0;
    int nw = 0, nh = 0;
    if (png_decode(arctian_setup_png[i], arctian_setup_png_len[i],
                   g_asi->mem->alloc, g_asi->mem->free, &native, &nw, &nh) != 0)
        return;
    uint32_t *scaled = scale_hq(native, nw, nh, s_sw, s_sh);
    g_asi->mem->free(native);
    if (!scaled) return;
    s_svg_buf[i] = scaled;
    lv_obj_t *can = lv_canvas_create(s_slides[i]);
    lv_obj_remove_style_all(can);
    lv_obj_remove_flag(can, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(can, scaled, s_sw, s_sh, LV_COLOR_FORMAT_XRGB8888);
    lv_obj_set_pos(can, 0, 0);
    lv_obj_move_background(can);
    s_svg_can[i] = can;
}

static void release_svg(int i) {
    if (!s_svg_buf[i]) return;
    if (s_svg_can[i]) {
        lv_obj_delete(s_svg_can[i]);
        s_svg_can[i] = 0;
    }
    g_asi->mem->free(s_svg_buf[i]);
    s_svg_buf[i] = 0;
}

static lv_obj_t *mk_slide(void) {
    lv_obj_t *s = lv_obj_create(s_stage);
    lv_obj_remove_style_all(s);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s, s_sw, s_sh);
    lv_obj_set_pos(s, 0, 0);
    lv_obj_set_style_bg_color(s, lv_color_hex(C_CREAM), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    return s;
}

static lv_obj_t *txt(lv_obj_t *p, int x, int y, int w, int fs, const char *t,
                     lv_font_t *f, uint32_t col, int center) {
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_style_text_align(l, center ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    int lead = P(fs) * 140 / 100 - f->line_height;
    lv_obj_set_style_text_line_space(l, lead, 0);
    lv_obj_set_pos(l, P(x), P(y) + lead / 2);
    lv_obj_set_width(l, P(w));
    return l;
}

static void anim_x(void *var, int32_t v) { lv_obj_set_x((lv_obj_t *)var, v); }

static int s_release_idx = -1;

static void release_ready(lv_anim_t *an) {
    (void)an;
    if (s_release_idx >= 0) {
        release_svg(s_release_idx);
        s_release_idx = -1;
    }
}

static void goto_slide(int n) {
    if (n < 0 || n >= 5 || n == s_cur) return;
    ensure_svg(n);
    lv_obj_t *a = s_slides[s_cur];
    lv_obj_t *b = s_slides[n];
    int w = s_sw;

    lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(b, w);
    lv_obj_move_foreground(b);

    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, a);
    lv_anim_set_values(&an, 0, -w);
    lv_anim_set_duration(&an, 1000);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&an, anim_x);
    s_release_idx = s_cur;
    lv_anim_set_ready_cb(&an, release_ready);
    lv_anim_start(&an);

    lv_anim_t bn;
    lv_anim_init(&bn);
    lv_anim_set_var(&bn, b);
    lv_anim_set_values(&bn, w, 0);
    lv_anim_set_duration(&bn, 1000);
    lv_anim_set_path_cb(&bn, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&bn, anim_x);
    lv_anim_start(&bn);

    s_cur = n;
}

static void next_cb(lv_event_t *e) { (void)e; goto_slide(s_cur + 1); }
static void skip_cb(lv_event_t *e) { (void)e; goto_slide(s_cur + 1); }

static void finish_cb(lv_event_t *e) {
    (void)e;
    kv_set("setup_done", "1");
    kv_set("tour_done", "1");
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, 0);
    if (s_group) {
        lv_group_delete(s_group);
        s_group = 0;
    }
    for (int i = 0; i < 5; i++) {
        release_svg(i);
        s_slides[i] = 0;
    }
    s_border = 0;
    s_kbd_lbl = 0;
    if (s_root) { lv_obj_delete(s_root); s_root = 0; }
}

static void border_update(void) {
    if (!s_border) return;
    int x = (s_theme == 0) ? 771 : 1107;
    int w = (s_theme == 0) ? 302 : 301;
    lv_obj_set_pos(s_border, P(x), P(330));
    lv_obj_set_size(s_border, P(w), P(219));
}

static void theme_cb(lv_event_t *e) {
    s_theme = (int)(intptr_t)lv_event_get_user_data(e);
    kv_set("theme", s_theme ? "1" : "0");
    border_update();
}

static const char *s_kbd_names[3] = { "Türkçe Q", "Türkçe F", "İngilizce Q" };

static void kbd_cb(lv_event_t *e) {
    (void)e;
    s_kbd = (s_kbd + 1) % 3;
    static const char *codes[3] = { "trq", "trf", "enq" };
    if (s_kbd_lbl) lv_label_set_text(s_kbd_lbl, s_kbd_names[s_kbd]);
    kv_set("kbd", codes[s_kbd]);
    if (g_asi->input->kbd_layout) g_asi->input->kbd_layout(s_kbd);
}

static void build_slides(void) {
    lv_obj_t *s;

    s = mk_slide();
    s_slides[0] = s;
    ensure_svg(0);
    txt(s, -137, 246, 902, 110, "Kurulum Özelleştirme", F_ANTON_L, C_BLACK, 1);
    txt(s, 128, 609, 370, 40, "Hadi seni tanıyalım!", F_ABRIL_S, C_BLUE, 1);
    txt(s, 866, 136, 511, 92, "Adın nedir?", F_ARIMO_L, C_BLACK, 1);

    lv_obj_t *ta = lv_textarea_create(s);
    lv_obj_remove_style_all(ta);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 27);
    lv_textarea_set_placeholder_text(ta, "Buraya yaz...");
    lv_obj_set_pos(ta, P(936), P(348));
    lv_obj_set_size(ta, P(373), P(71));
    lv_obj_set_style_text_font(ta, F_SERIF, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0x484B4E), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0x484B4E), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_font(ta, F_SERIF, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_all(ta, 0, 0);

    s_group = lv_group_create();
    lv_group_add_obj(s_group, ta);
    lv_group_focus_obj(ta);
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, s_group);

    txt(s, 1073, 651, 295, 67, "Devam Et", F_ABRIL_B, C_BLACK, 1);
    lv_obj_t *b0 = lv_obj_create(s);
    lv_obj_remove_style_all(b0);
    lv_obj_remove_flag(b0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(b0, P(982), P(638));
    lv_obj_set_size(b0, P(478), P(125));
    lv_obj_set_style_bg_opa(b0, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b0, 0, 0);
    lv_obj_add_flag(b0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b0, next_cb, LV_EVENT_CLICKED, 0);
    lv_obj_move_foreground(b0);

    s = mk_slide();
    s_slides[1] = s;
    txt(s, 0, 70, 588, 81, "Klavye\ndüzenin nedir?", F_ARIMO_K, C_BLACK, 1);
    s_kbd_lbl = txt(s, 133, 396, 251, 58, "Türkçe Q", F_ABRIL_M, C_BLACK, 1);
    lv_obj_t *kb = lv_obj_create(s);
    lv_obj_remove_style_all(kb);
    lv_obj_remove_flag(kb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(kb, P(54), P(360));
    lv_obj_set_size(kb, P(510), P(151));
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(kb, kbd_cb, LV_EVENT_CLICKED, 0);
    lv_obj_move_foreground(kb);

    txt(s, 698, 136, 741, 110, "Bakalım Klavyen Hangisi?", F_ANTON_L, C_BLACK, 1);
    txt(s, 884, 500, 370, 40, "Hadi seni tanıyalım!", F_ABRIL_S, C_BLUE, 1);
    txt(s, 54, 679, 295, 67, "Devam Et", F_ABRIL_B, C_BLACK, 1);
    lv_obj_t *b1 = lv_obj_create(s);
    lv_obj_remove_style_all(b1);
    lv_obj_remove_flag(b1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(b1, P(-36), P(666));
    lv_obj_set_size(b1, P(478), P(125));
    lv_obj_set_style_bg_opa(b1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b1, 0, 0);
    lv_obj_add_flag(b1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b1, next_cb, LV_EVENT_CLICKED, 0);
    lv_obj_move_foreground(b1);

    s = mk_slide();
    s_slides[2] = s;
    txt(s, 0, 171, 627, 110, "Bu Olmadan OS mi olur?", F_ANTON_L, C_BLACK, 1);
    txt(s, 128, 586, 370, 40, "Hadi seni tanıyalım!", F_ABRIL_S, C_BLUE, 1);
    txt(s, 725, 30, 714, 92, "Hangisi size uygun?", F_ARIMO_L, C_BLACK, 1);

    s_border = lv_obj_create(s);
    lv_obj_remove_style_all(s_border);
    lv_obj_remove_flag(s_border, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_border, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_border, P(8), 0);
    lv_obj_set_style_border_color(s_border, lv_color_hex(C_PURPLE), 0);
    lv_obj_set_style_border_side(s_border, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_radius(s_border, P(22), 0);
    border_update();

    for (int i = 0; i < 2; i++) {
        lv_obj_t *c = lv_obj_create(s);
        lv_obj_remove_style_all(c);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(c, P(i ? 1107 : 771), P(330));
        lv_obj_set_size(c, P(301), P(219));
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, theme_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_move_foreground(c);
    }

    txt(s, 1073, 651, 295, 67, "Devam Et", F_ABRIL_B, C_BLACK, 1);
    lv_obj_t *b2 = lv_obj_create(s);
    lv_obj_remove_style_all(b2);
    lv_obj_remove_flag(b2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(b2, P(982), P(638));
    lv_obj_set_size(b2, P(478), P(125));
    lv_obj_set_style_bg_opa(b2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b2, 0, 0);
    lv_obj_add_flag(b2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b2, next_cb, LV_EVENT_CLICKED, 0);
    lv_obj_move_foreground(b2);

    s = mk_slide();
    s_slides[3] = s;
    txt(s, 0, 10, 547, 58, "İstersen bu qr ile bilgisayarı daha iyi hale getirebilirsin",
        F_ARIMO_S, C_BLACK, 1);
    txt(s, 115, 414, 359, 52, "Dynamic QR", F_SERIF, C_BLACK, 1);
    txt(s, 698, 136, 741, 110, "Giriş Yapmaya Ne Dersin?", F_ANTON_L, C_BLACK, 1);
    txt(s, 884, 500, 370, 40, "Hadi seni tanıyalım!", F_ABRIL_S, C_BLUE, 1);
    txt(s, 54, 679, 295, 67, "Devam Et", F_ABRIL_B, C_BLACK, 1);
    lv_obj_t *b3 = lv_obj_create(s);
    lv_obj_remove_style_all(b3);
    lv_obj_remove_flag(b3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(b3, P(-36), P(666));
    lv_obj_set_size(b3, P(478), P(125));
    lv_obj_set_style_bg_opa(b3, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b3, 0, 0);
    lv_obj_add_flag(b3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b3, next_cb, LV_EVENT_CLICKED, 0);
    lv_obj_move_foreground(b3);

    txt(s, 1154, 658, 129, 67, "Atla", F_ABRIL_B, C_BLACK, 1);
    lv_obj_t *b3a = lv_obj_create(s);
    lv_obj_remove_style_all(b3a);
    lv_obj_remove_flag(b3a, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(b3a, P(980), P(645));
    lv_obj_set_size(b3a, P(478), P(125));
    lv_obj_set_style_bg_opa(b3a, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b3a, 0, 0);
    lv_obj_add_flag(b3a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b3a, skip_cb, LV_EVENT_CLICKED, 0);
    lv_obj_move_foreground(b3a);

    s = mk_slide();
    s_slides[4] = s;
    txt(s, 267, 131, 905, 136, "Kurulum Tamam!", F_ANTON_XL, C_BLACK, 1);
    txt(s, 462, 631, 514, 71, "Gezintiye başla", F_ARIMO_M, C_BLACK, 1);
    lv_obj_t *b4 = lv_obj_create(s);
    lv_obj_remove_style_all(b4);
    lv_obj_remove_flag(b4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(b4, P(447), P(602));
    lv_obj_set_size(b4, P(544), P(172));
    lv_obj_set_style_bg_opa(b4, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b4, 0, 0);
    lv_obj_add_flag(b4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b4, finish_cb, LV_EVENT_CLICKED, 0);
    lv_obj_move_foreground(b4);
}

void setup_wizard_show(void) {
    if (s_root) return;

    int sw = g_asi->gfx->width();
    int sh = g_asi->gfx->height();
    int sx = sw * 1000 / SW_W;
    int sy = sh * 1000 / SW_H;
    s_s = sx < sy ? sx : sy;
    s_sw = P(SW_W);
    s_sh = P(SW_H);
    s_ox = (sw - s_sw) / 2;
    s_oy = (sh - s_sh) / 2;

    if (!F_ANTON_L) {
        F_ANTON_L = lv_tiny_ttf_create_data(font_anton, font_anton_len, P(110));
        F_ANTON_XL = lv_tiny_ttf_create_data(font_anton, font_anton_len, P(136));
        F_ABRIL_S = lv_tiny_ttf_create_data(font_abril, font_abril_len, P(40));
        F_ABRIL_M = lv_tiny_ttf_create_data(font_abril, font_abril_len, P(58));
        F_ABRIL_B = lv_tiny_ttf_create_data(font_abril, font_abril_len, P(67));
        F_ARIMO_S = lv_tiny_ttf_create_data(font_arimo, font_arimo_len, P(58));
        F_ARIMO_M = lv_tiny_ttf_create_data(font_arimo, font_arimo_len, P(71));
        F_ARIMO_K = lv_tiny_ttf_create_data(font_arimo, font_arimo_len, P(81));
        F_ARIMO_L = lv_tiny_ttf_create_data(font_arimo, font_arimo_len, P(92));
        F_SERIF = lv_tiny_ttf_create_data(font_serif, font_serif_len, P(52));
    }

    s_root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_hex(C_CREAM), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_move_foreground(s_root);

    s_stage = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_stage);
    lv_obj_remove_flag(s_stage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_stage, s_sw, s_sh);
    lv_obj_set_pos(s_stage, s_ox, s_oy);

    s_cur = 0;
    s_theme = kv_get_int("theme", 1);
    s_kbd = 0;
    {
        const char *k = kv_get("kbd", "trq");
        s_kbd = (k[0] == 'e') ? 2 : ((k[2] == 'f') ? 1 : 0);
    }
    s_border = 0;
    s_kbd_lbl = 0;
    build_slides();
    border_update();
    if (s_kbd_lbl) lv_label_set_text(s_kbd_lbl, s_kbd_names[s_kbd]);

    for (int i = 1; i < 5; i++)
        lv_obj_add_flag(s_slides[i], LV_OBJ_FLAG_HIDDEN);
}
