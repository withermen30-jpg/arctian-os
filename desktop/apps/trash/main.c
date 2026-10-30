#include "app.h"
#include "adl_wolf.h"
#include <stdint.h>

#define CP_MAX   96
#define CP_NAME  64
#define CP_BUF   (128u * 1024u)
#define CP_DIR   "C:/Cop"

static asi_t    *A;
static lv_obj_t *g_list;
static lv_obj_t *g_status;
static lv_obj_t *g_empty;
static lv_obj_t *g_rows[CP_MAX];
static char      g_names[CP_MAX][CP_NAME];
static int       g_n;
static int       g_sel;

static uint8_t  *g_buf;

static void scpy(char *d, int cap, const char *s) {
    int i = 0;
    if (cap <= 0) return;
    if (!s) { d[0] = 0; return; }
    for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}

static void set_status(const char *txt) {
    if (g_status) lv_label_set_text(g_status, txt);
}

static void ume(int v, char *out, int cap) {
    char t[12];
    int n = 0, k = 0;
    if (v == 0) t[n++] = '0';
    while (v > 0 && n < (int)sizeof(t)) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n && k < cap - 1) out[k++] = t[--n];
    out[k] = 0;
}

static void clear_rows(void) {
    for (int i = 0; i < CP_MAX; i++) {
        if (g_rows[i]) { lv_obj_delete(g_rows[i]); g_rows[i] = 0; }
    }
    g_n = 0;
    g_sel = -1;
}

static void row_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_n) return;
    g_sel = idx;
    for (int i = 0; i < g_n; i++)
        if (g_rows[i]) lv_obj_set_style_bg_opa(g_rows[i], i == g_sel ? 90 : LV_OPA_TRANSP, 0);
}

static void refresh(void) {
    clear_rows();
    if (!A->fs || !A->fs->list_dir) { set_status("Dosya sistemi yok"); return; }
    if (A->fs->mkdir) A->fs->mkdir(CP_DIR);

    for (int i = 0; i < CP_MAX; i++) {
        char nm[CP_NAME];
        int is_dir = 0;
        if (A->fs->list_dir(CP_DIR, i, nm, sizeof(nm), &is_dir) != 0) break;
        if (is_dir || !nm[0]) continue;
        scpy(g_names[g_n], CP_NAME, nm);
        g_n++;
    }

    if (g_empty) lv_obj_set_style_opa(g_empty, g_n ? LV_OPA_TRANSP : LV_OPA_COVER, 0);

    int y = 4;
    for (int i = 0; i < g_n; i++) {
        lv_obj_t *r = lv_obj_create(g_list);
        lv_obj_remove_style_all(r);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(r, lv_obj_get_width(g_list) - 8, 34);
        lv_obj_set_pos(r, 4, y);
        lv_obj_set_style_radius(r, 8, 0);
        lv_obj_set_style_bg_color(r, lv_color_hex(ADL_ACCENT), 0);
        lv_obj_set_style_bg_opa(r, i == g_sel ? 90 : LV_OPA_TRANSP, 0);

        lv_obj_t *pick = lv_obj_create(r);
        lv_obj_remove_style_all(pick);
        lv_obj_set_size(pick, lv_obj_get_width(r), lv_obj_get_height(r));
        lv_obj_set_pos(pick, 0, 0);
        lv_obj_add_flag(pick, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(pick, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *l = lv_label_create(r);
        lv_label_set_text(l, g_names[i]);
        lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, lv_obj_get_width(r) - 24);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);

        g_rows[i] = r;
        y += 38;
    }

    char cnt[8];
    ume(g_n, cnt, (int)sizeof(cnt));
    char st[48];
    scpy(st, (int)sizeof(st), "Cop: ");
    int k = 0; while (st[k]) k++;
    scpy(st + k, (int)sizeof(st) - k, cnt);
    k = 0; while (st[k]) k++;
    scpy(st + k, (int)sizeof(st) - k, " oge");
    set_status(st);
}

static void trash_path(char *out, int cap, const char *name) {
    scpy(out, cap, CP_DIR "/");
    int k = 0; while (out[k]) k++;
    scpy(out + k, cap - k, name);
}

