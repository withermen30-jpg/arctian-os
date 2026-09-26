#include "lvgl.h"
#include "asi.h"
#include "lv_port.h"
#include "adl_wolf.h"
#include "png.h"
#include "svg.h"
#include "app.h"
#include "dpk.h"
#include "memory_map.h"
#include <stdint.h>

asi_t *g_asi = 0;

extern const unsigned char wallpaper_png[];
extern const unsigned int  wallpaper_png_len;

static lv_obj_t *g_start_menu;
static lv_obj_t *g_taskbar;

static const arctian_app_t *app_get(int i);
static int app_total(void);
static void rebuild_shell_ui(void);
static void arctian_dyn_mark_closed(int idx);
static void arctian_dyn_begin_launch(int idx);
static int  arctian_dyn_preload(int d, char *err, int cap);
static int  dpk_is_package_name(const char *nm);
static void dyn_app_entry(lv_obj_t *win, asi_t *asi);


static void scopy(char *d, const char *s, int max) {
    int i = 0;
    for (; s[i] && i < max; i++) d[i] = s[i];
    d[i] = '\0';
}

static void base_obj(lv_obj_t *o, uint32_t bg, int radius, uint32_t border) {
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *label(lv_obj_t *parent, const char *txt, uint32_t col) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    return l;
}

static lv_obj_t *make_icon(lv_obj_t *parent, const char *svg, int size) {
    uint32_t *buf = (uint32_t *)g_asi->mem->alloc((size_t)size * size * 4u);
    svg_render(svg, size, size, buf);
    lv_obj_t *c = lv_canvas_create(parent);
    lv_obj_remove_style_all(c);
    lv_canvas_set_buffer(c, buf, size, size, LV_COLOR_FORMAT_ARGB8888);
    return c;
}


static const char SVG_LOGO[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#1F2937\" stroke=\"#7DD3FC\" stroke-width=\"1.5\"/>"
    "<path d=\"M20 10 L10 32 L15 32 L17 26 L23 26 L25 32 L30 32 L20 10 Z M18 21 L20 15 L22 21 Z\" fill=\"#F3F4F6\"/>"
    "<circle cx=\"20\" cy=\"34\" r=\"2.5\" fill=\"#7DD3FC\"/></svg>";

static const char SVG_TERMINAL[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#2A2E38\" stroke=\"#9CA3AF\" stroke-width=\"1.5\"/>"
    "<path d=\"M12 14 L18 20 L12 26\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/>"
    "<line x1=\"22\" y1=\"26\" x2=\"28\" y2=\"26\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/></svg>";

static const char SVG_FOLDER[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#2A2E38\" stroke=\"#9CA3AF\" stroke-width=\"1.5\"/>"
    "<path d=\"M10 14 L18 14 L21 18 L30 18 L30 28 L10 28 Z\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/></svg>";

static const char SVG_GLOBE[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#2A2E38\" stroke=\"#9CA3AF\" stroke-width=\"1.5\"/>"
    "<circle cx=\"20\" cy=\"20\" r=\"10\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/>"
    "<ellipse cx=\"20\" cy=\"20\" rx=\"4\" ry=\"10\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2\"/>"
    "<line x1=\"10\" y1=\"20\" x2=\"30\" y2=\"20\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/></svg>";

static const char SVG_SETTINGS[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#2A2E38\" stroke=\"#9CA3AF\" stroke-width=\"1.5\"/>"
    "<circle cx=\"20\" cy=\"20\" r=\"4\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/>"
    "<path d=\"M20 8 L20 13 M20 27 L20 32 M8 20 L13 20 M27 20 L32 20 M11 11 L15 15 M25 25 L29 29 M11 29 L15 25 M25 15 L29 11\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/></svg>";

static const char SVG_MEDIA[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#1F2937\" stroke=\"#7DD3FC\" stroke-width=\"1.5\"/>"
    "<polygon points=\"16,13 28,20 16,27\" fill=\"#7DD3FC\"/></svg>";

static const char SVG_WIFI[] =
    "<svg viewBox=\"0 0 32 32\">"
    "<path d=\"M 2 14 A 14 14 0 0 1 30 14\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/>"
    "<path d=\"M 8 18 A 8 8 0 0 1 24 18\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/>"
    "<circle cx=\"16\" cy=\"24\" r=\"2.5\" fill=\"#7DD3FC\"/></svg>";

static const char SVG_CLOSE[] =
    "<svg viewBox=\"0 0 12 12\"><path d=\"M2 2 L10 10 M10 2 L2 10\" stroke=\"#FFFFFF\" stroke-width=\"1.6\" fill=\"none\"/></svg>";
static const char SVG_MAX[] =
    "<svg viewBox=\"0 0 12 12\"><rect x=\"2.5\" y=\"2.5\" width=\"7\" height=\"7\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"1.4\"/></svg>";
static const char SVG_MIN[] =
    "<svg viewBox=\"0 0 12 12\"><line x1=\"2.5\" y1=\"6\" x2=\"9.5\" y2=\"6\" stroke=\"#F3F4F6\" stroke-width=\"1.6\"/></svg>";

static const char SVG_POWER[] =
    "<svg viewBox=\"0 0 24 24\">"
    "<circle cx=\"12\" cy=\"13\" r=\"7\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.2\"/>"
    "<line x1=\"12\" y1=\"3\" x2=\"12\" y2=\"12\" stroke=\"#F3F4F6\" stroke-width=\"2.2\"/></svg>";

static const char SVG_SEARCH[] =
    "<svg viewBox=\"0 0 20 20\">"
    "<circle cx=\"8.5\" cy=\"8.5\" r=\"5\" fill=\"none\" stroke=\"#9AA0A6\" stroke-width=\"2\"/>"
    "<line x1=\"12.5\" y1=\"12.5\" x2=\"17\" y2=\"17\" stroke=\"#9AA0A6\" stroke-width=\"2\"/></svg>";

static const char SVG_NOTES[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#2A2E38\" stroke=\"#9CA3AF\" stroke-width=\"1.5\"/>"
    "<line x1=\"10\" y1=\"14\" x2=\"30\" y2=\"14\" stroke=\"#F3F4F6\" stroke-width=\"2.4\"/>"
    "<line x1=\"10\" y1=\"20\" x2=\"30\" y2=\"20\" stroke=\"#F3F4F6\" stroke-width=\"2.4\"/>"
    "<line x1=\"10\" y1=\"26\" x2=\"24\" y2=\"26\" stroke=\"#F3F4F6\" stroke-width=\"2.4\"/></svg>";


#define MAX_WINS 8

typedef struct {
    lv_obj_t *win;
    lv_obj_t *btn;
    char title[40];
} winrec_t;

static winrec_t *g_recs[MAX_WINS];
static int g_last_x = 0, g_last_y = 0;

static void update_running_layout(void) {
    int x = 58;
    for (int i = 0; i < MAX_WINS; i++) {
        if (!g_recs[i] || !g_recs[i]->btn) continue;
        lv_obj_set_pos(g_recs[i]->btn, x, 3);
        x += 86;
    }
}

static void running_btn_cb(lv_event_t *e) {
    winrec_t *r = (winrec_t *)lv_event_get_user_data(e);
    if (!r || !r->win) return;
    if (lv_obj_has_flag(r->win, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(r->win, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(r->win);
    } else {
        lv_obj_add_flag(r->win, LV_OBJ_FLAG_HIDDEN);
    }
}

static void win_close_cb(lv_event_t *e) {
    winrec_t *r = (winrec_t *)lv_event_get_user_data(e);
    if (!r) return;
    if (r->btn) lv_obj_delete(r->btn);
    if (r->win) lv_obj_delete(r->win);
    r->btn = 0; r->win = 0;
    update_running_layout();
}

static void win_min_cb(lv_event_t *e) {
    winrec_t *r = (winrec_t *)lv_event_get_user_data(e);
    if (r && r->win) lv_obj_add_flag(r->win, LV_OBJ_FLAG_HIDDEN);
}

static void win_max_cb(lv_event_t *e) {
    winrec_t *r = (winrec_t *)lv_event_get_user_data(e);
    if (!r || !r->win) return;
    lv_obj_t *w = r->win;
    lv_obj_set_style_align(w, LV_ALIGN_DEFAULT, 0);
    if (lv_obj_has_flag(w, LV_OBJ_FLAG_USER_1)) {
        lv_obj_set_size(w, 520, 340);
        lv_obj_set_pos(w, (g_asi->gfx->width() - 520) / 2, (g_asi->gfx->height() - 340) / 2);
        lv_obj_remove_flag(w, LV_OBJ_FLAG_USER_1);
    } else {
        lv_obj_set_size(w, g_asi->gfx->width() - 40, g_asi->gfx->height() - 120);
        lv_obj_set_pos(w, 20, 20);
        lv_obj_add_flag(w, LV_OBJ_FLAG_USER_1);
    }
    lv_obj_move_foreground(w);
}

static void title_drag_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *win = (lv_obj_t *)lv_event_get_user_data(e);
    if (!win) return;

    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        g_last_x = p.x;
        g_last_y = p.y;
        lv_obj_set_style_align(win, LV_ALIGN_DEFAULT, 0);
        lv_obj_move_foreground(win);
    } else if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        int dx = p.x - g_last_x;
        int dy = p.y - g_last_y;
        g_last_x = p.x;
        g_last_y = p.y;

        int nx = lv_obj_get_x(win) + dx;
        int ny = lv_obj_get_y(win) + dy;

        int sw = g_asi->gfx->width();
        int sh = g_asi->gfx->height();
        int ww = lv_obj_get_width(win);
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx + ww > sw) nx = sw - ww;
        if (ny > sh - 120) ny = sh - 120;
        lv_obj_set_pos(win, nx, ny);
    }
}

static void add_ctrl(lv_obj_t *tb, const char *svg, uint32_t bgcol,
                     lv_event_cb_t cb, void *ud, int xoff) {
    lv_obj_t *b = lv_button_create(tb);
    lv_obj_set_size(b, 22, 20);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bgcol), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, xoff, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *ic = make_icon(b, svg, 12);
    lv_obj_center(ic);
}

static lv_obj_t *create_window(const char *title, int w, int h) {
    lv_obj_t *win = lv_obj_create(lv_screen_active());
    base_obj(win, ADL_SURFACE, 12, ADL_BORDER);
    lv_obj_set_style_bg_opa(win, 240, 0);
    lv_obj_set_style_pad_all(win, 0, 0);
    lv_obj_set_size(win, w, h);
    lv_obj_set_style_align(win, LV_ALIGN_DEFAULT, 0);
    lv_obj_set_pos(win, (g_asi->gfx->width() - w) / 2,
                        (g_asi->gfx->height() - h) / 2 - 10);

    lv_obj_t *tb = lv_obj_create(win);
    lv_obj_remove_style_all(tb);
    lv_obj_remove_flag(tb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(tb, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(tb, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tb, 12, 0);
    lv_obj_set_size(tb, w - 2, 36);
    lv_obj_align(tb, LV_ALIGN_TOP_MID, 0, 1);
    lv_obj_add_flag(tb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(tb, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(tb, title_drag_cb, LV_EVENT_PRESSED, win);
    lv_obj_add_event_cb(tb, title_drag_cb, LV_EVENT_PRESSING, win);

    lv_obj_t *t = label(tb, title, ADL_TEXT);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 12, 0);

    winrec_t *r = (winrec_t *)g_asi->mem->alloc(sizeof(winrec_t));
    r->win = win; r->btn = 0;
    scopy(r->title, title, 39);
    int slot = -1;
    for (int i = 0; i < MAX_WINS; i++) if (!g_recs[i]) { slot = i; break; }
    if (slot < 0) slot = 0;
    g_recs[slot] = r;

    lv_obj_t *btn = lv_button_create(g_taskbar);
    lv_obj_set_size(btn, 78, 40);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(btn, 200, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(ADL_BORDER), 0);
    lv_obj_t *bl = label(btn, title, ADL_TEXT);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, running_btn_cb, LV_EVENT_CLICKED, r);
    r->btn = btn;

    add_ctrl(tb, SVG_CLOSE, ADL_DANGER, win_close_cb, r, -6);
    add_ctrl(tb, SVG_MAX,   0x3A3F4B,   win_max_cb,   r, -32);
    add_ctrl(tb, SVG_MIN,   0x3A3F4B,   win_min_cb,   r, -58);

    update_running_layout();
    lv_obj_move_foreground(win);
    return win;
}


typedef struct { lv_obj_t *overlay; char title[40]; } app_ctx_t;

static void loading_done_cb(lv_timer_t *timer) {
    app_ctx_t *c = (app_ctx_t *)lv_timer_get_user_data(timer);
    create_window(c->title, 520, 340);
    lv_obj_delete(c->overlay);
    lv_free(c);
    lv_timer_delete(timer);
}

static void open_app(const char *title) {
    lv_obj_t *ov = lv_obj_create(lv_screen_active());
    base_obj(ov, ADL_BG, 0, ADL_BG);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_align(ov, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_80, 0);

    lv_obj_t *arc = lv_arc_create(ov);
    lv_obj_set_size(arc, 84, 84);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, -18);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
    lv_anim_start(&a);

    lv_obj_t *txt = label(ov, "Yukleniyor...", ADL_TEXT_DIM);
    lv_obj_align(txt, LV_ALIGN_CENTER, 0, 42);

    app_ctx_t *c = (app_ctx_t *)lv_malloc(sizeof(app_ctx_t));
    scopy(c->title, title, 39);
    c->overlay = ov;
    lv_timer_create(loading_done_cb, 2000, c);
}

static void open_registry_app(int idx);

static int title_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static void app_btn_cb(lv_event_t *e) {
    const char *title = (const char *)lv_event_get_user_data(e);
    int total = app_total();
    for (int i = 0; i < total; i++) {
        const arctian_app_t *a = app_get(i);
        if (a && title_eq(a->title, title)) { open_registry_app(i); return; }
    }
    open_app(title);
}

typedef struct { lv_obj_t *overlay; int idx; } runctx_t;

static void dyn_win_del_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0) arctian_dyn_mark_closed(idx);
}

static void run_app_done(lv_timer_t *t) {
    runctx_t *c = (runctx_t *)lv_timer_get_user_data(t);
    const arctian_app_t *a = app_get(c->idx);
    if (!a) { lv_obj_delete(c->overlay); lv_free(c); lv_timer_delete(t); return; }
    int ww = a->win_w > 240 ? a->win_w : 560;
    int wh = a->win_h > 180 ? a->win_h : 380;
    lv_obj_t *win = create_window(a->title, ww, wh);
    lv_obj_t *body = lv_obj_create(win);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, ww - 4, wh - 42);
    lv_obj_set_pos(body, 2, 38);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    if (c->idx >= arctian_app_count) {
        arctian_dyn_begin_launch(c->idx - arctian_app_count);
        lv_obj_add_event_cb(win, dyn_win_del_cb, LV_EVENT_DELETE,
                            (void *)(intptr_t)(c->idx - arctian_app_count));
    } else {
        arctian_dyn_begin_launch(-1);
    }
    if (a->entry) a->entry(body, g_asi);
    lv_obj_delete(c->overlay);
    lv_free(c);
    lv_timer_delete(t);
}

static void show_error_window(const char *title, const char *msg) {
    lv_obj_t *w = create_window(title, 480, 220);
    lv_obj_t *l = label(w, msg, ADL_DANGER);
    lv_obj_set_width(l, 440);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 16, 52);
}

static void open_registry_app(int idx) {
    if (idx >= arctian_app_count) {
        char err[96];
        if (arctian_dyn_preload(idx - arctian_app_count, err, sizeof(err)) != 0) {
            show_error_window("Uygulama acilamadi", err);
            return;
        }
    }
    lv_obj_t *ov = lv_obj_create(lv_screen_active());
    base_obj(ov, ADL_BG, 0, ADL_BG);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_align(ov, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_80, 0);

    lv_obj_t *arc = lv_arc_create(ov);
    lv_obj_set_size(arc, 84, 84);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, -18);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
    lv_anim_start(&a);

    lv_obj_t *txt = label(ov, "Yukleniyor...", ADL_TEXT_DIM);
    lv_obj_align(txt, LV_ALIGN_CENTER, 0, 42);

    runctx_t *c = (runctx_t *)lv_malloc(sizeof(runctx_t));
    c->idx = idx;
    c->overlay = ov;
    lv_timer_create(run_app_done, 2000, c);
}

