#include "dsk.h"


/* ---------------------------------------------------------------- */
/* Baslat menusu                                                     */
/* ---------------------------------------------------------------- */

static lv_obj_t *s_sm_search;
static lv_obj_t *s_sm_browse;
static lv_obj_t *s_sm_results;
static lv_obj_t *s_sm_all;
static lv_obj_t *s_sm_all_list;
static lv_obj_t *s_sm_tabs[5];
static int       s_sm_cat = 0;
static lv_group_t *s_sm_group;
static lv_group_t *s_sm_prev_group;
static int s_sm_first = -1;

/* "Tumu" satirlari ile uygulama indekslerinin eslesmesi (sag tik)     */
#define SM_ALL_MAX 64
static int s_sm_all_idx[SM_ALL_MAX];
static int s_sm_all_rown;

static void sm_rebuild_results(const char *q);
static int  sm_line_h(void);
static int  sm_row_h(int base);

void start_menu_anim_cb(void *var, int32_t value) { lv_obj_set_y((lv_obj_t *)var, value); }


/* "Ne yapabilirim?" yardim paneli — menuyu kapatip paneli acar     */
static void help_btn_cb(lv_event_t *e) {
    (void)e;
    if (g_start_menu) lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    help_toggle();
}

/* Asistan panelini acar (menuyu kapatir).      */
static void assistant_btn_cb(lv_event_t *e) {
    (void)e;
    if (g_start_menu) lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    assistant_toggle();
}


void toggle_start_menu(void) {
    bool hidden = lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    int sw = g_asi->gfx->width();
    int sh = g_asi->gfx->height();
    int mw = lv_obj_get_width(g_start_menu);
    int mh = lv_obj_get_height(g_start_menu);
    int target_x = (sw - mw) / 2;
    int target_y = sh - 90 - mh;

    if (!hidden) {
        /* kapat */
        lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
        lv_indev_t *kp = lv_port_get_keypad();
        if (kp && s_sm_group && lv_indev_get_group(kp) == s_sm_group)
            lv_indev_set_group(kp, s_sm_prev_group);
        return;
    }

    lv_obj_remove_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    if (s_sm_search) {
        lv_textarea_set_text(s_sm_search, "");
        sm_rebuild_results("");
        lv_indev_t *kp = lv_port_get_keypad();
        s_sm_prev_group = kp ? lv_indev_get_group(kp) : 0;
        if (kp && s_sm_group) {
            lv_group_remove_all_objs(s_sm_group);
            lv_group_add_obj(s_sm_group, s_sm_search);
            lv_indev_set_group(kp, s_sm_group);
            lv_group_focus_obj(s_sm_search);
        }
    }
    lv_obj_set_x(g_start_menu, target_x);
    lv_obj_set_y(g_start_menu, sh);
    lv_obj_move_foreground(g_start_menu); /* pencerelerin ustunde */

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_start_menu);
    lv_anim_set_values(&a, sh, target_y);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, start_menu_anim_cb);
    lv_anim_start(&a);
}



static void sm_launch_idx(int idx);

/* 0.6: Dock arama kutusu icin — menuyu ACAR ama klavye odagini CALMAZ.
 * Kullanici yazmaya dock kutusunda devam eder, menu canli filtrelenir.
 * Boylece gorev cubugundaki kutu "gercek" bir arama alanidir. */
void startmenu_search_live(const char *q) {
    if (!g_start_menu) return;
    int sh = g_asi->gfx->height();
    int mw = lv_obj_get_width(g_start_menu);
    int mh = lv_obj_get_height(g_start_menu);
    int target_x = (g_asi->gfx->width() - mw) / 2;
    int target_y = sh - 90 - mh;

    if (lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(g_start_menu, target_x);
        lv_obj_set_y(g_start_menu, target_y);
        lv_obj_move_foreground(g_start_menu);
    }
    if (!s_sm_search) return;
    /* metni yansit ama odagi degistirme */
    if (!s_eq(lv_textarea_get_text(s_sm_search), q ? q : ""))
        lv_textarea_set_text(s_sm_search, q ? q : "");
    sm_rebuild_results(q ? q : "");
}

