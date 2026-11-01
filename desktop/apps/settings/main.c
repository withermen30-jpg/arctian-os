#include "app.h"
#include "adl_wolf.h"
#include "lv_port.h"
#include <stdint.h>

#define S_NAV_W   190
#define S_POOL    64

static asi_t    *s_asi;
static lv_obj_t *g_win;
static lv_obj_t *g_nav;
static lv_obj_t *g_content;
static lv_obj_t *g_nav_btns[9];
static lv_obj_t *g_logo;
static int       g_page = 0;
static lv_obj_t *g_crypt_ta;
static lv_group_t *g_crypt_group;
static char      s_crypt_msg[96];

static lv_obj_t *g_fw_port;
static lv_obj_t *g_fw_name;
static lv_obj_t *g_fw_dir;
static lv_obj_t *g_fw_proto;
static lv_obj_t *g_fw_action;
static char      s_fw_msg[96];

static char s_pool[S_POOL][40];
static int  s_pool_n = 0;

/* Guncelleme karti durumu       */
static lv_obj_t *g_upd_msg;
static lv_obj_t *g_upd_bar;
static lv_timer_t *g_upd_timer;

static const char *const g_nav_labels[9] = {
    "Kisisellestirme", "Masaustu", "Ekran", "Ag", "Uygulamalar", "Sistem",
    "Erisilebilirlik", "Guncelleme", "Hakkinda"
};

static int slen(const char *s) { int n = 0; if (s) while (s[n]) n++; return n; }

static void scpy(char *d, int cap, const char *s) {
    int i = 0;
    if (cap <= 0) return;
    if (!s) { d[0] = 0; return; }
    for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}

static int seq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static void snum(char *b, int cap, unsigned int v) {
    char t[12];
    int n = 0, k = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n && k < cap - 1) b[k++] = t[--n];
    b[k] = 0;
}

static void scat(char *d, int cap, const char *s) {
    int i = slen(d), j = 0;
    for (; s && s[j] && i < cap - 1; i++, j++) d[i] = s[j];
    d[i] = 0;
}

static const char *spool(const char *s) {
    if (s_pool_n >= S_POOL) return "";
    char *d = s_pool[s_pool_n++];
    scpy(d, 40, s);
    return d;
}

static void sip(char *out, int cap, const uint8_t ip[4]) {
    out[0] = 0;
    for (int i = 0; i < 4; i++) {
        char n[8];
        snum(n, sizeof(n), ip[i]);
        scat(out, cap, n);
        if (i < 3) scat(out, cap, ".");
    }
}

static lv_obj_t *scard(lv_obj_t *parent) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_pad_all(c, 14, 0);
    lv_obj_set_style_pad_row(c, 8, 0);
    lv_obj_set_style_pad_column(c, 8, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static void stitle(lv_obj_t *parent, const char *txt) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_text_font(l, adl_font(), 0);
}

static void sdesc(lv_obj_t *parent, const char *txt) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
}

static lv_obj_t *srow(lv_obj_t *parent, int h) {
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_width(r, LV_PCT(100));
    lv_obj_set_height(r, h);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_column(r, 8, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return r;
}

static lv_obj_t *srow_start(lv_obj_t *parent, int h) {
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_width(r, LV_PCT(100));
    lv_obj_set_height(r, h);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_column(r, 8, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return r;
}

static lv_obj_t *sbutton(lv_obj_t *parent, const char *txt, uint32_t bg, uint32_t fg, int w, int h) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
    return b;
}

static void skv(lv_obj_t *parent, const char *k, const char *v) {
    lv_obj_t *r = srow(parent, 30);
    lv_obj_t *kl = lv_label_create(r);
    lv_label_set_text(kl, k);
    lv_obj_set_style_text_color(kl, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_t *vl = lv_label_create(r);
    lv_label_set_text(vl, v);
    lv_obj_set_style_text_color(vl, lv_color_hex(ADL_TEXT), 0);
}

static void s_content_clear(void) {
    if (g_content) lv_obj_clean(g_content);
}

static void s_nav_active(void) {
    for (int i = 0; i < 9; i++) {   /* 9 sekme var; eskiden i<7 son ikisi vurgulamiyordu */
        if (!g_nav_btns[i]) continue;
        uint32_t bg = (i == g_page) ? ADL_ACCENT : ADL_SURFACE_2;
        lv_obj_set_style_bg_color(g_nav_btns[i], lv_color_hex(bg), 0);
    }
}

static void s_page(int p);

static void s_nav_cb(lv_event_t *e) {
    int p = (int)(intptr_t)lv_event_get_user_data(e);
    s_page(p);
}

static void s_toggle_cb(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    const char *key = (const char *)lv_event_get_user_data(e);
    int on = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
    if (arctian_settings && arctian_settings->set_int) arctian_settings->set_int(key, on);
}

static void s_add_toggle(lv_obj_t *card, const char *label, const char *key, int def) {
    lv_obj_t *r = srow(card, 40);
    lv_obj_t *l = lv_label_create(r);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
    lv_obj_t *sw = lv_switch_create(r);
    if (arctian_settings && arctian_settings->get_int &&
        arctian_settings->get_int(key, def)) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, s_toggle_cb, LV_EVENT_VALUE_CHANGED, (void *)key);
}

/* 0.6: secenek dongusu (buton). Etiket "Kapali / 5 dk / 10 dk / 30 dk". */
static void s_choice_text(char *out, int cap, const char *unit, int v) {
    int n = 0;
    if (v <= 0) {
        const char *s = "Kapali";
        while (s[n] && n < cap - 1) { out[n] = s[n]; n++; }
        out[n] = 0;
        return;
    }
    if (v >= 10) out[n++] = (char)('0' + (v / 10) % 10);
    out[n++] = (char)('0' + v % 10);
    out[n++] = ' ';
    for (int i = 0; unit[i] && n < cap - 1; i++) out[n++] = unit[i];
    out[n] = 0;
}

static void s_idle_cycle_cb(lv_event_t *e) {
    lv_obj_t *b = lv_event_get_target(e);
    int v = (arctian_settings && arctian_settings->get_int)
            ? arctian_settings->get_int("idle_lock_min", 0) : 0;
    static const int opts[4] = {0, 5, 10, 30};
    int idx = 0;
    for (int i = 0; i < 4; i++) if (opts[i] == v) idx = i;
    idx = (idx + 1) % 4;
    if (arctian_settings && arctian_settings->set_int)
        arctian_settings->set_int("idle_lock_min", opts[idx]);
    char t[24];
    s_choice_text(t, sizeof(t), "dk", opts[idx]);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    if (l) lv_label_set_text(l, t);
}

static void s_ws_cycle_cb(lv_event_t *e) {
    lv_obj_t *b = lv_event_get_target(e);
    int v = (arctian_settings && arctian_settings->get_int)
            ? arctian_settings->get_int("ws_count", 3) : 3;
    v++;
    if (v > 4) v = 1;
    if (arctian_settings && arctian_settings->set_int)
        arctian_settings->set_int("ws_count", v);
    char t[24];
    s_choice_text(t, sizeof(t), "alan", v);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    if (l) lv_label_set_text(l, t);
}

/* 0.6: klavye duzeni secimi (kurulum sihirbazindaki ayarin aynisi).
 * Aninda uygulanir; "kbdlayout" anahtari settings.c uzerinden "kbd"
 * dizesine cevrilir ve kalici olur. */
static void s_kbd_cycle_cb(lv_event_t *e) {
    lv_obj_t *b = lv_event_get_target(e);
    int idx = (arctian_settings && arctian_settings->get_int)
              ? arctian_settings->get_int("kbdlayout", 0) : 0;
    idx = (idx + 1) % 3;
    static const char *names[3] = { "Turkce Q", "Turkce F", "Ingilizce Q" };
    if (arctian_settings && arctian_settings->set_int)
        arctian_settings->set_int("kbdlayout", idx);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    if (l) lv_label_set_text(l, names[idx]);
}

static void s_add_choice(lv_obj_t *card, const char *label, const char *cur,
                         lv_event_cb_t cb) {
    lv_obj_t *r = srow(card, 40);
    lv_obj_t *l = lv_label_create(r);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
    lv_obj_t *b = lv_button_create(r);
    lv_obj_set_size(b, 96, 32);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(ADL_BORDER), 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, 0);
    lv_obj_t *bl = lv_label_create(b);
    lv_label_set_text(bl, cur);
    lv_obj_set_style_text_color(bl, lv_color_hex(ADL_TEXT), 0);
    lv_obj_center(bl);
}