static void restore_cb(lv_event_t *e) {
    (void)e;
    if (g_sel < 0 || g_sel >= g_n) { set_status("Once bir oge secin"); return; }
    if (!A->fs || !A->fs->read_path || !A->fs->create_in || !A->fs->write || !A->fs->remove) return;
    if (!g_buf) {
        g_buf = (uint8_t *)A->mem->alloc(CP_BUF);
        if (!g_buf) { set_status("Bellek yetersiz"); return; }
    }

    char src[128];
    trash_path(src, (int)sizeof(src), g_names[g_sel]);
    uint32_t r = A->fs->read_path(src, g_buf, CP_BUF);

    if (A->fs->mkdir) A->fs->mkdir("C:/belgeler");
    void *fh = A->fs->create_in("C:/belgeler", g_names[g_sel], 0);
    if (!fh) { set_status("Geri yuklenemedi"); return; }
    A->fs->write(fh, g_buf, r);
    A->fs->remove(src);
    set_status("Geri yuklendi: C:/belgeler");
    refresh();
}

static void empty_cb(lv_event_t *e) {
    (void)e;
    if (!A->fs || !A->fs->remove || !g_n) { set_status("Cop zaten bos"); return; }
    int n = 0;
    for (int i = 0; i < g_n; i++) {
        char p[128];
        trash_path(p, (int)sizeof(p), g_names[i]);
        if (A->fs->remove(p) == 0) n++;
    }
    char cnt[8];
    ume(n, cnt, (int)sizeof(cnt));
    char st[40];
    scpy(st, (int)sizeof(st), "Bosaltildi: ");
    int k = 0; while (st[k]) k++;
    scpy(st + k, (int)sizeof(st) - k, cnt);
    set_status(st);
    refresh();
}

static void do_refresh_cb(lv_event_t *e) { (void)e; refresh(); }

static lv_obj_t *mk_btn(lv_obj_t *parent, const char *txt, int w, uint32_t col,
                        lv_event_cb_t cb) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, 34);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(col), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, 0);
    return b;
}

static void app_del_cb(lv_event_t *e) {
    (void)e;
    if (g_buf && A && A->mem && A->mem->free) { A->mem->free(g_buf); }
    g_buf = 0;
    g_list = 0; g_status = 0; g_empty = 0;
    for (int i = 0; i < CP_MAX; i++) g_rows[i] = 0;
    g_n = 0; g_sel = -1;
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    A = asi;
    int W = lv_obj_get_width(win);
    int H = lv_obj_get_height(win);
    if (W < 300) W = 750;
    if (H < 200) H = 470;

    lv_obj_t *bar = lv_obj_create(win);
    lv_obj_remove_style_all(bar);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, W, 42);
    lv_obj_set_pos(bar, 0, 0);

    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, "Cop Kovasi");
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 6, 0);

    lv_obj_t *brest = mk_btn(bar, "Geri Yukle", 100, ADL_SURFACE_2, restore_cb);
    lv_obj_align(brest, LV_ALIGN_RIGHT_MID, -206, 0);
    lv_obj_t *bdel = mk_btn(bar, "Copu Bosalt", 110, ADL_DANGER, empty_cb);
    lv_obj_align(bdel, LV_ALIGN_RIGHT_MID, -96, 0);
    lv_obj_t *bref = mk_btn(bar, "Yenile", 80, ADL_SURFACE_2, do_refresh_cb);
    lv_obj_align(bref, LV_ALIGN_RIGHT_MID, -6, 0);

    g_list = lv_obj_create(win);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_style_bg_color(g_list, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_list, 1, 0);
    lv_obj_set_style_border_color(g_list, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_radius(g_list, 10, 0);
    lv_obj_set_pos(g_list, 0, 48);
    lv_obj_set_size(g_list, W, H - 48 - 28);
    lv_obj_remove_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);

    g_empty = lv_label_create(g_list);
    lv_label_set_text(g_empty, "Cop kovasi bos");
    lv_obj_set_style_text_color(g_empty, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_align(g_empty, LV_ALIGN_TOP_MID, 0, 24);

    g_status = lv_label_create(win);
    lv_label_set_text(g_status, "");
    lv_obj_set_style_text_color(g_status, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_align(g_status, LV_ALIGN_BOTTOM_LEFT, 6, -4);

    lv_obj_add_event_cb(win, app_del_cb, LV_EVENT_DELETE, 0);
    refresh();
}
