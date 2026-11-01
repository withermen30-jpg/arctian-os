#include "dsk.h"

/* ---- Terminal koprusu (0.7) ----------------------------------------
 * Terminal bir uygulamadir; kabuk ve rol=cli/runtime .wlf uygulamalari
 * onunla buradan konusur. Boylece "calistir" eylemi her zaman gorunur bir
 * terminal penceresinde olur (0.7 hedefi: terminal merkezli calisma). */

static char         s_term_pend[224];
static int          s_term_pend_on;
static term_puts_fn s_term_puts;

void arctian_term_set_puts(term_puts_fn fn) { s_term_puts = fn; }

void arctian_term_write(const char *s) {
    if (s_term_puts && s) s_term_puts(s);
}

int arctian_term_is_open(void) {
    for (int j = 0; j < MAX_WINS; j++) {
        winrec_t *r = g_recs[j];
        if (r && r->win && title_eq(r->title, "Terminal")) return 1;
    }
    return 0;
}

int arctian_term_open(void) {
    int idx = app_idx_by_name("terminal");
    if (idx < 0) return -1;
    open_registry_app(idx);
    return 0;
}

int arctian_term_run(const char *cmd) {
    if (!cmd || !cmd[0]) return -1;
    int n = 0;
    for (; cmd[n] && n < (int)sizeof(s_term_pend) - 1; n++) s_term_pend[n] = cmd[n];
    s_term_pend[n] = 0;
    s_term_pend_on = 1;
    return arctian_term_open();
}

const char *arctian_term_pending(void) {
    if (!s_term_pend_on) return 0;
    s_term_pend_on = 0;
    return s_term_pend;
}


void recover_set_reason(const char *s) {
    int i = 0;
    for (; s[i] && i < (int)sizeof(g_recover.reason) - 1; i++) g_recover.reason[i] = s[i];
    g_recover.reason[i] = 0;
}


void recover_prepare(const char *title) {
    for (int i = 0; i < 48; i++) g_launch_title[i] = 0;
    if (title) { int i = 0; for (; title[i] && i < 47; i++) g_launch_title[i] = title[i]; }
}


void recover_begin_call(uint32_t timeout_ms) {
    if (g_asi && g_asi->system && g_asi->system->recover_begin)
        g_asi->system->recover_begin(&g_recover, timeout_ms);
}


void recover_disarm(void) {
    if (g_asi && g_asi->system && g_asi->system->recover_end)
        g_asi->system->recover_end();
    g_launch_timer = 0;
    g_launch_overlay = 0;
    g_launch_win = 0;
    g_launch_ctx = 0;
    g_launch_title[0] = 0;
}


void loading_done_cb(lv_timer_t *timer) {
    app_ctx_t *c = (app_ctx_t *)lv_timer_get_user_data(timer);
    create_window(c->title, 520, 340);
    lv_obj_delete(c->overlay);
    lv_free(c);
    lv_timer_delete(timer);
    recover_disarm();
}


