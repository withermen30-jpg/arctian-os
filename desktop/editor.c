#include "dsk.h"

/* ------------------------------------------------------------------ */
/* Metin duzenleyici: geri al/yinele, otomatik kayit, satir no.        */
/* ------------------------------------------------------------------ */

#define ED_UNDO_MAX 16      /* yigin derinligi */
#define ED_SNAP_MAX 4096    /* yigina giran metin siniri (bellek icin) */

/* Acik duzenleyiciler (Ctrl+Z odakli duzenleyiciyi bulmak icin) */
static ed_ctx_t *s_eds[8];
static int       s_eds_n = 0;

static void ed_reg_add(ed_ctx_t *c) {
    if (s_eds_n < 8) s_eds[s_eds_n++] = c;
}

static void ed_reg_del(ed_ctx_t *c) {
    for (int i = 0; i < s_eds_n; i++)
        if (s_eds[i] == c) { s_eds[i] = s_eds[--s_eds_n]; break; }
}

/* Metin anlik goruntusu (sinirli uzunlukta kopya) */
static char *ed_snap(const char *s) {
    int n = 0;
    if (s) while (s[n] && n < ED_SNAP_MAX - 1) n++;
    char *d = (char *)lv_malloc((size_t)n + 1);
    if (!d) return 0;
    for (int i = 0; i < n; i++) d[i] = s[i] ? s[i] : 0;
    d[n] = 0;
    return d;
}

static void ed_push(char **stack, int *n, const char *txt) {
    if (*n >= ED_UNDO_MAX) {           /* yer ac: en eskisini at */
        if (stack[0]) lv_free(stack[0]);
        for (int i = 1; i < *n; i++) stack[i - 1] = stack[i];
        (*n)--;
    }
    char *d = ed_snap(txt);
    if (d) stack[(*n)++] = d;
}


void ed_split(const char *path, char *dir, int dcap, char *name, int ncap) {
    int last = -1;
    for (int i = 0; path[i]; i++) if (path[i] == '/') last = i;
    int di = 0;
    for (int i = 0; i <= last && di < dcap - 1; i++) dir[di++] = path[i];
    dir[di] = 0;
    int ni = 0;
    for (int i = last + 1; path[i] && ni < ncap - 1; i++) name[ni++] = path[i];
    name[ni] = 0;
}


/* ---- Satir numaralari (sutun) + kaydirma esitleme ---- */
static void ed_lineno_update(ed_ctx_t *c) {
    if (!c || !c->gutter || !c->ta) return;
    const char *txt = lv_textarea_get_text(c->ta);
    int lines = 1;
    if (txt) for (int i = 0; txt[i]; i++) if (txt[i] == '\n') lines++;
    if (lines > 500) lines = 500;
    static char buf[2300];
    int n = 0;
    for (int i = 1; i <= lines && n < 2200; i++) {
        char tmp[8];
        int k = 0, v = i;
        if (v == 0) tmp[k++] = '0';
        while (v > 0 && k < 7) { tmp[k++] = (char)('0' + (v % 10)); v /= 10; }
        while (k > 0 && n < 2200) buf[n++] = tmp[--k];
        if (n < 2200) buf[n++] = '\n';
    }
    buf[n] = 0;
    lv_label_set_text(c->gutter, buf);
    /* metin kutusuyla ayni kaydirma */
    lv_obj_scroll_to_y(c->gutter, lv_obj_get_scroll_y(c->ta), LV_ANIM_OFF);
}


static void ed_scroll_cb(lv_event_t *e) {
    ed_ctx_t *c = (ed_ctx_t *)lv_event_get_user_data(e);
    if (c && c->gutter)
        lv_obj_scroll_to_y(c->gutter, lv_obj_get_scroll_y(c->ta), LV_ANIM_OFF);
}


/* ---- Kayit ---- */
static int ed_save(ed_ctx_t *c, int manual) {
    if (!c || !c->ta) return 0;
    const char *txt = lv_textarea_get_text(c->ta);
    if (!txt) txt = "";
    if (!g_asi || !g_asi->fs || !g_asi->fs->find || !g_asi->fs->write) {
        if (c->status) lv_label_set_text(c->status, "Dosya sistemi yok");
        return 0;
    }
    void *h = g_asi->fs->find(0, c->path);
    if (!h) {
        char dir[160], nm[96];
        ed_split(c->path, dir, sizeof(dir), nm, sizeof(nm));
        h = g_asi->fs->create_in ? g_asi->fs->create_in(dir, nm, 0) : 0;
    }
    if (!h) {
        if (c->status) lv_label_set_text(c->status, "Kaydedilemedi");
        return 0;
    }
    uint32_t n = 0; while (txt[n]) n++;
    int r = g_asi->fs->write(h, txt, n);
    if (r == 0) {
        c->dirty = 0;
        if (c->status)
            lv_label_set_text(c->status, manual ? "Kaydedildi" : "Otomatik kaydedildi");
        return 1;
    }
    if (c->status) lv_label_set_text(c->status, "Yazma hatasi");
    return 0;
}