/* Ilk sonucu ac (dock kutusunda Enter). Sonuc yoksa hicbir sey yapmaz. */
int startmenu_launch_first(void) {
    if (s_sm_first < 0) return 0;
    sm_launch_idx(s_sm_first);
    return 1;
}

/* Menu acik mi? (dock kutusu Enter davranisi icin) */
int startmenu_is_open(void) {
    return g_start_menu && !lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
}

void start_btn_cb(lv_event_t *e) { (void)e; toggle_start_menu(); }


static char sm_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}


static int sm_ci_has(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    for (const char *p = hay; *p; p++) {
        const char *h = p, *n = needle;
        while (*h && *n && sm_lower(*h) == sm_lower(*n)) { h++; n++; }
        if (!*n) return 1;
    }
    return 0;
}


static void sm_launch_idx(int idx) {
    if (idx < 0) return;
    if (s_sm_search) lv_textarea_set_text(s_sm_search, "");
    lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    open_registry_app(idx);
}


static void sm_result_cb(lv_event_t *e) {
    sm_launch_idx((int)(intptr_t)lv_event_get_user_data(e));
}


static lv_obj_t *sm_result_row(lv_obj_t *parent, int idx, const arctian_app_t *a) {
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(r, LV_PCT(100), sm_row_h(52));
    lv_obj_set_style_radius(r, ui_s(10), 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r, ui_s(1), 0);
    lv_obj_set_style_border_color(r, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(ADL_BORDER), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(r, lv_color_hex(ADL_ACCENT), LV_STATE_PRESSED);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, sm_result_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *ic = make_app_icon(r, a, ui_s(30));
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, ui_s(11), 0);
    lv_obj_t *t = label(r, a->title, ADL_TEXT);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, ui_s(52), -(sm_line_h() + 1) / 2);
    lv_obj_t *c = label(r, a->category[0] ? a->category : "Uygulama", ADL_TEXT_DIM);
    lv_obj_align(c, LV_ALIGN_LEFT_MID, ui_s(52), (sm_line_h() + 1) / 2);
    return r;
}


