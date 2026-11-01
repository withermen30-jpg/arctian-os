/**
 * Wolf calisma zamani — libWolf <-> LVGL koprusu (0.7)
 *
 * `.wlf` uygulamalari LVGL gormez. Onlara `wolf_api_t` tablosu verilir; bu
 * dosya tablodaki her islevi gercek LVGL nesnelerine baglar. Uygulama ile
 * cekirdek arasindaki katman budur (Windows'ta kernel32/user32 ne ise).
 *
 * Widget kimligi `wolf_id` = tablodaki 1-tabanli indeks. 0 gecersizdir.
 * Widget silinince kimlik de gecersiz olur (yeniden kullanilmaz).
 *
 * Izinler: fs/net/exec cagrilari uygulamanin `dyn_perms_for` degerine gore
 * reddedilir.
 */
#include "dsk.h"
#include "png.h"

#define WOLF_MAX_WIDGETS 192
#define WOLF_MAX_TEXT    256

typedef struct {
    lv_obj_t *obj;
    int       used;
    int       kind;
    char      text[WOLF_MAX_TEXT];
} wolf_slot_t;

static wolf_slot_t  s_slot[WOLF_MAX_WIDGETS];
static int          s_n;
static lv_obj_t    *s_root;
static lv_obj_t    *s_body;
static asi_t       *s_asi;
static wolf_api_t   s_api;
static int          s_api_ready;
/* 0.7: terminal modu (rol=cli/runtime). */
static void        (*s_sink)(const char *s);
static const char   *s_arg = "";

void wlf_host_set_sink(void (*fn)(const char *s)) { s_sink = fn; }
void wlf_host_set_arg(const char *arg) { s_arg = arg ? arg : ""; }

static void sink_line(const char *prefix, const char *s) {
    if (!s_sink) return;
    if (prefix && prefix[0]) s_sink(prefix);
    s_sink(s ? s : "");
    s_sink("\n");
}

/* Olay kayitlari: widget basina tek tik + tek degisim (basit ve yeterli). */
typedef struct {
    int          wid;
    wolf_event_fn cb;
    void        *user;
} wolf_ev_t;

#define WOLF_MAX_EV 128
static wolf_ev_t s_ev_click[WOLF_MAX_EV];
static wolf_ev_t s_ev_change[WOLF_MAX_EV];
static int       s_ev_click_n, s_ev_change_n;

/* ------------------------------------------------------------------ */
/* Yardimcilar                                                         */
/* ------------------------------------------------------------------ */

static int starts_with(const char *s, const char *p) {
    if (!s || !p) return 0;
    for (int i = 0; p[i]; i++) if (s[i] != p[i]) return 0;
    return 1;
}

/* Komut beyaz listesi eskiden sadece on ek bakuyordu; "lsmeh" "ls" kuralina
 * uyuyordu. Burada eslesmenin ardindan bosluk veya satir sonu gelmeli. */
