#include "dsk.h"


int app_idx_by_name(const char *nm) {
    for (int i = 0; i < app_total(); i++) {
        const arctian_app_t *a = app_get(i);
        if (a && tb_streq(a->name, nm)) return i;
    }
    return -1;
}


int app_idx_by_title(const char *t) {
    for (int i = 0; i < app_total(); i++) {
        const arctian_app_t *a = app_get(i);
        if (a && title_eq(a->title, t)) return i;
    }
    return -1;
}


/* ---- Sabitleme kalici ayarlar (settings.cfg "pins" anahtari) ---- */

void tb_pins_load(void) {
    const char *s = kv_get("pins", "settings,filemanager,browser,duckstore");
    g_tb_pin_n = 0;
    while (s && *s && g_tb_pin_n < TB_MAX_ICONS) {
        const char *q = s;
        while (*q && *q != ',') q++;
        int len = (int)(q - s);
        if (len > 0 && len < 24) {
            for (int i = 0; i < len; i++) g_tb_pins[g_tb_pin_n][i] = s[i];
            g_tb_pins[g_tb_pin_n][len] = 0;
            g_tb_pin_n++;
        }
        s = (*q) ? q + 1 : q;
    }
}


void tb_pins_save(void) {
    char buf[96];
    int n = 0;
    for (int i = 0; i < g_tb_pin_n && n < 90; i++) {
        if (i) buf[n++] = ',';
        const char *p = g_tb_pins[i];
        while (*p && n < 90) buf[n++] = *p++;
    }
    buf[n] = 0;
    kv_set("pins", buf);
}


int tb_pin_find_name(const char *nm) {
    for (int i = 0; i < g_tb_pin_n; i++) if (tb_streq(g_tb_pins[i], nm)) return i;
    return -1;
}


void tb_pin_add_name(const char *nm) {
    if (tb_pin_find_name(nm) >= 0 || g_tb_pin_n >= TB_MAX_ICONS) return;
    int i = 0;
    for (; nm[i] && i < 23; i++) g_tb_pins[g_tb_pin_n][i] = nm[i];
    g_tb_pins[g_tb_pin_n][i] = 0;
    g_tb_pin_n++;
    tb_pins_save();
    g_tb_dirty = 1;
}


void tb_pin_del_name(const char *nm) {
    int i = tb_pin_find_name(nm);
    if (i < 0) return;
    for (; i < g_tb_pin_n - 1; i++) scopy(g_tb_pins[i], g_tb_pins[i + 1], 23);
    g_tb_pin_n--;
    tb_pins_save();
    g_tb_dirty = 1;
}


/* ---- Masaustu kisayollari (settings.cfg "dshort" anahtari) ---- */

void dshort_load(void) {
    const char *s = kv_get("dshort", "");
    g_dshort_n = 0;
    while (s && *s && g_dshort_n < 8) {
        const char *q = s;
        while (*q && *q != ',') q++;
        int len = (int)(q - s);
        if (len > 0 && len < 24) {
            for (int i = 0; i < len; i++) g_dshort[g_dshort_n][i] = s[i];
            g_dshort[g_dshort_n][len] = 0;
            g_dshort_n++;
        }
        s = (*q) ? q + 1 : q;
    }
}


void dshort_save(void) {
    char buf[96];
    int n = 0;
    for (int i = 0; i < g_dshort_n && n < 90; i++) {
        if (i) buf[n++] = ',';
        const char *p = g_dshort[i];
        while (*p && n < 90) buf[n++] = *p++;
    }
    buf[n] = 0;
    kv_set("dshort", buf);
}


int dshort_find(const char *nm) {
    for (int i = 0; i < g_dshort_n; i++) if (tb_streq(g_dshort[i], nm)) return i;
    return -1;
}


void dshort_toggle(const char *nm) {
    int i = dshort_find(nm);
    if (i >= 0) {
        for (; i < g_dshort_n - 1; i++) scopy(g_dshort[i], g_dshort[i + 1], 23);
        g_dshort_n--;
    } else {
        if (g_dshort_n >= 8) return;
        int k = 0;
        for (; nm[k] && k < 23; k++) g_dshort[g_dshort_n][k] = nm[k];
        g_dshort[g_dshort_n][k] = 0;
        g_dshort_n++;
    }
    dshort_save();
    dicons_rebuild();
}


void tb_menu_desk_cb(lv_event_t *e) {
    (void)e;
    if (s_rmb_app) dshort_toggle(s_rmb_app->name);
    tb_menu_close();
}