static void sm_rebuild_results(const char *q) {
    if (!s_sm_results) return;
    lv_obj_clean(s_sm_results);
    s_sm_first = -1;

    if (!q || !*q) {
        lv_obj_add_flag(s_sm_results, LV_OBJ_FLAG_HIDDEN);
        if (s_sm_all) lv_obj_add_flag(s_sm_all, LV_OBJ_FLAG_HIDDEN);
        if (s_sm_browse) lv_obj_remove_flag(s_sm_browse, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (s_sm_browse) lv_obj_add_flag(s_sm_browse, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_sm_results, LV_OBJ_FLAG_HIDDEN);

    int rows = 0;
    for (int i = 0; i < app_total(); i++) {
        const arctian_app_t *a = app_get(i);
        if (!a) continue;
        if (!sm_ci_has(a->title, q) && !sm_ci_has(a->name, q) &&
            !sm_ci_has(a->category, q)) continue;
        sm_result_row(s_sm_results, i, a);
        if (s_sm_first < 0) s_sm_first = i;
        rows++;
    }

    if (rows == 0) {
        lv_obj_t *l = label(s_sm_results, "Sonuc bulunamadi", ADL_TEXT_DIM);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(l, LV_PCT(100));
    }
}


static const char *sm_cat_key(int i) {
    static const char *k[5] = { "", "Araclar", "Ag", "Medya", "Sistem" };
    return (i >= 0 && i < 5) ? k[i] : "";
}

static void sm_all_style_tabs(void) {
    for (int i = 0; i < 5; i++) {
        if (!s_sm_tabs[i]) continue;
        lv_obj_set_style_bg_color(s_sm_tabs[i],
            lv_color_hex(i == s_sm_cat ? ADL_ACCENT : ADL_SURFACE_2), 0);
    }
}

static void sm_all_rebuild(void) {
    if (!s_sm_all_list) return;
    lv_obj_clean(s_sm_all_list);
    s_sm_all_rown = 0;
    const char *key = sm_cat_key(s_sm_cat);
    for (int i = 0; i < app_total(); i++) {
        const arctian_app_t *a = app_get(i);
        if (!a) continue;
        if (key[0] && !sm_ci_has(a->category, key)) continue;
        sm_result_row(s_sm_all_list, i, a);
        /* Sag tik menusu icin satir -> uygulama eslesmesi     */
        if (s_sm_all_rown < SM_ALL_MAX) s_sm_all_idx[s_sm_all_rown++] = i;
    }
    sm_all_style_tabs();
}


/* "Tumu" listesinde (mx,my) noktasindaki satirin uygulama indeksi.
 * Gorunur degil ya da satir yoksa -1. */
int sm_all_hit(int mx, int my) {
    if (!s_sm_all_list || !s_sm_all) return -1;
    if (lv_obj_has_flag(s_sm_all, LV_OBJ_FLAG_HIDDEN)) return -1;
    uint32_t n = lv_obj_get_child_count(s_sm_all_list);
    if (n > (uint32_t)s_sm_all_rown) n = (uint32_t)s_sm_all_rown;
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *r = lv_obj_get_child(s_sm_all_list, i);
        if (!r) continue;
        lv_area_t ar;
        lv_obj_get_coords(r, &ar);
        if (mx < ar.x1 || mx > ar.x2 || my < ar.y1 || my > ar.y2) continue;
        return s_sm_all_idx[i];
    }
    return -1;
}

static void sm_all_cat_cb(lv_event_t *e) {
    s_sm_cat = (int)(intptr_t)lv_event_get_user_data(e);
    sm_all_rebuild();
}

static void sm_all_open_cb(lv_event_t *e) {
    (void)e;
    if (s_sm_browse) lv_obj_add_flag(s_sm_browse, LV_OBJ_FLAG_HIDDEN);
    if (s_sm_results) lv_obj_add_flag(s_sm_results, LV_OBJ_FLAG_HIDDEN);
    if (s_sm_all) lv_obj_remove_flag(s_sm_all, LV_OBJ_FLAG_HIDDEN);
    sm_all_rebuild();
}

static void sm_search_cb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    sm_rebuild_results(lv_textarea_get_text(ta));
}


static void sm_search_ready_cb(lv_event_t *e) {
    (void)e;
    sm_launch_idx(s_sm_first);
}


/* Dock arama kutusundan canli arama: gorev cubugundaki kutuya yazilan
 * metin baslat menusunun arama alanina aktarilir ve sonuclar filtrelenir.
 * Menu kapaliysa acilir. */
void startmenu_filter(const char *q) {
    if (!q) q = "";
    if (g_start_menu && lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN))
        toggle_start_menu();
    if (!s_sm_search) return;
    lv_textarea_set_text(s_sm_search, q);
    sm_rebuild_results(q);
}


void sm_item_cb(lv_event_t *e) {
    const char *title = (const char *)lv_event_get_user_data(e);
    lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    open_app(title);
}


/* Iki satirli satirlarda (ad + alt bilgi) dikey aralik yazi yuksekliginden
 * gelir. Sabit ui_s(8)/ui_s(9) ofsetleri 720p'de (olcek 0.7) ust uste
 * biniyordu: yazi boyu ui_s ile olceklenmiyor. */
static int sm_line_h(void) {
    lv_font_t *f = adl_font();
    int lh = f ? lv_font_get_line_height(f) : 18;
    if (lh < 10) lh = 10;
    return lh;
}