static void s_accent_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (arctian_settings && arctian_settings->accent_set) arctian_settings->accent_set(i);
    s_page(g_page);
}

static void s_wall_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (!s_asi || !s_asi->fs || !arctian_store || !arctian_store->set_wallpaper) return;
    const char *nm = s_asi->fs->name(idx);
    if (nm) arctian_store->set_wallpaper(nm);
    s_page(0);
}

static void s_wall_reset_cb(lv_event_t *e) {
    (void)e;
    if (arctian_store && arctian_store->set_wallpaper) arctian_store->set_wallpaper("");
    s_page(0);
}

static void s_res_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (arctian_settings && arctian_settings->resolution_set)
        arctian_settings->resolution_set(i);
}

static void s_open_cb(lv_event_t *e) {
    const char *q = (const char *)lv_event_get_user_data(e);
    arctian_shell_launch(q);
}

static void s_inst_open_cb(lv_event_t *e) {
    const char *id = (const char *)lv_event_get_user_data(e);
    if (arctian_store && arctian_store->launch) arctian_store->launch(id);
}

static void s_inst_del_cb(lv_event_t *e) {
    const char *id = (const char *)lv_event_get_user_data(e);
    if (arctian_store && arctian_store->uninstall) arctian_store->uninstall(id);
    s_page(g_page);
}

static void s_dhcp_cb(lv_event_t *e) {
    (void)e;
    if (s_asi && s_asi->net && s_asi->net->autoconfig) s_asi->net->autoconfig();
    s_page(3);
}

static void s_netpanel_cb(lv_event_t *e) { (void)e; arctian_shell_network_panel(); }