void ed_save_cb(lv_event_t *e) {
    ed_ctx_t *c = (ed_ctx_t *)lv_event_get_user_data(e);
    ed_save(c, 1);
}


/* 5 sn boyunca dokunulmadiysa dosyaya kaydet      */
static void ed_autosave_cb(lv_timer_t *t) {
    ed_ctx_t *c = (ed_ctx_t *)lv_timer_get_user_data(t);
    if (c && c->dirty) ed_save(c, 0);
}


/* ---- Geri al / yinele ---- */
static void ed_apply(ed_ctx_t *c, const char *txt) {
    c->guard = 1;                       /* set_text kaydi tetiklemesin */
    lv_textarea_set_text(c->ta, txt);
    c->guard = 0;
    c->dirty = 1;
    ed_lineno_update(c);
}


static void ed_do_undo(ed_ctx_t *c, int redo) {
    if (!c) return;
    const char *cur = lv_textarea_get_text(c->ta);
    if (redo) {
        if (c->redo_n <= 0) {
            if (c->status) lv_label_set_text(c->status, "Yinelenecek degisiklik yok");
            return;
        }
        ed_push(c->undo, &c->undo_n, cur);
        char *top = c->redo[--c->redo_n];
        ed_apply(c, top);
        lv_free(top);
    } else {
        if (c->undo_n <= 0) {
            if (c->status) lv_label_set_text(c->status, "Geri alinacak degisiklik yok");
            return;
        }
        ed_push(c->redo, &c->redo_n, cur);
        char *top = c->undo[--c->undo_n];
        ed_apply(c, top);
        lv_free(top);
    }
    if (c->status)
        lv_label_set_text(c->status, redo ? "Yinelendi" : "Geri alindi");
}


/* Duzenleyicinin degisikliklerini yigina kaydet */
static void ed_change_cb(lv_event_t *e) {
    ed_ctx_t *c = (ed_ctx_t *)lv_event_get_user_data(e);
    if (!c || c->guard) return;
    const char *cur = lv_textarea_get_text(c->ta);
    /* eski durum yiginda, yeni durum 'last' olur */
    ed_push(c->undo, &c->undo_n, c->last ? c->last : "");
    if (c->last) lv_free(c->last);
    c->last = ed_snap(cur);
    c->dirty = 1;
    c->redo_n = 0;                       /* yeni degisiklik yinele yiginini siler */
    ed_lineno_update(c);
}


/* Uygulama API (app.h): odakli textarea'nin metin geri al/yinele'si */
int arctian_shell_text_undo(int redo) {
    lv_indev_t *kp = lv_port_get_keypad();
    lv_group_t *g = kp ? lv_indev_get_group(kp) : 0;
    lv_obj_t *foc = g ? lv_group_get_focused(g) : 0;
    if (!foc || !lv_obj_check_type(foc, &lv_textarea_class)) return 0;
    for (int i = 0; i < s_eds_n; i++) {
        if (s_eds[i] && s_eds[i]->ta == foc) { ed_do_undo(s_eds[i], redo); return 1; }
    }
    return 0;
}


/* Pencere kapaninca zamanlayici + yiginlar temizlenir */
static void ed_win_del_cb(lv_event_t *e) {
    ed_ctx_t *c = (ed_ctx_t *)lv_event_get_user_data(e);
    if (!c) return;
    if (c->autosave) lv_timer_delete(c->autosave);
    for (int i = 0; i < c->undo_n; i++) if (c->undo[i]) lv_free(c->undo[i]);
    for (int i = 0; i < c->redo_n; i++) if (c->redo[i]) lv_free(c->redo[i]);
    if (c->last) lv_free(c->last);
    ed_reg_del(c);
    lv_free(c);
}


void ed_set_focus(lv_obj_t *ta) {
    if (!ta) return;
    if (!g_ed_group) g_ed_group = lv_group_create();
    if (!g_ed_group) return;
    lv_indev_t *kp = lv_port_get_keypad();
    lv_group_t *cur = kp ? lv_indev_get_group(kp) : 0;
    if (cur && cur != g_ed_group) g_ed_prev = cur;
    lv_group_remove_all_objs(g_ed_group);
    lv_group_add_obj(g_ed_group, ta);
    if (kp) lv_indev_set_group(kp, g_ed_group);
    lv_group_focus_obj(ta);
}


void ed_focus_cb(lv_event_t *e) {
    ed_set_focus(lv_event_get_target(e));
}


void ed_del_cb(lv_event_t *e) {
    (void)e;
    if (g_ed_group) lv_group_remove_all_objs(g_ed_group);
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, g_ed_prev);
}


/* Pencere boyutu degince (snap/buyut/kucult) icerik yeniden yerlesir.
 * Cocuk sirasi: 0 baslik cubugu (windows.c), 1 metin kutusu, 2 satir
 * numarasi sutunu, 3 Kaydet, 4 durum satiri. */