void open_app(const char *title) {
    /* Kayitli uygulama ise dogru yoldan ac: open_app yalnizca bos bir pencere
     * uretiyordu (uygulama entry()'si hic cagrilmiyordu -> Ctrl+Alt+T ile
     * acilan terminal bos/kullanilamaz kaliyordu). Kayit yoksa eski
     * davranisa (yukleme animasyonu) dusulur. */
    int total = app_total();
    for (int i = 0; i < total; i++) {
        const arctian_app_t *ra = app_get(i);
        if (ra && title_eq(ra->title, title)) { open_registry_app(i); return; }
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

    app_ctx_t *c = (app_ctx_t *)lv_malloc(sizeof(app_ctx_t));
    scopy(c->title, title, 39);
    c->overlay = ov;
    recover_prepare(title);
    g_launch_overlay = ov;
    g_launch_ctx = c;
    g_launch_timer = lv_timer_create(loading_done_cb, 2000, c);
}


void app_btn_cb(lv_event_t *e) {
    const char *title = (const char *)lv_event_get_user_data(e);
    for (int j = 0; j < MAX_WINS; j++) {
        winrec_t *r = g_recs[j];
        if (!r || !r->win || !title_eq(r->title, title)) continue;
        if (lv_obj_has_flag(r->win, LV_OBJ_FLAG_HIDDEN))
            lv_obj_remove_flag(r->win, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(r->win, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(r->win);
        return;
    }
    int total = app_total();
    for (int i = 0; i < total; i++) {
        const arctian_app_t *a = app_get(i);
        if (a && title_eq(a->title, title)) { open_registry_app(i); return; }
    }
    open_app(title);
}


void tb_app_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const arctian_app_t *a = app_get(idx);
    if (!a) return;
    for (int j = 0; j < MAX_WINS; j++) {
        winrec_t *r = g_recs[j];
        if (!r || !r->win || !title_eq(r->title, a->title)) continue;
        if (lv_obj_has_flag(r->win, LV_OBJ_FLAG_HIDDEN))
            lv_obj_remove_flag(r->win, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(r->win, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(r->win);
        return;
    }
    open_registry_app(idx);
}


void dyn_win_del_cb(lv_event_t *e) {
    /* user_data'da indeks degil id duruyor: aradan bir uygulama
     * kaldirildiginda g_dyn kayar, eski indeks baska bir uygulamaya
     * denk gelip onun "acik" bayragini temizleyebilirdi. */
    char *cid = (char *)lv_event_get_user_data(e);
    if (!cid) return;
    int idx = dyn_find(cid);
    if (idx >= 0) arctian_dyn_mark_closed(idx);
    if (g_asi && g_asi->mem && g_asi->mem->free) g_asi->mem->free(cid);
}


void run_app_done(lv_timer_t *t) {
    runctx_t *c = (runctx_t *)lv_timer_get_user_data(t);

    int rc = arctian_setjmp(&g_recover);
    if (rc) {
        if (g_asi && g_asi->system && g_asi->system->recover_end) g_asi->system->recover_end();
        handle_app_crash(rc);
        return;
    }

    const arctian_app_t *a = app_get(c->idx);
    if (!a) { lv_obj_delete(c->overlay); lv_free(c); lv_timer_delete(t); return; }
    int ww = a->win_w > 240 ? a->win_w : 560;
    int wh = a->win_h > 180 ? a->win_h : 380;
    g_pending_caps = dyn_perms_for(c->idx);
    lv_obj_t *win = create_window(a->title, ww, wh);
    g_launch_win = win;
    lv_obj_t *body = lv_obj_create(win);
    lv_obj_remove_style_all(body);
    /* Gercek pencere boyutunu kullan: create_window kv'den hatirlanan
     * boyutu uygulamis olabilir (ww/wh'dan farkli). */
    lv_obj_set_size(body, lv_obj_get_width(win) - 4, lv_obj_get_height(win) - 42);
    lv_obj_set_pos(body, 2, 38);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    /* Govde isareti: pencere boyut degisiminde kabuk bu nesneyi birlikte
     * boyutlandirir ve uygulamaya relayout olarak body'yi geri verir. */
    lv_obj_add_flag(body, LV_OBJ_FLAG_USER_2);
    if (c->idx >= arctian_app_count) {
        arctian_dyn_begin_launch(c->idx - arctian_app_count);
        g_crash_dyn = c->idx - arctian_app_count;
        /* id kopyasini sakla; indeks kayabilir (bkz. dyn_win_del_cb) */
        char *cid = 0;
        int d = c->idx - arctian_app_count;
        if (d >= 0 && d < g_dyn_count && g_asi && g_asi->mem && g_asi->mem->alloc) {
            cid = (char *)g_asi->mem->alloc(DPK_ID_LEN);
            if (cid) {
                const char *p = g_dyn[d].id;
                int n = 0;
                for (; n < DPK_ID_LEN - 1 && p[n]; n++) cid[n] = p[n];
                cid[n] = 0;
            }
        }
        lv_obj_add_event_cb(win, dyn_win_del_cb, LV_EVENT_DELETE, cid);
    } else {
        arctian_dyn_begin_launch(-1);
        g_crash_dyn = -1;
    }
    recover_begin_call(5000);
    int wpid = win_pid(win);
    if (g_asi->sandbox && wpid > 0) g_asi->sandbox->enter(wpid);

    /* 0.7: her acilista ozne kaydi. Kutulu paket guvenilir degildir;
     * govede silinince kayit kapali (perm_body_del_cb). */
    {
        char sid[56];
        const char *ttl = a->title;
        int trusted = 1;
        const char *pkg = "";
        if (c->idx >= arctian_app_count) {
            dyn_app_t *da = &g_dyn[c->idx - arctian_app_count];
            int n = 0;
            const char *src = "wlf:";
            for (int i = 0; src[i] && n < (int)sizeof(sid) - 2; i++) sid[n++] = src[i];
            for (int i = 0; da->id[i] && n < (int)sizeof(sid) - 1; i++) sid[n++] = da->id[i];
            sid[n] = 0;
            trusted = 0;
            pkg = da->fname;
            ttl = da->title[0] ? da->title : da->id;
        } else {
            int n = 0;
            const char *src = "gomulu:";
            for (int i = 0; src[i] && n < (int)sizeof(sid) - 2; i++) sid[n++] = src[i];
            for (int i = 0; a->name[i] && n < (int)sizeof(sid) - 1; i++) sid[n++] = a->name[i];
            sid[n] = 0;
        }
        perm_app_open(body, sid, ttl, trusted, pkg);
        lv_obj_add_event_cb(body, perm_body_del_cb, LV_EVENT_DELETE, body);
        perm_dispatch_begin();   /* entry boyunca uygulama kodunda sayilir */
    }

    /* Uygulama entry() icinde kendi klavye grubunu baglarsa kaydet.      */
    lv_indev_t *kp0 = lv_port_get_keypad();
    lv_group_t *g_before = kp0 ? lv_indev_get_group(kp0) : 0;
    if (a->entry) a->entry(body, g_asi);
    perm_dispatch_end();
    if (kp0 && lv_indev_get_group(kp0) != g_before)
        win_set_focus_group(win, lv_indev_get_group(kp0));
    if (g_asi && g_asi->system && g_asi->system->recover_end) g_asi->system->recover_end();
    lv_obj_delete(c->overlay);
    lv_free(c);
    lv_timer_delete(t);
    g_launch_win = 0;
    g_launch_overlay = 0;
    g_launch_ctx = 0;
    g_launch_timer = 0;
}


void show_error_window(const char *title, const char *msg) {
    lv_obj_t *w = create_window(title, 480, 220);
    lv_obj_t *l = label(w, msg, ADL_DANGER);
    lv_obj_set_width(l, 440);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 16, 52);
}


void open_registry_app(int idx) {
    const arctian_app_t *pre = app_get(idx);
    if (pre && (pre->flags & ARCTIAN_APP_ENGINE)) {
        idx = -1;
        for (int i = 0; i < arctian_app_count; i++) {
            if (title_eq(arctian_apps[i].name, "terminal")) { idx = i; break; }
        }
        if (idx < 0) return;
    }
    /* 0.7 uygulama modeli: .wlf rolu neyse o olur.
     *   ui      -> normal pencere
     *   cli     -> terminal acilir, program terminalde calisir
     *   runtime -> terminal acilir; dosya verilmediyse kullanim bilgisi
     * Yani .wlf "acilinca pencere" varsayimina bagli degildir. */
    if (idx >= arctian_app_count) {
        dyn_app_t *d = &g_dyn[idx - arctian_app_count];
        if (d->role == WLF_ROLE_CLI || d->role == WLF_ROLE_RUNTIME) {
            char cmd[200];
            int n = 0;
            const char *pre2 = "wf run ";
            for (int i = 0; pre2[i] && n < (int)sizeof(cmd) - 2; i++) cmd[n++] = pre2[i];
            for (int i = 0; d->id[i] && n < (int)sizeof(cmd) - 2; i++) cmd[n++] = d->id[i];
            const char *tgt = arctian_open_target ? arctian_open_target : "";
            if (tgt[0]) {
                cmd[n++] = ' ';
                /* Masaustu simgesi yalnizca dosya adini tasir; mutlak yol kur. */
                int abs = 0;
                for (int i = 0; tgt[i]; i++) if (tgt[i] == ':') { abs = 1; break; }
                if (!abs) {
                    const char *pre3 = "C:/";
                    for (int i = 0; pre3[i] && n < (int)sizeof(cmd) - 2; i++) cmd[n++] = pre3[i];
                }
                for (int i = 0; tgt[i] && n < (int)sizeof(cmd) - 1; i++) cmd[n++] = tgt[i];
            }
            cmd[n] = 0;
            arctian_term_run(cmd);
            return;
        }
    }
    /* Tek ornek kurali: ayni baslikli pencere zaten aciksa yenisi olusturmak
     * yerine gizliyi kaldirip on plana al (goreve cubuguyla ayni davranis;
     * ikon/menus aramasi ikinci ornek uretemez). */
    const arctian_app_t *a0 = app_get(idx);
    if (a0) {
        for (int j = 0; j < MAX_WINS; j++) {
            winrec_t *r = g_recs[j];
            if (!r || !r->win || !title_eq(r->title, a0->title)) continue;
            if (lv_obj_has_flag(r->win, LV_OBJ_FLAG_HIDDEN))
                lv_obj_remove_flag(r->win, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(r->win);
            return;
        }
    }
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
    const arctian_app_t *la = app_get(idx);
    recover_prepare(la ? la->title : "Uygulama");
    if (idx >= arctian_app_count) g_crash_dyn = idx - arctian_app_count;
    g_launch_overlay = ov;
    g_launch_ctx = c;
    g_launch_timer = lv_timer_create(run_app_done, 2000, c);
}


void reg_tile_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    open_registry_app(idx);
}


/* ---- Cokme diyalogu + log ---- */

void crash_ui_close_cb(lv_event_t *e) {
    lv_obj_delete((lv_obj_t *)lv_event_get_user_data(e));
}


void destroy_win_obj(lv_obj_t *win) {
    if (!win) return;
    for (int j = 0; j < MAX_WINS; j++) {
        winrec_t *r = g_recs[j];
        if (!r || r->win != win) continue;
        if (r->pid > 0 && g_asi->proc && g_asi->proc->app_close) {
            g_asi->proc->app_close(r->pid, 0);
            r->pid = 0;
        }
        if (r->btn) lv_obj_delete(r->btn);
        lv_obj_delete(r->win);
        r->btn = 0; r->win = 0;
        break;
    }
    update_app_indicators();
}


void crash_log_write(const char *title, const char *reason, int code) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in || !g_asi->fs->write) return;
    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/logs");
    char name[44];
    int l = 0;
    for (int i = 0; title[i] && l < (int)sizeof(name) - 5; i++) {
        char c = title[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-';
        name[l++] = ok ? c : '_';
    }
    if (l == 0) { const char *d = "uygulama"; for (int i = 0; d[i] && l < (int)sizeof(name) - 5; i++) name[l++] = d[i]; }
    name[l] = '.'; name[l + 1] = 'l'; name[l + 2] = 'o'; name[l + 3] = 'g'; name[l + 4] = 0;

    char line[300];
    int o = 0;
    const char *p1 = "Arctian uygulama cokme kaydi\r\nuygulama: ";
    for (int i = 0; p1[i] && o < (int)sizeof(line) - 1; i++) line[o++] = p1[i];
    for (int i = 0; title[i] && o < (int)sizeof(line) - 1; i++) line[o++] = title[i];
    const char *p2 = "\r\nsebep: ";
    for (int i = 0; p2[i] && o < (int)sizeof(line) - 1; i++) line[o++] = p2[i];
    for (int i = 0; reason[i] && o < (int)sizeof(line) - 1; i++) line[o++] = reason[i];
    const char *p3 = (code == 2) ? "\r\ntur: zaman asimi\r\n"
                   : (code == 3) ? "\r\ntur: gecersiz cagri\r\n"
                                 : "\r\ntur: CPU istisnasi\r\n";
    for (int i = 0; p3[i] && o < (int)sizeof(line) - 1; i++) line[o++] = p3[i];
    line[o] = 0;

    void *h = g_asi->fs->create_in("C:/logs", name, 0);
    if (!h) return;
    g_asi->fs->write(h, line, (uint32_t)o);
}


void show_crash_dialog(const char *title, const char *reason) {
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
    lv_obj_set_size(panel, 480, 268);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);

    lv_obj_t *ti = label(panel, "Uygulama coktu", ADL_DANGER);
    lv_obj_align(ti, LV_ALIGN_TOP_MID, 0, 0);

    char msg[300];
    int o = 0;
    const char *p1 = "Uygulama: ";
    for (int i = 0; p1[i] && o < (int)sizeof(msg) - 1; i++) msg[o++] = p1[i];
    for (int i = 0; title[i] && o < (int)sizeof(msg) - 1; i++) msg[o++] = title[i];
    const char *p2 = "\n\nCokme sebebi:\n";
    for (int i = 0; p2[i] && o < (int)sizeof(msg) - 1; i++) msg[o++] = p2[i];
    for (int i = 0; reason[i] && o < (int)sizeof(msg) - 1; i++) msg[o++] = reason[i];
    const char *p3 = "\n\nUygulama guvenli sekilde kapatildi.";
    for (int i = 0; p3[i] && o < (int)sizeof(msg) - 1; i++) msg[o++] = p3[i];
    msg[o] = 0;

    lv_obj_t *ml = label(panel, msg, ADL_TEXT_DIM);
    lv_label_set_long_mode(ml, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ml, 436);
    lv_obj_set_style_text_align(ml, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ml, LV_ALIGN_TOP_MID, 0, 34);

    lv_obj_t *ok = lv_button_create(panel);
    lv_obj_set_size(ok, 128, 40);
    lv_obj_set_style_radius(ok, 8, 0);
    lv_obj_set_style_bg_color(ok, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *ol = label(ok, "Tamam", ADL_TEXT); lv_obj_center(ol);
    lv_obj_add_event_cb(ok, crash_ui_close_cb, LV_EVENT_CLICKED, ov);
}


void handle_app_crash(int code) {
    if (g_launch_timer) { lv_timer_delete(g_launch_timer); g_launch_timer = 0; }
    if (g_launch_overlay) { lv_obj_delete(g_launch_overlay); g_launch_overlay = 0; }
    if (g_launch_win) { destroy_win_obj(g_launch_win); g_launch_win = 0; }
    if (g_launch_ctx) { lv_free(g_launch_ctx); g_launch_ctx = 0; }
    if (g_crash_dyn >= 0) { arctian_dyn_mark_closed(g_crash_dyn); g_crash_dyn = -1; }
    char title[48];
    bin_copy(title, sizeof(title), g_launch_title[0] ? g_launch_title : "Uygulama");
    const char *reason = g_recover.reason[0] ? g_recover.reason : "Bilinmeyen hata";
    crash_log_write(title, reason, code);
    show_crash_dialog(title, reason);
}