static int word_is(const char *s, const char *p) {
    if (!starts_with(s, p)) return 0;
    int i = 0; while (p[i]) i++;
    char c = s[i];
    return (c == 0 || c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static int h_now(int *y, int *mo, int *d, int *hh, int *mi, int *ss);
static int h_list_selected(wolf_id id);
static void list_row_cb(lv_event_t *e);
static void anim_release_obj(lv_obj_t *o); /* v2 animasyon temizligi */
static void anim_opa_cb(void *var, int32_t v);
static void anim_ty_cb(void *var, int32_t v);
static int s_trans_n;                   /* gecis arenasi doluluk sayaci */


static wolf_slot_t *slot_of(wolf_id id) {
    if (id <= 0 || id > WOLF_MAX_WIDGETS) return 0;
    wolf_slot_t *s = &s_slot[id - 1];
    if (!s->used || !s->obj) return 0;
    return s;
}

static wolf_id slot_new(lv_obj_t *obj, int kind) {
    if (!obj) return 0;
    for (int i = 0; i < WOLF_MAX_WIDGETS; i++) {
        if (!s_slot[i].used) {
            s_slot[i].used = 1;
            s_slot[i].obj = obj;
            s_slot[i].kind = kind;
            s_slot[i].text[0] = 0;
            if (i + 1 > s_n) s_n = i + 1;
            return i + 1;
        }
    }
    lv_obj_delete(obj);
    return 0;
}

static lv_obj_t *parent_of(wolf_id p) {
    if (p == 0) return s_root ? s_root : s_body;
    wolf_slot_t *s = slot_of(p);
    return s ? s->obj : (s_root ? s_root : s_body);
}

/* Basit stil: masaustu temasina uyumlu, seffaf panel. */
static lv_obj_t *mk_box(lv_obj_t *parent, int x, int y, int w, int h, int click) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 10, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    if (click) lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void ev_click_cb(lv_event_t *e) {
    int wid = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < s_ev_click_n; i++) {
        if (s_ev_click[i].wid == wid && s_ev_click[i].cb) {
            s_ev_click[i].cb(wid, s_ev_click[i].user);
            return;
        }
    }
}

static void ev_change_cb(lv_event_t *e) {
    int wid = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < s_ev_change_n; i++) {
        if (s_ev_change[i].wid == wid && s_ev_change[i].cb) {
            s_ev_change[i].cb(wid, s_ev_change[i].user);
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Pencere                                                             */
/* ------------------------------------------------------------------ */

static wolf_id h_root(void) {
    if (!s_root) return 0;
    return 1;   /* kok her zaman 1 (slot 0) */
}

static void h_win_title(const char *title) {
    if (!title || !s_body) return;
    winrec_t *r = winrec_of(s_body);
    if (r) {
        bin_copy(r->title, sizeof(r->title), title);
        /* Baslik etiketi yeniden cizilir: pencereyi yeniden kurmak yerine
         * sadece gorev cubugu/indicator tazelenir. */
        update_app_indicators();
    }
}

static void h_win_size(int w, int h) {
    if (!s_root || w < 120 || h < 80) return;
    lv_obj_set_size(s_root, w, h);
}

/* ------------------------------------------------------------------ */
/* Cizim                                                               */
/* ------------------------------------------------------------------ */

static wolf_id h_panel(wolf_id p, int x, int y, int w, int h) {
    lv_obj_t *o = mk_box(parent_of(p), x, y, w, h, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(ADL_BORDER), 0);
    return slot_new(o, 1);
}

static wolf_id h_label(wolf_id p, int x, int y, const char *text) {
    lv_obj_t *o = lv_label_create(parent_of(p));
    lv_label_set_text(o, text ? text : "");
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_text_color(o, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_text_font(o, adl_font(), 0);
    wolf_id id = slot_new(o, 2);
    wolf_slot_t *s = slot_of(id);
    if (s) bin_copy(s->text, sizeof(s->text), text ? text : "");
    return id;
}

static wolf_id h_button(wolf_id p, int x, int y, int w, int h, const char *text) {
    lv_obj_t *b = lv_button_create(parent_of(p));
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 9, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(l, adl_font(), 0);
    lv_obj_center(l);
    wolf_id id = slot_new(b, 3);
    wolf_slot_t *s = slot_of(id);
    if (s) bin_copy(s->text, sizeof(s->text), text ? text : "");
    /* CLICK olayi h_on_click'te tek yerden baglanir (cift tetiklemeyi
     * onlemek icin burada baglanmaz). */
    return id;
}

static wolf_id h_input(wolf_id p, int x, int y, int w, int h, const char *ph) {
    lv_obj_t *t = lv_textarea_create(parent_of(p));
    lv_obj_set_pos(t, x, y);
    lv_obj_set_size(t, w, h);
    lv_textarea_set_one_line(t, true);
    lv_textarea_set_max_length(t, 200);
    if (ph) lv_textarea_set_placeholder_text(t, ph);
    lv_obj_set_style_radius(t, 8, 0);
    lv_obj_set_style_bg_color(t, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_text_font(t, adl_font(), 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_border_color(t, lv_color_hex(ADL_BORDER), 0);
    wolf_id id = slot_new(t, 4);
    lv_obj_add_event_cb(t, ev_change_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)id);
    return id;
}

/* Liste: LVGL'in deprecated lv_list'i yerine kendi dikey akisimiz.
 * Satirlar tiklanabilir; secim indeksi slot metninde tutulur. */
static wolf_id h_listbox(wolf_id p, int x, int y, int w, int h) {
    lv_obj_t *l = lv_obj_create(parent_of(p));
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, h);
    lv_obj_set_style_radius(l, 9, 0);
    lv_obj_set_style_bg_color(l, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l, 1, 0);
    lv_obj_set_style_border_color(l, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_pad_all(l, 4, 0);
    lv_obj_set_style_pad_row(l, 3, 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    return slot_new(l, 5);
}

static wolf_id h_image(wolf_id p, int x, int y, int w, int h, const char *path) {
    if (!path || !s_asi || !s_asi->fs || !s_asi->fs->read_path || !s_asi->mem) return 0;
    uint32_t sz = 0;
    asi_stat_t st;
    if (s_asi->fs->stat && s_asi->fs->stat(path, &st) == 0) sz = st.size;
    if (sz == 0 || sz > 512u * 1024u) return 0;
    uint8_t *buf = (uint8_t *)s_asi->mem->alloc(sz);
    if (!buf) return 0;
    uint32_t n = s_asi->fs->read_path(path, buf, sz);
    uint32_t *argb = 0;
    int iw = 0, ih = 0;
    if (n == 0 || png_decode(buf, n, s_asi->mem->alloc, s_asi->mem->free,
                             &argb, &iw, &ih) != 0) {
        s_asi->mem->free(buf);
        return 0;
    }
    s_asi->mem->free(buf);
    lv_obj_t *c = lv_canvas_create(parent_of(p));
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, iw, ih);
    lv_canvas_set_buffer(c, argb, iw, ih, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_add_event_cb(c, canvas_buf_free_cb, LV_EVENT_DELETE, argb);
    if (w > 0 && h > 0) lv_obj_set_size(c, w, h);
    return slot_new(c, 6);
}

static wolf_id h_progress(wolf_id p, int x, int y, int w, int h, int pct) {
    lv_obj_t *b = lv_bar_create(parent_of(p));
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_bar_set_range(b, 0, 100);
    lv_bar_set_value(b, pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    return slot_new(b, 7);
}

static wolf_id h_slider(wolf_id p, int x, int y, int w, int h, int pct) {
    lv_obj_t *s = lv_slider_create(parent_of(p));
    lv_obj_set_pos(s, x, y);
    lv_obj_set_size(s, w, h);
    lv_slider_set_range(s, 0, 100);
    lv_slider_set_value(s, pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s, lv_color_hex(ADL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, lv_color_hex(ADL_ACCENT), LV_PART_KNOB);
    wolf_id id = slot_new(s, 8);
    lv_obj_add_event_cb(s, ev_change_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)id);
    return id;
}

static wolf_id h_separator(wolf_id p, int x, int y, int w) {
    lv_obj_t *o = lv_obj_create(parent_of(p));
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, 1);
    lv_obj_set_style_bg_color(o, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return slot_new(o, 9);
}

static wolf_id h_checkbox(wolf_id p, int x, int y, const char *text, int checked) {
    lv_obj_t *c = lv_checkbox_create(parent_of(p));
    lv_obj_set_pos(c, x, y);
    lv_checkbox_set_text(c, text ? text : "");
    if (checked) lv_obj_add_state(c, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(c, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_text_font(c, adl_font(), 0);
    wolf_id id = slot_new(c, 10);
    lv_obj_add_event_cb(c, ev_change_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)id);
    return id;
}

/* ------------------------------------------------------------------ */
/* Ozellikler                                                          */
/* ------------------------------------------------------------------ */

static lv_obj_t *label_child(lv_obj_t *o) {
    if (!o) return 0;
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(o, (int32_t)i);
        if (c && lv_obj_check_type(c, &lv_label_class)) return c;
    }
    return 0;
}

static void h_set_text(wolf_id id, const char *text) {
    wolf_slot_t *s = slot_of(id);
    if (!s || !text) return;
    bin_copy(s->text, sizeof(s->text), text);
    if (s->kind == 3 || s->kind == 10) {
        lv_obj_t *l = label_child(s->obj);
        if (l) lv_label_set_text(l, text);
        if (s->kind == 10) lv_checkbox_set_text(s->obj, text);
    } else if (s->kind == 4) {
        lv_textarea_set_text(s->obj, text);
    } else {
        lv_label_set_text(s->obj, text);
    }
}

static int h_get_text(wolf_id id, char *out, int cap) {
    wolf_slot_t *s = slot_of(id);
    if (!s || !out || cap <= 0) return -1;
    const char *src = s->text;
    if (s->kind == 4) src = lv_textarea_get_text(s->obj);
    else if (s->kind == 3) {
        lv_obj_t *l = label_child(s->obj);
        if (l) src = lv_label_get_text(l);
    } else if (s->kind == 10) src = lv_checkbox_get_text(s->obj);
    else if (s->kind != 2) src = lv_label_get_text(s->obj);
    bin_copy(out, cap, src ? src : "");
    return slen2(out);
}

static void h_set_value(wolf_id id, int v) {
    wolf_slot_t *s = slot_of(id);
    if (!s) return;
    if (s->kind == 7) lv_bar_set_value(s->obj, v, LV_ANIM_OFF);
    else if (s->kind == 8) lv_slider_set_value(s->obj, v, LV_ANIM_OFF);
    else if (s->kind == 12) lv_arc_set_value(s->obj, v);
    else if (s->kind == 10) {
        if (v) lv_obj_add_state(s->obj, LV_STATE_CHECKED);
        else lv_obj_remove_state(s->obj, LV_STATE_CHECKED);
    }
}

static int h_get_value(wolf_id id) {
    wolf_slot_t *s = slot_of(id);
    if (!s) return 0;
    if (s->kind == 7) return (int)lv_bar_get_value(s->obj);
    if (s->kind == 8) return (int)lv_slider_get_value(s->obj);
    if (s->kind == 12) return (int)lv_arc_get_value(s->obj);
    if (s->kind == 10) return lv_obj_has_state(s->obj, LV_STATE_CHECKED) ? 1 : 0;
    if (s->kind == 5) return h_list_selected(id);
    return 0;
}

static void h_set_pos(wolf_id id, int x, int y) {
    wolf_slot_t *s = slot_of(id);
    if (s) lv_obj_set_pos(s->obj, x, y);
}

static void h_set_size(wolf_id id, int w, int h) {
    wolf_slot_t *s = slot_of(id);
    if (s && w > 0 && h > 0) lv_obj_set_size(s->obj, w, h);
}

static void h_set_color(wolf_id id, uint32_t rgb) {
    wolf_slot_t *s = slot_of(id);
    if (!s) return;
    lv_obj_set_style_bg_color(s->obj, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(s->obj, LV_OPA_COVER, 0);
    if (s->kind == 2) lv_obj_set_style_text_color(s->obj, lv_color_hex(rgb), 0);
}

static void h_set_visible(wolf_id id, int vis) {
    wolf_slot_t *s = slot_of(id);
    if (!s) return;
    if (vis) lv_obj_remove_flag(s->obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s->obj, LV_OBJ_FLAG_HIDDEN);
}

static void h_set_enabled(wolf_id id, int en) {
    wolf_slot_t *s = slot_of(id);
    if (!s) return;
    if (en) lv_obj_remove_state(s->obj, LV_STATE_DISABLED);
    else lv_obj_add_state(s->obj, LV_STATE_DISABLED);
}

static void h_clear(wolf_id id) {
    wolf_slot_t *s = slot_of(id);
    if (!s) return;
    lv_obj_clean(s->obj);
}

static void h_destroy(wolf_id id) {
    wolf_slot_t *s = slot_of(id);
    if (!s) return;
    anim_release_obj(s->obj);
    lv_obj_delete(s->obj);
    s->used = 0;
    s->obj = 0;
    for (int i = 0; i < s_ev_click_n; i++)
        if (s_ev_click[i].wid == id) s_ev_click[i].cb = 0;
    for (int i = 0; i < s_ev_change_n; i++)
        if (s_ev_change[i].wid == id) s_ev_change[i].cb = 0;
}

static void h_on_click(wolf_id id, wolf_event_fn cb, void *user) {
    wolf_slot_t *s = slot_of(id);
    if (!s || !cb) return;
    int found = -1;
    for (int i = 0; i < s_ev_click_n; i++)
        if (s_ev_click[i].wid == id) { found = i; break; }
    if (found < 0) {
        if (s_ev_click_n >= WOLF_MAX_EV) return;
        found = s_ev_click_n++;
        s_ev_click[found].wid = id;
        /* LV olayi ilk kayitta ve YALNIZ once baglanir. Not: duz panel
         * sinifi LVGL'de varsayilan olarak CLICKABLE oldugundan bayrak
         * kontrolu ile ayirt edilemez; tek-baglanti kayit tablosu ile
         * saglanir (cift tiklama yaniti boylece olusmaz). */
        lv_obj_add_flag(s->obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s->obj, ev_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)id);
    }
    s_ev_click[found].cb = cb;
    s_ev_click[found].user = user;
}

static void h_on_change(wolf_id id, wolf_event_fn cb, void *user) {
    if (!slot_of(id) || !cb) return;
    for (int i = 0; i < s_ev_change_n; i++) {
        if (s_ev_change[i].wid == id) {
            s_ev_change[i].cb = cb;
            s_ev_change[i].user = user;
            return;
        }
    }
    if (s_ev_change_n >= WOLF_MAX_EV) return;
    s_ev_change[s_ev_change_n].wid = id;
    s_ev_change[s_ev_change_n].cb = cb;
    s_ev_change[s_ev_change_n].user = user;
    s_ev_change_n++;
}

/* ------------------------------------------------------------------ */
/* Liste                                                               */
/* ------------------------------------------------------------------ */

/* Liste satiri secildi: indeksi slot metnine yaz ("sel:N"). */
static void list_row_cb(lv_event_t *e) {
    wolf_id id = (wolf_id)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *row = lv_event_get_target(e);
    wolf_slot_t *s = slot_of(id);
    if (!s || !row) return;
    int idx = (int)lv_obj_get_index(row);
    s->text[0] = 's'; s->text[1] = 'e'; s->text[2] = 'l'; s->text[3] = ':';
    s->text[4] = (char)('0' + (idx % 10));
    s->text[5] = 0;
    for (int i = 0; i < s_ev_click_n; i++)
        if (s_ev_click[i].wid == id && s_ev_click[i].cb)
            s_ev_click[i].cb(id, s_ev_click[i].user);
}

static void h_list_add(wolf_id id, const char *item) {
    wolf_slot_t *s = slot_of(id);
    if (!s || s->kind != 5 || !item) return;
    lv_obj_t *row = lv_obj_create(s->obj);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(row, 7, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, item);
    lv_obj_set_style_text_color(lbl, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_text_font(lbl, adl_font(), 0);
    lv_obj_add_event_cb(row, list_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
}

static void h_list_clear(wolf_id id) {
    wolf_slot_t *s = slot_of(id);
    if (s && s->kind == 5) lv_obj_clean(s->obj);
}

static int h_list_count(wolf_id id) {
    wolf_slot_t *s = slot_of(id);
    if (!s || s->kind != 5) return 0;
    return (int)lv_obj_get_child_count(s->obj);
}

static int h_list_item(wolf_id id, int index, char *out, int cap) {
    wolf_slot_t *s = slot_of(id);
    if (!s || s->kind != 5 || index < 0) return -1;
    lv_obj_t *btn = lv_obj_get_child(s->obj, index);
    if (!btn) return -1;
    lv_obj_t *l = label_child(btn);
    bin_copy(out, cap, l ? lv_label_get_text(l) : "");
    return 0;
}

static int h_list_selected(wolf_id id) {
    wolf_slot_t *s = slot_of(id);
    if (!s || s->kind != 5) return -1;
    /* h_list_item_cb secimi s->text icinde tutar: "sel:<indeks>" */
    if (s->text[0] == 's' && s->text[1] == 'e' && s->text[2] == 'l' &&
        s->text[3] == ':')
        return (int)(s->text[4] - '0');
    return -1;
}

/* ------------------------------------------------------------------ */
/* Dosya sistemi / sistem                                              */
/* ------------------------------------------------------------------ */

static int h_fs_read(const char *path, void *buf, int cap) {
    if (!s_asi || !s_asi->fs || !s_asi->fs->read_path || !path) return -1;
    /* 0.7: izin yetkiden degil, kapidan gelir (perm.c / caps_fs_guard). */
    return (int)s_asi->fs->read_path(path, buf, (uint32_t)cap);
}

static int h_fs_write(const char *path, const void *buf, int len) {
    if (!s_asi || !s_asi->fs || !s_asi->fs->create_in || !s_asi->fs->write || !path) return -1;
    char dir[200], name[80];
    int last = -1;
    bin_copy(dir, sizeof(dir), path);
    for (int i = 0; dir[i]; i++) if (dir[i] == '/') last = i;
    if (last <= 0) return -1;
    bin_copy(name, sizeof(name), path + last + 1);
    dir[last] = 0;
    if (s_asi->fs->mkdir) s_asi->fs->mkdir(dir);
    void *h = s_asi->fs->create_in(dir, name, 0);
    if (!h) return -1;
    return s_asi->fs->write(h, buf, (uint32_t)len);
}

static int h_fs_list(const char *dir, int index, char *name, int cap, int *is_dir) {
    if (!s_asi || !s_asi->fs || !s_asi->fs->list_dir) return -1;
    return s_asi->fs->list_dir(dir, index, name, cap, is_dir);
}

static int h_fs_count(const char *dir) {
    if (!s_asi || !s_asi->fs || !s_asi->fs->count_dir) return 0;
    return s_asi->fs->count_dir(dir);
}

static int h_fs_exists(const char *path) {
    if (!s_asi || !s_asi->fs || !s_asi->fs->stat) return 0;
    asi_stat_t st;
    return s_asi->fs->stat(path, &st) == 0;
}

static int h_fs_remove(const char *path) {
    if (!s_asi || !s_asi->fs || !s_asi->fs->remove) return -1;
    return s_asi->fs->remove(path);
}

static int h_fs_mkdir(const char *path) {
    if (!s_asi || !s_asi->fs || !s_asi->fs->mkdir) return -1;
    return s_asi->fs->mkdir(path);
}

static int h_fs_rename(const char *a, const char *b) {
    if (!s_asi || !s_asi->fs || !s_asi->fs->rename) return -1;
    return s_asi->fs->rename(a, b);
}

static void h_log(const char *text) {
    if (!text) return;
    if (s_sink) { sink_line("[wlf] ", text); return; }
    if (s_asi && s_asi->console && s_asi->console->puts) {
        s_asi->console->puts("[wlf] ");
        s_asi->console->puts(text);
        s_asi->console->puts("\n");
    }
}

static void h_notify(const char *title, const char *msg) {
    if (s_sink) { sink_line(title ? title : "Uygulama", msg ? msg : ""); return; }
    arctian_notify(title ? title : "Uygulama", msg ? msg : "");
}

static void h_dialog(const char *title, const char *msg, const char *ok) {
    (void)ok;
    if (s_sink) { sink_line(title ? title : "Uygulama", msg ? msg : ""); return; }
    arctian_notify(title ? title : "Uygulama", msg ? msg : "");
}

static const char *h_arg(void) { return s_arg ? s_arg : ""; }

static int h_exec(const char *cmd, char *out, int cap) {
    if (!cmd) return -1;
    /* Komut kapisi - karar deposu veya onay penceresi.     */
    if (perm_exec(cmd)) return -2;
    /* Kabuk alt kumesi: yalnizca bilgi komutlari (guvenlik). */
    const char *allowed[] = { "date", "sysinfo", "mem", "whoami", "pwd",
                              "ls", "ip", "apps", "uptime", 0 };
    int okcmd = 0;
    for (int i = 0; allowed[i]; i++)
        if (word_is(cmd, allowed[i])) { okcmd = 1; break; }
    if (!okcmd) {
        bin_copy(out, cap, "izin yok");
        return -1;
    }
    /* exec'i kabuga yonlendirmek yerine dogrudan yanitla (kabuk yok). */
    if (word_is(cmd, "date")) {
        int y, mo, d, hh, mi, ss;
        if (h_now(&y, &mo, &d, &hh, &mi, &ss) == 0) {
            char b[40];
            int n = 0;
            for (int k = 0; k < 4; k++) b[n++] = (char)('0' + (y / (k == 0 ? 1000 : (k == 1 ? 100 : (k == 2 ? 10 : 1)))) % 10);
            b[n++] = '-';
            b[n++] = (char)('0' + (mo / 10) % 10);
            b[n++] = (char)('0' + mo % 10);
            b[n++] = '-';
            b[n++] = (char)('0' + (d / 10) % 10);
            b[n++] = (char)('0' + d % 10);
            b[n++] = ' ';
            b[n++] = (char)('0' + (hh / 10) % 10);
            b[n++] = (char)('0' + hh % 10);
            b[n++] = ':';
            b[n++] = (char)('0' + (mi / 10) % 10);
            b[n++] = (char)('0' + mi % 10);
            b[n] = 0;
            bin_copy(out, cap, b);
            return 0;
        }
    }
    if (word_is(cmd, "whoami")) {
        bin_copy(out, cap, arctian_auth_current());
        return 0;
    }
    if (word_is(cmd, "pwd")) {
        bin_copy(out, cap, "C:/");
        return 0;
    }
    if (word_is(cmd, "sysinfo")) {
        bin_copy(out, cap, "Arctian 0.7 WLF runtime");
        return 0;
    }
    bin_copy(out, cap, "");
    return 0;
}

static uint64_t h_ticks(void) {
    return (s_asi && s_asi->time && s_asi->time->ticks_ms)
           ? s_asi->time->ticks_ms() : 0;
}

static void h_sleep(uint32_t ms) {
    if (s_asi && s_asi->time && s_asi->time->sleep_ms) s_asi->time->sleep_ms(ms);
}

static int h_now(int *y, int *mo, int *d, int *hh, int *mi, int *ss) {
    if (!s_asi || !s_asi->time || !s_asi->time->now) return -1;
    asi_datetime_t dt;
    if (!s_asi->time->now(&dt)) return -1;
    if (y) *y = dt.year;
    if (mo) *mo = dt.month;
    if (d) *d = dt.day;
    if (hh) *hh = dt.hour;
    if (mi) *mi = dt.minute;
    if (ss) *ss = dt.second;
    return 0;
}

static int h_version(char *out, int cap) {
    bin_copy(out, cap, "0.7");
    return 0;
}

static int h_open_app(const char *name) {
    if (!name) return -1;
    /* Baska bir uygulama acmak da komut sayilir.     */
    if (perm_exec(name)) return -2;
    return arctian_shell_launch(name);
}

static void h_clip_set(const char *text) {
    if (perm_policy("C:/etc/clip.txt", 1)) return;
    arctian_clipboard_set(text);
}

static int h_clip_get(char *out, int cap) {
    if (perm_policy("C:/etc/clip.txt", 0)) return -2;
    const char *c = arctian_clipboard_get();
    bin_copy(out, cap, c ? c : "");
    return slen2(out);
}

static int h_fetch(const char *url, void *buf, int cap, int *len_out) {
    if (!s_asi || !s_asi->net || !s_asi->net->download) return -1;
    /* Uygulama basina tek ag izni (`net *`).        */
    if (perm_net()) return -2;
    uint32_t n = 0;
    int rc = s_asi->net->download(url, buf, (uint32_t)cap, &n);
    if (len_out) *len_out = (int)n;
    return rc;
}

/* ------------------------------------------------------------------ */
/* ABI v2: stil motoru                                                 */
/* ------------------------------------------------------------------ */

/* CSS secicisi: durum bitleri | (kisim << 8) — WOLF_SEL() ile uretilir. */
static lv_style_selector_t sel_from(int flags) {
    lv_style_selector_t sel = 0;
    if (flags & WOLF_SF_HOVER)    sel |= LV_STATE_HOVERED;
    if (flags & WOLF_SF_PRESSED)  sel |= LV_STATE_PRESSED;
    if (flags & WOLF_SF_CHECKED)  sel |= LV_STATE_CHECKED;
    if (flags & WOLF_SF_FOCUSED)  sel |= LV_STATE_FOCUSED;
    if (flags & WOLF_SF_DISABLED) sel |= LV_STATE_DISABLED;
    if (flags & WOLF_SF_EDITED)   sel |= LV_STATE_EDITED;
    if (flags & WOLF_SF_USER1)    sel |= LV_STATE_USER_1;
    if (flags & WOLF_SF_USER2)    sel |= LV_STATE_USER_2;
    switch ((flags >> 8) & 0x3F) {
        case WOLF_PF_SCROLLBAR: sel |= LV_PART_SCROLLBAR; break;
        case WOLF_PF_INDICATOR: sel |= LV_PART_INDICATOR; break;
        case WOLF_PF_KNOB:      sel |= LV_PART_KNOB; break;
        case WOLF_PF_CURSOR:    sel |= LV_PART_CURSOR; break;
        case WOLF_PF_ITEMS:     sel |= LV_PART_ITEMS; break;
        default:                sel |= LV_PART_MAIN; break;
    }
    return sel;
}

static lv_opa_t opa_from(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return (lv_opa_t)(percent * 255 / 100);
}

/* Gomulu arayuz fontu (Inter TTF) istenen piksel boyutunda. tiny_ttf ile
 * uretilir; oturumlar arasi onbellekte tutulur (16 boyut). Deterministik
 * rasterlestirme: masaustu temasinda da ayni yol kullaniliyor. */
#define WOLF_MAX_FONTS 16
static lv_font_t *s_wf_font[WOLF_MAX_FONTS];
static int        s_wf_font_px[WOLF_MAX_FONTS];

static lv_font_t *wf_font_at(int px) {
    if (px < 6) px = 6;
    if (px > 72) px = 72;
    for (int i = 0; i < WOLF_MAX_FONTS; i++)
        if (s_wf_font[i] && s_wf_font_px[i] == px) return s_wf_font[i];
    for (int i = 0; i < WOLF_MAX_FONTS; i++) {
        if (s_wf_font[i]) continue;
        lv_font_t *f = lv_tiny_ttf_create_data(font_ttf, font_ttf_len, (uint32_t)px);
        if (!f) return 0;
        s_wf_font[i] = f;
        s_wf_font_px[i] = px;
        return f;
    }
    return 0; /* onbellek dolu */
}

static void h_style(wolf_id id, int prop, int value, int flags) {
    wolf_slot_t *s = slot_of(id);
    if (!s) return;
    lv_obj_t *o = s->obj;
    lv_style_selector_t sel = sel_from(flags);
    switch (prop) {
    case WOLF_SP_BG_COLOR:
        lv_obj_set_style_bg_color(o, lv_color_hex((uint32_t)value), sel); break;
    case WOLF_SP_BG_OPA:
        lv_obj_set_style_bg_opa(o, opa_from(value), sel); break;
    case WOLF_SP_BG_GRAD_COLOR:
        lv_obj_set_style_bg_grad_color(o, lv_color_hex((uint32_t)value), sel); break;
    case WOLF_SP_BG_GRAD_DIR:
        lv_obj_set_style_bg_grad_dir(o,
            value == 1 ? LV_GRAD_DIR_VER : value == 2 ? LV_GRAD_DIR_HOR
                                                      : LV_GRAD_DIR_NONE, sel); break;
    case WOLF_SP_BG_GRAD_OPA:
        lv_obj_set_style_bg_grad_opa(o, opa_from(value), sel); break;
    case WOLF_SP_TEXT_COLOR:
        lv_obj_set_style_text_color(o, lv_color_hex((uint32_t)value), sel); break;
    case WOLF_SP_TEXT_OPA:
        lv_obj_set_style_text_opa(o, opa_from(value), sel); break;
    case WOLF_SP_TEXT_LETTER:
        lv_obj_set_style_text_letter_space(o, value, sel); break;
    case WOLF_SP_TEXT_LINE:
        lv_obj_set_style_text_line_space(o, value, sel); break;
    case WOLF_SP_RADIUS:
        lv_obj_set_style_radius(o, value, sel); break;
    case WOLF_SP_BORDER_WIDTH:
        lv_obj_set_style_border_width(o, value, sel); break;
    case WOLF_SP_BORDER_COLOR:
        lv_obj_set_style_border_color(o, lv_color_hex((uint32_t)value), sel); break;
    case WOLF_SP_BORDER_OPA:
        lv_obj_set_style_border_opa(o, opa_from(value), sel); break;
    case WOLF_SP_BORDER_SIDE: {
        lv_border_side_t bs = LV_BORDER_SIDE_NONE;
        if (value & 1) bs |= LV_BORDER_SIDE_TOP;
        if (value & 2) bs |= LV_BORDER_SIDE_BOTTOM;
        if (value & 4) bs |= LV_BORDER_SIDE_LEFT;
        if (value & 8) bs |= LV_BORDER_SIDE_RIGHT;
        if (value == 15) bs = LV_BORDER_SIDE_FULL;
        lv_obj_set_style_border_side(o, bs, sel); break;
    }
    case WOLF_SP_PAD:
        lv_obj_set_style_pad_all(o, value, sel); break;
    case WOLF_SP_PAD_TOP:
        lv_obj_set_style_pad_top(o, value, sel); break;
    case WOLF_SP_PAD_BOTTOM:
        lv_obj_set_style_pad_bottom(o, value, sel); break;
    case WOLF_SP_PAD_LEFT:
        lv_obj_set_style_pad_left(o, value, sel); break;
    case WOLF_SP_PAD_RIGHT:
        lv_obj_set_style_pad_right(o, value, sel); break;
    case WOLF_SP_PAD_ROW:
        lv_obj_set_style_pad_row(o, value, sel); break;
    case WOLF_SP_PAD_COLUMN:
        lv_obj_set_style_pad_column(o, value, sel); break;
    case WOLF_SP_SHADOW_WIDTH:
        lv_obj_set_style_shadow_width(o, value, sel); break;
    case WOLF_SP_SHADOW_COLOR:
        lv_obj_set_style_shadow_color(o, lv_color_hex((uint32_t)value), sel); break;
    case WOLF_SP_SHADOW_OPA:
        lv_obj_set_style_shadow_opa(o, opa_from(value), sel); break;
    case WOLF_SP_SHADOW_SPREAD:
        lv_obj_set_style_shadow_spread(o, value, sel); break;
    case WOLF_SP_SHADOW_OFS_X:
        lv_obj_set_style_shadow_ofs_x(o, value, sel); break;
    case WOLF_SP_SHADOW_OFS_Y:
        lv_obj_set_style_shadow_ofs_y(o, value, sel); break;
    case WOLF_SP_OPA:
        lv_obj_set_style_opa(o, opa_from(value), sel); break;
    case WOLF_SP_ROTATE:
        lv_obj_set_style_transform_rotation(o, value, sel); break;
    case WOLF_SP_SCALE: {
        int v256 = value * 256 / 100;
        lv_obj_set_style_transform_scale_x(o, v256, sel);
        lv_obj_set_style_transform_scale_y(o, v256, sel); break;
    }
    case WOLF_SP_SCALE_X:
        lv_obj_set_style_transform_scale_x(o, value * 256 / 100, sel); break;
    case WOLF_SP_SCALE_Y:
        lv_obj_set_style_transform_scale_y(o, value * 256 / 100, sel); break;
    case WOLF_SP_PIVOT_X:
        lv_obj_set_style_transform_pivot_x(o, value, sel); break;
    case WOLF_SP_PIVOT_Y:
        lv_obj_set_style_transform_pivot_y(o, value, sel); break;
    case WOLF_SP_LINE_WIDTH:
        lv_obj_set_style_line_width(o, value, sel); break;
    case WOLF_SP_LINE_COLOR:
        lv_obj_set_style_line_color(o, lv_color_hex((uint32_t)value), sel); break;
    case WOLF_SP_LINE_OPA:
        lv_obj_set_style_line_opa(o, opa_from(value), sel); break;
    case WOLF_SP_LINE_ROUNDED:
        lv_obj_set_style_line_rounded(o, value ? true : false, sel); break;
    case WOLF_SP_OUTLINE_WIDTH:
        lv_obj_set_style_outline_width(o, value, sel); break;
    case WOLF_SP_OUTLINE_COLOR:
        lv_obj_set_style_outline_color(o, lv_color_hex((uint32_t)value), sel); break;
    case WOLF_SP_OUTLINE_OPA:
        lv_obj_set_style_outline_opa(o, opa_from(value), sel); break;
    case WOLF_SP_OUTLINE_PAD:
        lv_obj_set_style_outline_pad(o, value, sel); break;
    case WOLF_SP_ANIM_TIME:
        lv_obj_set_style_anim_time(o, value, sel); break;
    case WOLF_SP_SIZE:
        lv_obj_set_style_size(o, value, value, sel); break;
    case WOLF_SP_ARC_WIDTH:
        lv_obj_set_style_arc_width(o, value, sel); break;
    case WOLF_SP_ARC_COLOR:
        lv_obj_set_style_arc_color(o, lv_color_hex((uint32_t)value), sel); break;
    case WOLF_SP_ARC_ROUNDED:
        lv_obj_set_style_arc_rounded(o, value ? true : false, sel); break;
    case WOLF_SP_CLIP_CORNER:
        lv_obj_set_style_clip_corner(o, value ? true : false, sel); break;
    case WOLF_SP_TRANSLATE_X:
        lv_obj_set_style_translate_x(o, value, sel); break;
    case WOLF_SP_TRANSLATE_Y:
        lv_obj_set_style_translate_y(o, value, sel); break;
    case WOLF_SP_FONT_PX: {
        lv_font_t *f = wf_font_at(value);
        if (f) {
            lv_obj_set_style_text_font(o, f, sel);
            /* Etiket boyu yeni fonta gore yeniden olculmeli: metni
             * yeniden vermek LVGL'de tip alanini gunceller (buyuk
             * fontlarda ust kirilma sorununu cozer). */
            if (lv_obj_check_type(o, &lv_label_class))
                lv_label_set_text(o, lv_label_get_text(o));
        }
        break;
    }
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* ABI v2: poligon cizgisi (lv_line)                                   */
/* ------------------------------------------------------------------ */

#define WOLF_MAX_LINE   12
#define WOLF_LINE_MAXPTS 512

typedef struct {
    lv_obj_t          *obj;
    lv_point_precise_t pts[WOLF_LINE_MAXPTS];
    int                n;
    int                used;
} wolf_line_t;

static wolf_line_t s_line[WOLF_MAX_LINE];

static void line_del_cb(lv_event_t *e) {
    int k = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (k >= 0 && k < WOLF_MAX_LINE) { s_line[k].used = 0; s_line[k].n = 0; }
}

static wolf_line_t *line_entry(wolf_id id) {
    wolf_slot_t *s = slot_of(id);
    if (!s || s->kind != 11) return 0;
    for (int i = 0; i < WOLF_MAX_LINE; i++)
        if (s_line[i].used && s_line[i].obj == s->obj) return &s_line[i];
    return 0;
}

static wolf_id h_line(wolf_id p, int x, int y) {
    int k = -1;
    for (int i = 0; i < WOLF_MAX_LINE; i++) if (!s_line[i].used) { k = i; break; }
    if (k < 0) return 0;
    lv_obj_t *l = lv_line_create(parent_of(p));
    if (!l) return 0;
    lv_obj_set_pos(l, x, y);
    s_line[k].used = 1;
    s_line[k].obj = l;
    s_line[k].n = 0;
    lv_line_set_points(l, s_line[k].pts, 0);
    lv_obj_add_event_cb(l, line_del_cb, LV_EVENT_DELETE,
                        (void *)(intptr_t)(k + 1));
    return slot_new(l, 11);
}

static void h_line_reset(wolf_id id) {
    wolf_line_t *e = line_entry(id);
    if (!e) return;
    e->n = 0;
    lv_line_set_points(e->obj, e->pts, 0);
}

static int h_line_to(wolf_id id, int x, int y) {
    wolf_line_t *e = line_entry(id);
    if (!e || e->n >= WOLF_LINE_MAXPTS) return -1;
    e->pts[e->n].x = (lv_value_precise_t)x;
    e->pts[e->n].y = (lv_value_precise_t)y;
    e->n++;
    lv_line_set_points(e->obj, e->pts, (uint32_t)e->n);
    return 0;
}

/* ------------------------------------------------------------------ */
/* ABI v2: yay (lv_arc)                                                */
/* ------------------------------------------------------------------ */

static wolf_id h_arc(wolf_id p, int x, int y, int r) {
    if (r < 8) r = 8;
    lv_obj_t *a = lv_arc_create(parent_of(p));
    if (!a) return 0;
    lv_obj_set_pos(a, x, y);
    lv_obj_set_size(a, 2 * r, 2 * r);
    return slot_new(a, 12);
}

static void h_arc_set(wolf_id id, int start, int end, int rot) {
    wolf_slot_t *s = slot_of(id);
    if (!s || s->kind != 12) return;
    lv_arc_set_bg_angles(s->obj, (lv_value_precise_t)start,
                         (lv_value_precise_t)end);
    lv_arc_set_rotation(s->obj, rot);
}

/* ------------------------------------------------------------------ */
/* ABI v2: piksel tuvali (ARGB8888 = 0xAARRGGBB)                       */
/* ------------------------------------------------------------------ */

#define WOLF_MAX_CANVAS   8
#define WOLF_CANVAS_MAXPX (512 * 512)

typedef struct {
    lv_obj_t *cv;
    uint32_t *buf;
    int       w, h;
    int       used;
} wolf_cv_t;

static wolf_cv_t s_cv[WOLF_MAX_CANVAS];

static void cv_del_cb(lv_event_t *e) {
    int k = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (k < 0 || k >= WOLF_MAX_CANVAS || !s_cv[k].used) return;
    if (s_cv[k].buf && s_asi && s_asi->mem) s_asi->mem->free(s_cv[k].buf);
    s_cv[k].used = 0;
    s_cv[k].buf = 0;
}

static wolf_cv_t *cv_of(wolf_id id) {
    wolf_slot_t *s = slot_of(id);
    if (!s || s->kind != 13) return 0;
    for (int i = 0; i < WOLF_MAX_CANVAS; i++)
        if (s_cv[i].used && s_cv[i].cv == s->obj) return &s_cv[i];
    return 0;
}

static wolf_id h_canvas(wolf_id p, int x, int y, int w, int h) {
    if (w < 1 || h < 1 || w * h > WOLF_CANVAS_MAXPX) return 0;
    if (!s_asi || !s_asi->mem) return 0;
    int k = -1;
    for (int i = 0; i < WOLF_MAX_CANVAS; i++) if (!s_cv[i].used) { k = i; break; }
    if (k < 0) return 0;
    uint32_t *buf = (uint32_t *)s_asi->mem->alloc((uint32_t)w * (uint32_t)h * 4u);
    if (!buf) return 0;
    lv_obj_t *cv = lv_canvas_create(parent_of(p));
    if (!cv) { s_asi->mem->free(buf); return 0; }
    lv_obj_set_pos(cv, x, y);
    lv_obj_set_size(cv, w, h);
    lv_canvas_set_buffer(cv, buf, w, h, LV_COLOR_FORMAT_ARGB8888);
    for (int i = 0; i < w * h; i++) buf[i] = 0x00000000u;   /* saydam basla */
    s_cv[k].used = 1;
    s_cv[k].cv = cv;
    s_cv[k].buf = buf;
    s_cv[k].w = w;
    s_cv[k].h = h;
    lv_obj_add_event_cb(cv, cv_del_cb, LV_EVENT_DELETE,
                        (void *)(intptr_t)(k + 1));
    return slot_new(cv, 13);
}

/* tamsayi karekok (Newton); 512'lik tuvalde guvenli */
static int w_isqrt(int n) {
    if (n <= 0) return 0;
    int x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

/* "over" harmani: src (0xAARRGGBB) uzerine yazilir */
static void cv_px(uint32_t *buf, int w, int h, int x, int y, uint32_t argb) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint32_t *d = buf + y * w + x;
    uint32_t a = argb >> 24;
    if (a >= 255u) { *d = argb; return; }
    if (a == 0u) return;
    uint32_t dv = *d, da = dv >> 24;
    uint32_t oa = a + (da * (255u - a)) / 255u;
    if (oa == 0u) { *d = 0; return; }
    uint32_t den = 255u * oa;
    uint32_t sr = (argb >> 16) & 0xFF, sg = (argb >> 8) & 0xFF, sb = argb & 0xFF;
    uint32_t dr = (dv >> 16) & 0xFF, dg = (dv >> 8) & 0xFF, db = dv & 0xFF;
    uint32_t r = (sr * a * 255u + dr * da * (255u - a)) / den;
    uint32_t g = (sg * a * 255u + dg * da * (255u - a)) / den;
    uint32_t b = (sb * a * 255u + db * da * (255u - a)) / den;
    *d = (oa << 24) | (r << 16) | (g << 8) | b;
}

static void h_canvas_px(wolf_id id, int x, int y, uint32_t argb) {
    wolf_cv_t *c = cv_of(id);
    if (!c) return;
    cv_px(c->buf, c->w, c->h, x, y, argb);
    lv_obj_invalidate(c->cv);
}

static void h_canvas_rect(wolf_id id, int x, int y, int rw, int rh,
                          uint32_t argb, int radius) {
    wolf_cv_t *c = cv_of(id);
    if (!c || rw <= 0 || rh <= 0) return;
    int rad = radius;
    if (rad > rw / 2) rad = rw / 2;
    if (rad > rh / 2) rad = rh / 2;
    for (int yy = 0; yy < rh; yy++) {
        int inset = 0;
        if (rad > 0) {
            int q = (yy < rad) ? (rad - 1 - yy)
                  : (yy >= rh - rad ? yy - (rh - rad) : -1);
            if (q >= 0) inset = rad - w_isqrt(rad * rad - q * q);
        }
        for (int xx = inset; xx < rw - inset; xx++)
            cv_px(c->buf, c->w, c->h, x + xx, y + yy, argb);
    }
    lv_obj_invalidate(c->cv);
}

static void h_canvas_line(wolf_id id, int x1, int y1, int x2, int y2,
                          uint32_t argb, int width) {
    wolf_cv_t *c = cv_of(id);
    if (!c) return;
    if (width < 1) width = 1;
    int half = (width - 1) / 2;
    int dx = x2 - x1, dy = y2 - y1;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err = adx - ady, guard = adx + ady + 2;
    int x = x1, y = y1;
    while (guard-- >= 0) {
        for (int oy = -half; oy <= half; oy++)
            for (int ox = -half; ox <= half; ox++)
                cv_px(c->buf, c->w, c->h, x + ox, y + oy, argb);
        if (x == x2 && y == y2) break;
        int e2 = 2 * err;
        if (e2 > -ady) { err -= ady; x += sx; }
        if (e2 < adx)  { err += adx; y += sy; }
    }
    lv_obj_invalidate(c->cv);
}

static void h_canvas_circle(wolf_id id, int cx, int cy, int r,
                            uint32_t argb, int fill) {
    wolf_cv_t *c = cv_of(id);
    if (!c || r < 0) return;
    if (fill) {
        for (int dy = -r; dy <= r; dy++) {
            int dx = w_isqrt(r * r - dy * dy);
            for (int xx = cx - dx; xx <= cx + dx; xx++)
                cv_px(c->buf, c->w, c->h, xx, cy + dy, argb);
        }
    } else {
        /* orta nokta cemberi */
        int x = r, y = 0, err = 1 - r;
        while (x >= y) {
            cv_px(c->buf, c->w, c->h, cx + x, cy + y, argb);
            cv_px(c->buf, c->w, c->h, cx - x, cy + y, argb);
            cv_px(c->buf, c->w, c->h, cx + x, cy - y, argb);
            cv_px(c->buf, c->w, c->h, cx - x, cy - y, argb);
            cv_px(c->buf, c->w, c->h, cx + y, cy + x, argb);
            cv_px(c->buf, c->w, c->h, cx - y, cy + x, argb);
            cv_px(c->buf, c->w, c->h, cx + y, cy - x, argb);
            cv_px(c->buf, c->w, c->h, cx - y, cy - x, argb);
            y++;
            if (err < 0) err += 2 * y + 1;
            else { x--; err += 2 * (y - x) + 1; }
        }
    }
    lv_obj_invalidate(c->cv);
}

static uint32_t lerp_c(uint32_t a, uint32_t b, int t, int steps) {
    /* her kanal icin t/steps oransinda gecis (isaretli farklarla) */
    uint32_t out = 0;
    int shift[3] = {16, 8, 0};
    for (int i = 0; i < 3; i++) {
        int av = (int)((a >> shift[i]) & 0xFF), bv = (int)((b >> shift[i]) & 0xFF);
        int v = av + ((bv - av) * t) / (steps > 1 ? steps : 1);
        out |= (uint32_t)(v & 0xFF) << shift[i];
    }
    out |= (a & 0xFF000000u);   /* alfa ust rengin alfasini tasinir */
    return out;
}

static void h_canvas_grad(wolf_id id, int x, int y, int gw, int gh,
                          uint32_t c0, uint32_t c1, int dir) {
    wolf_cv_t *c = cv_of(id);
    if (!c || gw <= 0 || gh <= 0) return;
    for (int yy = 0; yy < gh; yy++) {
        for (int xx = 0; xx < gw; xx++) {
            uint32_t col = (dir == 1) ? lerp_c(c0, c1, xx, gw)
                                      : lerp_c(c0, c1, yy, gh);
            cv_px(c->buf, c->w, c->h, x + xx, y + yy, col);
        }
    }
    lv_obj_invalidate(c->cv);
}

static void h_canvas_clear(wolf_id id, uint32_t argb) {
    wolf_cv_t *c = cv_of(id);
    if (!c) return;
    for (int i = 0; i < c->w * c->h; i++) c->buf[i] = argb;
    lv_obj_invalidate(c->cv);
}

static uint32_t h_canvas_getpx(wolf_id id, int x, int y) {
    wolf_cv_t *c = cv_of(id);
    if (!c) return 0xFFFFFFFFu;
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return 0xFFFFFFFFu;
    return c->buf[y * c->w + x];
}

/* ------------------------------------------------------------------ */
/* ABI v2: ses                                                          */
/* ------------------------------------------------------------------ */

/* Surucu zero-copy calisir: play_wav ardicil tamponi okumaya devam eder.
 * Bu yuzden kopya tampon is bitene (veya yeni play'e) kadar canli kalir. */
static uint8_t *s_aud_buf;
static uint32_t s_aud_len;

static void audio_release(void) {
    if (s_asi && s_asi->audio && s_asi->audio->stop) s_asi->audio->stop();
    if (s_aud_buf && s_asi && s_asi->mem) {
        s_asi->mem->free(s_aud_buf);
        s_aud_buf = 0;
        s_aud_len = 0;
    }
}

static int h_audio_ok(void) {
    return (s_asi && s_asi->audio && s_asi->audio->present &&
            s_asi->audio->present()) ? 1 : 0;
}

static int h_audio_play_mem_x(const void *data, int len);
static int h_audio_play_mem_x(const void *data, int len) {
    if (!s_asi || !s_asi->audio || !s_asi->audio->play_wav) return -1;
    if (!s_asi->mem || len < 44 || len > 4 * 1024 * 1024) return -1;
    audio_release();   /* eski tamponu birak (durum: surucu stop ile temizlendi) */
    uint8_t *b = (uint8_t *)s_asi->mem->alloc((uint32_t)len);
    if (!b) return -1;
    s_asi->mem->memcpy(b, data, (size_t)len);
    s_aud_buf = b;
    s_aud_len = (uint32_t)len;
    if (s_asi->audio->play_wav(s_aud_buf, s_aud_len) != 0) {
        s_asi->mem->free(s_aud_buf);
        s_aud_buf = 0;
        s_aud_len = 0;
        return -1;
    }
    return 0;
}

static int h_audio_play(const char *path) {
    if (!path || !s_asi || !s_asi->fs || !s_asi->fs->read_path || !s_asi->mem)
        return -1;
    asi_stat_t st;
    if (!s_asi->fs->stat || s_asi->fs->stat(path, &st) != 0) return -1;
    if (st.size < 44u || st.size > 4u * 1024u * 1024u) return -1;
    uint8_t *tmp = (uint8_t *)s_asi->mem->alloc(st.size);
    if (!tmp) return -1;
    uint32_t n = s_asi->fs->read_path(path, tmp, st.size);
    int rc;
    if (n < 44u) rc = -1;
    else rc = h_audio_play_mem_x(tmp, (int)n);
    s_asi->mem->free(tmp);
    return rc;
}

static void h_audio_stop(void) {
    if (s_asi && s_asi->audio && s_asi->audio->stop) s_asi->audio->stop();
}

static int h_audio_playing(void) {
    if (!s_asi || !s_asi->audio || !s_asi->audio->is_playing) return 0;
    if (!s_aud_buf) return 0;
    return s_asi->audio->is_playing();
}

static int h_audio_vol(void) {
    if (!s_asi || !s_asi->audio || !s_asi->audio->volume) return 0;
    return s_asi->audio->volume();
}

static void h_audio_set_vol(int percent) {
    if (s_asi && s_asi->audio && s_asi->audio->set_volume)
        s_asi->audio->set_volume(percent);
}

/* ------------------------------------------------------------------ */
/* ABI v2: zamanlayici                                                 */
/* ------------------------------------------------------------------ */

#define WOLF_MAX_TIMER 16

typedef struct {
    lv_timer_t   *t;
    wolf_event_fn cb;
    void         *user;
    int           used;
} wolf_timer_t;

static wolf_timer_t s_timer[WOLF_MAX_TIMER];

static void timer_fire(lv_timer_t *t) {
    int k = (int)(intptr_t)lv_timer_get_user_data(t) - 1;
    if (k < 0 || k >= WOLF_MAX_TIMER || !s_timer[k].used) return;
    if (s_timer[k].cb) s_timer[k].cb((wolf_id)(k + 1), s_timer[k].user);
}

static wolf_id h_timer(uint32_t period_ms, wolf_event_fn cb, void *user) {
    if (!cb) return 0;
    if (period_ms < 10u) period_ms = 10u;
    for (int i = 0; i < WOLF_MAX_TIMER; i++) {
        if (s_timer[i].used) continue;
        s_timer[i].used = 1;
        s_timer[i].cb = cb;
        s_timer[i].user = user;
        s_timer[i].t = lv_timer_create(timer_fire, period_ms,
                                       (void *)(intptr_t)(i + 1));
        if (!s_timer[i].t) { s_timer[i].used = 0; return 0; }
        return (wolf_id)(i + 1);
    }
    return 0;
}

static void h_timer_cancel(wolf_id tid) {
    int k = (int)tid - 1;
    if (k < 0 || k >= WOLF_MAX_TIMER || !s_timer[k].used) return;
    if (s_timer[k].t) lv_timer_delete(s_timer[k].t);
    s_timer[k].t = 0;
    s_timer[k].cb = 0;
    s_timer[k].used = 0;
}

static void v2_session_clear(void) {
    for (int i = 0; i < WOLF_MAX_TIMER; i++) {
        if (s_timer[i].used && s_timer[i].t) lv_timer_delete(s_timer[i].t);
        s_timer[i].t = 0;
        s_timer[i].cb = 0;
        s_timer[i].used = 0;
    }
    /* anim_in/anim_pulse animlari + gecis arenasi (widget agaci silinir) */
    lv_anim_delete(NULL, anim_opa_cb);
    lv_anim_delete(NULL, anim_ty_cb);
    s_trans_n = 0;
    audio_release();
}

/* ------------------------------------------------------------------ */
/* ABI v2: yigin sirasi                                                */
/* ------------------------------------------------------------------ */

static void h_to_front(wolf_id id) {
    wolf_slot_t *s = slot_of(id);
    if (s) lv_obj_move_foreground(s->obj);
}

static void h_to_back(wolf_id id) {
    wolf_slot_t *s = slot_of(id);
    if (s) lv_obj_move_background(s->obj);
}

/* ------------------------------------------------------------------ */
/* ABI v2: gecis + durum + giris/pulse animasyonlari                   */
/* ------------------------------------------------------------------ */

/* lv_style_transition_dsc_t + props[] yasamali olmali; dsc props'u
 * KOPYALAMAZ, isaretcide kalir. Bu yuzden oturumlu arena'da tutulur.
 * wlf_host_reset() arenayi bastan doldurur (widget agaci da silinir). */
#define WOLF_MAX_TRANS 64
typedef struct {
    lv_style_transition_dsc_t dsc;
    lv_style_prop_t           props[24];
} wolf_trans_t;
static wolf_trans_t s_trans[WOLF_MAX_TRANS];
static int          s_trans_n;

static void h_transition(wolf_id id, unsigned mask, int dur, int delay,
                         int easing) {
    wolf_slot_t *s = slot_of(id);
    if (!s || !mask) return;
    if (dur < 0) dur = 0;
    if (delay < 0) delay = 0;
    if (s_trans_n >= WOLF_MAX_TRANS) {
        h_log("[wolf] uyari: gecis havuzu doldu, kural uygulanamadi");
        return;
    }
    wolf_trans_t *e = &s_trans[s_trans_n];
    lv_style_prop_t *p = e->props;
    if (mask & WOLF_TP_BG_COLOR)     *p++ = LV_STYLE_BG_COLOR;
    if (mask & WOLF_TP_BG_OPA)       *p++ = LV_STYLE_BG_OPA;
    if (mask & WOLF_TP_TEXT_COLOR)   *p++ = LV_STYLE_TEXT_COLOR;
    if (mask & WOLF_TP_TEXT_OPA)     *p++ = LV_STYLE_TEXT_OPA;
    if (mask & WOLF_TP_BORDER_COLOR) *p++ = LV_STYLE_BORDER_COLOR;
    if (mask & WOLF_TP_BORDER_WIDTH) *p++ = LV_STYLE_BORDER_WIDTH;
    if (mask & WOLF_TP_RADIUS)       *p++ = LV_STYLE_RADIUS;
    if (mask & WOLF_TP_SHADOW) {
        *p++ = LV_STYLE_SHADOW_WIDTH;  *p++ = LV_STYLE_SHADOW_COLOR;
        *p++ = LV_STYLE_SHADOW_OPA;    *p++ = LV_STYLE_SHADOW_SPREAD;
        *p++ = LV_STYLE_SHADOW_OFS_X;  *p++ = LV_STYLE_SHADOW_OFS_Y;
    }
    if (mask & WOLF_TP_TRANSLATE_X)  *p++ = LV_STYLE_TRANSLATE_X;
    if (mask & WOLF_TP_TRANSLATE_Y)  *p++ = LV_STYLE_TRANSLATE_Y;
    if (mask & WOLF_TP_SCALE) {
        *p++ = LV_STYLE_TRANSFORM_SCALE_X; *p++ = LV_STYLE_TRANSFORM_SCALE_Y;
    }
    if (mask & WOLF_TP_OPA)          *p++ = LV_STYLE_OPA;
    if (mask & WOLF_TP_OUTLINE) {
        *p++ = LV_STYLE_OUTLINE_WIDTH; *p++ = LV_STYLE_OUTLINE_COLOR;
        *p++ = LV_STYLE_OUTLINE_OPA;
    }
    if (mask & WOLF_TP_ARC) {
        *p++ = LV_STYLE_ARC_WIDTH; *p++ = LV_STYLE_ARC_COLOR;
        *p++ = LV_STYLE_ARC_OPA;
    }
    *p = LV_STYLE_PROP_INV; /* dizi sonu isaretcisi (0) */
    lv_anim_path_cb_t path =
        easing == WOLF_EASE_IN     ? lv_anim_path_ease_in     :
        easing == WOLF_EASE_OUT    ? lv_anim_path_ease_out    :
        easing == WOLF_EASE_IN_OUT ? lv_anim_path_ease_in_out :
                                     lv_anim_path_linear;
    lv_style_transition_dsc_init(&e->dsc, e->props, path,
                                 (uint32_t)dur, (uint32_t)delay, NULL);
    lv_obj_set_style_transition(s->obj, &e->dsc, 0);
    s_trans_n++;
}

static void h_state_set(wolf_id id, int state_flags, int on) {
    wolf_slot_t *s = slot_of(id);
    if (!s) return;
    lv_state_t st = 0;
    if (state_flags & WOLF_SF_HOVER)    st |= LV_STATE_HOVERED;
    if (state_flags & WOLF_SF_PRESSED)  st |= LV_STATE_PRESSED;
    if (state_flags & WOLF_SF_CHECKED)  st |= LV_STATE_CHECKED;
    if (state_flags & WOLF_SF_FOCUSED)  st |= LV_STATE_FOCUSED;
    if (state_flags & WOLF_SF_DISABLED) st |= LV_STATE_DISABLED;
    if (state_flags & WOLF_SF_EDITED)   st |= LV_STATE_EDITED;
    if (state_flags & WOLF_SF_USER1)    st |= LV_STATE_USER_1;
    if (state_flags & WOLF_SF_USER2)    st |= LV_STATE_USER_2;
    if (!st) return;
    if (on) lv_obj_add_state(s->obj, st);
    else    lv_obj_remove_state(s->obj, st);
}

/* anim_in / anim_pulse yalniz bu iki exec_cb'yi kullanir; temizlik de
 * bunlar uzerinden yapilir (LVGL'in kendi animlari etkilenmez). */
static void anim_opa_cb(void *var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}
static void anim_ty_cb(void *var, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void h_anim_in(wolf_id id, int dy, int dur, int delay) {
    wolf_slot_t *s = slot_of(id);
    if (!s) return;
    lv_obj_t *o = s->obj;
    if (dur <= 0) { /* hareketsiz gorunur ol */
        lv_obj_set_style_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_translate_y(o, 0, 0);
        return;
    }
    if (delay < 0) delay = 0;
    if (dy < -200) dy = -200;
    if (dy > 200)  dy = 200;
    /* CSS "both" dolgu: gecikme boyunca gizli + asagida bekle. */
    lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_translate_y(o, dy, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_duration(&a, (uint32_t)dur);
    lv_anim_set_delay(&a, (uint32_t)delay);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_start(&a);

    lv_anim_set_exec_cb(&a, anim_ty_cb);
    lv_anim_set_values(&a, dy, 0);
    lv_anim_start(&a);
}

static void h_anim_pulse(wolf_id id, int period, int low) {
    wolf_slot_t *s = slot_of(id);
    if (!s || period <= 0) return;
    lv_opa_t lo = opa_from(low);
    uint32_t half = (uint32_t)(period / 2 > 0 ? period / 2 : 1);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s->obj);
    lv_anim_set_duration(&a, half);
    lv_anim_set_reverse_duration(&a, half);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, lo, LV_OPA_COVER);
    lv_anim_start(&a);
}

/* Widget icin uygulama animasyonlarini birak (destroy / session reset). */
static void anim_release_obj(lv_obj_t *o) {
    if (!o) return;
    lv_anim_delete(o, anim_opa_cb);
    lv_anim_delete(o, anim_ty_cb);
}

/* ------------------------------------------------------------------ */
/* Kurulum / sifirlama                                                 */
/* ------------------------------------------------------------------ */

static void api_fill(void) {
    for (unsigned i = 0; i < sizeof(s_api); i++) ((char *)&s_api)[i] = 0;
    s_api.magic = WOLF_API_MAGIC;
    /* ABI v2 kurali: `version` majör sözleşmedir ve 1'de KALIR (eski
     * uygulamalarin kati "version > bilinen" denetisini bozmamak icin).
     * Alan büyümesi `size` ile duyurulur; v2 varlığı wolf_api_v2()/HAS(). */
    s_api.version = 1u;
    s_api.size = (uint32_t)sizeof(wolf_api_t);

    s_api.root = h_root;
    s_api.win_title = h_win_title;
    s_api.win_size = h_win_size;

    s_api.panel = h_panel;
    s_api.label = h_label;
    s_api.button = h_button;
    s_api.input = h_input;
    s_api.listbox = h_listbox;
    s_api.image = h_image;
    s_api.progress = h_progress;
    s_api.slider = h_slider;
    s_api.separator = h_separator;
    s_api.checkbox = h_checkbox;

    s_api.set_text = h_set_text;
    s_api.get_text = h_get_text;
    s_api.set_value = h_set_value;
    s_api.get_value = h_get_value;
    s_api.set_pos = h_set_pos;
    s_api.set_size = h_set_size;
    s_api.set_color = h_set_color;
    s_api.set_visible = h_set_visible;
    s_api.set_enabled = h_set_enabled;
    s_api.clear = h_clear;
    s_api.destroy = h_destroy;

    s_api.on_click = h_on_click;
    s_api.on_change = h_on_change;

    s_api.list_add = h_list_add;
    s_api.list_clear = h_list_clear;
    s_api.list_count = h_list_count;
    s_api.list_item = h_list_item;
    s_api.list_selected = h_list_selected;

    s_api.fs_read = h_fs_read;
    s_api.fs_write = h_fs_write;
    s_api.fs_list = h_fs_list;
    s_api.fs_count = h_fs_count;
    s_api.fs_exists = h_fs_exists;
    s_api.fs_remove = h_fs_remove;
    s_api.fs_mkdir = h_fs_mkdir;
    s_api.fs_rename = h_fs_rename;

    s_api.log = h_log;
    s_api.notify = h_notify;
    s_api.dialog = h_dialog;
    s_api.exec = h_exec;
    s_api.ticks_ms = h_ticks;
    s_api.sleep_ms = h_sleep;
    s_api.now = h_now;
    s_api.os_version = h_version;
    s_api.open_app = h_open_app;
    s_api.clip_set = h_clip_set;
    s_api.clip_get = h_clip_get;
    s_api.fetch = h_fetch;
    s_api.arg = h_arg;

    /* ABI v2 */
    s_api.style = h_style;
    s_api.line = h_line;
    s_api.line_reset = h_line_reset;
    s_api.line_to = h_line_to;
    s_api.arc = h_arc;
    s_api.arc_set = h_arc_set;
    s_api.canvas = h_canvas;
    s_api.canvas_px = h_canvas_px;
    s_api.canvas_rect = h_canvas_rect;
    s_api.canvas_line = h_canvas_line;
    s_api.canvas_circle = h_canvas_circle;
    s_api.canvas_grad = h_canvas_grad;
    s_api.canvas_clear = h_canvas_clear;
    s_api.canvas_getpx = h_canvas_getpx;
    s_api.audio_ok = h_audio_ok;
    s_api.audio_play = h_audio_play;
    s_api.audio_play_mem = h_audio_play_mem_x;
    s_api.audio_stop = h_audio_stop;
    s_api.audio_playing = h_audio_playing;
    s_api.audio_vol = h_audio_vol;
    s_api.audio_set_vol = h_audio_set_vol;
    s_api.timer = h_timer;
    s_api.timer_cancel = h_timer_cancel;
    s_api.to_front = h_to_front;
    s_api.to_back = h_to_back;
    s_api.transition = h_transition;
    s_api.state_set = h_state_set;
    s_api.anim_in = h_anim_in;
    s_api.anim_pulse = h_anim_pulse;
}

void wlf_host_reset(lv_obj_t *body) {
    /* v2: onceki oturumun zamanlayicilari/sesi birakilir (lv_timer'lari
     * LVGL listesinde yasamaya devam ederdi — sarkik geri cagri riski). */
    v2_session_clear();
    s_body = body;
    s_asi = g_asi;
    s_n = 0;
    s_ev_click_n = 0;
    s_ev_change_n = 0;
    for (int i = 0; i < WOLF_MAX_WIDGETS; i++) {
        s_slot[i].used = 0;
        s_slot[i].obj = 0;
        s_slot[i].kind = 0;
        s_slot[i].text[0] = 0;
    }
    if (!s_api_ready) { api_fill(); s_api_ready = 1; }

    /* 0.7: uygulamanin izinleri artik yetki maskesi degil, kapidir
     * (perm.c + caps_fs_guard); burada s_perms bakilmaz. */

    /* Kok: govde icinde tam boyutlu seffaf kapsayici. */
    s_root = lv_obj_create(body);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    /* 0.6: govde (launch.c) remove_style_all ile olusturuldugu icin saydam.
     * .wlf uygulamalari kendi arka planlarini cizmez; kok kapsayiciyi opak
     * yapmazsak arkadaki pencere/terminal icerigi gorunur. */
    lv_obj_set_style_bg_color(s_root, lv_color_hex(ADL_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    slot_new(s_root, 0);
}

wolf_api_t *wlf_api_get(lv_obj_t *body) {
    if (!s_root || s_body != body) wlf_host_reset(body);
    if (!s_api_ready) { api_fill(); s_api_ready = 1; }
    return &s_api;
}

void wlf_host_error(const char *msg) {
    if (!s_body) return;
    lv_obj_t *l = lv_label_create(s_body);
    lv_label_set_text(l, msg ? msg : "Uygulama baslatilamadi");
    lv_obj_set_style_text_color(l, lv_color_hex(ADL_DANGER), 0);
    lv_obj_set_style_text_font(l, adl_font(), 0);
    lv_obj_set_pos(l, 16, 16);
}

lv_obj_t *wolf_host_obj(wolf_id id) {
    wolf_slot_t *s = slot_of(id);
    return s ? s->obj : 0;
}