static void reg_tile_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    open_registry_app(idx);
}


static void start_menu_anim_cb(void *var, int32_t value) { lv_obj_set_y((lv_obj_t *)var, value); }

static void toggle_start_menu(void) {
    bool hidden = lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    int sw = g_asi->gfx->width();
    int sh = g_asi->gfx->height();
    int mw = lv_obj_get_width(g_start_menu);
    int mh = lv_obj_get_height(g_start_menu);
    int target_x = (sw - mw) / 2;
    int target_y = sh - 90 - mh;

    if (!hidden) {
        lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(g_start_menu, target_x);
    lv_obj_set_y(g_start_menu, sh);
    lv_obj_move_foreground(g_start_menu);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_start_menu);
    lv_anim_set_values(&a, sh, target_y);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, start_menu_anim_cb);
    lv_anim_start(&a);
}

static void start_btn_cb(lv_event_t *e) { (void)e; toggle_start_menu(); }

static void sm_item_cb(lv_event_t *e) {
    const char *title = (const char *)lv_event_get_user_data(e);
    lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    open_app(title);
}


static lv_obj_t *g_net_panel = 0;
static lv_obj_t *g_net_body = 0;
static int   g_net_stage = 0;
static char  g_net_ssid[33];
static char  g_net_pass[64];
static asi_wifi_net_t g_scan[16];
static int   g_scan_n = 0;

static void net_panel_refresh(void);

static void net_panel_close(void) {
    if (g_net_panel) { lv_obj_delete(g_net_panel); g_net_panel = 0; g_net_body = 0; }
    g_net_stage = 0;
}
static void net_close_cb(lv_event_t *e) { (void)e; net_panel_close(); }
static void net_overlay_click_cb(lv_event_t *e) {
    if (lv_event_get_target(e) == g_net_panel) net_panel_close();
}

static void wifi_pick_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_scan_n) return;
    int i = 0;
    for (; g_scan[idx].ssid[i] && i < 32; i++) g_net_ssid[i] = g_scan[idx].ssid[i];
    g_net_ssid[i] = 0;
    g_net_pass[0] = 0;
    if (!g_scan[idx].secure) {
        if (g_asi->net && g_asi->net->wifi_connect) g_asi->net->wifi_connect(g_net_ssid, "");
        g_net_stage = 0;
    } else {
        g_net_stage = 1;
    }
    net_panel_refresh();
}
static void wifi_back_cb(lv_event_t *e) { (void)e; g_net_stage = 0; net_panel_refresh(); }
static void wifi_connect_cb(lv_event_t *e) {
    (void)e;
    if (g_asi->net && g_asi->net->wifi_connect) g_asi->net->wifi_connect(g_net_ssid, g_net_pass);
    g_net_stage = 0;
    net_panel_refresh();
}
static void wifi_disc_cb(lv_event_t *e) {
    (void)e;
    if (g_asi->net && g_asi->net->wifi_disconnect) g_asi->net->wifi_disconnect();
    net_panel_refresh();
}
static void pass_ta_cb(lv_event_t *e) {
    const char *t = lv_textarea_get_text(lv_event_get_target(e));
    int i = 0; for (; t[i] && i < 63; i++) g_net_pass[i] = t[i];
    g_net_pass[i] = 0;
}

static lv_obj_t *net_row(lv_obj_t *parent, const char *ssid, int secure, int rssi) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 288, 46);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_t *nm = label(b, ssid, ADL_TEXT);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 12, secure ? -8 : 0);
    if (secure) {
        lv_obj_t *lk = label(b, "sifreli", ADL_WARN);
        lv_obj_align(lk, LV_ALIGN_LEFT_MID, 12, 10);
    }
    int bars = (rssi > -50) ? 4 : (rssi > -62) ? 3 : (rssi > -74) ? 2 : 1;
    char sig[8]; int i = 0;
    for (; i < bars; i++) sig[i] = '|';
    sig[i] = 0;
    lv_obj_t *sg = label(b, sig, ADL_OK);
    lv_obj_align(sg, LV_ALIGN_RIGHT_MID, -12, 0);
    return b;
}