void tb_show_desk_menu(int x, int y, const arctian_app_t *a) {
    tb_menu_close();
    if (!a) return;
    s_rmb_app = a;
    int has = dshort_find(a->name) >= 0;

    g_tb_menu = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(g_tb_menu);
    lv_obj_set_size(g_tb_menu, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(g_tb_menu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_tb_menu, tb_menu_overlay_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *p = lv_obj_create(g_tb_menu);
    base_obj(p, ADL_SURFACE, ui_s(10), ADL_BORDER);
    lv_obj_set_style_bg_opa(p, 250, 0);
    lv_obj_set_style_shadow_width(p, ui_s(16), 0);
    lv_obj_set_style_shadow_color(p, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(p, 150, 0);
    lv_obj_set_size(p, ui_s(280), ui_s(68));
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    int sw = g_asi->gfx->width(), sh = g_asi->gfx->height();
    if (x > sw - ui_s(288)) x = sw - ui_s(288);
    if (x < ui_s(6)) x = ui_s(6);
    if (y > sh - ui_s(80)) y = sh - ui_s(80);
    if (y < ui_s(6)) y = ui_s(6);
    lv_obj_set_pos(p, x, y);

    tb_menu_button(p, 0, has ? "Masaustunden kaldir" : "Masaustune ekle", tb_menu_desk_cb);
}


/* Baslat menusu kartlari icin kombine sag tik menusu — gorev cubuguna
 * sabitle/kaldir + masaustune ekle/kaldir + (aciksa) pencereyi kapat. */
void tb_show_app_menu(int x, int y, const arctian_app_t *a) {
    tb_menu_close();
    if (!a) return;
    scopy(g_tb_menu_title, a->title, 39);
    s_rmb_app = a;
    int running = win_find(a->title) != 0;
    int pinned = tb_pin_find_name(a->name) >= 0;
    int desk = dshort_find(a->name) >= 0;
    (void)desk;
    int items = 2 + (running ? 1 : 0);   /* sabitle/kaldir + masaustu (+ kapat) */

    int mh = ui_s(24) + items * ui_s(52);
    g_tb_menu = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(g_tb_menu);
    lv_obj_set_size(g_tb_menu, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(g_tb_menu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_tb_menu, tb_menu_overlay_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *p = lv_obj_create(g_tb_menu);
    base_obj(p, ADL_SURFACE, ui_s(10), ADL_BORDER);
    lv_obj_set_style_bg_opa(p, 250, 0);
    lv_obj_set_style_shadow_width(p, ui_s(16), 0);
    lv_obj_set_style_shadow_color(p, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(p, 150, 0);
    lv_obj_set_size(p, ui_s(280), mh);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    int sw = g_asi->gfx->width(), sh = g_asi->gfx->height();
    if (x > sw - ui_s(288)) x = sw - ui_s(288);
    if (x < ui_s(6)) x = ui_s(6);
    if (y > sh - mh - ui_s(6)) y = sh - mh - ui_s(6);
    if (y < ui_s(6)) y = ui_s(6);
    lv_obj_set_pos(p, x, y);

    int k = 0;
    if (pinned) tb_menu_button(p, k++, "Görev çubuğundan kaldır", tb_menu_unpin_cb);
    else        tb_menu_button(p, k++, "Görev çubuğuna sabitle", tb_menu_pin_cb);
    tb_menu_button(p, k++, desk ? "Masaustunden kaldir" : "Masaustune ekle", tb_menu_desk_cb);
    if (running) tb_menu_button(p, k++, "Pencereyi kapat", tb_menu_kill_cb);
}


/* ---- Sag tik menusu (taskbar simgesi uzerinde) ---- */

void tb_menu_close(void) {
    if (g_tb_menu) { lv_obj_delete(g_tb_menu); g_tb_menu = 0; }
}


void tb_menu_overlay_cb(lv_event_t *e) { (void)e; tb_menu_close(); }


void tb_menu_pin_cb(lv_event_t *e) {
    (void)e;
    int ai = app_idx_by_title(g_tb_menu_title);
    if (ai >= 0) tb_pin_add_name(app_get(ai)->name);
    tb_menu_close();
}


void tb_menu_unpin_cb(lv_event_t *e) {
    (void)e;
    int ai = app_idx_by_title(g_tb_menu_title);
    if (ai >= 0) tb_pin_del_name(app_get(ai)->name);
    tb_menu_close();
}


void tb_menu_kill_cb(lv_event_t *e) {
    (void)e;
    win_close_rec(win_find(g_tb_menu_title));
    tb_menu_close();
}


void tb_menu_button(lv_obj_t *p, int idx, const char *txt, lv_event_cb_t cb) {
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, ui_s(256), ui_s(44));
    lv_obj_set_pos(b, ui_s(12), ui_s(12) + idx * ui_s(52));
    lv_obj_set_style_radius(b, ui_s(10), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_t *l = label(b, txt, ADL_TEXT);
    if (!s_f_dlabel) s_f_dlabel = lv_tiny_ttf_create_data(font_ttf, font_ttf_len, ui_s(22));
    if (s_f_dlabel) lv_tiny_ttf_set_size(s_f_dlabel, ui_s(22));
    lv_obj_set_style_text_font(l, s_f_dlabel ? s_f_dlabel : adl_font(), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, 0);
}


void tb_show_menu(int x, const char *title) {
    tb_menu_close();
    scopy(g_tb_menu_title, title, 39);
    int ai = app_idx_by_title(title);
    const arctian_app_t *a = (ai >= 0) ? app_get(ai) : 0;
    int running = win_find(title) != 0;
    int pinned = a && tb_pin_find_name(a->name) >= 0;
    int i_pin = running && !pinned && a && g_tb_pin_n < TB_MAX_ICONS;
    int i_unpin = pinned;
    int i_kill = running;
    int items = i_pin + i_unpin + i_kill;
    if (!items) return;

    int mh = ui_s(24) + items * ui_s(52);
    g_tb_menu = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(g_tb_menu);
    lv_obj_set_size(g_tb_menu, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(g_tb_menu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_tb_menu, tb_menu_overlay_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *p = lv_obj_create(g_tb_menu);
    base_obj(p, ADL_SURFACE, ui_s(10), ADL_BORDER);
    lv_obj_set_style_bg_opa(p, 250, 0);
    lv_obj_set_style_shadow_width(p, ui_s(16), 0);
    lv_obj_set_style_shadow_color(p, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(p, 150, 0);
    lv_obj_set_size(p, ui_s(280), mh);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    int sw = g_asi->gfx->width();
    if (x > sw - ui_s(288)) x = sw - ui_s(288);
    if (x < ui_s(6)) x = ui_s(6);
    int y = g_asi->gfx->height() - TB_BH - TB_Y_OFF - mh - ui_s(6);
    lv_obj_set_pos(p, x, y);

    int k = 0;
    if (i_pin)   tb_menu_button(p, k++, "Sabitle", tb_menu_pin_cb);
    if (i_unpin) tb_menu_button(p, k++, "Sabitlemeyi kaldir", tb_menu_unpin_cb);
    if (i_kill)  tb_menu_button(p, k++, "Pencereyi kapat", tb_menu_kill_cb);
}


/* ---- Bildirim zili + hizli ayarlar --------- */
static lv_obj_t *s_bell;
static lv_obj_t *s_bell_badge;
static lv_obj_t *s_tb_search_ta;

/* Pil ikonu: seviyeye gore yeniden cizilen tuval.             */
static lv_obj_t *s_bat_canvas;
static uint32_t *s_bat_buf;
static int       s_bat_w, s_bat_h;
static int       s_bat_level = 87;

/* Okunmamis bildirim rozeti: sayac 0 ise gizle.      */
void tb_refresh_bell_badge(void) {
    if (!s_bell_badge) return;
    if (qc_unread_count() > 0) lv_obj_remove_flag(s_bell_badge, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_bell_badge, LV_OBJ_FLAG_HIDDEN);
}
static lv_obj_t *s_qs_btn;

static void tb_bell_cb(lv_event_t *e) { (void)e; qc_show_notif(); qc_unread_clear(); }

/* Dock arama kutusu: yazarken baslat menusu filtrelenir.      */
/* 0.6: dock arama kutusu — GERCEK arama.
 * Yazarken baslat menusu canli filtrelenir (odak kutuda kalir), Enter ilk
 * sonucu acar. Onceden kutu yalnizca menuyu aciyordu. */
static lv_group_t *s_tb_group;

static void tb_search_cb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    startmenu_search_live(lv_textarea_get_text(ta));
}

static void tb_search_ready_cb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    const char *q = lv_textarea_get_text(ta);
    if (startmenu_is_open() && startmenu_launch_first()) {
        lv_textarea_set_text(ta, "");
        return;
    }
    if (q && q[0]) startmenu_search_live(q);
}

/* Kutuya tiklaninca klavye odagi kutuya gelir (yazmaya baslayabilirsiniz). */
static void tb_search_click_cb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    if (!ta || !lv_obj_check_type(ta, &lv_textarea_class)) return;
    lv_indev_t *kp = lv_port_get_keypad();
    if (!s_tb_group) s_tb_group = lv_group_create();
    lv_group_remove_all_objs(s_tb_group);
    lv_group_add_obj(s_tb_group, ta);
    if (kp) lv_indev_set_group(kp, s_tb_group);
    lv_group_focus_obj(ta);
}

static void tb_qs_cb(lv_event_t *e) { (void)e; qc_show_quick(); }

/* Asistan dugmesi: paneli acar/kapatir.       */
void ai_btn_cb(lv_event_t *e) { (void)e; assistant_toggle(); }

/* Klavye gezinme: ogeleri masaustu grubuna ekle (Tab/Enter).      */
static void group_add(lv_obj_t *o) {
    if (g_desktop_group && o) lv_group_add_obj(g_desktop_group, o);
    /* Bu ogeler remove_style_all gecirir; tema halkasi silinir -> geri ekle */
    adl_focus_ring_attach(o);
}


/* ---- Calisma alani gostergesi (0.6) ----
 * Gorev cubugunun solunda kucuk noktalar: etkin alan dolu ve vurgulu.
 * Tiklayinca o alana gecilir. Alan sayisi ayardan gelir. */
static lv_obj_t *s_ws_box;
static lv_obj_t *s_ws_dot[4];

/* Kabuk yeniden kurulurken gorev cubugu nesneleri silinir; statik isaretciler
 * sifirlanmali (aksi halde tb_ws_refresh silinmis nesneye erisir). */
void tb_reset_objects(void) {
    s_ws_box = 0;
    for (int i = 0; i < 4; i++) s_ws_dot[i] = 0;
    s_bell = 0;
    s_bell_badge = 0;
    s_tb_search_ta = 0;
    s_qs_btn = 0;
    s_tb_group = 0;
    s_bat_canvas = 0;
    s_bat_buf = 0;
    s_bat_w = 0;
    s_bat_h = 0;
}

/* Dock'unda calisma alani noktalari yok; isaretciler silinmis
 * olabilecegi icin fonksiyon korunur (settings.c / windows.c cagirir). */
void tb_ws_refresh(void) {
    if (!s_ws_box) return;    for (int i = 0; i < 4; i++) {
        if (!s_ws_dot[i]) continue;
        if (i >= kv_get_int("ws_count", 3)) {
            lv_obj_add_flag(s_ws_dot[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_ws_dot[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(s_ws_dot[i], i == g_ws ? LV_OPA_COVER : 110, 0);
        lv_obj_set_size(s_ws_dot[i], i == g_ws ? ui_s(16) : ui_s(10), ui_s(10));
    }
}



/* ---- Duzen + yeniden kurma ---- */

void tb_layout(void) {
    if (!g_taskbar || !g_tb_logo) return;

    /* dock_genisligi = 596 + 48n - 4 (tasarim birimi).
     * Tum x degerleri tasarim biriminde biriktirilip tek seferde olceklenir;
     * boylece kucuk yuvarlamalar birikip kayma yapmaz. */
    int n = g_tb_n;
    int w_du = 596 + 48 * n - 4;
    int u = 8;

    lv_obj_set_size(g_taskbar, dk_s(w_du), dk_s(60));

    lv_obj_set_pos(g_tb_logo, dk_s(u), dk_s(8));       u += 44 + 8;
    lv_obj_set_pos(g_tb_search, dk_s(u), dk_s(10));    u += 200 + 14;
    lv_obj_set_pos(g_tb_sep1, dk_s(u), dk_s(18));      u += 1 + 14;
    lv_obj_set_pos(g_tb_apps_cont, dk_s(u), dk_s(8));
    u += n * 44 + (n > 0 ? (n - 1) * 4 : 0);
    u += 14;
    lv_obj_set_pos(g_tb_sep2, dk_s(u), dk_s(18));      u += 1 + 14;
    lv_obj_set_pos(g_tb_status, dk_s(u), dk_s(8));     u += 72 + 2;
    if (s_bell)   { lv_obj_set_pos(s_bell, dk_s(u), dk_s(8));   u += 44 + 2; }
    if (s_qs_btn) { lv_obj_set_pos(s_qs_btn, dk_s(u), dk_s(8)); u += 44 + 10; }
    lv_obj_set_pos(g_tb_sep3, dk_s(u), dk_s(18));      u += 1 + 6;
    lv_obj_set_pos(g_tb_clock, dk_s(u), dk_s(8));

    lv_obj_align(g_taskbar, LV_ALIGN_BOTTOM_MID, 0, -TB_Y_OFF);
}

void tb_btn_click_cb(lv_event_t *e) {
    char *title = (char *)lv_event_get_user_data(e);
    winrec_t *r = win_find(title);
    if (r) {
        if (lv_obj_has_flag(r->win, LV_OBJ_FLAG_HIDDEN))
            lv_obj_remove_flag(r->win, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(r->win, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(r->win);
        return;
    }
    int ai = app_idx_by_title(title);
    if (ai >= 0) open_registry_app(ai);
}


void tb_rebuild_icons(void) {
    if (!g_tb_apps_cont) return;
    lv_obj_clean(g_tb_apps_cont);
    g_tb_n = 0;

    /* 1) Sabitlenenler (siralari korunur) */
    for (int p = 0; p < g_tb_pin_n && g_tb_n < TB_MAX_ICONS; p++) {
        int ai = app_idx_by_name(g_tb_pins[p]);
        if (ai < 0) continue;
        const arctian_app_t *a = app_get(ai);
        if (!a) continue;
        int dup = 0;
        for (int i = 0; i < g_tb_n; i++)
            if (title_eq(g_tb[i].title, a->title)) { dup = 1; break; }
        if (dup) continue;
        scopy(g_tb[g_tb_n].title, a->title, 39);
        g_tb[g_tb_n].app_idx = ai;
        g_tb[g_tb_n].pinned = 1;
        g_tb[g_tb_n].btn = 0;
        g_tb_n++;
    }

    /* 2) Acik ama sabit olmayan pencereler */
    for (int j = 0; j < MAX_WINS && g_tb_n < TB_MAX_ICONS; j++) {
        winrec_t *r = g_recs[j];
        if (!r || !r->win) continue;
        int known = 0;
        for (int i = 0; i < g_tb_n; i++)
            if (title_eq(g_tb[i].title, r->title)) { known = 1; break; }
        if (known) continue;
        scopy(g_tb[g_tb_n].title, r->title, 39);
        g_tb[g_tb_n].app_idx = app_idx_by_title(r->title);
        g_tb[g_tb_n].pinned = 0;
        g_tb[g_tb_n].btn = 0;
        g_tb_n++;
    }

    /* On plandaki pencere: secili (btn-selected) ve 16x2 accent gosterge. */
    winrec_t *top = winrec_top();
    const char *focus_title = (top && top->win &&
                               !lv_obj_has_flag(top->win, LV_OBJ_FLAG_HIDDEN))
                              ? top->title : 0;

    for (int i = 0; i < g_tb_n; i++) {
        tb_slot_t *s = &g_tb[i];
        winrec_t *wr = win_find(s->title);
        int focused = wr && focus_title && title_eq(focus_title, s->title);

        lv_obj_t *t = lv_obj_create(g_tb_apps_cont);
        lv_obj_remove_style_all(t);
        adl_focus_ring_attach(t);   /* remove_style_all halkayi siler */
        lv_obj_set_size(t, TB_IC, TB_IC);
        lv_obj_set_pos(t, dk_s(i * 48), 0);
        lv_obj_set_style_radius(t, dk_s(8), 0);
        lv_obj_set_style_bg_color(t, lv_color_hex(DK_TEXT), 0);
        lv_obj_set_style_bg_opa(t, focused ? DK_SEL_OPA : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(t, DK_HOVER_OPA, LV_STATE_HOVERED);
        lv_obj_set_style_bg_opa(t, DK_PRESS_OPA, LV_STATE_PRESSED);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
        if (g_desktop_group) lv_group_add_obj(g_desktop_group, t);

        const arctian_app_t *a = (s->app_idx >= 0) ? app_get(s->app_idx) : 0;
        /* Renkli uygulama ikonu 24x24 alanda (dugmede ortalı).            */
        lv_obj_t *gl = a ? make_app_icon(t, a, dk_s(24))
                         : make_icon(t, SVG_FOLDER, dk_s(22));
        lv_obj_center(gl);
        lv_obj_add_event_cb(t, tb_btn_click_cb, LV_EVENT_CLICKED, s->title);

        /* Alt gosterge: odaktaki 16x2 accent, calisan 6x2 text-muted.
         * Kutu icinde ortalı; alt kenardan 3 px yukarida. */
        if (wr) {
            int iw = focused ? 16 : 6;
            lv_obj_t *ind = lv_obj_create(t);
            lv_obj_remove_style_all(ind);
            lv_obj_remove_flag(ind, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_radius(ind, 0, 0);
            lv_obj_set_style_bg_color(ind,
                lv_color_hex(focused ? DK_ACCENT : DK_MUTED), 0);
            lv_obj_set_style_bg_opa(ind, LV_OPA_COVER, 0);
            lv_obj_set_size(ind, dk_s(iw), dk_s(2));
            lv_obj_align(ind, LV_ALIGN_BOTTOM_MID, 0, -dk_s(3));
        }
        s->btn = t;
    }
    lv_obj_set_width(g_tb_apps_cont,
                     g_tb_n > 0 ? dk_s(g_tb_n * 48 - 4) : 0);
    tb_layout();
}

void tb_sep(lv_obj_t *parent, lv_obj_t **out) {
    lv_obj_t *s = lv_obj_create(parent);
    lv_obj_remove_style_all(s);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, lv_color_hex(DK_TEXT), 0);
    lv_obj_set_style_bg_opa(s, DK_DIV_OPA, 0);      /* rgba(text, 0.16) */
    lv_obj_set_style_radius(s, 0, 0);
    lv_obj_set_size(s, TB_SEP_W, TB_SEP_H);
    *out = s;
}

/* Pil ikonu: 26x24 tuval, dolgu cubugu seviyeye gore.
 * Statik SVG yerine her seviyede yeniden cizilir (width = 15 * seviye). */

static void bat_render(void)
{
    if (!s_bat_canvas || !s_bat_buf || s_bat_w <= 0 || s_bat_h <= 0) return;
    int fill = (15 * s_bat_level + 50) / 100;      /* seviye 0..100 */
    if (fill < 1 && s_bat_level > 0) fill = 1;
    char svg[512];
    int k = 0;
    const char *a =
        "<svg viewBox=\"0 0 26 24\">"
        "<rect x=\"1.5\" y=\"6.5\" width=\"20\" height=\"11\" rx=\"2.5\" "
        "stroke=\"#EEF0F6\" stroke-width=\"1.6\" fill=\"none\"/>"
        "<rect x=\"23\" y=\"10.2\" width=\"2\" height=\"3.6\" rx=\"1\" "
        "fill=\"#EEF0F6\"/>";
    while (*a && k < (int)sizeof(svg) - 160) svg[k++] = *a++;
    /* dolgu rengi: %15 altinda accent */
    const char *fc = (s_bat_level < 15 && s_bat_level >= 0) ? "#F0A35E" : "#EEF0F6";
    const char *b = "<rect x=\"4\" y=\"9\" width=\"";
    while (*b) svg[k++] = *b++;
    svg[k++] = (char)('0' + (fill / 10) % 10);
    svg[k++] = (char)('0' + fill % 10);
    const char *c = "\" height=\"6\" rx=\"1\" fill=\"";
    while (*c) svg[k++] = *c++;
    while (*fc) svg[k++] = *fc++;
    const char *d = "\"/></svg>";
    while (*d) svg[k++] = *d++;
    svg[k] = 0;
    svg_render(svg, s_bat_w, s_bat_h, s_bat_buf);
    lv_obj_invalidate(s_bat_canvas);
}

void dock_battery_level(int pct)
{
    if (pct < 0) pct = 100;      /* AC: dolu goster */
    if (pct > 100) pct = 100;
    /* Seviye degismediyse SVG'yi yeniden rasterlestirmeye gerek yok;
     * power_tick her ~2 sn'de bir ayni yuzdeyle buraya gelir. */
    if (pct == s_bat_level) return;
    s_bat_level = pct;
    bat_render();
}

static lv_obj_t *mk_battery(lv_obj_t *parent, int w, int h)
{
    s_bat_w = w; s_bat_h = h;
    s_bat_buf = (uint32_t *)g_asi->mem->alloc((size_t)w * (size_t)h * 4u);
    if (!s_bat_buf) return 0;
    s_bat_canvas = lv_canvas_create(parent);
    lv_obj_remove_style_all(s_bat_canvas);
    lv_canvas_set_buffer(s_bat_canvas, s_bat_buf, w, h,
                         LV_COLOR_FORMAT_ARGB8888);
    bat_render();
    return s_bat_canvas;
}

void build_taskbar(lv_obj_t *scr) {
    tb_pins_load();

    /* IBM Plex Sans (400/600), olcekli punto.                    */
    if (!s_f_time) {
        s_f_time = lv_tiny_ttf_create_data(font_plex_sb, font_plex_sb_len, dk_s(16));
        s_f_date = lv_tiny_ttf_create_data(font_plex, font_plex_len, dk_s(12));
        s_f_search = lv_tiny_ttf_create_data(font_plex, font_plex_len, dk_s(14));
    }
    if (s_f_time) lv_tiny_ttf_set_size(s_f_time, dk_s(16));
    if (s_f_date) lv_tiny_ttf_set_size(s_f_date, dk_s(12));
    if (s_f_search) lv_tiny_ttf_set_size(s_f_search, dk_s(14));

    /* ---- Dock zemini: duz opak, golge/kontur/blur YOK ---- */
    g_taskbar = lv_obj_create(scr);
    base_obj(g_taskbar, DK_BG, dk_s(12), DK_BG);
    lv_obj_set_style_bg_opa(g_taskbar, LV_OPA_COVER, 0);   /* tam opak */
    lv_obj_set_style_border_width(g_taskbar, 0, 0);
    lv_obj_set_style_shadow_width(g_taskbar, 0, 0);
    lv_obj_set_style_blur_backdrop(g_taskbar, false, 0);
    lv_obj_set_style_pad_all(g_taskbar, 0, 0);
    lv_obj_remove_flag(g_taskbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(g_taskbar, dk_s(784), dk_s(60));
    lv_obj_align(g_taskbar, LV_ALIGN_BOTTOM_MID, 0, -TB_Y_OFF);

    /* ---- Baslat dugmesi 44x44, logo 36x36 ---- */
    g_tb_logo = lv_obj_create(g_taskbar);
    lv_obj_remove_style_all(g_tb_logo);
    lv_obj_set_size(g_tb_logo, TB_LOGO, TB_LOGO);
    lv_obj_set_style_radius(g_tb_logo, dk_s(8), 0);
    lv_obj_set_style_bg_color(g_tb_logo, lv_color_hex(DK_TEXT), 0);
    lv_obj_set_style_bg_opa(g_tb_logo, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(g_tb_logo, DK_HOVER_OPA, LV_STATE_HOVERED);
    lv_obj_set_style_bg_opa(g_tb_logo, DK_PRESS_OPA, LV_STATE_PRESSED);
    lv_obj_add_flag(g_tb_logo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_tb_logo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_tb_logo, start_btn_cb, LV_EVENT_CLICKED, 0);
    {
        lv_obj_t *lg = make_logo(g_tb_logo, dk_s(36));
        lv_obj_center(lg);
    }

    /* ---- Arama alani 200x40, field-bg, radius 8 ---- */
    g_tb_search = lv_obj_create(g_taskbar);
    lv_obj_remove_style_all(g_tb_search);
    lv_obj_set_style_bg_color(g_tb_search, lv_color_hex(DK_FIELD), 0);
    lv_obj_set_style_bg_opa(g_tb_search, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_tb_search, lv_color_hex(DK_FIELD_HOV),
                              LV_STATE_HOVERED);
    lv_obj_set_style_radius(g_tb_search, dk_s(8), 0);
    lv_obj_set_style_border_width(g_tb_search, 0, 0);
    lv_obj_set_size(g_tb_search, TB_SRCH_W, TB_SRCH_H);
    lv_obj_remove_flag(g_tb_search, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_tb_search, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_tb_search, start_btn_cb, LV_EVENT_CLICKED, 0);
    {
        /* Ikon alaninin solundan 12 px iceride, 18x18, text-muted */
        lv_obj_t *sic = make_icon(g_tb_search, SVG_DK_SEARCH, dk_s(18));
        lv_obj_align(sic, LV_ALIGN_LEFT_MID, dk_s(12), 0);
    }
    /* Gercek arama alani      */
    s_tb_search_ta = lv_textarea_create(g_tb_search);
    lv_obj_remove_style_all(s_tb_search_ta);
    lv_textarea_set_one_line(s_tb_search_ta, true);
    lv_textarea_set_max_length(s_tb_search_ta, 32);
    lv_textarea_set_placeholder_text(s_tb_search_ta, "Uygulama ara");
    lv_obj_set_style_bg_opa(s_tb_search_ta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tb_search_ta, 0, 0);
    lv_obj_set_style_text_color(s_tb_search_ta, lv_color_hex(DK_TEXT), 0);
    lv_obj_set_style_text_color(s_tb_search_ta, lv_color_hex(DK_MUTED),
                                LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_font(s_tb_search_ta, s_f_search ? s_f_search : adl_font(), 0);
    lv_obj_set_style_pad_all(s_tb_search_ta, 0, 0);
    /* Metin alanin dikey merkezinde olmali. Sabit yukseklik
     * verildiginde LVGL metni ustten hizaliyordu (ikonla ayni cizgide
     * durmuyordu). Yukseklik icerige birakilir, kutu dikeyde ortalanir. */
    lv_obj_set_height(s_tb_search_ta, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(s_tb_search_ta, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(s_tb_search_ta, TB_SRCH_W - dk_s(50));
    lv_obj_align(s_tb_search_ta, LV_ALIGN_LEFT_MID, dk_s(38), -1);
    lv_obj_add_event_cb(s_tb_search_ta, tb_search_cb, LV_EVENT_VALUE_CHANGED, 0);
    lv_obj_add_event_cb(s_tb_search_ta, tb_search_ready_cb, LV_EVENT_READY, 0);
    lv_obj_add_event_cb(s_tb_search_ta, tb_search_click_cb, LV_EVENT_CLICKED, 0);

    tb_sep(g_taskbar, &g_tb_sep1);
    tb_sep(g_taskbar, &g_tb_sep2);
    tb_sep(g_taskbar, &g_tb_sep3);

    /* ---- Uygulama yuvalari (dinamik) ---- */
    g_tb_apps_cont = lv_obj_create(g_taskbar);
    lv_obj_remove_style_all(g_tb_apps_cont);
    lv_obj_remove_flag(g_tb_apps_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(g_tb_apps_cont, 0, TB_IC);

    /* ---- Ag + pil: 72x44 kutu, 10 px yatay dolgu, 8 px ara ---- */
    g_tb_status = lv_obj_create(g_taskbar);
    lv_obj_remove_style_all(g_tb_status);
    lv_obj_remove_flag(g_tb_status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(g_tb_status, TB_STATUS_W, TB_IC);
    lv_obj_set_style_radius(g_tb_status, dk_s(8), 0);
    lv_obj_set_style_bg_color(g_tb_status, lv_color_hex(DK_TEXT), 0);
    lv_obj_set_style_bg_opa(g_tb_status, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(g_tb_status, DK_HOVER_OPA, LV_STATE_HOVERED);
    lv_obj_set_style_bg_opa(g_tb_status, DK_PRESS_OPA, LV_STATE_PRESSED);
    {
        lv_obj_t *wifi = make_icon(g_tb_status, SVG_DK_WIFI, dk_s(20));
        lv_obj_align(wifi, LV_ALIGN_LEFT_MID, dk_s(10), 0);
        lv_obj_add_flag(wifi, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(wifi, wifi_btn_cb, LV_EVENT_CLICKED, 0);

        lv_obj_t *bat = mk_battery(g_tb_status, dk_s(24), dk_s(20));
        if (bat) {
            lv_obj_align(bat, LV_ALIGN_LEFT_MID, dk_s(38), 0);
            lv_obj_add_flag(bat, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(bat, battery_btn_cb, LV_EVENT_CLICKED, 0);
        }
        /* Yuzde metni dock'ta gorunmez ama power_tick
         * ve t06 oz-testi bu nesneyi bekler; gizli tutulur. */
        g_power_label = lv_label_create(g_tb_status);
        lv_label_set_text(g_power_label, "");
        lv_obj_add_flag(g_power_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(g_power_label, 0, 0);
    }

    /* ---- Bildirim dugmesi 44x44 ---- */
    s_bell = lv_obj_create(g_taskbar);
    lv_obj_remove_style_all(s_bell);
    lv_obj_remove_flag(s_bell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_bell, TB_IC, TB_IC);
    lv_obj_set_style_radius(s_bell, dk_s(8), 0);
    lv_obj_set_style_bg_color(s_bell, lv_color_hex(DK_TEXT), 0);
    lv_obj_set_style_bg_opa(s_bell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_bell, DK_HOVER_OPA, LV_STATE_HOVERED);
    lv_obj_set_style_bg_opa(s_bell, DK_PRESS_OPA, LV_STATE_PRESSED);
    lv_obj_add_flag(s_bell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_bell, tb_bell_cb, LV_EVENT_CLICKED, 0);
    {
        lv_obj_t *bico = make_icon(s_bell, SVG_DK_BELL, dk_s(20));
        lv_obj_center(bico);
    }
    /* Okunmamis bildirim rozeti: 6x6 accent nokta                     */
    s_bell_badge = lv_obj_create(s_bell);
    lv_obj_remove_style_all(s_bell_badge);
    lv_obj_remove_flag(s_bell_badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_bell_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_bell_badge, dk_s(6), dk_s(6));
    lv_obj_set_style_radius(s_bell_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_bell_badge, lv_color_hex(DK_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_bell_badge, LV_OPA_COVER, 0);
    lv_obj_align(s_bell_badge, LV_ALIGN_TOP_RIGHT, -dk_s(10), dk_s(10));
    tb_refresh_bell_badge();

    /* ---- Hizli ayarlar dugmesi 44x44 ---- */
    s_qs_btn = lv_obj_create(g_taskbar);
    lv_obj_remove_style_all(s_qs_btn);
    lv_obj_remove_flag(s_qs_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_qs_btn, TB_IC, TB_IC);
    lv_obj_set_style_radius(s_qs_btn, dk_s(8), 0);
    lv_obj_set_style_bg_color(s_qs_btn, lv_color_hex(DK_TEXT), 0);
    lv_obj_set_style_bg_opa(s_qs_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_qs_btn, DK_HOVER_OPA, LV_STATE_HOVERED);
    lv_obj_set_style_bg_opa(s_qs_btn, DK_PRESS_OPA, LV_STATE_PRESSED);
    lv_obj_add_flag(s_qs_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_qs_btn, tb_qs_cb, LV_EVENT_CLICKED, 0);
    {
        lv_obj_t *qico = make_icon(s_qs_btn, SVG_DK_SLIDERS, dk_s(20));
        lv_obj_center(qico);
    }

    /* Klavye gezinme      */
    group_add(g_tb_logo);
    group_add(g_tb_search);
    group_add(g_tb_status);
    group_add(s_bell);
    group_add(s_qs_btn);

    /* ---- Saat dugmesi 89x44, icerik saga hizali ---- */
    g_tb_clock = lv_obj_create(g_taskbar);
    lv_obj_remove_style_all(g_tb_clock);
    lv_obj_remove_flag(g_tb_clock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(g_tb_clock, TB_CLK_W, TB_IC);
    lv_obj_set_style_radius(g_tb_clock, dk_s(8), 0);
    lv_obj_set_style_bg_color(g_tb_clock, lv_color_hex(DK_TEXT), 0);
    lv_obj_set_style_bg_opa(g_tb_clock, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(g_tb_clock, DK_HOVER_OPA, LV_STATE_HOVERED);
    lv_obj_set_style_bg_opa(g_tb_clock, DK_PRESS_OPA, LV_STATE_PRESSED);
    lv_obj_add_flag(g_tb_clock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_tb_clock, clock_btn_cb, LV_EVENT_CLICKED, 0);
    group_add(g_tb_clock);   /* Klavye gezinme */

    g_clock_label = lv_label_create(g_tb_clock);
    lv_label_set_text(g_clock_label, "--:--");
    lv_obj_set_style_text_color(g_clock_label, lv_color_hex(DK_TEXT), 0);
    lv_obj_set_style_text_font(g_clock_label, s_f_time ? s_f_time : adl_font(), 0);
    lv_obj_set_style_text_align(g_clock_label, LV_TEXT_ALIGN_RIGHT, 0);
    /* 720p gibi kucuk olceklerde tarih (54 px) eski 24 birimlik dolguya
     * sigmiyor ve alt satira kayip saatle cakisiyordu. Dolgu 18 birime
     * indirildi; metin saga hizali oldugundan 1x'te gorunum degismez. */
    lv_obj_set_width(g_clock_label, TB_CLK_W - dk_s(18));
    lv_label_set_long_mode(g_clock_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(g_clock_label, LV_ALIGN_TOP_RIGHT, -dk_s(12), dk_s(4));

    g_date_label = lv_label_create(g_tb_clock);
    lv_label_set_text(g_date_label, "00.00.0000");
    lv_obj_set_style_text_color(g_date_label, lv_color_hex(DK_MUTED), 0);
    lv_obj_set_style_text_font(g_date_label, s_f_date ? s_f_date : adl_font(), 0);
    lv_obj_set_style_text_align(g_date_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(g_date_label, TB_CLK_W - dk_s(18));
    lv_label_set_long_mode(g_date_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(g_date_label, LV_ALIGN_BOTTOM_RIGHT, -dk_s(12), -dk_s(5));

    tb_rebuild_icons();
    clock_tick();
}

void tb_poll(void) {
    if (g_tb_dirty) { g_tb_dirty = 0; tb_rebuild_icons(); }
    if (!g_taskbar || !g_asi->input || !g_asi->input->mouse_buttons) return;
    int b = g_asi->input->mouse_buttons();
    int right = (b >> 1) & 1;
    if (right && !s_tb_pr && !g_tb_menu) {
        int mx = g_asi->input->mouse_x();
        int my = g_asi->input->mouse_y();
        int hit = 0;
        if (!lv_obj_has_flag(g_taskbar, LV_OBJ_FLAG_HIDDEN)) {
            for (int i = 0; i < g_tb_n && !hit; i++) {
                lv_obj_t *btn = g_tb[i].btn;
                if (!btn || lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN)) continue;
                lv_area_t ar;
                lv_obj_get_coords(btn, &ar);
                if (mx < ar.x1 || mx > ar.x2 || my < ar.y1 || my > ar.y2) continue;
                tb_show_menu(ar.x1, g_tb[i].title);
                hit = 1;
            }
        }
        if (!hit && g_start_menu && !lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN)) {
            for (int i = 0; i < s_sm_n && !hit; i++) {
                lv_obj_t *t = s_sm_tiles[i];
                if (!t || lv_obj_has_flag(t, LV_OBJ_FLAG_HIDDEN)) continue;
                lv_area_t ar;
                lv_obj_get_coords(t, &ar);
                if (mx < ar.x1 || mx > ar.x2 || my < ar.y1 || my > ar.y2) continue;
                tb_show_app_menu(mx, my, s_sm_apps[i]);
                hit = 1;
            }
            if (!hit) {
                /* "Tumu" listesindeki satirlar da sag tik menusu alir     */
                int ai = sm_all_hit(mx, my);
                if (ai >= 0 && app_get(ai)) { tb_show_app_menu(mx, my, app_get(ai)); hit = 1; }
            }
        }
    }
    s_tb_pr = right;
}