static int sm_row_h(int base) {
    int need = 2 * sm_line_h() + 8;
    int h = ui_s(base);
    return h > need ? h : need;
}


lv_obj_t *sm_card(lv_obj_t *parent, int x, int y, int w, int h, int radius) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(c, radius, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(ADL_BORDER), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(c, lv_color_hex(ADL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, x, y);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}


void sm_pinned(lv_obj_t *parent, int x, int y, int w, int h,
                      const arctian_app_t *app, const char *svg,
                      const char *text, lv_event_cb_t cb, const void *ud) {
    lv_obj_t *t = sm_card(parent, x, y, w, h, ui_s(12));
    lv_obj_t *ic = app ? make_app_icon(t, app, ui_s(46)) : make_icon(t, svg ? svg : "", ui_s(46));
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, ui_s(20));
    lv_obj_t *l = label(t, text, ADL_TEXT);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -ui_s(14));
    lv_obj_add_event_cb(t, cb, LV_EVENT_CLICKED, (void *)ud);
    if (app && s_sm_n < SM_TILE_MAX) {
        s_sm_tiles[s_sm_n] = t;
        s_sm_apps[s_sm_n] = app;
        s_sm_n++;
    }
}


void sm_rec(lv_obj_t *parent, int x, int y, int w,
                   const char *svg, const char *name, const char *sub) {
    int lh = sm_line_h();
    lv_obj_t *r = sm_card(parent, x, y, w, sm_row_h(50), ui_s(10));
    lv_obj_t *ic = make_icon(r, svg, ui_s(24));
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, ui_s(12), 0);
    lv_obj_t *l = label(r, name, ADL_TEXT);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, ui_s(48), -(lh + 1) / 2);
    lv_obj_t *s = label(r, sub, ADL_TEXT_DIM);
    lv_obj_align(s, LV_ALIGN_LEFT_MID, ui_s(48), (lh + 1) / 2);
}


lv_obj_t *sm_header(lv_obj_t *parent, int x, int y, const char *text) {
    lv_obj_t *l = label(parent, text, ADL_TEXT);
    lv_obj_set_pos(l, x, y);
    return l;
}


static void sm_search_focus_cb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t *pill = lv_obj_get_parent(ta);
    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_set_style_border_color(pill,
        lv_color_hex(c == LV_EVENT_FOCUSED ? ADL_ACCENT : ADL_BORDER), 0);
}