/* ---- Guncelleme ---------- */
static void s_upd_refresh(void) {
    const update_status_t *st = update_status();
    if (g_upd_msg) lv_label_set_text(g_upd_msg, st->message);
    if (g_upd_bar) {
        if (st->state == UPDATE_DOWNLOADING) {
            lv_obj_remove_flag(g_upd_bar, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(g_upd_bar, st->progress, LV_ANIM_OFF);
        } else {
            lv_obj_add_flag(g_upd_bar, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void s_upd_timer_cb(lv_timer_t *t) {
    (void)t;
    s_upd_refresh();
    const update_status_t *st = update_status();
    if (st->state != UPDATE_CHECKING && st->state != UPDATE_DOWNLOADING) {
        if (g_upd_timer) { lv_timer_delete(g_upd_timer); g_upd_timer = 0; }
    }
}

static void s_upd_work(void *u) {
    if ((intptr_t)u) update_apply();
    else update_check();
}

static void s_upd_done(int status, void *user) { (void)status; (void)user; }

static void s_upd_start(int apply) {
    if (!s_asi || !s_asi->net || !s_asi->net->submit) {
        if (apply) update_apply(); else update_check();
        s_upd_refresh();
        return;
    }
    if (s_asi->net->busy && s_asi->net->busy()) {
        if (g_upd_msg) lv_label_set_text(g_upd_msg, "Ag mesgul, sonra deneyin");
        return;
    }
    if (apply && !update_has_update()) {
        if (g_upd_msg) lv_label_set_text(g_upd_msg, "Once 'Kontrol Et'");
        return;
    }
    if (s_asi->net->submit(s_upd_work, s_upd_done, (void *)(intptr_t)apply) != 0) {
        if (g_upd_msg) lv_label_set_text(g_upd_msg, "Islem baslatilamadi");
        return;
    }
    if (g_upd_msg) lv_label_set_text(g_upd_msg, apply ? "Guncelleme basladi..." : "Kontrol ediliyor...");
    if (g_upd_timer) { lv_timer_delete(g_upd_timer); g_upd_timer = 0; }
    g_upd_timer = lv_timer_create(s_upd_timer_cb, 250, 0);
}

static void s_upd_check_cb(lv_event_t *e) { (void)e; s_upd_start(0); }
static void s_upd_apply_cb(lv_event_t *e) { (void)e; s_upd_start(1); }

/* ---- Guvenlik duvari ---- */
static void sfw_read(lv_obj_t *ta, char *out, int cap) {
    out[0] = 0;
    if (!ta) return;
    const char *t = lv_textarea_get_text(ta);
    if (!t) return;
    int i = 0;
    for (; t[i] && i < cap - 1; i++) out[i] = t[i];
    out[i] = 0;
}

static int sfw_num(const char *s, int *out) {
    if (!s || !*s) { *out = 0; return 0; }
    int v = 0, any = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; any = 1; }
    *out = v;
    return any;
}

static void s_fw_toggle_cb(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    int on = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
    if (s_asi && s_asi->firewall && s_asi->firewall->set_enabled)
        s_asi->firewall->set_enabled(on);
    scpy(s_fw_msg, sizeof(s_fw_msg), on ? "Guvenlik duvari acik." : "Guvenlik duvari kapali.");
    s_page(3);
}

static void s_fw_add_cb(lv_event_t *e) {
    (void)e;
    s_fw_msg[0] = 0;
    asi_firewall_t *fw = s_asi ? s_asi->firewall : 0;
    if (!fw || !fw->add) { scpy(s_fw_msg, sizeof(s_fw_msg), "Guvenlik duvari servisi yok"); s_page(3); return; }

    asi_fw_rule_t r;
    for (unsigned i = 0; i < sizeof(r); i++) ((uint8_t *)&r)[i] = 0;
    sfw_read(g_fw_name, r.name, ASI_FW_NAME_MAX);
    if (!r.name[0]) scpy(r.name, ASI_FW_NAME_MAX, "kural");
    r.enabled = 1;
    r.dir = (uint8_t)(lv_dropdown_get_selected(g_fw_dir) + 1);
    int pi = lv_dropdown_get_selected(g_fw_proto);
    r.proto = (pi == 0) ? ASI_FW_PROTO_ANY : (pi == 1 ? ASI_FW_PROTO_TCP : ASI_FW_PROTO_UDP);
    r.action = (lv_dropdown_get_selected(g_fw_action) == 0) ? ASI_FW_DENY : ASI_FW_ALLOW;

    char pt[24];
    sfw_read(g_fw_port, pt, sizeof(pt));
    int lo = 0, hi = 0, dash = -1;
    for (int i = 0; pt[i]; i++) if (pt[i] == '-') { dash = i; break; }
    if (dash >= 0) {
        pt[dash] = 0;
        sfw_num(pt, &lo);
        sfw_num(pt + dash + 1, &hi);
    } else {
        sfw_num(pt, &lo);
        hi = lo;
    }
    if (lo < 0) lo = 0; if (lo > 65535) lo = 65535;
    if (hi < 0) hi = 0; if (hi > 65535) hi = 65535;
    r.port_lo = (uint16_t)lo;
    r.port_hi = (uint16_t)hi;

    int rc = fw->add(&r);
    scpy(s_fw_msg, sizeof(s_fw_msg), rc < 0 ? "Kural eklenemedi (liste dolu?)" : "Kural eklendi.");
    s_page(3);
}

static void s_fw_del_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_asi && s_asi->firewall && s_asi->firewall->remove) s_asi->firewall->remove(idx);
    scpy(s_fw_msg, sizeof(s_fw_msg), "Kural kaldirildi.");
    s_page(3);
}

static void s_page_firewall(lv_obj_t *c) {
    asi_firewall_t *fw = s_asi ? s_asi->firewall : 0;
    if (!fw) { sdesc(c, "Guvenlik duvari servisi yok."); return; }

    lv_obj_t *tr = srow(c, 40);
    lv_obj_t *tl = lv_label_create(tr);
    lv_label_set_text(tl, "Etkin");
    lv_obj_set_style_text_color(tl, lv_color_hex(ADL_TEXT), 0);
    lv_obj_t *sw = lv_switch_create(tr);
    if (fw->enabled && fw->enabled()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, s_fw_toggle_cb, LV_EVENT_VALUE_CHANGED, 0);

    int n = fw->count ? fw->count() : 0;
    if (n <= 0) {
        sdesc(c, "Henuz kural yok. Kural eslesmezse trafik izinlidir.");
    } else {
        for (int i = 0; i < n; i++) {
            asi_fw_rule_t r;
            if (!fw->rule || fw->rule(i, &r) != 0) continue;
            lv_obj_t *row = srow_start(c, 34);
            char txt[96];
            scpy(txt, sizeof(txt), r.name[0] ? r.name : "kural");
            scat(txt, sizeof(txt), "  ");
            scat(txt, sizeof(txt), r.dir == ASI_FW_DIR_IN ? "gelen" :
                                     r.dir == ASI_FW_DIR_OUT ? "giden" : "gelen/giden");
            scat(txt, sizeof(txt), " ");
            scat(txt, sizeof(txt), r.proto == ASI_FW_PROTO_TCP ? "TCP" :
                                     r.proto == ASI_FW_PROTO_UDP ? "UDP" : "hepsi");
            scat(txt, sizeof(txt), " ");
            if (r.port_lo || r.port_hi) {
                char a[8];
                snum(a, sizeof(a), r.port_lo);
                scat(txt, sizeof(txt), ":");
                scat(txt, sizeof(txt), a);
                if (r.port_hi && r.port_hi != r.port_lo) {
                    char b[8];
                    snum(b, sizeof(b), r.port_hi);
                    scat(txt, sizeof(txt), "-");
                    scat(txt, sizeof(txt), b);
                }
            } else {
                scat(txt, sizeof(txt), ":*");
            }
            scat(txt, sizeof(txt), r.action == ASI_FW_ALLOW ? "  izin" : "  engel");
            lv_obj_t *l = lv_label_create(row);
            lv_label_set_text(l, txt);
            lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
            lv_obj_t *bd = sbutton(row, "Kaldir", ADL_SURFACE_2, ADL_DANGER, 76, 30);
            lv_obj_add_event_cb(bd, s_fw_del_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    }

    stitle(c, "Yeni Kural");
    lv_obj_t *f1 = srow_start(c, 44);
    g_fw_name = lv_textarea_create(f1);
    lv_obj_set_size(g_fw_name, 150, 38);
    lv_textarea_set_one_line(g_fw_name, true);
    lv_textarea_set_max_length(g_fw_name, ASI_FW_NAME_MAX - 1);
    lv_textarea_set_placeholder_text(g_fw_name, "Ad");

    g_fw_dir = lv_dropdown_create(f1);
    lv_dropdown_set_options(g_fw_dir, "Gelen\nGiden\nHer ikisi");
    lv_obj_set_width(g_fw_dir, 130);

    g_fw_proto = lv_dropdown_create(f1);
    lv_dropdown_set_options(g_fw_proto, "Hepsi\nTCP\nUDP");
    lv_obj_set_width(g_fw_proto, 110);

    g_fw_action = lv_dropdown_create(f1);
    lv_dropdown_set_options(g_fw_action, "Engelle\nIzin ver");
    lv_obj_set_width(g_fw_action, 120);

    g_fw_port = lv_textarea_create(f1);
    lv_obj_set_size(g_fw_port, 130, 38);
    lv_textarea_set_one_line(g_fw_port, true);
    lv_textarea_set_max_length(g_fw_port, 12);
    lv_textarea_set_placeholder_text(g_fw_port, "Port (80 / 80-90)");

    lv_obj_t *addb = sbutton(c, "Kural Ekle", ADL_ACCENT, 0xFFFFFF, 140, 36);
    lv_obj_add_event_cb(addb, s_fw_add_cb, LV_EVENT_CLICKED, 0);
    if (s_fw_msg[0]) sdesc(c, s_fw_msg);
}

static void s_crypt_focus(void) {
    if (!g_crypt_ta) return;
    if (!g_crypt_group) g_crypt_group = lv_group_create();
    lv_group_remove_all_objs(g_crypt_group);
    lv_group_add_obj(g_crypt_group, g_crypt_ta);
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, g_crypt_group);
    lv_group_focus_obj(g_crypt_ta);
}

static void read_crypt_pw(char *out, int cap) {
    out[0] = 0;
    if (!g_crypt_ta) return;
    const char *t = lv_textarea_get_text(g_crypt_ta);
    if (!t) return;
    int i = 0;
    for (; t[i] && i < cap - 1; i++) out[i] = t[i];
    out[i] = 0;
}

static void s_crypt_setup_cb(lv_event_t *e) {
    (void)e;
    char p[40];
    read_crypt_pw(p, sizeof(p));
    s_crypt_msg[0] = 0;
    if (!s_asi || !s_asi->crypt || !s_asi->crypt->setup) { scpy(s_crypt_msg, sizeof(s_crypt_msg), "Sifreleme servisi yok"); s_page(5); return; }
    int r = s_asi->crypt->setup(p);
    if (r == 0) scpy(s_crypt_msg, sizeof(s_crypt_msg), "Disk sifrelendi. Yeniden baslatinca parola sorulur.");
    else if (r == -2) scpy(s_crypt_msg, sizeof(s_crypt_msg), "Parola en az 4 karakter olmali.");
    else scpy(s_crypt_msg, sizeof(s_crypt_msg), "Sifreleme baslatilamadi.");
    s_page(5);
}

static void s_crypt_disable_cb(lv_event_t *e) {
    (void)e;
    char p[40];
    read_crypt_pw(p, sizeof(p));
    s_crypt_msg[0] = 0;
    if (!s_asi || !s_asi->crypt || !s_asi->crypt->disable) { scpy(s_crypt_msg, sizeof(s_crypt_msg), "Sifreleme servisi yok"); s_page(5); return; }
    int r = s_asi->crypt->disable(p);
    if (r == 0) scpy(s_crypt_msg, sizeof(s_crypt_msg), "Sifreleme kaldirildi.");
    else if (r == -4) scpy(s_crypt_msg, sizeof(s_crypt_msg), "Parola hatali.");
    else scpy(s_crypt_msg, sizeof(s_crypt_msg), "Islem basarisiz.");
    s_page(5);
}

static void s_reboot_cb(lv_event_t *e) { (void)e; if (s_asi && s_asi->system && s_asi->system->reboot) s_asi->system->reboot(); }
static void s_shutdown_cb(lv_event_t *e) { (void)e; if (s_asi && s_asi->system && s_asi->system->shutdown) s_asi->system->shutdown(); }
static void s_power_cb(lv_event_t *e) { (void)e; arctian_shell_power_dialog(); }

static void s_page_personalize(void) {
    lv_obj_t *c1 = scard(g_content);
    stitle(c1, "Vurgu Rengi");
    sdesc(c1, "Pencereler, dugmeler ve baslat menusu icin ana renk. Yeni acilan pencerelere uygulanir.");
    lv_obj_t *wrap = lv_obj_create(c1);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_width(wrap, LV_PCT(100));
    lv_obj_set_height(wrap, LV_SIZE_CONTENT);
    lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(wrap, 8, 0);
    lv_obj_set_style_pad_column(wrap, 8, 0);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW_WRAP);

    int n = (arctian_settings && arctian_settings->accent_count) ? arctian_settings->accent_count() : 0;
    int cur = (arctian_settings && arctian_settings->accent_current) ? arctian_settings->accent_current() : 0;
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = lv_button_create(wrap);
        lv_obj_set_size(b, 108, 50);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(arctian_settings->accent_color(i)), 0);
        lv_obj_set_style_border_width(b, (i == cur) ? 3 : 0, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0xFFFFFF), 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, arctian_settings->accent_name(i));
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, s_accent_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    lv_obj_t *c2 = scard(g_content);
    stitle(c2, "Arka Plan");
    sdesc(c2, "AFS'te kurulu .wp gorselleri. Yeni arka planlari DuckStore > Arka Planlar'dan indirebilirsiniz.");
    const char *active = (arctian_store && arctian_store->wallpaper_active && arctian_store->wallpaper_active())
                         ? arctian_store->wallpaper_name() : "";
    int found = 0;
    if (s_asi && s_asi->fs && s_asi->fs->count) {
        int fc = s_asi->fs->count();
        for (int i = 0; i < fc; i++) {
            const char *nm = s_asi->fs->name(i);
            int L = slen(nm);
            if (L <= 3 || nm[L - 3] != '.' || nm[L - 2] != 'w' || nm[L - 1] != 'p') continue;
            found = 1;
            lv_obj_t *r = srow_start(c2, 36);
            lv_obj_t *l = lv_label_create(r);
            char txt[48];
            scpy(txt, sizeof(txt), nm);
            if (active[0] && seq(active, nm)) scat(txt, sizeof(txt), "  (aktif)");
            lv_label_set_text(l, txt);
            lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
            lv_obj_t *b = sbutton(r, "Uygula", ADL_ACCENT, 0xFFFFFF, 90, 32);
            lv_obj_add_event_cb(b, s_wall_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    }
    if (!found) sdesc(c2, "Henuz .wp gorseli yok.");
    lv_obj_t *rb = sbutton(c2, "Varsayilan Arka Plan", ADL_SURFACE_2, ADL_TEXT, 190, 36);
    lv_obj_add_event_cb(rb, s_wall_reset_cb, LV_EVENT_CLICKED, 0);
}

static void s_page_desktop(void) {
    lv_obj_t *c = scard(g_content);
    stitle(c, "Masaustu");
    sdesc(c, "Masaustu ogeleri ve gorev cubugu davranisi.");
    s_add_toggle(c, "Masaustu kisayollarini goster", "dicons", 1);
    s_add_toggle(c, "Gorev cubugunda saati goster", "clockshow", 1);
    s_add_toggle(c, "24 saat formati", "clock24", 1);
    s_add_toggle(c, "Bostayken kilitle (5 dk)", "idle_lock", 0);
    s_add_toggle(c, "Acik tema", "light", 0);

    /* 0.6: secenekler */
    {
        int mi = (arctian_settings && arctian_settings->get_int)
                 ? arctian_settings->get_int("idle_lock_min", 0) : 0;
        char t[24];
        s_choice_text(t, sizeof(t), "dk", mi);
        s_add_choice(c, "Bosta kilit suresi", t, s_idle_cycle_cb);

        int ws = (arctian_settings && arctian_settings->get_int)
                 ? arctian_settings->get_int("ws_count", 3) : 3;
        s_choice_text(t, sizeof(t), "alan", ws);
        s_add_choice(c, "Calisma alani sayisi", t, s_ws_cycle_cb);

        const char *kname = "Turkce Q";
        if (arctian_settings && arctian_settings->get_int) {
            int ki = arctian_settings->get_int("kbdlayout", 0);
            kname = (ki == 2) ? "Ingilizce Q" : (ki == 1 ? "Turkce F" : "Turkce Q");
        }
        s_add_choice(c, "Klavye duzeni", kname, s_kbd_cycle_cb);
    }
}

static void s_page_screen(void) {
    lv_obj_t *c = scard(g_content);
    stitle(c, "Ekran");
    sdesc(c, "Goruntunun cozunurlugu. Degisiklik aninda uygulanir: acik "
             "pencereler kapanir ve masaustu yeni boyuta gore yeniden kurulur.");

    int n   = (arctian_settings && arctian_settings->resolution_count)
              ? arctian_settings->resolution_count() : 0;
    int cur = (arctian_settings && arctian_settings->resolution_current)
              ? arctian_settings->resolution_current() : -1;
    if (n <= 0) { sdesc(c, "Cozunurluk servisi bulunamadi."); return; }

    for (int i = 0; i < n; i++) {
        lv_obj_t *r = srow(c, 46);
        lv_obj_t *l = lv_label_create(r);
        lv_label_set_text(l, arctian_settings->resolution_name(i));
        lv_obj_set_style_text_color(l, lv_color_hex(i == cur ? ADL_ACCENT : ADL_TEXT), 0);
        if (i == cur) {
            lv_obj_t *b = sbutton(r, "Aktif", ADL_SURFACE_2, ADL_ACCENT, 96, 34);
            lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_t *b = sbutton(r, "Uygula", ADL_ACCENT, 0xFFFFFF, 96, 34);
            lv_obj_add_event_cb(b, s_res_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    }

    lv_obj_t *c2 = scard(g_content);
    stitle(c2, "Bilgi");
    sdesc(c2, "Ayni 16:9 orani korunur. Varsayilan 1920 x 1080 (Full HD), "
              "daha yuksek cozunurluk icin 2560 x 1440 (2K) secilebilir.");
    sdesc(c2, "Bazi sistemlerde (ozellikle gercek donanimda) tum modlar "
              "desteklenmeyebilir; uygulanamayan secim yok sayilir ve mevcut "
              "cozunurluk korunur.");
    if (cur < 0)
        sdesc(c2, "Aktif cozunurluk desteklenen liste ile eslesmiyor.");
}

static void s_page_network(void) {
    char v[48];

    lv_obj_t *c = scard(g_content);
    stitle(c, "Baglanti Durumu");
    asi_net_t *n = (s_asi ? s_asi->net : 0);
    int lt = (n && n->link_type) ? n->link_type() : 0;
    skv(c, "Baglanti", lt == ASI_LINK_ETHERNET ? "Ethernet" : lt == ASI_LINK_WIFI ? "Wi-Fi" : "yok");
    uint8_t ip[4];
    if (n && n->ip) { n->ip(ip); sip(v, sizeof(v), ip); skv(c, "IP Adresi", v); }
    if (n && n->netmask) { n->netmask(ip); sip(v, sizeof(v), ip); skv(c, "Alt Ag Maskesi", v); }
    if (n && n->gateway) { n->gateway(ip); sip(v, sizeof(v), ip); skv(c, "Gecit", v); }
    if (n && n->dns) { n->dns(ip); sip(v, sizeof(v), ip); skv(c, "DNS", v); }
    if (n && n->wifi_state) {
        int ws = n->wifi_state();
        const char *st = ws == ASI_WIFI_CONNECTED ? "bagli" :
                         ws == ASI_WIFI_CONNECTING ? "baglaniyor" :
                         ws == ASI_WIFI_FAILED ? "basarisiz" : "kapali";
        skv(c, "Wi-Fi", st);
    }

    lv_obj_t *c2 = scard(g_content);
    stitle(c2, "Islemler");
    sdesc(c2, "IP adresini DHCP ile yenile veya kablosuz aglari yonet.");
    lv_obj_t *r = srow_start(c2, 44);
    lv_obj_t *b1 = sbutton(r, "IP Yenile (DHCP)", ADL_ACCENT, 0xFFFFFF, 180, 38);
    lv_obj_add_event_cb(b1, s_dhcp_cb, LV_EVENT_CLICKED, 0);
    lv_obj_t *b2 = sbutton(r, "Ag Ayarlari", ADL_SURFACE_2, ADL_TEXT, 150, 38);
    lv_obj_add_event_cb(b2, s_netpanel_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *c3 = scard(g_content);
    stitle(c3, "Guvenlik Duvari");
    sdesc(c3, "Istenmeyen portlari kapatmak icin kural tanimlayin. Eslesmeyen trafik izinlidir.");
    s_page_firewall(c3);
}

static void s_page_apps(void) {
    lv_obj_t *c = scard(g_content);
    stitle(c, "Uygulamalar");
    sdesc(c, "Sistemle gelen uygulamalar. Acmak icin dokun.");
    int n = arctian_shell_app_count();
    for (int i = 0; i < n; i++) {
        char title[48], name[32], cat[32];
        if (arctian_shell_app_info(i, title, sizeof(title), name, sizeof(name), cat, sizeof(cat)) != 0) continue;
        lv_obj_t *r = srow_start(c, 38);
        lv_obj_t *l = lv_label_create(r);
        char txt[80];
        scpy(txt, sizeof(txt), title);
        scat(txt, sizeof(txt), "   [");
        scat(txt, sizeof(txt), cat);
        scat(txt, sizeof(txt), "]");
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
        lv_obj_t *b = sbutton(r, "Ac", ADL_ACCENT, 0xFFFFFF, 70, 32);
        lv_obj_add_event_cb(b, s_open_cb, LV_EVENT_CLICKED, (void *)spool(name));
    }

    lv_obj_t *c2 = scard(g_content);
    stitle(c2, "Kurulu Paketler");
    int ic = (arctian_store && arctian_store->count_installed) ? arctian_store->count_installed() : 0;
    if (ic <= 0) {
        sdesc(c2, "Henuz ek paket kurulmadi. DuckStore'dan uygulama kurabilirsiniz.");
    } else {
        for (int i = 0; i < ic; i++) {
            const char *id = arctian_store->installed_id(i);
            const char *title = arctian_store->installed_title(i);
            const char *ver = arctian_store->installed_version_at(i);
            lv_obj_t *r = srow_start(c2, 38);
            lv_obj_t *l = lv_label_create(r);
            char txt[80];
            scpy(txt, sizeof(txt), title && title[0] ? title : id);
            scat(txt, sizeof(txt), "  ");
            scat(txt, sizeof(txt), ver ? ver : "");
            lv_label_set_text(l, txt);
            lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
            lv_obj_t *bo = sbutton(r, "Ac", ADL_ACCENT, 0xFFFFFF, 60, 32);
            lv_obj_add_event_cb(bo, s_inst_open_cb, LV_EVENT_CLICKED, (void *)spool(id));
            lv_obj_t *bd = sbutton(r, "Kaldir", ADL_SURFACE_2, ADL_DANGER, 76, 32);
            lv_obj_add_event_cb(bd, s_inst_del_cb, LV_EVENT_CLICKED, (void *)spool(id));
        }
    }
}

static void s_page_system(void) {
    char v[48];

    lv_obj_t *c = scard(g_content);
    stitle(c, "Sistem Bilgisi");
    if (s_asi && s_asi->gfx) {
        char res[24];
        snum(res, sizeof(res), (unsigned int)s_asi->gfx->width());
        scat(res, sizeof(res), " x ");
        char h[12];
        snum(h, sizeof(h), (unsigned int)s_asi->gfx->height());
        scat(res, sizeof(res), h);
        skv(c, "Ekran", res);
    }
    snum(v, sizeof(v), (unsigned int)arctian_shell_app_count());
    skv(c, "Uygulama Sayisi", v);
    snum(v, sizeof(v), (unsigned int)arctian_shell_window_count());
    skv(c, "Acik Pencere", v);
    snum(v, sizeof(v), (unsigned int)((arctian_store && arctian_store->count_installed) ? arctian_store->count_installed() : 0));
    skv(c, "Kurulu Paket", v);
    if (s_asi && s_asi->time && s_asi->time->now) {
        asi_datetime_t dt;
        if (s_asi->time->now(&dt)) {
            char d[32];
            snum(d, sizeof(d), (unsigned int)dt.year);
            scat(d, sizeof(d), "-");
            char t[8];
            snum(t, sizeof(t), (unsigned int)dt.month); if (dt.month < 10) { t[1] = t[0]; t[0] = '0'; t[2] = 0; }
            scat(d, sizeof(d), t);
            scat(d, sizeof(d), "-");
            snum(t, sizeof(t), (unsigned int)dt.day); if (dt.day < 10) { t[1] = t[0]; t[0] = '0'; t[2] = 0; }
            scat(d, sizeof(d), t);
            skv(c, "Tarih", d);
        }
    }

    lv_obj_t *c2 = scard(g_content);
    stitle(c2, "Guc");
    sdesc(c2, "Sistemi yeniden baslat veya kapat. Uyku icin Guc Menusu.");
    if (s_asi && s_asi->power) {
        asi_power_t *pw = s_asi->power;
        int present = pw->present ? pw->present() : 0;
        int pct = pw->percent ? pw->percent() : -1;
        int src = pw->source ? pw->source() : ASI_POWER_AC;
        int chg = pw->charging ? pw->charging() : 0;
        char pb[16];
        if (pct >= 0) { snum(pb, sizeof(pb), (unsigned int)pct); scat(pb, sizeof(pb), "%"); }
        else scpy(pb, sizeof(pb), "--");
        skv(c2, "Guc Kaynagi", src == ASI_POWER_BATTERY ? "Pil" : "AC");
        skv(c2, "Pil", present ? pb : "Yok (AC)");
        if (present) skv(c2, "Sarj", chg ? "Sarj oluyor" : "Desarj");
        skv(c2, "ACPI", (pw->acpi && pw->acpi()) ? "Var" : "Yok");
    }
    lv_obj_t *r = srow_start(c2, 46);
    lv_obj_t *b1 = sbutton(r, "Yeniden Baslat", ADL_ACCENT, 0xFFFFFF, 170, 40);
    lv_obj_add_event_cb(b1, s_reboot_cb, LV_EVENT_CLICKED, 0);
    lv_obj_t *b2 = sbutton(r, "Kapat", ADL_DANGER, 0xFFFFFF, 120, 40);
    lv_obj_add_event_cb(b2, s_shutdown_cb, LV_EVENT_CLICKED, 0);
    lv_obj_t *b3 = sbutton(r, "Guc Menusu", ADL_SURFACE_2, ADL_TEXT, 140, 40);
    lv_obj_add_event_cb(b3, s_power_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *cupd = scard(g_content);
    stitle(cupd, "Guncelleme");
    sdesc(cupd, "Sistem ve modulleri DuckStore sunucusundan guncelleyin. Indirilen paketler CRC32 ile dogrulanir.");
    skv(cupd, "Mevcut Surum", update_applied_version());
    skv(cupd, "Derleme", update_current_version());
    skv(cupd, "Sunucu", update_base_url());
    g_upd_msg = lv_label_create(cupd);
    lv_label_set_text(g_upd_msg, update_status()->message);
    lv_obj_set_style_text_color(g_upd_msg, lv_color_hex(ADL_TEXT_DIM), 0);
    g_upd_bar = lv_bar_create(cupd);
    lv_obj_set_size(g_upd_bar, LV_PCT(100), 10);
    lv_bar_set_range(g_upd_bar, 0, 100);
    lv_bar_set_value(g_upd_bar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(g_upd_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *urow = srow_start(cupd, 46);
    lv_obj_t *ub1 = sbutton(urow, "Kontrol Et", ADL_ACCENT, 0xFFFFFF, 150, 40);
    lv_obj_add_event_cb(ub1, s_upd_check_cb, LV_EVENT_CLICKED, 0);
    lv_obj_t *ub2 = sbutton(urow, "Guncelle", ADL_SURFACE_2, ADL_TEXT, 130, 40);
    lv_obj_add_event_cb(ub2, s_upd_apply_cb, LV_EVENT_CLICKED, 0);
    s_upd_refresh();

    lv_obj_t *c3 = scard(g_content);
    stitle(c3, "Disk Şifreleme");
    asi_crypt_t *cr = s_asi ? s_asi->crypt : 0;
    int present = (cr && cr->present) ? cr->present() : 0;
    if (present) {
        sdesc(c3, "Disk AES-XTS ile sifreli. Acilista parola sorulur. Kaldirmak icin parolayi girin.");
        skv(c3, "Durum", (cr && cr->status) ? cr->status() : "Acik");
        lv_obj_t *row = srow_start(c3, 52);
        g_crypt_ta = lv_textarea_create(row);
        lv_obj_set_size(g_crypt_ta, 240, 40);
        lv_textarea_set_one_line(g_crypt_ta, true);
        lv_textarea_set_password_mode(g_crypt_ta, true);
        lv_textarea_set_max_length(g_crypt_ta, 31);
        lv_textarea_set_placeholder_text(g_crypt_ta, "Parola");
        lv_obj_t *b = sbutton(row, "Kaldır", ADL_DANGER, 0xFFFFFF, 110, 40);
        lv_obj_add_event_cb(b, s_crypt_disable_cb, LV_EVENT_CLICKED, 0);
    } else {
        sdesc(c3, "Diski AES-XTS ile sifreleyin. Bir parola belirleyin; sonraki acilislarda parola sorulur.");
        lv_obj_t *row = srow_start(c3, 52);
        g_crypt_ta = lv_textarea_create(row);
        lv_obj_set_size(g_crypt_ta, 240, 40);
        lv_textarea_set_one_line(g_crypt_ta, true);
        lv_textarea_set_password_mode(g_crypt_ta, true);
        lv_textarea_set_max_length(g_crypt_ta, 31);
        lv_textarea_set_placeholder_text(g_crypt_ta, "Yeni parola (min 4)");
        lv_obj_t *b = sbutton(row, "Şifrele", ADL_ACCENT, 0xFFFFFF, 110, 40);
        lv_obj_add_event_cb(b, s_crypt_setup_cb, LV_EVENT_CLICKED, 0);
    }
    if (s_crypt_msg[0]) sdesc(c3, s_crypt_msg);
    s_crypt_focus();
}

static void s_page_about(void) {
    lv_obj_t *c = scard(g_content);
    stitle(c, "Arctian OS");
    sdesc(c, "Beyaz Kurt temali, terminal ile yonetilebilen masaustu isletim sistemi.");
    skv(c, "Surum", update_current_version());
    skv(c, "Arayuz", "LVGL + ADL-Wolf tema");
    skv(c, "Kabuk", "arctian> (terminal)");

    lv_obj_t *c2 = scard(g_content);
    stitle(c2, "Ozellestirme");
    sdesc(c2, "Vurgu rengi, arka plan, masaustu ogeleri ve saat bicimi buradan degistirilebilir.");
    sdesc(c2, "Ayni islemler terminalden de yapilabilir: apps, run, wallpaper, pkg, sysinfo.");

    /* 0.6: klavye kisayollari ve yeni araclar tek yerde. */
    lv_obj_t *c3 = scard(g_content);
    stitle(c3, "Klavye Kisayollari");
    skv(c3, "Alt+Tab", "Pencere degistir");
    skv(c3, "Ctrl+Alt+T", "Terminal ac");
    skv(c3, "Ctrl+Alt+A", "Asistan paneli");
    skv(c3, "Ctrl+Alt+F", "FPS gostergesi");
    skv(c3, "Ctrl+Alt+H", "Ne yapabilirim?");
    skv(c3, "Ctrl+Alt+, / .", "Onceki / sonraki calisma alani");
    skv(c3, "Ctrl+C / X / V", "Kopyala / kes / yapistir");
    skv(c3, "Ctrl+Z / Y", "Geri al / yinele");

    lv_obj_t *c4 = scard(g_content);
    stitle(c4, "Yeni Araclar (0.6)");
    sdesc(c4, "Terminal: cp, find, du, tree, trash, stat. Dosya Yoneticisi: Ad (yeniden adlandir), i (ozellikler).");
    sdesc(c4, "Tarayici: * yer imi, Y yer imleri, H indirmeler, G gecmis, > ileri.");
}

/* ---- Erisilebilirlik sayfasi --------- */

static void s_scale_cb(lv_event_t *e) {
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (arctian_settings && arctian_settings->set_int)
        arctian_settings->set_int("fontscale", v);   /* settings.c kabugu yeniden kurar */
}

static void s_page_access(void) {
    lv_obj_t *c1 = scard(g_content);
    stitle(c1, "Yazi Olcegi");
    sdesc(c1, "Tum arayuz yazilarinin olcegi. Degisiklik aninda tum masaustu yeniden kurulur.");
    lv_obj_t *wrap = lv_obj_create(c1);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_width(wrap, LV_PCT(100));
    lv_obj_set_height(wrap, LV_SIZE_CONTENT);
    lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(wrap, 8, 0);
    lv_obj_set_style_pad_column(wrap, 8, 0);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW_WRAP);

    int cur = (arctian_settings && arctian_settings->get_int)
              ? arctian_settings->get_int("fontscale", 100) : 100;
    int opts[3] = { 100, 125, 150 };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_button_create(wrap);
        lv_obj_set_size(b, 104, 48);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(b, 10, 0);
        uint32_t bg = (cur == opts[i]) ? ADL_ACCENT : ADL_SURFACE_2;
        lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        char t[8];
        int k = 0;
        t[k++] = '%';
        int v = opts[i];
        char tmp[4];
        int m = 0;
        if (v == 0) tmp[m++] = '0';
        while (v > 0 && m < 3) { tmp[m++] = (char)('0' + (v % 10)); v /= 10; }
        while (m > 0 && k < 7) t[k++] = tmp[--m];
        t[k] = 0;
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, t);
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, s_scale_cb, LV_EVENT_CLICKED, (void *)(intptr_t)opts[i]);
    }

    lv_obj_t *c2 = scard(g_content);
    stitle(c2, "Gorunurluk");
    sdesc(c2, "Yuksek kontrast metin ile zemin ayrimini guclendirir. Odak halkasi klavye ile "
               "gezinirken odaktaki ogeleri vurgular. Buyuk simgeler masaustu simgelerini irilestirir.");
    s_add_toggle(c2, "Yuksek kontrast", "contrast", 0);
    s_add_toggle(c2, "Klavye odak halkasi", "focusring", 1);
    s_add_toggle(c2, "Buyuk simgeler", "bigicons", 0);

    lv_obj_t *c3 = scard(g_content);
    stitle(c3, "Ekran");
    sdesc(c3, "Gece modu ve parlaklik Hizli Ayarlar panelinden yonetilir "
               "(gorev cubugundaki ayarlar dugmesi).");
}


/* ---- 0.7: Guncelleme sekmesi --------- */
static lv_obj_t *g_upd_plan_lbl;
static lv_obj_t *g_upd_delay_lbl;
static lv_obj_t *g_upd_auto_lbl;

static int s_upd_get(const char *k, int def) {
    return (arctian_settings && arctian_settings->get_int)
           ? arctian_settings->get_int(k, def) : def;
}

static void s_upd_set(const char *k, int v) {
    if (arctian_settings && arctian_settings->set_int)
        arctian_settings->set_int(k, v);
}

static void s_upd_plan_refresh(void) {
    if (!g_upd_plan_lbl) return;
    const update_manifest_t *m = update_manifest();
    int delay = s_upd_get("update_delay_days", 0);
    char b[180];
    scpy(b, sizeof(b), "");
    if (!m->version[0]) {
        scpy(b, sizeof(b), "Sunucu bilgisi yok. Once 'Kontrol Et'.");
    } else if (m->released) {
        uint64_t need = (uint64_t)m->released + (uint64_t)delay * 86400ull;
        uint64_t now = 0;
        if (s_asi && s_asi->time && s_asi->time->unix_now) now = s_asi->time->unix_now();
        char nb[8];
        scpy(b, sizeof(b), "Sunucu: ");
        scat(b, sizeof(b), m->version);
        scat(b, sizeof(b), " | gecikme: ");
        snum(nb, sizeof(nb), (unsigned)delay);
        scat(b, sizeof(b), nb);
        scat(b, sizeof(b), " gun | ");
        if (!now) scat(b, sizeof(b), "saat yok");
        else if (now >= need) scat(b, sizeof(b), "kurulabilir");
        else {
            uint64_t kalan = (need - now + 86399) / 86400;
            scat(b, sizeof(b), "yaklasik ");
            snum(nb, sizeof(nb), (unsigned)kalan);
            scat(b, sizeof(b), nb);
            scat(b, sizeof(b), " gun sonra kurulur");
        }
    } else {
        scpy(b, sizeof(b), "Sunucu surumu: ");
        scat(b, sizeof(b), m->version);
    }
    lv_label_set_text(g_upd_plan_lbl, b);
    if (g_upd_auto_lbl) {
        lv_label_set_text(g_upd_auto_lbl,
            s_upd_get("update_auto", 1) ? "Otomatik guncellemeler: Acik"
                                        : "Otomatik guncellemeler: Kapali");
    }
    if (g_upd_delay_lbl) {
        char d[32];
        char nb[8];
        snum(nb, sizeof(nb), (unsigned)delay);
        scpy(d, sizeof(d), "Gecikme: ");
        scat(d, sizeof(d), nb);
        scat(d, sizeof(d), " gun");
        lv_label_set_text(g_upd_delay_lbl, d);
    }
}

static void s_upd_delay_cb(lv_event_t *e) {
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int v = s_upd_get("update_delay_days", 0) + delta;
    if (v < 0) v = 0;
    if (v > 30) v = 30;
    s_upd_set("update_delay_days", v);
    s_upd_plan_refresh();
}

static void s_upd_apply_cb2(lv_event_t *e) {
    (void)e;
    update_set_ready(1);
    update_install_now();
    s_upd_plan_refresh();
}

static void s_page_update(void) {
    lv_obj_t *c = scard(g_content);
    stitle(c, "Otomatik Guncelleme");
    sdesc(c, "Acikken sistem yeni surumu kendisi indirir ve uygun anda kurar. "
             "Kurulum kapanis veya acilista yapilir; indirme masaustunu kilitlemez.");
    g_upd_auto_lbl = lv_label_create(c);
    lv_obj_set_style_text_color(g_upd_auto_lbl, lv_color_hex(ADL_TEXT), 0);
    s_add_toggle(c, "Otomatik guncellemeler", "update_auto", 1);

    lv_obj_t *c2 = scard(g_content);
    stitle(c2, "Gecikme");
    sdesc(c2, "Yayinlandiktan kac gun sonra kurulsun? 0 = sonraki kapanis veya acilis. "
              "Bir surumde sorun gorurseniz otomatik guncellemeyi kapatabilirsiniz; "
              "kapatmak kalan gunu silmez.");
    g_upd_delay_lbl = lv_label_create(c2);
    lv_obj_set_style_text_color(g_upd_delay_lbl, lv_color_hex(ADL_TEXT), 0);
    lv_obj_t *r = srow_start(c2, 46);
    lv_obj_t *m10 = sbutton(r, "-10", ADL_SURFACE_2, ADL_TEXT, 70, 40);
    lv_obj_add_event_cb(m10, s_upd_delay_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-10);
    lv_obj_t *m1 = sbutton(r, "-1", ADL_SURFACE_2, ADL_TEXT, 60, 40);
    lv_obj_add_event_cb(m1, s_upd_delay_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_t *p1 = sbutton(r, "+1", ADL_SURFACE_2, ADL_TEXT, 60, 40);
    lv_obj_add_event_cb(p1, s_upd_delay_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *p10 = sbutton(r, "+10", ADL_SURFACE_2, ADL_TEXT, 70, 40);
    lv_obj_add_event_cb(p10, s_upd_delay_cb, LV_EVENT_CLICKED, (void *)(intptr_t)10);

    lv_obj_t *c3 = scard(g_content);
    stitle(c3, "Durum");
    skv(c3, "Kurulu Surum", update_applied_version());
    skv(c3, "Derleme", update_current_version());
    skv(c3, "Sunucu", update_base_url());
    skv(c3, "Manifesto Imzasi", update_manifest_verified() ? "Gecerli" : "Yok");
    skv(c3, "Hazir Paket", update_ready() ? "Var" : "Yok");
    g_upd_plan_lbl = lv_label_create(c3);
    lv_obj_set_style_text_color(g_upd_plan_lbl, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_t *r2 = srow_start(c3, 46);
    lv_obj_t *b1 = sbutton(r2, "Kontrol Et", ADL_ACCENT, 0xFFFFFF, 150, 40);
    lv_obj_add_event_cb(b1, s_upd_check_cb, LV_EVENT_CLICKED, 0);
    lv_obj_t *b2 = sbutton(r2, "Kur", ADL_SURFACE_2, ADL_TEXT, 110, 40);
    lv_obj_add_event_cb(b2, s_upd_apply_cb2, LV_EVENT_CLICKED, 0);
    sdesc(c3, "\"Kur\" gecikmeyi yok sayar: bu kullanici kararidir.");
    s_upd_plan_refresh();
}

static void s_page(int p) {
    if (p < 0 || p > 8) p = 0;
    g_page = p;
    s_pool_n = 0;
    g_crypt_ta = 0;
    g_fw_port = 0;
    g_fw_name = 0;
    g_fw_dir = 0;
    g_fw_proto = 0;
    g_fw_action = 0;
    if (g_upd_timer) { lv_timer_delete(g_upd_timer); g_upd_timer = 0; }
    g_upd_msg = 0;
    g_upd_bar = 0;
    if (g_crypt_group) lv_group_remove_all_objs(g_crypt_group);
    {
        lv_indev_t *kp = lv_port_get_keypad();
        if (kp) lv_indev_set_group(kp, 0);
    }
    s_content_clear();
    s_nav_active();
    switch (p) {
        case 0: s_page_personalize(); break;
        case 1: s_page_desktop(); break;
        case 2: s_page_screen(); break;
        case 3: s_page_network(); break;
        case 4: s_page_apps(); break;
        case 5: s_page_system(); break;
        case 6: s_page_access(); break;
        case 7: s_page_update(); break;
        default: s_page_about(); break;
    }
}

static void s_build_nav(void) {
    g_nav = lv_obj_create(g_win);
    lv_obj_remove_style_all(g_nav);
    lv_obj_set_size(g_nav, S_NAV_W, LV_PCT(100));
    lv_obj_set_style_bg_color(g_nav, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(g_nav, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_nav, 12, 0);
    lv_obj_set_style_pad_row(g_nav, 8, 0);
    lv_obj_set_flex_flow(g_nav, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(g_nav, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logo = lv_label_create(g_nav);
    lv_label_set_text(logo, "Ayarlar");
    lv_obj_set_style_text_color(logo, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_pad_bottom(logo, 8, 0);
    g_logo = logo;

    for (int i = 0; i < 9; i++) {
        lv_obj_t *b = lv_button_create(g_nav);
        lv_obj_set_size(b, LV_PCT(100), 40);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(ADL_SURFACE_2), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, g_nav_labels[i]);
        lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_add_event_cb(b, s_nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        g_nav_btns[i] = b;
    }
}

static void s_build_content(void) {
    g_content = lv_obj_create(g_win);
    lv_obj_remove_style_all(g_content);
    lv_obj_set_height(g_content, LV_PCT(100));
    lv_obj_set_flex_grow(g_content, 1);
    lv_obj_set_style_bg_color(g_content, lv_color_hex(ADL_BG), 0);
    lv_obj_set_style_bg_opa(g_content, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_content, 16, 0);
    lv_obj_set_style_pad_row(g_content, 12, 0);
    lv_obj_set_flex_flow(g_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_content, LV_DIR_VER);
}

static void s_win_del_cb(lv_event_t *e) {
    (void)e;
    /* Guncelleme denetimi/indirmesi surerken pencere kapatilirsa timer
     * silinmis etiket ve cubuga yazmaya devam ediyordu. */
    if (g_upd_timer) { lv_timer_delete(g_upd_timer); g_upd_timer = 0; }
    g_upd_msg = 0;
    g_upd_bar = 0;
    g_upd_plan_lbl = 0;
    g_crypt_ta = 0;
    g_win = 0;
    g_nav = 0;
    g_content = 0;
    for (int i = 0; i < 9; i++) g_nav_btns[i] = 0;
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    s_asi = asi;
    g_win = win;
    g_nav = 0;
    g_content = 0;
    g_page = 0;
    s_pool_n = 0;
    for (int i = 0; i < 9; i++) g_nav_btns[i] = 0;

    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(win, 0, 0);
    lv_obj_set_style_pad_column(win, 0, 0);

    s_build_nav();
    s_build_content();
    lv_obj_add_event_cb(win, s_win_del_cb, LV_EVENT_DELETE, 0);

    s_page(0);
#ifdef ARCTIAN_UPDATE_SELFTEST
    s_page(5);   /* test: Guncelleme kartini goster */
#endif
}