static void net_panel_refresh(void) {
    if (!g_net_body) return;
    lv_obj_clean(g_net_body);
    asi_net_t *n = g_asi ? g_asi->net : 0;
    if (!n) {
        lv_obj_t *l = label(g_net_body, "Ag servisi yok", ADL_TEXT_DIM);
        lv_obj_set_pos(l, 4, 4);
        return;
    }

    int lt = n->link_type ? n->link_type() : 0;
    uint8_t ip[4] = {0, 0, 0, 0};
    if (n->ip) n->ip(ip);

    if (g_net_stage == 1) {
        lv_obj_t *t = label(g_net_body, "Sifre gir", ADL_TEXT);
        lv_obj_set_pos(t, 4, 2);
        lv_obj_t *s = label(g_net_body, g_net_ssid, ADL_TEXT_DIM);
        lv_obj_set_pos(s, 4, 24);

        lv_obj_t *ta = lv_textarea_create(g_net_body);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, true);
        lv_textarea_set_placeholder_text(ta, "Wi-Fi sifresi");
        lv_obj_set_width(ta, 288);
        lv_obj_set_pos(ta, 4, 48);
        lv_obj_add_event_cb(ta, pass_ta_cb, LV_EVENT_VALUE_CHANGED, 0);

        lv_obj_t *kb = lv_keyboard_create(g_net_body);
        lv_obj_set_size(kb, 288, 150);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_keyboard_set_textarea(kb, ta);
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);

        lv_obj_t *b = lv_button_create(g_net_body);
        lv_obj_set_size(b, 140, 36);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(ADL_ACCENT), 0);
        lv_obj_align_to(b, kb, LV_ALIGN_OUT_TOP_LEFT, 0, -6);
        lv_obj_t *bl = label(b, "Baglan", ADL_TEXT); lv_obj_center(bl);
        lv_obj_add_event_cb(b, wifi_connect_cb, LV_EVENT_CLICKED, 0);

        lv_obj_t *bb = lv_button_create(g_net_body);
        lv_obj_set_size(bb, 140, 36);
        lv_obj_set_style_radius(bb, 8, 0);
        lv_obj_set_style_bg_color(bb, lv_color_hex(ADL_SURFACE_2), 0);
        lv_obj_align_to(bb, kb, LV_ALIGN_OUT_TOP_RIGHT, 0, -6);
        lv_obj_t *bbl = label(bb, "Geri", ADL_TEXT); lv_obj_center(bbl);
        lv_obj_add_event_cb(bb, wifi_back_cb, LV_EVENT_CLICKED, 0);
        return;
    }

    lv_obj_t *card = lv_obj_create(g_net_body);
    lv_obj_remove_style_all(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_size(card, 288, 64);
    lv_obj_set_pos(card, 4, 2);

    if (lt == ASI_LINK_ETHERNET) {
        lv_obj_t *t = label(card, "Ethernet ile bagli", ADL_OK);
        lv_obj_set_pos(t, 12, 8);
        lv_obj_t *s = label(card, "", ADL_TEXT_DIM);
        lv_obj_set_pos(s, 12, 34);
        lv_label_set_text_fmt(s, "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    } else if (lt == ASI_LINK_WIFI) {
        const char *ssid = (n->wifi_ssid) ? n->wifi_ssid() : "";
        lv_obj_t *t = label(card, "Wi-Fi ile bagli", ADL_OK);
        lv_obj_set_pos(t, 12, 8);
        lv_obj_t *s = label(card, "", ADL_TEXT_DIM);
        lv_obj_set_pos(s, 12, 34);
        lv_label_set_text_fmt(s, "%s  IP: %d.%d.%d.%d", ssid, ip[0], ip[1], ip[2], ip[3]);
        lv_obj_t *d = lv_button_create(card);
        lv_obj_set_size(d, 90, 28);
        lv_obj_set_style_radius(d, 6, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(ADL_DANGER), 0);
        lv_obj_align(d, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_t *dl = label(d, "Kes", ADL_TEXT); lv_obj_center(dl);
        lv_obj_add_event_cb(d, wifi_disc_cb, LV_EVENT_CLICKED, 0);
    } else {
        lv_obj_t *t = label(card, "Baglanti yok", ADL_WARN);
        lv_obj_set_pos(t, 12, 8);
        lv_obj_t *s = label(card, "Bir ag secin", ADL_TEXT_DIM);
        lv_obj_set_pos(s, 12, 34);
    }

    if (lt != ASI_LINK_ETHERNET && lt != ASI_LINK_WIFI) {
        lv_obj_t *h = label(g_net_body, "Kullanilabilir aglar", ADL_TEXT_DIM);
        lv_obj_set_pos(h, 4, 76);

        int wifi_hw = (n->wifi_hw) ? n->wifi_hw() : 0;
        g_scan_n = (n->wifi_scan) ? n->wifi_scan(g_scan, 16) : 0;
        if (g_scan_n == 0) {
            const char *msg;
            if (!wifi_hw)       msg = "Wi-Fi donanimi bulunamadi";
            else                msg = "Wi-Fi karti var, surucu/beyan yok";
            lv_obj_t *e = label(g_net_body, msg, ADL_TEXT_DIM);
            lv_obj_set_pos(e, 4, 100);
        }
        for (int i = 0; i < g_scan_n; i++) {
            lv_obj_t *b = net_row(g_net_body, g_scan[i].ssid, g_scan[i].secure, g_scan[i].rssi);
            lv_obj_set_pos(b, 4, 100 + i * 52);
            lv_obj_add_event_cb(b, wifi_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    }
}

static void open_network_panel(void) {
    if (g_net_panel) { net_panel_close(); return; }
    g_net_stage = 0;

    g_net_panel = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(g_net_panel);
    lv_obj_set_size(g_net_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(g_net_panel, 0, 0);
    lv_obj_add_flag(g_net_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_net_panel, net_overlay_click_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *panel = lv_obj_create(g_net_panel);
    base_obj(panel, ADL_SURFACE, 14, ADL_BORDER);
    lv_obj_set_style_bg_opa(panel, 250, 0);
    lv_obj_set_style_shadow_width(panel, 26, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(panel, 180, 0);
    lv_obj_set_size(panel, 320, 420);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_RIGHT, -30, -92);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = label(panel, "Ag", ADL_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 10);

    lv_obj_t *x = lv_button_create(panel);
    lv_obj_set_size(x, 28, 28);
    lv_obj_set_style_radius(x, 14, 0);
    lv_obj_set_style_bg_color(x, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, -10, 8);
    lv_obj_t *xl = label(x, "x", ADL_TEXT); lv_obj_center(xl);
    lv_obj_add_event_cb(x, net_close_cb, LV_EVENT_CLICKED, 0);

    g_net_body = lv_obj_create(panel);
    lv_obj_remove_style_all(g_net_body);
    lv_obj_set_size(g_net_body, 296, 354);
    lv_obj_set_pos(g_net_body, 12, 44);
    lv_obj_remove_flag(g_net_body, LV_OBJ_FLAG_SCROLLABLE);

    net_panel_refresh();
}

static void wifi_btn_cb(lv_event_t *e) { (void)e; open_network_panel(); }


typedef struct { const char *svg; const char *title; } appdef_t;
static const appdef_t g_apps[5] = {
    { SVG_TERMINAL, "Terminal" },
    { SVG_FOLDER,   "Dosya Yoneticisi" },
    { SVG_GLOBE,    "Tarayici" },
    { SVG_SETTINGS, "Ayarlar" },
    { SVG_MEDIA,    "Medya Oynatici" },
};

static void build_taskbar(lv_obj_t *scr) {
    int sw = g_asi->gfx->width();
    int bw = sw * 82 / 100;
    if (bw > 900) bw = 900;
    if (bw < 660) bw = 660;
    int bh = 60;

    g_taskbar = lv_obj_create(scr);
    base_obj(g_taskbar, 0x232733, 16, 0x3A3F4B);
    lv_obj_set_style_bg_grad_color(g_taskbar, lv_color_hex(0x14161C), 0);
    lv_obj_set_style_bg_grad_dir(g_taskbar, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(g_taskbar, 215, 0);
    lv_obj_set_style_border_opa(g_taskbar, 70, 0);
    lv_obj_set_style_shadow_width(g_taskbar, 26, 0);
    lv_obj_set_style_shadow_color(g_taskbar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(g_taskbar, 170, 0);
    lv_obj_set_style_shadow_ofs_y(g_taskbar, 10, 0);
    lv_obj_set_style_pad_all(g_taskbar, 8, 0);
    lv_obj_set_size(g_taskbar, bw, bh);
    lv_obj_align(g_taskbar, LV_ALIGN_BOTTOM_MID, 0, -14);

    lv_obj_t *start = make_icon(g_taskbar, SVG_LOGO, 40);
    lv_obj_set_pos(start, 2, 2);
    lv_obj_add_flag(start, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(start, start_btn_cb, LV_EVENT_CLICKED, 0);

    int ic = 40, gap = 10;
    int total = 5 * ic + 4 * gap;
    int x0 = (bw - 16 - total) / 2;
    if (x0 < 56) x0 = 56;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *c = make_icon(g_taskbar, g_apps[i].svg, ic);
        lv_obj_set_pos(c, x0 + i * (ic + gap), 2);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, app_btn_cb, LV_EVENT_CLICKED, (void *)g_apps[i].title);
    }

    lv_obj_t *accent = lv_obj_create(g_taskbar);
    lv_obj_remove_style_all(accent);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x7DD3FC), 0);
    lv_obj_set_style_bg_opa(accent, 90, 0);
    lv_obj_set_style_radius(accent, 1, 0);
    lv_obj_set_size(accent, bw - 40, 2);
    lv_obj_align(accent, LV_ALIGN_BOTTOM_MID, 0, -2);

    lv_obj_t *wifi = make_icon(g_taskbar, SVG_WIFI, 24);
    lv_obj_align(wifi, LV_ALIGN_RIGHT_MID, -74, -1);
    lv_obj_add_flag(wifi, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wifi, wifi_btn_cb, LV_EVENT_CLICKED, 0);
    lv_obj_t *clk = label(g_taskbar, "14:30", ADL_TEXT);
    lv_obj_align(clk, LV_ALIGN_RIGHT_MID, -6, 0);
}

static void power_cancel_cb(lv_event_t *e) {
    lv_obj_delete((lv_obj_t *)lv_event_get_user_data(e));
}
static void power_off_cb(lv_event_t *e) {
    (void)e;
    if (g_asi->system) g_asi->system->shutdown();
}
static void power_reboot_cb(lv_event_t *e) {
    (void)e;
    if (g_asi->system) g_asi->system->reboot();
}

static void open_power_dialog(void) {
    lv_obj_t *ov = lv_obj_create(lv_screen_active());
    base_obj(ov, 0x000000, 0, 0x000000);
    lv_obj_set_style_bg_opa(ov, 150, 0);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_align(ov, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *panel = lv_obj_create(ov);
    base_obj(panel, ADL_SURFACE, 14, ADL_BORDER);
    lv_obj_set_style_shadow_width(panel, 30, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(panel, 190, 0);
    lv_obj_set_size(panel, 400, 200);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);

    lv_obj_t *ic = make_icon(panel, SVG_POWER, 34);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *title = label(panel, "Bilgisayari Kapat", ADL_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);

    lv_obj_t *msg = label(panel, "Kapatmak veya yeniden baslatmak istediginizden emin misiniz?", ADL_TEXT_DIM);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg, 356);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 68);

    lv_obj_t *off = lv_button_create(panel);
    lv_obj_set_size(off, 104, 40);
    lv_obj_set_style_radius(off, 8, 0);
    lv_obj_set_style_bg_color(off, lv_color_hex(ADL_DANGER), 0);
    lv_obj_align(off, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *ol = label(off, "Kapat", ADL_TEXT); lv_obj_center(ol);
    lv_obj_add_event_cb(off, power_off_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *rb = lv_button_create(panel);
    lv_obj_set_size(rb, 128, 40);
    lv_obj_set_style_radius(rb, 8, 0);
    lv_obj_set_style_bg_color(rb, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_align(rb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *rl = label(rb, "Yeniden Baslat", ADL_TEXT); lv_obj_center(rl);
    lv_obj_add_event_cb(rb, power_reboot_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *cx = lv_button_create(panel);
    lv_obj_set_size(cx, 88, 40);
    lv_obj_set_style_radius(cx, 8, 0);
    lv_obj_set_style_bg_color(cx, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_border_width(cx, 1, 0);
    lv_obj_set_style_border_color(cx, lv_color_hex(ADL_BORDER), 0);
    lv_obj_align(cx, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t *xl = label(cx, "Iptal", ADL_TEXT); lv_obj_center(xl);
    lv_obj_add_event_cb(cx, power_cancel_cb, LV_EVENT_CLICKED, ov);
}

static void power_btn_cb(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    open_power_dialog();
}

static void build_start_menu(lv_obj_t *scr) {
    int sw = g_asi->gfx->width();
    int sh = g_asi->gfx->height();
    int mw = 600, mh = 500;

    g_start_menu = lv_obj_create(scr);
    base_obj(g_start_menu, ADL_SURFACE, 14, ADL_BORDER);
    lv_obj_set_style_bg_opa(g_start_menu, 240, 0);
    lv_obj_set_style_shadow_width(g_start_menu, 28, 0);
    lv_obj_set_style_shadow_color(g_start_menu, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(g_start_menu, 170, 0);
    lv_obj_set_size(g_start_menu, mw, mh);
    lv_obj_set_pos(g_start_menu, (sw - mw) / 2, sh - 90 - mh);
    lv_obj_set_style_pad_all(g_start_menu, 0, 0);

    lv_obj_t *search = lv_obj_create(g_start_menu);
    lv_obj_remove_style_all(search);
    lv_obj_set_style_bg_color(search, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(search, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(search, 8, 0);
    lv_obj_set_style_border_width(search, 1, 0);
    lv_obj_set_style_border_color(search, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_size(search, mw - 40, 40);
    lv_obj_set_pos(search, 20, 18);
    lv_obj_t *sic = make_icon(search, SVG_SEARCH, 16);
    lv_obj_align(sic, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *st = label(search, "Aramak icin yazin", ADL_TEXT_DIM);
    lv_obj_align(st, LV_ALIGN_LEFT_MID, 34, 0);

    lv_obj_t *h1 = label(g_start_menu, "Sabitlenmis", ADL_TEXT);
    lv_obj_set_pos(h1, 24, 72);

    int tw = 172, th = 92, tgap = 12, tx0 = 24;
    if (app_total() > 0) {
        int n = app_total();
        if (n > 6) n = 6;
        for (int i = 0; i < n; i++) {
            const arctian_app_t *a = app_get(i);
            if (!a) continue;
            int col = i % 3, row = i / 3;
            lv_obj_t *tile = lv_obj_create(g_start_menu);
            lv_obj_remove_style_all(tile);
            lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(tile, 10, 0);
            lv_obj_set_style_bg_color(tile, lv_color_hex(ADL_SURFACE_2), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_set_size(tile, tw, th);
            lv_obj_set_pos(tile, tx0 + col * (tw + tgap), 100 + row * (th + tgap));
            lv_obj_t *ic = make_icon(tile, a->icon_svg ? a->icon_svg : "", 40);
            lv_obj_align(ic, LV_ALIGN_LEFT_MID, 16, -8);
            lv_obj_t *tl = label(tile, a->title, ADL_TEXT);
            lv_obj_align(tl, LV_ALIGN_LEFT_MID, 64, 0);
            lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(tile, reg_tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    } else {
        static const char *pinned_svg[6] = { SVG_TERMINAL, SVG_FOLDER, SVG_GLOBE, SVG_SETTINGS, SVG_MEDIA, SVG_NOTES };
        static const char *pinned_label[6] = { "Terminal", "Dosya", "Tarayici", "Ayarlar", "Medya", "Notlar" };
        static const char *pinned_full[6] = { "Terminal", "Dosya Yoneticisi", "Tarayici", "Ayarlar", "Medya Oynatici", "Not Defteri" };
        for (int i = 0; i < 6; i++) {
            int col = i % 3, row = i / 3;
            lv_obj_t *tile = lv_obj_create(g_start_menu);
            lv_obj_remove_style_all(tile);
            lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(tile, 10, 0);
            lv_obj_set_style_bg_color(tile, lv_color_hex(ADL_SURFACE_2), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_set_size(tile, tw, th);
            lv_obj_set_pos(tile, tx0 + col * (tw + tgap), 100 + row * (th + tgap));
            lv_obj_t *ic = make_icon(tile, pinned_svg[i], 40);
            lv_obj_align(ic, LV_ALIGN_LEFT_MID, 16, -8);
            lv_obj_t *tl = label(tile, pinned_label[i], ADL_TEXT);
            lv_obj_align(tl, LV_ALIGN_LEFT_MID, 64, 0);
            lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(tile, sm_item_cb, LV_EVENT_CLICKED, (void *)pinned_full[i]);
        }
    }

    lv_obj_t *h2 = label(g_start_menu, "Onerilen", ADL_TEXT);
    lv_obj_set_pos(h2, 24, 300);

    static const char *rec[2] = { "back.png", "arctian.iso" };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *row = lv_obj_create(g_start_menu);
        lv_obj_remove_style_all(row);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(ADL_SURFACE_2), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_size(row, mw - 48, 40);
        lv_obj_set_pos(row, 24, 328 + i * 46);
        lv_obj_t *l = label(row, rec[i], ADL_TEXT_DIM);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 14, 0);
    }

    lv_obj_t *sep = lv_obj_create(g_start_menu);
    lv_obj_remove_style_all(sep);
    lv_obj_set_style_bg_color(sep, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_size(sep, mw - 40, 1);
    lv_obj_set_pos(sep, 20, mh - 62);

    lv_obj_t *av = lv_obj_create(g_start_menu);
    lv_obj_remove_style_all(av);
    lv_obj_set_style_bg_color(av, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_bg_opa(av, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(av, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(av, 30, 30);
    lv_obj_set_pos(av, 24, mh - 48);
    lv_obj_t *ul = label(g_start_menu, "Kullanici", ADL_TEXT);
    lv_obj_set_pos(ul, 64, mh - 44);

    lv_obj_t *pw = lv_button_create(g_start_menu);
    lv_obj_set_size(pw, 40, 40);
    lv_obj_set_style_radius(pw, 10, 0);
    lv_obj_set_style_bg_color(pw, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_border_width(pw, 1, 0);
    lv_obj_set_style_border_color(pw, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_pos(pw, mw - 60, mh - 52);
    lv_obj_t *pi = make_icon(pw, SVG_POWER, 20);
    lv_obj_center(pi);
    lv_obj_add_event_cb(pw, power_btn_cb, LV_EVENT_CLICKED, 0);

    lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
}


const char *arctian_open_target = "";

static const char SVG_FILE[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#1F2937\" stroke=\"#9CA3AF\" stroke-width=\"1.5\"/>"
    "<path d=\"M14 8 L24 8 L30 14 L30 32 L14 32 Z\" fill=\"#2A2E38\" stroke=\"#F3F4F6\" stroke-width=\"2\"/>"
    "<path d=\"M24 8 L24 14 L30 14\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2\"/></svg>";

#define MAX_DICONS 24
#define ICON_W 92
#define ICON_H 100
#define GRID_X 24
#define GRID_Y 20

typedef struct {
    char name[48];
    int  is_file;
    int  app_idx;
    const char *svg;
    int  gx, gy;
    int  selected;
    lv_obj_t *cell;
} dicon_t;

static dicon_t g_dicons[MAX_DICONS];
static int g_dicon_count = 0;
static int g_sel_count = 0;
static lv_obj_t *g_icon_layer = 0;
static lv_obj_t *g_rubber = 0;
static lv_obj_t *g_context_menu = 0;
static lv_obj_t *g_dialog = 0;
static int g_modal = 0;
static char g_ow_file[48];

static int slen2(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static int chr_low(char c) { if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return c; }
static int s_eq(const char *a, const char *b) {
    int i = 0; for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0; return a[i] == b[i];
}
static int iabs2(int v) { return v < 0 ? -v : v; }

static int has_ext(const char *name, char ext[], int extsz) {
    int n = slen2(name), dot = -1;
    for (int i = 0; i < n; i++) if (name[i] == '.') dot = i;
    if (dot < 0 || dot == n - 1) return 0;
    for (int i = dot + 1; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    int len = n - dot - 1; if (len >= extsz) len = extsz - 1;
    for (int i = 0; i < len; i++) ext[i] = name[dot + 1 + i];
    ext[len] = 0;
    return 1;
}

static int app_handles_ext(int i, const char *ext) {
    const arctian_app_t *a = app_get(i);
    const char *h = a ? a->handles : 0; if (!h || !*h) return 0;
    int el = slen2(ext); const char *p = h;
    while (*p) {
        while (*p == ',' || *p == ' ') p++;
        const char *q = p; while (*q && *q != ',') q++;
        int len = (int)(q - p);
        if (len == el) {
            int k; for (k = 0; k < el; k++) if (chr_low(p[k]) != chr_low(ext[k])) break;
            if (k == el) return 1;
        }
        p = q;
    }
    return 0;
}

static void icon_apply_pos(int i) {
    dicon_t *d = &g_dicons[i];
    if (!d->cell) return;
    lv_obj_set_pos(d->cell, GRID_X + d->gx * ICON_W, GRID_Y + d->gy * ICON_H);
}
static void icon_set_sel(int i, int sel) {
    dicon_t *d = &g_dicons[i];
    d->selected = sel;
    if (!d->cell) return;
    lv_obj_set_style_bg_opa(d->cell, sel ? 45 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(d->cell, sel ? 255 : LV_OPA_TRANSP, 0);
}
static void clear_selection(void) {
    for (int i = 0; i < g_dicon_count; i++) icon_set_sel(i, 0);
    g_sel_count = 0;
}

static void icon_build(int i) {
    dicon_t *d = &g_dicons[i];
    lv_obj_t *c = lv_obj_create(g_icon_layer);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(c, ICON_W - 8, ICON_H - 8);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(ADL_ACCENT), 0);

    lv_obj_t *ic = make_icon(c, d->svg, 48);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *lab = label(c, d->name, ADL_TEXT);
    lv_obj_set_width(lab, ICON_W - 16);
    lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
    lv_obj_align(lab, LV_ALIGN_BOTTOM_MID, 0, -6);

    d->cell = c;
    icon_set_sel(i, 0);
    icon_apply_pos(i);
}

static void dicon_add(const char *name, int is_file, int app_idx, const char *svg) {
    if (g_dicon_count >= MAX_DICONS) return;
    dicon_t *d = &g_dicons[g_dicon_count];
    scopy(d->name, name, 47);
    d->is_file = is_file; d->app_idx = app_idx; d->svg = svg;
    d->gx = 0; d->gy = g_dicon_count;
    g_dicon_count++;
    icon_build(g_dicon_count - 1);
}

static int has_suffix_ci(const char *nm, const char *ext) {
    int n = slen2(nm), l = slen2(ext);
    if (n < l) return 0;
    for (int i = 0; i < l; i++)
        if (chr_low(nm[n - l + i]) != chr_low(ext[i])) return 0;
    return 1;
}

static int afs_name_is_hidden(const char *nm) {
    return has_suffix_ci(nm, ".dpk") || has_suffix_ci(nm, ".wp") ||
           has_suffix_ci(nm, ".cfg");
}

static void build_desktop_icons(void) {
    g_icon_layer = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(g_icon_layer);
    lv_obj_set_size(g_icon_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(g_icon_layer, 0, 0);
    lv_obj_remove_flag(g_icon_layer, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < app_total() && i < MAX_DICONS; i++) {
        const arctian_app_t *a = app_get(i);
        if (a) dicon_add(a->title, 0, i, a->icon_svg ? a->icon_svg : "");
    }

    if (g_asi->fs && g_asi->fs->count && g_asi->fs->name) {
        int fc = g_asi->fs->count();
        for (int i = 0; i < fc && g_dicon_count < MAX_DICONS; i++) {
            const char *nm = g_asi->fs->name(i);
            if (nm && nm[0] && !afs_name_is_hidden(nm)) dicon_add(nm, 1, -1, SVG_FILE);
        }
    }

    int H = g_asi->gfx->height();
    int rows = (H - GRID_Y - 130) / ICON_H; if (rows < 1) rows = 1;
    for (int i = 0; i < g_dicon_count; i++) {
        g_dicons[i].gx = i / rows;
        g_dicons[i].gy = i % rows;
        icon_apply_pos(i);
    }
}

static int icon_hit(int x, int y, int *out) {
    for (int i = 0; i < g_dicon_count; i++) {
        lv_obj_t *c = g_dicons[i].cell; if (!c) continue;
        int cx = lv_obj_get_x(c), cy = lv_obj_get_y(c);
        int w = lv_obj_get_width(c), h = lv_obj_get_height(c);
        if (x >= cx && x < cx + w && y >= cy && y < cy + h) { if (out) *out = i; return 1; }
    }
    return 0;
}

static int rect_at(lv_obj_t *o, int x, int y) {
    if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return 0;
    int ox = lv_obj_get_x(o), oy = lv_obj_get_y(o);
    int w = lv_obj_get_width(o), h = lv_obj_get_height(o);
    return (x >= ox && x < ox + w && y >= oy && y < oy + h);
}
static int window_at(int x, int y) {
    for (int i = 0; i < MAX_WINS; i++) {
        if (!g_recs[i] || !g_recs[i]->win) continue;
        if (rect_at(g_recs[i]->win, x, y)) return 1;
    }
    return 0;
}
static int point_over_ui(int x, int y) {
    if (g_modal) return 1;
    if (g_net_panel) return 1;
    if (window_at(x, y)) return 1;
    if (g_taskbar && rect_at(g_taskbar, x, y)) return 1;
    if (g_start_menu && !lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN) && rect_at(g_start_menu, x, y)) return 1;
    return 0;
}

static char g_open_target_buf[48];

static void open_arcapp_file(const char *file) {
    lv_obj_t *w = create_window("ArcApp", 520, 320);
    lv_obj_t *t = label(w, "ArcApp Goruntuleyici", ADL_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t *b = label(w, file, ADL_TEXT_DIM);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 44);
}

static void dialog_close(void) {
    if (g_dialog) { lv_obj_delete(g_dialog); g_dialog = 0; }
    g_modal = 0;
}
static void dialog_cancel_cb(lv_event_t *e) { (void)e; dialog_close(); }
static void dialog_pick_cb(lv_event_t *e) {
    int which = (int)(intptr_t)lv_event_get_user_data(e);
    char file[48]; scopy(file, g_ow_file, 47);
    dialog_close();
    if (which < 0) {
        open_arcapp_file(file);
    } else {
        scopy(g_open_target_buf, file, 47);
        arctian_open_target = g_open_target_buf;
        open_registry_app(which);
    }
}

static void open_with_dialog(int idx) {
    dicon_t *d = &g_dicons[idx];
    char ext[16];
    int isfile = d->is_file && has_ext(d->name, ext, sizeof(ext));
    scopy(g_ow_file, d->name, 47);

    int opts[16]; int n = 0;
    if (isfile) {
        int total = app_total();
        for (int i = 0; i < total && n < 15; i++) {
            const arctian_app_t *a = app_get(i);
            if (a && s_eq(a->category, "appviewer") && app_handles_ext(i, ext))
                opts[n++] = i;
        }
    }

    g_dialog = lv_obj_create(lv_screen_active());
    base_obj(g_dialog, 0x000000, 0, 0x000000);
    lv_obj_set_style_bg_opa(g_dialog, 150, 0);
    lv_obj_set_size(g_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_align(g_dialog, LV_ALIGN_CENTER, 0, 0);
    g_modal = 1;

    int ph = 76 + (n + 1) * 42 + 48;
    lv_obj_t *panel = lv_obj_create(g_dialog);
    base_obj(panel, ADL_SURFACE, 12, ADL_BORDER);
    lv_obj_set_style_shadow_width(panel, 24, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(panel, 170, 0);
    lv_obj_set_size(panel, 420, ph);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *tt = label(panel, isfile ? "Hangisi ile acmak istersin?" : "Birlikte ac", ADL_TEXT);
    lv_obj_align(tt, LV_ALIGN_TOP_MID, 0, 10);

    for (int k = 0; k <= n; k++) {
        int which = (k == 0) ? -1 : opts[k - 1];
        const arctian_app_t *wa = (which >= 0) ? app_get(which) : 0;
        const char *nm = (k == 0) ? "ArcApp ile ac" : (wa ? wa->title : "?");
        lv_obj_t *b = lv_button_create(panel);
        lv_obj_set_size(b, 388, 34);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(k == 0 ? ADL_ACCENT : ADL_SURFACE_2), 0);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 36 + k * 42);
        lv_obj_t *l = label(b, nm, ADL_TEXT); lv_obj_center(l);
        lv_obj_add_event_cb(b, dialog_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)which);
    }

    lv_obj_t *cx = lv_button_create(panel);
    lv_obj_set_size(cx, 120, 34);
    lv_obj_set_style_radius(cx, 8, 0);
    lv_obj_set_style_bg_color(cx, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_align(cx, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_t *xl = label(cx, "Iptal", ADL_TEXT); lv_obj_center(xl);
    lv_obj_add_event_cb(cx, dialog_cancel_cb, LV_EVENT_CLICKED, 0);
}

static void menu_close(void) { if (g_context_menu) { lv_obj_delete(g_context_menu); g_context_menu = 0; } g_modal = 0; }

static void menu_delete_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < g_dicon_count && g_dicons[idx].cell) {
        lv_obj_delete(g_dicons[idx].cell);
        g_dicons[idx].cell = 0;
        g_dicons[idx].selected = 0;
    }
    menu_close();
}
static void menu_openwith_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    menu_close();
    open_with_dialog(idx);
}

static void open_context_menu(int x, int y, int idx) {
    menu_close();
    lv_obj_t *m = lv_obj_create(lv_screen_active());
    base_obj(m, ADL_SURFACE, 8, ADL_BORDER);
    lv_obj_set_style_bg_opa(m, 244, 0);
    lv_obj_set_style_shadow_width(m, 16, 0);
    lv_obj_set_style_shadow_color(m, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(m, 150, 0);
    lv_obj_set_size(m, 156, 78);
    lv_obj_set_pos(m, x, y);
    g_context_menu = m; g_modal = 1;

    lv_obj_t *b1 = lv_button_create(m);
    lv_obj_set_size(b1, 140, 30); lv_obj_set_pos(b1, 8, 6);
    lv_obj_set_style_radius(b1, 6, 0); lv_obj_set_style_bg_color(b1, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_t *l1 = label(b1, "Birlikte ac", ADL_TEXT); lv_obj_center(l1);
    lv_obj_add_event_cb(b1, menu_openwith_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *b2 = lv_button_create(m);
    lv_obj_set_size(b2, 140, 30); lv_obj_set_pos(b2, 8, 42);
    lv_obj_set_style_radius(b2, 6, 0); lv_obj_set_style_bg_color(b2, lv_color_hex(ADL_DANGER), 0);
    lv_obj_t *l2 = label(b2, "Simgeyi sil", ADL_TEXT); lv_obj_center(l2);
    lv_obj_add_event_cb(b2, menu_delete_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
}

static void open_dicon(int idx) {
    dicon_t *d = &g_dicons[idx];
    if (!d->is_file) { open_registry_app(d->app_idx); return; }

    char ext[16];
    if (!has_ext(d->name, ext, sizeof(ext))) { open_arcapp_file(d->name); return; }

    int matches = 0;
    int total = app_total();
    for (int i = 0; i < total; i++) {
        const arctian_app_t *a = app_get(i);
        if (a && s_eq(a->category, "appviewer") && app_handles_ext(i, ext)) matches++;
    }

    if (matches == 0) { open_arcapp_file(d->name); return; }
    open_with_dialog(idx);
}

static void rubber_show(int x0, int y0, int x1, int y1) {
    if (!g_rubber) {
        g_rubber = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(g_rubber);
        lv_obj_set_style_bg_color(g_rubber, lv_color_hex(ADL_ACCENT), 0);
        lv_obj_set_style_bg_opa(g_rubber, 50, 0);
        lv_obj_set_style_border_width(g_rubber, 1, 0);
        lv_obj_set_style_border_color(g_rubber, lv_color_hex(ADL_ACCENT), 0);
        lv_obj_remove_flag(g_rubber, LV_OBJ_FLAG_CLICKABLE);
    }
    int x = x0 < x1 ? x0 : x1, y = y0 < y1 ? y0 : y1;
    int w = iabs2(x0 - x1), h = iabs2(y0 - y1);
    lv_obj_set_pos(g_rubber, x, y);
    lv_obj_set_size(g_rubber, w < 2 ? 2 : w, h < 2 ? 2 : h);
    lv_obj_remove_flag(g_rubber, LV_OBJ_FLAG_HIDDEN);

    g_sel_count = 0;
    for (int i = 0; i < g_dicon_count; i++) {
        lv_obj_t *c = g_dicons[i].cell; if (!c) continue;
        int cx = lv_obj_get_x(c), cy = lv_obj_get_y(c);
        int cw = lv_obj_get_width(c), ch = lv_obj_get_height(c);
        int hit = !(cx > x + w || cx + cw < x || cy > y + h || cy + ch < y);
        icon_set_sel(i, hit);
        if (hit) g_sel_count++;
    }
}
static void rubber_hide(void) { if (g_rubber) lv_obj_add_flag(g_rubber, LV_OBJ_FLAG_HIDDEN); }

static int ic_pl = 0, ic_pr = 0;
static int ic_drag = -1, ic_dox = 0, ic_doy = 0;
static int ic_rub = 0, ic_rx = 0, ic_ry = 0;
static int ic_last_i = -1;

static void poll_icons(void) {
    int mx = g_asi->input->mouse_x();
    int my = g_asi->input->mouse_y();
    int b = g_asi->input->mouse_buttons();
    int left = b & 1, right = (b >> 1) & 1;
    int over = point_over_ui(mx, my);

    if (!over) {
        if (left && !ic_pl) {
            int idx;
            if (icon_hit(mx, my, &idx)) {
                if (ic_last_i == idx) {
                    ic_last_i = -1;
                    ic_drag = -1;
                    open_dicon(idx);
                } else {
                    ic_last_i = idx;
                    if (!(g_sel_count == 1 && g_dicons[idx].selected)) {
                        clear_selection();
                        icon_set_sel(idx, 1);
                        g_sel_count = 1;
                    }
                    ic_drag = idx;
                    ic_dox = mx - lv_obj_get_x(g_dicons[idx].cell);
                    ic_doy = my - lv_obj_get_y(g_dicons[idx].cell);
                }
            } else {
                ic_last_i = -1;
                clear_selection();
                ic_rub = 1; ic_rx = mx; ic_ry = my;
                rubber_show(ic_rx, ic_ry, mx, my);
            }
        }
        if (left && ic_drag >= 0 && g_dicons[ic_drag].cell)
            lv_obj_set_pos(g_dicons[ic_drag].cell, mx - ic_dox, my - ic_doy);
        if (left && ic_rub)
            rubber_show(ic_rx, ic_ry, mx, my);
        if (!left && ic_pl) {
            if (ic_drag >= 0 && g_dicons[ic_drag].cell) {
                dicon_t *d = &g_dicons[ic_drag];
                int gx = (lv_obj_get_x(d->cell) - GRID_X + ICON_W / 2) / ICON_W; if (gx < 0) gx = 0;
                int gy = (lv_obj_get_y(d->cell) - GRID_Y + ICON_H / 2) / ICON_H; if (gy < 0) gy = 0;
                d->gx = gx; d->gy = gy;
                icon_apply_pos(ic_drag);
            }
            if (ic_rub) { rubber_hide(); ic_rub = 0; }
            ic_drag = -1;
        }
        if (right && !ic_pr) {
            int idx;
            if (icon_hit(mx, my, &idx)) {
                if (!(g_sel_count == 1 && g_dicons[idx].selected)) {
                    clear_selection();
                    icon_set_sel(idx, 1);
                    g_sel_count = 1;
                }
                open_context_menu(mx, my, idx);
            } else {
                menu_close();
            }
        }
    } else {
        if (left && !ic_pl) ic_last_i = -1;
        if (g_context_menu && left && !ic_pl && !rect_at(g_context_menu, mx, my)) menu_close();
    }
    ic_pl = left;
    ic_pr = right;
}


static uint32_t *scale_nn(const uint32_t *src, int sw, int sh, int dw, int dh) {
    uint32_t *d = (uint32_t *)g_asi->mem->alloc((size_t)dw * dh * 4u);
    if (!d) return 0;
    for (int y = 0; y < dh; y++) {
        int sy = (int)((long)y * sh / dh);
        const uint32_t *srow = src + (size_t)sy * sw;
        uint32_t *drow = d + (size_t)y * dw;
        for (int x = 0; x < dw; x++) {
            int sx = (int)((long)x * sw / dw);
            drow[x] = srow[sx];
        }
    }
    return d;
}

static lv_obj_t *g_wallpaper = 0;
static char g_wall_active[40];

static int afs_index_of(const char *name) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->count || !g_asi->fs->name) return -1;
    int n = g_asi->fs->count();
    for (int i = 0; i < n; i++) {
        const char *nm = g_asi->fs->name(i);
        if (nm && s_eq(nm, name)) return i;
    }
    return -1;
}

static void wallpaper_show(uint32_t *scaled, int W, int H) {
    if (g_wallpaper) { lv_obj_delete(g_wallpaper); g_wallpaper = 0; }
    g_wallpaper = lv_canvas_create(lv_screen_active());
    lv_obj_remove_style_all(g_wallpaper);
    lv_obj_remove_flag(g_wallpaper, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(g_wallpaper, scaled, W, H, LV_COLOR_FORMAT_XRGB8888);
    lv_obj_set_pos(g_wallpaper, 0, 0);
    lv_obj_move_to_index(g_wallpaper, 0);
}

static int wallpaper_from(const uint8_t *data, uint32_t len) {
    uint32_t *native = 0;
    int nw = 0, nh = 0;
    if (png_decode(data, len, g_asi->mem->alloc, g_asi->mem->free, &native, &nw, &nh) != 0)
        return -1;
    int W = g_asi->gfx->width(), H = g_asi->gfx->height();
    uint32_t *scaled = scale_nn(native, nw, nh, W, H);
    g_asi->mem->free(native);
    if (!scaled) return -1;
    wallpaper_show(scaled, W, H);
    return 0;
}

static int wallpaper_load_afs(const char *name) {
    int i = afs_index_of(name);
    if (i < 0) return -1;
    uint32_t sz = g_asi->fs->size(i);
    if (sz < 8 || sz > (4u * 1024u * 1024u)) return -1;
    void *h = g_asi->fs->find(0, name);
    if (!h) return -1;
    uint8_t *buf = (uint8_t *)g_asi->mem->alloc(sz);
    if (!buf) return -1;
    uint32_t n = g_asi->fs->read(h, buf, sz);
    if (n == 0) return -1;
    return wallpaper_from(buf, n);
}

static void wallpaper_cfg_read(void) {
    g_wall_active[0] = 0;
    int i = afs_index_of("wallpaper.cfg");
    if (i < 0) return;
    uint32_t sz = g_asi->fs->size(i);
    if (sz == 0 || sz > 63) return;
    void *h = g_asi->fs->find(0, "wallpaper.cfg");
    if (!h) return;
    char buf[64];
    uint32_t n = g_asi->fs->read(h, buf, sizeof(buf) - 1);
    if (!n) return;
    buf[n] = 0;
    int e = 0; while (buf[e] && buf[e] != '\n' && buf[e] != '\r') e++;
    buf[e] = 0;
    if (buf[0]) scopy(g_wall_active, buf, 39);
}

static void wallpaper_cfg_write(const char *name) {
    if (!g_asi || !g_asi->fs) return;
    if (!name || !name[0]) {
        if (g_asi->fs->remove) g_asi->fs->remove("wallpaper.cfg");
        return;
    }
    void *h = g_asi->fs->create(0, "wallpaper.cfg", 0);
    if (h) g_asi->fs->write(h, name, (uint32_t)slen2(name));
}

static void build_wallpaper(void) {
    wallpaper_cfg_read();
    if (g_wall_active[0] && wallpaper_load_afs(g_wall_active) == 0)
        return;
    g_wall_active[0] = 0;
    wallpaper_from(wallpaper_png, wallpaper_png_len);
}

static int store_set_wallpaper(const char *afs_name) {
    if (!g_asi || !g_asi->fs) return -1;
    if (!afs_name || !afs_name[0]) {
        wallpaper_cfg_write("");
        g_wall_active[0] = 0;
        return wallpaper_from(wallpaper_png, wallpaper_png_len);
    }
    if (afs_index_of(afs_name) < 0) return -1;
    if (wallpaper_load_afs(afs_name) != 0) return -2;
    scopy(g_wall_active, afs_name, 39);
    wallpaper_cfg_write(afs_name);
    return 0;
}
static int store_wallpaper_active(void) { return g_wall_active[0] ? 1 : 0; }
static const char *store_wallpaper_name(void) { return g_wall_active; }

static int point_in_poly(float px, float py, const float *poly, int n) {
    int inside = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = poly[i*2], yi = poly[i*2+1], xj = poly[j*2], yj = poly[j*2+1];
        if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
            inside = !inside;
    }
    return inside;
}

static void build_cursor(void) {
    enum { S = 28 };
    static uint32_t buf[S * S];
    static const float outer[8] = { 0, 0, 47, 12, 26, 22, 22, 52 };
    static const float inner[8] = { 3.5f, 4, 41, 13.5f, 23.5f, 20.5f, 20.8f, 45.5f };
    const uint32_t ivory = 0xFFF4EFE2u, dark = 0xFF0B0B0Au;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float u = ((float)x + 0.5f) * 64.0f / (float)S;
            float v = ((float)y + 0.5f) * 64.0f / (float)S;
            uint32_t c = 0;
            if (point_in_poly(u, v, inner, 4)) c = ivory;
            else if (point_in_poly(u, v, outer, 4)) c = dark;
            buf[y * S + x] = c;
        }
    lv_obj_t *cur = lv_canvas_create(lv_screen_active());
    lv_obj_remove_style_all(cur);
    lv_obj_remove_flag(cur, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(cur, buf, S, S, LV_COLOR_FORMAT_ARGB8888);
    lv_indev_t *indev = lv_port_get_indev();
    if (indev) lv_indev_set_cursor(indev, cur);
}

#define MAX_DYN_APPS   32
#define DYN_FNAME_LEN  40

typedef struct {
    char     id[DPK_ID_LEN];
    char     title[48];
    char     category[32];
    char     version[16];
    char     publisher[32];
    char     desc[176];
    char     fname[DYN_FNAME_LEN];
    uint32_t load_addr;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t entry_off;
    int      win_w, win_h;
    char    *icon_svg;
    int      open;
} dyn_app_t;

static dyn_app_t g_dyn[MAX_DYN_APPS];
static int  g_dyn_count = 0;
static int  g_launch_dyn = -1;
static uint8_t g_pkg_buf[DPK_MAX_PACKAGE];

static void bin_copy(char *dst, int cap, const char *src) {
    int i = 0;
    if (cap <= 0) return;
    if (!src) { dst[0] = 0; return; }
    for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

static int hex_nib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void pct_decode(char *dst, int cap, const char *src, int len) {
    int o = 0;
    for (int i = 0; i < len && o < cap - 1; i++) {
        char c = src[i];
        if (c == '%' && i + 2 < len) {
            int hi = hex_nib(src[i + 1]), lo = hex_nib(src[i + 2]);
            if (hi >= 0 && lo >= 0) { dst[o++] = (char)((hi << 4) | lo); i += 2; continue; }
        }
        dst[o++] = c;
    }
    dst[o] = 0;
}

static int key_eq(const char *k, int klen, const char *want) {
    int i = 0;
    for (; want[i]; i++) {
        if (i >= klen) return 0;
        if (chr_low(k[i]) != chr_low(want[i])) return 0;
    }
    return i == klen;
}

static int dpk_is_package_name(const char *nm) {
    int n = slen2(nm);
    if (n < 5) return 0;
    const char *s = nm + n - 4;
    return s[0] == '.' && chr_low(s[1]) == 'd' && chr_low(s[2]) == 'p' && chr_low(s[3]) == 'k';
}

static int dpk_validate(const uint8_t *buf, uint32_t size, const dpk_header_t **out) {
    if (!buf || size < DPK_HEADER_SIZE) return -1;
    const dpk_header_t *h = (const dpk_header_t *)buf;
    if (h->magic != DPK_MAGIC || h->version != DPK_VERSION) return -1;
    int idlen = 0; while (idlen < DPK_ID_LEN && h->id[idlen]) idlen++;
    if (idlen < 1 || idlen > 26) return -1;
    if (h->image_size == 0) return -1;
    if (h->code_off != DPK_HEADER_SIZE + h->meta_len + h->icon_len) return -1;
    if (h->code_off < DPK_HEADER_SIZE || h->code_off > size) return -1;
    if ((uint64_t)h->code_off + h->image_size > size) return -1;
    if ((uint64_t)h->load_addr < APP_BASE) return -1;
    if ((uint64_t)h->load_addr + h->image_size + h->bss_size > APP_BASE + APP_MAX) return -1;
    if (dpk_checksum(buf + DPK_HEADER_SIZE, size - DPK_HEADER_SIZE) != h->checksum) return -1;
    if (out) *out = h;
    return 0;
}

static void dpk_parse_meta(const uint8_t *m, uint32_t len, dyn_app_t *d) {
    uint32_t i = 0;
    while (i < len) {
        uint32_t ls = i;
        while (i < len && m[i] != '\n' && m[i] != '\r') i++;
        uint32_t le = i;
        while (i < len && (m[i] == '\n' || m[i] == '\r')) i++;
        uint32_t eq = ls;
        while (eq < le && m[eq] != '=') eq++;
        if (eq >= le) continue;
        const char *key = (const char *)(m + ls);
        int klen = (int)(eq - ls);
        char tmp[176];
        pct_decode(tmp, sizeof(tmp), (const char *)(m + eq + 1), (int)(le - eq - 1));
        if (key_eq(key, klen, DPK_META_TITLE)) bin_copy(d->title, sizeof(d->title), tmp);
        else if (key_eq(key, klen, DPK_META_CATEGORY)) bin_copy(d->category, sizeof(d->category), tmp);
        else if (key_eq(key, klen, DPK_META_VERSION)) bin_copy(d->version, sizeof(d->version), tmp);
        else if (key_eq(key, klen, DPK_META_PUBLISHER)) bin_copy(d->publisher, sizeof(d->publisher), tmp);
        else if (key_eq(key, klen, DPK_META_DESCRIPTION)) bin_copy(d->desc, sizeof(d->desc), tmp);
        else if (key_eq(key, klen, DPK_META_WINDOW)) {
            int w = 0, h = 0, n = 0;
            const char *p = tmp;
            while (*p >= '0' && *p <= '9') { w = w * 10 + (*p - '0'); p++; n++; }
            if (*p == ',') p++;
            while (*p >= '0' && *p <= '9') { h = h * 10 + (*p - '0'); p++; n++; }
            if (n >= 2) { d->win_w = w; d->win_h = h; }
        }
    }
}

static int dyn_find(const char *id) {
    for (int i = 0; i < g_dyn_count; i++) if (s_eq(g_dyn[i].id, id)) return i;
    return -1;
}

static dyn_app_t *dyn_register(const char *id) {
    int e = dyn_find(id);
    if (e >= 0) return &g_dyn[e];
    if (g_dyn_count >= MAX_DYN_APPS) return 0;
    dyn_app_t *d = &g_dyn[g_dyn_count++];
    for (unsigned k = 0; k < sizeof(*d); k++) ((char *)d)[k] = 0;
    bin_copy(d->id, sizeof(d->id), id);
    bin_copy(d->fname, sizeof(d->fname), id);
    int l = slen2(d->fname);
    d->fname[l] = '.'; d->fname[l + 1] = 'd'; d->fname[l + 2] = 'p'; d->fname[l + 3] = 'k'; d->fname[l + 4] = 0;
    return d;
}

static int afs_read_file(const char *name, void *buf, uint32_t cap, uint32_t *out_len) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->find || !g_asi->fs->read) return -1;
    void *h = g_asi->fs->find(0, name);
    if (!h) return -1;
    uint32_t n = g_asi->fs->read(h, buf, cap);
    if (out_len) *out_len = n;
    return n > 0 ? 0 : -1;
}

static int afs_write_file(const char *name, const void *buf, uint32_t len) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create || !g_asi->fs->write) return -1;
    void *h = g_asi->fs->create(0, name, 0);
    if (!h) return -1;
    return g_asi->fs->write(h, buf, len);
}

static char *icon_dup(const uint8_t *p, uint32_t n) {
    if (!p || n == 0) return 0;
    char *s = (char *)g_asi->mem->alloc(n + 1);
    if (!s) return 0;
    for (uint32_t i = 0; i < n; i++) s[i] = (char)p[i];
    s[n] = 0;
    return s;
}

static void dyn_apply_package(dyn_app_t *d, const uint8_t *buf, uint32_t size, const dpk_header_t *h) {
    (void)size;
    const uint8_t *meta = buf + DPK_HEADER_SIZE;
    const uint8_t *icon = meta + h->meta_len;
    dpk_parse_meta(meta, h->meta_len, d);
    if (!d->title[0]) bin_copy(d->title, sizeof(d->title), d->id);
    if (!d->category[0]) bin_copy(d->category, sizeof(d->category), "Genel");
    d->load_addr = h->load_addr;
    d->image_size = h->image_size;
    d->bss_size = h->bss_size;
    d->entry_off = h->entry_off;
    if (h->icon_len) {
        char *ic = icon_dup(icon, h->icon_len);
        if (ic) d->icon_svg = ic;
    }
}

static void dyn_scan_afs(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->count) return;
    int n = g_asi->fs->count();
    for (int i = 0; i < n && g_dyn_count < MAX_DYN_APPS; i++) {
        const char *nm = g_asi->fs->name(i);
        if (!nm || !dpk_is_package_name(nm)) continue;
        uint32_t len = 0;
        if (afs_read_file(nm, g_pkg_buf, sizeof(g_pkg_buf), &len) != 0) continue;
        const dpk_header_t *h = 0;
        if (dpk_validate(g_pkg_buf, len, &h) != 0) continue;
        char id[DPK_ID_LEN];
        bin_copy(id, sizeof(id), h->id);
        dyn_app_t *d = dyn_register(id);
        if (!d) continue;
        dyn_apply_package(d, g_pkg_buf, len, h);
    }
}

static const arctian_app_t *app_get(int i) {
    if (i >= 0 && i < arctian_app_count) return &arctian_apps[i];
    int d = i - arctian_app_count;
    if (d < 0 || d >= g_dyn_count) return 0;
    dyn_app_t *a = &g_dyn[d];
    static arctian_app_t v;
    for (unsigned k = 0; k < sizeof(v); k++) ((char *)&v)[k] = 0;
    bin_copy(v.name, sizeof(v.name), a->id);
    bin_copy(v.title, sizeof(v.title), a->title[0] ? a->title : a->id);
    bin_copy(v.category, sizeof(v.category), a->category);
    v.handles = "";
    v.icon_svg = a->icon_svg ? a->icon_svg : "";
    v.entry = dyn_app_entry;
    v.win_w = a->win_w;
    v.win_h = a->win_h;
    return &v;
}

static int app_total(void) { return arctian_app_count + g_dyn_count; }

static void dyn_app_entry(lv_obj_t *win, asi_t *asi) {
    if (g_launch_dyn < 0 || g_launch_dyn >= g_dyn_count) return;
    dyn_app_t *d = &g_dyn[g_launch_dyn];
    arctian_app_entry_t fn = (arctian_app_entry_t)(uintptr_t)(d->load_addr + d->entry_off);
    fn(win, asi);
}

static void arctian_dyn_begin_launch(int idx) { g_launch_dyn = idx; }
static void arctian_dyn_mark_closed(int idx) { if (idx >= 0 && idx < g_dyn_count) g_dyn[idx].open = 0; }

static int arctian_dyn_preload(int d, char *err, int cap) {
    if (d < 0 || d >= g_dyn_count) { bin_copy(err, cap, "Gecersiz uygulama."); return -1; }
    dyn_app_t *a = &g_dyn[d];
    for (int i = 0; i < g_dyn_count; i++) {
        if (i == d) continue;
        if (g_dyn[i].open && g_dyn[i].load_addr == a->load_addr) {
            bin_copy(err, cap, "Bellek cakismasi: ayni bolgede acik bir uygulama var.");
            return -1;
        }
    }
    uint32_t len = 0;
    if (afs_read_file(a->fname, g_pkg_buf, sizeof(g_pkg_buf), &len) != 0) {
        bin_copy(err, cap, "Paket diskten okunamadi.");
        return -1;
    }
    const dpk_header_t *h = 0;
    if (dpk_validate(g_pkg_buf, len, &h) != 0) {
        bin_copy(err, cap, "Paket bozuk (dogrulama hatasi).");
        return -1;
    }
    if (h->load_addr != a->load_addr) { bin_copy(err, cap, "Paket surumu uyumsuz."); return -1; }
    uint8_t *dst = (uint8_t *)(uintptr_t)h->load_addr;
    for (uint32_t i = 0; i < h->image_size; i++) dst[i] = g_pkg_buf[h->code_off + i];
    if (h->bss_size) for (uint32_t i = 0; i < h->bss_size; i++) dst[h->image_size + i] = 0;
    return 0;
}

static void rebuild_shell_ui(void) {
    if (g_icon_layer) { lv_obj_delete(g_icon_layer); g_icon_layer = 0; }
    g_dicon_count = 0;
    if (g_start_menu) { lv_obj_delete(g_start_menu); g_start_menu = 0; }
    build_desktop_icons();
    build_start_menu(lv_screen_active());
    if (g_icon_layer) lv_obj_move_to_index(g_icon_layer, 1);
}

static int store_install(const void *dpk, uint32_t size, char *err, int errcap) {
    if (err && errcap > 0) err[0] = 0;
    const dpk_header_t *h = 0;
    if (dpk_validate((const uint8_t *)dpk, size, &h) != 0) {
        bin_copy(err, errcap, "Paket gecersiz veya bozuk.");
        return -1;
    }
    char id[DPK_ID_LEN];
    bin_copy(id, sizeof(id), h->id);
    dyn_app_t *d = dyn_register(id);
    if (!d) { bin_copy(err, errcap, "Uygulama limiti doldu."); return -1; }
    if (afs_write_file(d->fname, dpk, size) != 0) {
        bin_copy(err, errcap, "Diske yazilamadi (AFS).");
        return -1;
    }
    dyn_apply_package(d, (const uint8_t *)dpk, size, h);
    rebuild_shell_ui();
    return 0;
}

static int store_uninstall(const char *id) {
    int e = dyn_find(id);
    if (e < 0) return -1;
    if (g_dyn[e].open) return -2;
    if (g_asi && g_asi->fs && g_asi->fs->remove) g_asi->fs->remove(g_dyn[e].fname);
    for (int i = e; i < g_dyn_count - 1; i++) g_dyn[i] = g_dyn[i + 1];
    g_dyn_count--;
    rebuild_shell_ui();
    return 0;
}

static int store_launch(const char *id) {
    int e = dyn_find(id);
    if (e < 0) return -1;
    open_registry_app(arctian_app_count + e);
    return 0;
}

static int store_installed(const char *id) { return dyn_find(id) >= 0 ? 1 : 0; }

static const char *store_installed_version(const char *id) {
    int e = dyn_find(id);
    return e < 0 ? "" : g_dyn[e].version;
}
static int store_count_installed(void) { return g_dyn_count; }
static const char *store_installed_id(int i) { return (i >= 0 && i < g_dyn_count) ? g_dyn[i].id : ""; }
static const char *store_installed_title(int i) {
    return (i >= 0 && i < g_dyn_count) ? (g_dyn[i].title[0] ? g_dyn[i].title : g_dyn[i].id) : "";
}
static const char *store_installed_version_at(int i) {
    return (i >= 0 && i < g_dyn_count) ? g_dyn[i].version : "";
}

static arctian_store_api_t g_store_api = {
    store_install, store_uninstall, store_launch, store_installed,
    store_installed_version, store_count_installed, store_installed_id,
    store_installed_title, store_installed_version_at, rebuild_shell_ui,
    store_set_wallpaper, store_wallpaper_active, store_wallpaper_name
};
arctian_store_api_t *arctian_store = &g_store_api;

const char *arctian_net_http_get(void *asi, const char *url, uint32_t *len) {
    asi_t *a = (asi_t *)asi;
    return (a && a->net && a->net->http_get) ? a->net->http_get(url, len) : 0;
}
const char *arctian_net_https_get(void *asi, const char *url, uint32_t *len) {
    asi_t *a = (asi_t *)asi;
    return (a && a->net && a->net->https_get) ? a->net->https_get(url, len) : 0;
}
int arctian_net_link_type(void *asi) {
    asi_t *a = (asi_t *)asi;
    return (a && a->net && a->net->link_type) ? a->net->link_type() : 0;
}

#ifdef ARCTIAN_DS_TEST
static inline void dst_outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p));
}
static inline uint8_t dst_inb(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v;
}
static void dst_puts(const char *s) {
    for (; s && *s; s++) {
        while (!(dst_inb(0x3FD) & 0x20)) { }
        dst_outb(0x3F8, (uint8_t)*s);
    }
}
static void dst_putx(uint32_t v) {
    static const char h[] = "0123456789abcdef";
    char b[9]; int i = 0;
    if (v == 0) { dst_puts("0"); return; }
    while (v && i < 8) { b[i++] = h[v & 0xF]; v >>= 4; }
    while (i) { char c = b[--i]; while (!(dst_inb(0x3FD) & 0x20)) { } dst_outb(0x3F8, (uint8_t)c); }
}

static void duckstore_selftest(void) {
    dst_puts("[ds] selftest start\n");
    if (!g_asi || !g_asi->net || !g_asi->net->http_get) { dst_puts("[ds] no net\n"); return; }
    if (!arctian_store || !arctian_store->install) { dst_puts("[ds] no api\n"); return; }

    {
        dst_puts("[ds] wall start\n");
        uint32_t cap = 6u * 1024u * 1024u + 8u * 1024u;
        uint8_t *wb = (uint8_t *)g_asi->mem->alloc(cap);
        uint32_t wl = 0;
        int dr = (wb && g_asi->net->download)
                 ? g_asi->net->download("http://10.0.2.2:8091/wallpapers/detay.png",
                                        wb, cap, &wl)
                 : -100;
        dst_puts("[ds] wall dl rc="); dst_putx((uint32_t)dr);
        dst_puts(" bytes="); dst_putx(wl); dst_puts("\n");
        if (dr == 0 && wl > 0) {
            void *h = g_asi->fs->create(0, "detay.wp", 0);
            int wr = h ? g_asi->fs->write(h, wb, wl) : -1;
            dst_puts("[ds] wall save rc="); dst_putx((uint32_t)wr); dst_puts("\n");
            int sr = arctian_store->set_wallpaper("detay.wp");
            dst_puts("[ds] wall set rc="); dst_putx((uint32_t)sr);
            dst_puts(" active="); dst_putx((uint32_t)arctian_store->wallpaper_active());
            dst_puts(" name="); dst_puts(arctian_store->wallpaper_name()); dst_puts("\n");
        }
    }

    uint32_t len = 0;
    const char *body = g_asi->net->http_get("http://10.0.2.2:8091/packages/hesapmakinesi.dpk", &len);
    if (!body || !len) { dst_puts("[ds] fetch FAIL\n"); return; }
    dst_puts("[ds] fetched len="); dst_putx(len); dst_puts("\n");

    char err[110];
    int r = arctian_store->install(body, len, err, sizeof(err));
    dst_puts("[ds] install rc="); dst_putx((uint32_t)r);
    dst_puts(" err="); dst_puts(err); dst_puts("\n");

    dst_puts("[ds] installed="); dst_putx((uint32_t)arctian_store->installed("hesapmakinesi"));
    dst_puts(" count="); dst_putx((uint32_t)arctian_store->count_installed()); dst_puts("\n");

    char e2[110];
    e2[0] = 0;
    int r2 = arctian_dyn_preload(0, e2, sizeof(e2));
    dst_puts("[ds] preload rc="); dst_putx((uint32_t)r2);
    dst_puts(" err="); dst_puts(e2); dst_puts("\n");

    uint8_t *code = (uint8_t *)(uintptr_t)0x01800000u;
    uint32_t sig = 0;
    for (int i = 0; i < 16; i++) sig = (sig << 1) ^ code[i];
    dst_puts("[ds] code_sig="); dst_putx(sig); dst_puts("\n");

    arctian_dyn_begin_launch(0);
    const arctian_app_t *av = app_get(arctian_app_count);
    dst_puts("[ds] entry_off="); dst_putx(g_dyn[0].entry_off);
    dst_puts(" load="); dst_putx(g_dyn[0].load_addr);
    dst_puts(" img="); dst_putx(g_dyn[0].image_size);
    dst_puts(" bss="); dst_putx(g_dyn[0].bss_size);
    dst_puts("\n");
    if (av && av->entry) {
        lv_obj_t *tmp = lv_obj_create(lv_screen_active());
        lv_obj_set_size(tmp, 300, 400);
        dst_puts("[ds] calling entry\n");
        av->entry(tmp, g_asi);
        dst_puts("[ds] entry_ok\n");
    } else {
        dst_puts("[ds] entry MISSING\n");
    }

    {
        uint32_t zl = 0;
        const char *zb = g_asi->net->http_get("http://10.0.2.2:8091/packages/zigornek.dpk", &zl);
        if (!zb || !zl) {
            dst_puts("[ds] zig fetch FAIL\n");
        } else {
            char zerr[110];
            zerr[0] = 0;
            int zr = arctian_store->install(zb, zl, zerr, sizeof(zerr));
            dst_puts("[ds] zig install rc="); dst_putx((uint32_t)zr);
            dst_puts(" err="); dst_puts(zerr); dst_puts("\n");
            int di = dyn_find("zigornek");
            dst_puts("[ds] zig idx="); dst_putx((uint32_t)di); dst_puts("\n");
            if (di >= 0) {
                char ze[110];
                ze[0] = 0;
                int pr = arctian_dyn_preload(di, ze, sizeof(ze));
                dst_puts("[ds] zig preload rc="); dst_putx((uint32_t)pr);
                dst_puts(" err="); dst_puts(ze); dst_puts("\n");
                arctian_dyn_begin_launch(di);
                const arctian_app_t *zv = app_get(arctian_app_count + di);
                if (zv && zv->entry) {
                    lv_obj_t *zt = lv_obj_create(lv_screen_active());
                    lv_obj_set_size(zt, 400, 260);
                    dst_puts("[ds] zig calling entry\n");
                    zv->entry(zt, g_asi);
                    dst_puts("[ds] zig entry_ok\n");
                } else {
                    dst_puts("[ds] zig entry MISSING\n");
                }
            }
        }
    }
    dst_puts("[ds] selftest end\n");
}
#endif


void desktop_main(void *arg) __attribute__((section(".text.entry")));
void desktop_main(void *arg) {
    g_asi = (asi_t *)arg;

    lv_port_init(g_asi);
    adl_font_init();
    adl_wolf_theme_init();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(ADL_BG), 0);
    lv_obj_set_style_text_font(scr, adl_font(), 0);

    build_wallpaper();
    dyn_scan_afs();
    build_desktop_icons();
    build_start_menu(scr);
    build_taskbar(scr);
    build_cursor();

#ifdef ARCTIAN_DS_TEST
    duckstore_selftest();
#endif

    lv_obj_t *w = create_window("Arctian'a Hos Geldiniz", 560, 300);
    lv_obj_t *body = label(w,
        "Beyaz Kurt temali Arctian masaustu.\n\n"
        "Pencereyi basligindan tutup tasiyabilir,\n"
        "sag ustteki kucult / buyut / kapat tuslarini kullanabilirsin.\n\n"
        "Bu pencereyi sag ustteki X ile kapatabilirsin.",
        ADL_TEXT_DIM);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 20, 56);

    for (;;) {
        lv_port_tick_advance(16);
        poll_icons();
        lv_timer_handler();
        g_asi->gfx->present();
        for (volatile int i = 0; i < 200000; i++) asm volatile("nop");
    }
}