static void ed_relayout(lv_obj_t *win) {
    int w = lv_obj_get_width(win);
    int h = lv_obj_get_height(win);
    if (w < 300 || h < 220) return;
    if (lv_obj_get_child_count(win) < 5) return;
    lv_obj_t *ta = lv_obj_get_child(win, 1);
    lv_obj_t *gt = lv_obj_get_child(win, 2);
    lv_obj_t *btn = lv_obj_get_child(win, 3);
    lv_obj_t *st = lv_obj_get_child(win, 4);
    if (gt) { lv_obj_set_pos(gt, 6, 44); lv_obj_set_size(gt, 34, h - 108); }
    if (ta) { lv_obj_set_pos(ta, 46, 44); lv_obj_set_size(ta, w - 56, h - 108); }
    if (btn) lv_obj_set_pos(btn, 12, h - 54);
    if (st) lv_obj_set_pos(st, 136, h - 44);
}


void arctian_shell_editor(const char *path) {
    if (!path || !path[0]) return;
    char title[64];
    const char *nm = path;
    for (const char *p = path; *p; p++) if (*p == '/') nm = p + 1;
    int t = 0;
    const char *pre = "Duzenleyici - ";
    while (pre[t] && t < 40) { title[t] = pre[t]; t++; }
    for (int i = 0; nm[i] && t < 62; i++) title[t++] = nm[i];
    title[t] = 0;

    int w = 640, h = 460;
    lv_obj_t *win = create_window(title, w, h);

    lv_obj_t *ta = lv_textarea_create(win);
    lv_obj_set_pos(ta, 46, 44);
    lv_obj_set_size(ta, w - 56, h - 108);
    lv_textarea_set_placeholder_text(ta, "Metin yazin...");
    lv_obj_set_style_bg_color(ta, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_pad_all(ta, 8, 0);
    lv_font_t *f = adl_font();
    if (f) lv_obj_set_style_text_font(ta, f, 0);
    lv_textarea_set_text_selection(ta, true);   /* fare ile secim acik */

    /* Satir numarasi sutunu      */
    lv_obj_t *gt = lv_label_create(win);
    lv_obj_remove_style_all(gt);
    lv_obj_set_pos(gt, 6, 44);
    lv_obj_set_size(gt, 34, h - 108);
    lv_label_set_long_mode(gt, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_bg_color(gt, lv_color_hex(ADL_BG), 0);
    lv_obj_set_style_bg_opa(gt, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(gt, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_set_style_text_align(gt, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_pad_top(gt, 8, 0);
    if (f) lv_obj_set_style_text_font(gt, f, 0);
    lv_obj_remove_flag(gt, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(ta, ed_focus_cb, LV_EVENT_CLICKED, 0);
    lv_obj_add_event_cb(ta, ed_del_cb, LV_EVENT_DELETE, 0);
    ed_set_focus(ta);

    if (g_asi && g_asi->fs && g_asi->fs->read_path) {
        static char ebuf[8192];
        uint32_t r = g_asi->fs->read_path(path, ebuf, sizeof(ebuf) - 1);
        if (r > 0) { ebuf[r] = 0; lv_textarea_set_text(ta, ebuf); }
    }

    ed_ctx_t *c = (ed_ctx_t *)lv_malloc(sizeof(ed_ctx_t));
    if (c) {
        for (unsigned i = 0; i < sizeof(*c); i++) ((unsigned char *)c)[i] = 0;
        int i = 0; for (; path[i] && i < 159; i++) c->path[i] = path[i]; c->path[i] = 0;
        c->ta = ta;
        c->status = 0;
        c->gutter = gt;
        c->win = win;
        c->last = ed_snap(lv_textarea_get_text(ta));
        ed_reg_add(c);
        lv_obj_add_event_cb(ta, ed_change_cb, LV_EVENT_VALUE_CHANGED, c);
        lv_obj_add_event_cb(ta, ed_scroll_cb, LV_EVENT_SCROLL, c);
        lv_obj_add_event_cb(win, ed_win_del_cb, LV_EVENT_DELETE, c);
        c->autosave = lv_timer_create(ed_autosave_cb, 5000, c);
        ed_lineno_update(c);
    }

    lv_obj_t *btn = lv_button_create(win);
    lv_obj_set_size(btn, 110, 36);
    lv_obj_set_pos(btn, 12, h - 54);
    lv_obj_t *bl = label(btn, "Kaydet", 0xFFFFFF);
    lv_obj_center(bl);
    if (c) lv_obj_add_event_cb(btn, ed_save_cb, LV_EVENT_CLICKED, c);

    lv_obj_t *st = label(win, "", ADL_TEXT_DIM);
    lv_obj_set_pos(st, 136, h - 44);
    if (c) c->status = st;

    /* Pencere boyutu degince icerik yeniden aksin     */
    win_set_relayout(win, ed_relayout);
}