void sm_search_bar(lv_obj_t *parent, int mw) {
    lv_obj_t *search = lv_obj_create(parent);
    lv_obj_remove_style_all(search);
    lv_obj_set_style_bg_color(search, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(search, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(search, ui_s(24), 0);
    lv_obj_set_style_border_width(search, ui_s(1), 0);
    lv_obj_set_style_border_color(search, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_size(search, mw - ui_s(48), ui_s(46));
    lv_obj_set_pos(search, ui_s(24), ui_s(20));
    lv_obj_remove_flag(search, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *sic = make_icon(search, SVG_SEARCH, ui_s(18));
    lv_obj_align(sic, LV_ALIGN_LEFT_MID, ui_s(16), 0);

    lv_obj_t *ta = lv_textarea_create(search);
    lv_obj_remove_style_all(ta);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 32);
    lv_textarea_set_placeholder_text(ta, "Aramak icin yazin");
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(ADL_TEXT_DIM), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_font(ta, adl_font(), 0);
    lv_obj_set_style_pad_all(ta, 0, 0);
    lv_obj_set_size(ta, mw - ui_s(48) - ui_s(60), ui_s(34));
    lv_obj_align(ta, LV_ALIGN_LEFT_MID, ui_s(44), 0);
    lv_obj_add_event_cb(ta, sm_search_cb, LV_EVENT_VALUE_CHANGED, 0);
    lv_obj_add_event_cb(ta, sm_search_ready_cb, LV_EVENT_READY, 0);
    lv_obj_add_event_cb(ta, sm_search_focus_cb, LV_EVENT_FOCUSED, 0);
    lv_obj_add_event_cb(ta, sm_search_focus_cb, LV_EVENT_DEFOCUSED, 0);
    s_sm_search = ta;
}


/* ---------------- Windows 11 tarzi baslat menusu ---------------- */
void build_start_menu(lv_obj_t *scr) {
    int sw = g_asi->gfx->width();
    int sh = g_asi->gfx->height();
    int mw = ui_s(640), mh = ui_s(560);

    for (int i = 0; i < SM_TILE_MAX; i++) { s_sm_tiles[i] = 0; s_sm_apps[i] = 0; }
    s_sm_n = 0;

    g_start_menu = lv_obj_create(scr);
    base_obj(g_start_menu, ADL_SURFACE, ui_s(16), ADL_BORDER);
    lv_obj_set_style_bg_opa(g_start_menu, 242, 0);
    lv_obj_set_style_shadow_width(g_start_menu, ui_s(32), 0);
    lv_obj_set_style_shadow_color(g_start_menu, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(g_start_menu, 180, 0);
    lv_obj_set_size(g_start_menu, mw, mh);
    lv_obj_set_pos(g_start_menu, (sw - mw) / 2, sh - ui_s(90) - mh);
    lv_obj_set_style_pad_all(g_start_menu, 0, 0);

    sm_search_bar(g_start_menu, mw);

    int by = ui_s(84);
    int bh = mh - by - ui_s(84);

    s_sm_browse = lv_obj_create(g_start_menu);
    lv_obj_remove_style_all(s_sm_browse);
    lv_obj_remove_flag(s_sm_browse, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_sm_browse, mw, bh);
    lv_obj_set_pos(s_sm_browse, 0, by);

    s_sm_results = lv_obj_create(g_start_menu);
    lv_obj_remove_style_all(s_sm_results);
    lv_obj_set_size(s_sm_results, mw, bh);
    lv_obj_set_pos(s_sm_results, 0, by);
    lv_obj_set_flex_flow(s_sm_results, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(s_sm_results, ui_s(4), 0);
    lv_obj_set_style_pad_left(s_sm_results, ui_s(24), 0);
    lv_obj_set_style_pad_right(s_sm_results, ui_s(24), 0);
    lv_obj_set_style_pad_row(s_sm_results, ui_s(8), 0);
    lv_obj_add_flag(s_sm_results, LV_OBJ_FLAG_HIDDEN);

    s_sm_all = lv_obj_create(g_start_menu);
    lv_obj_remove_style_all(s_sm_all);
    lv_obj_remove_flag(s_sm_all, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_sm_all, mw, bh);
    lv_obj_set_pos(s_sm_all, 0, by);
    lv_obj_add_flag(s_sm_all, LV_OBJ_FLAG_HIDDEN);

    static const char *tabn[5] = { "Tumu", "Araclar", "Ag", "Medya", "Sistem" };
    int tabw = (mw - ui_s(48) - ui_s(8) * 4) / 5;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *b = lv_button_create(s_sm_all);
        lv_obj_set_size(b, tabw, ui_s(30));
        lv_obj_set_pos(b, ui_s(24) + i * (tabw + ui_s(8)), ui_s(2));
        lv_obj_set_style_radius(b, ui_s(8), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(i == 0 ? ADL_ACCENT : ADL_SURFACE_2), 0);
        lv_obj_t *l = label(b, tabn[i], ADL_TEXT);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, sm_all_cat_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_sm_tabs[i] = b;
    }

    s_sm_all_list = lv_obj_create(s_sm_all);
    lv_obj_remove_style_all(s_sm_all_list);
    lv_obj_set_size(s_sm_all_list, mw, bh - ui_s(42));
    lv_obj_set_pos(s_sm_all_list, 0, ui_s(42));
    lv_obj_set_flex_flow(s_sm_all_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(s_sm_all_list, ui_s(24), 0);
    lv_obj_set_style_pad_right(s_sm_all_list, ui_s(24), 0);
    lv_obj_set_style_pad_row(s_sm_all_list, ui_s(8), 0);
    s_sm_cat = 0;

    sm_header(s_sm_browse, ui_s(24), 0, "Sabitlenmis");

    lv_obj_t *allb = lv_button_create(s_sm_browse);
    lv_obj_set_size(allb, ui_s(96), ui_s(26));
    lv_obj_set_pos(allb, mw - ui_s(120), 0);
    lv_obj_set_style_radius(allb, ui_s(8), 0);
    lv_obj_set_style_bg_color(allb, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_t *alll = label(allb, "Tumu >", ADL_TEXT);
    lv_obj_center(alll);
    lv_obj_add_event_cb(allb, sm_all_open_cb, LV_EVENT_CLICKED, 0);

    int tw = ui_s(189), th = ui_s(104), tgap = ui_s(12), tx0 = ui_s(24);
    if (app_total() > 0) {
        int n = app_total();
        if (n > 6) n = 6;
        for (int i = 0; i < n; i++) {
            const arctian_app_t *a = app_get(i);
            if (!a) continue;
            int col = i % 3, row = i / 3;
            sm_pinned(s_sm_browse, tx0 + col * (tw + tgap), ui_s(28) + row * (th + tgap),
                      tw, th, a, 0, a->title, reg_tile_cb, (void *)(intptr_t)i);
        }
    } else {
        static const char *pinned_svg[6] = { SVG_TERMINAL, SVG_FOLDER, SVG_GLOBE, SVG_SETTINGS, SVG_MEDIA, SVG_NOTES };
        static const char *pinned_label[6] = { "Terminal", "Dosya", "Tarayici", "Ayarlar", "Medya", "Notlar" };
        static const char *pinned_full[6] = { "Terminal", "Dosya Yoneticisi", "Tarayici", "Ayarlar", "Medya Oynatici", "Not Defteri" };
        for (int i = 0; i < 6; i++) {
            int col = i % 3, row = i / 3;
            sm_pinned(s_sm_browse, tx0 + col * (tw + tgap), ui_s(28) + row * (th + tgap),
                      tw, th, 0, pinned_svg[i], pinned_label[i], sm_item_cb, pinned_full[i]);
        }
    }

    sm_header(s_sm_browse, ui_s(24), ui_s(272), "Onerilen");
    sm_rec(s_sm_browse, ui_s(24), ui_s(300), mw - ui_s(48), SVG_FILE, "main.js", "WolfEngine js dosyası");

    lv_obj_t *sep = lv_obj_create(g_start_menu);
    lv_obj_remove_style_all(sep);
    lv_obj_set_style_bg_color(sep, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_size(sep, mw - ui_s(40), ui_s(1));
    lv_obj_set_pos(sep, ui_s(20), mh - ui_s(70));

    lv_obj_t *av = lv_obj_create(g_start_menu);
    lv_obj_remove_style_all(av);
    lv_obj_set_style_bg_color(av, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_bg_opa(av, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(av, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(av, ui_s(14), 0);
    lv_obj_set_style_shadow_color(av, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_shadow_opa(av, 120, 0);
    lv_obj_set_size(av, ui_s(34), ui_s(34));
    lv_obj_set_pos(av, ui_s(24), mh - ui_s(54));
    lv_obj_add_flag(av, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(av, switch_user_cb, LV_EVENT_CLICKED, 0);
    const char *g_un = arctian_auth_current();
    char g_ini = (g_un && g_un[0]) ? g_un[0] : 'K';
    if (g_ini >= 'a' && g_ini <= 'z') g_ini = (char)(g_ini - 32);
    char g_ini_s[2] = { g_ini, 0 };
    lv_obj_t *ai = label(av, g_ini_s, 0xFFFFFF);
    lv_obj_center(ai);
    lv_obj_t *ul = label(g_start_menu, (g_un && g_un[0]) ? g_un : "Kullanici", ADL_TEXT);
    int uy = mh - ui_s(58);
    lv_obj_set_pos(ul, ui_s(70), uy);
    lv_obj_t *us = label(g_start_menu, "Kullanıcı değiştir", ADL_TEXT_DIM);
    lv_obj_set_pos(us, ui_s(70), uy + sm_line_h() + 1);

    lv_obj_t *sb = lv_button_create(g_start_menu);
    lv_obj_set_size(sb, ui_s(42), ui_s(42));
    lv_obj_set_style_radius(sb, ui_s(12), 0);
    lv_obj_set_style_bg_color(sb, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_border_width(sb, ui_s(1), 0);
    lv_obj_set_style_border_color(sb, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_border_color(sb, lv_color_hex(ADL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_pos(sb, mw - ui_s(116), mh - ui_s(56));
    lv_obj_t *si = make_icon(sb, SVG_SETTINGS, ui_s(20));
    lv_obj_center(si);
    lv_obj_add_event_cb(sb, settings_btn_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *pw = lv_button_create(g_start_menu);
    lv_obj_set_size(pw, ui_s(42), ui_s(42));
    lv_obj_set_style_radius(pw, ui_s(12), 0);
    lv_obj_set_style_bg_color(pw, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_border_width(pw, ui_s(1), 0);
    lv_obj_set_style_border_color(pw, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_border_color(pw, lv_color_hex(ADL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_pos(pw, mw - ui_s(66), mh - ui_s(56));
    lv_obj_t *pi = make_icon(pw, SVG_POWER, ui_s(20));
    lv_obj_center(pi);
    lv_obj_add_event_cb(pw, power_btn_cb, LV_EVENT_CLICKED, 0);

    /* "Ne yapabilirim?" yardim paneli dugmesi     */
    lv_obj_t *hp = lv_button_create(g_start_menu);
    lv_obj_set_size(hp, ui_s(42), ui_s(42));
    lv_obj_set_style_radius(hp, ui_s(12), 0);
    lv_obj_set_style_bg_color(hp, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_border_width(hp, ui_s(1), 0);
    lv_obj_set_style_border_color(hp, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_border_color(hp, lv_color_hex(ADL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_pos(hp, mw - ui_s(166), mh - ui_s(56));
    lv_obj_t *hi = label(hp, "?", ADL_ACCENT);
    lv_obj_center(hi);
    lv_obj_add_event_cb(hp, help_btn_cb, LV_EVENT_CLICKED, 0);

    /* Asistan dugmesi — sag alttaki dugme sirasinin en solu. Eskiden
     * x = mw-118 ile aynasinda duran ayarlar dislisinin (mw-116) tam
     * uzerine biniyor, disli fareyle basilamiyordu. */
    lv_obj_t *ap = lv_button_create(g_start_menu);
    lv_obj_set_size(ap, ui_s(42), ui_s(42));
    lv_obj_set_style_radius(ap, ui_s(12), 0);
    lv_obj_set_style_bg_color(ap, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_pos(ap, mw - ui_s(216), mh - ui_s(56));
    lv_obj_t *ail = label(ap, "AI", 0xFFFFFF);
    lv_obj_center(ail);
    lv_obj_add_event_cb(ap, assistant_btn_cb, LV_EVENT_CLICKED, 0);

    if (!s_sm_group) s_sm_group = lv_group_create();
    else lv_group_remove_all_objs(s_sm_group);
    s_sm_prev_group = 0;
    s_sm_first = -1;
    if (s_sm_search) lv_group_add_obj(s_sm_group, s_sm_search);

    lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
}

