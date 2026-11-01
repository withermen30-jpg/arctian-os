#include "dsk.h"


int kv_find(const char *k) {
    for (int i = 0; i < s_kv_n; i++) if (s_eq(s_kv[i].k, k)) return i;
    return -1;
}


const char *kv_get(const char *k, const char *def) {
    int i = kv_find(k);
    return i >= 0 ? s_kv[i].v : def;
}


int kv_get_int(const char *k, int def) {
    const char *v = kv_get(k, 0);
    if (!v) return def;
    int sign = 1, n = 0, any = 0;
    if (*v == '-') { sign = -1; v++; }
    while (*v >= '0' && *v <= '9') { n = n * 10 + (*v - '0'); v++; any = 1; }
    return any ? sign * n : def;
}


void kv_set(const char *k, const char *v) {
    int i = kv_find(k);
    if (i < 0) {
        if (s_kv_n >= SET_KV_MAX) return;
        i = s_kv_n++;
        bin_copy(s_kv[i].k, (int)sizeof(s_kv[i].k), k);
    }
    bin_copy(s_kv[i].v, (int)sizeof(s_kv[i].v), v);
    settings_save();
}


void settings_apply_values(void) {
    int ai = kv_get_int("accent", 0);
    if (ai < 0 || ai >= g_accent_n) ai = 0;
    g_accent_idx = ai;
    adl_set_accent(g_accents[ai].a, g_accents[ai].a2);
    g_opt_dicons = kv_get_int("dicons", 1) ? 1 : 0;
    g_opt_clockshow = kv_get_int("clockshow", 1) ? 1 : 0;
    g_opt_clock24 = kv_get_int("clock24", 1) ? 1 : 0;
    /* Erisilebilirlik tercihleri: boot'ta da, canli degisiklikte de
     * ayni yerden uygulanir. */
    adl_set_font_scale(kv_get_int("fontscale", 100));
    adl_set_contrast(kv_get_int("contrast", 0));
    adl_set_light(kv_get_int("light", 0));
    adl_set_focus_ring(kv_get_int("focusring", 1));
    g_icon_big = kv_get_int("bigicons", 0) ? 1 : 0;
}


void settings_load(void) {
    s_kv_n = 0;
    if (g_asi && g_asi->fs && g_asi->fs->find && g_asi->fs->read) {
        void *h = g_asi->fs->find(0, "settings.cfg");
        if (h) {
            char buf[1600];
            uint32_t r = g_asi->fs->read(h, buf, sizeof(buf) - 1);
            buf[r] = 0;
            uint32_t i = 0;
            while (i < r) {
                uint32_t ls = i;
                while (i < r && buf[i] != '\n' && buf[i] != '\r') i++;
                uint32_t le = i;
                while (i < r && (buf[i] == '\n' || buf[i] == '\r')) i++;
                if (le <= ls) continue;
                uint32_t eq = ls;
                while (eq < le && buf[eq] != '=') eq++;
                if (eq >= le || s_kv_n >= SET_KV_MAX) continue;
                set_kv_t *kv = &s_kv[s_kv_n++];
                int kl = (int)(eq - ls); if (kl > 23) kl = 23;
                for (int j = 0; j < kl; j++) kv->k[j] = buf[ls + j];
                kv->k[kl] = 0;
                int vl = (int)(le - eq - 1); if (vl > 95) vl = 95;
                for (int j = 0; j < vl; j++) kv->v[j] = buf[eq + 1 + j];
                kv->v[vl] = 0;
            }
        }
    }
    settings_apply_values();
}


void settings_save(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create) return;
    char buf[1800];
    int n = 0;
    for (int i = 0; i < s_kv_n; i++) {
        const char *p = s_kv[i].k;
        while (*p && n < 1790) buf[n++] = *p++;
        if (n < 1790) buf[n++] = '=';
        p = s_kv[i].v;
        while (*p && n < 1790) buf[n++] = *p++;
        if (n < 1790) buf[n++] = '\n';
    }
    void *h = g_asi->fs->create(0, "settings.cfg", 0);
    if (h) g_asi->fs->write(h, buf, (uint32_t)n);
}


void settings_apply_runtime(void) {
    g_shell_dirty = 3;
}

/* shell_rebuild'i bir sonraki ana dongu turune erteler. Callback sirasinda
 * cagrilirsa widget kendi olay islenirken yok olur (LVGL icinde yuruyen
 * dongu bayat isaretciler uzerinde devam eder); timer ana dongunun
 * lv_timer_handler'inda, olay dispatchi bitmisken calisir. */
static lv_timer_t *s_rebuild_timer;
static void s_rebuild_timer_cb(lv_timer_t *t) {
    s_rebuild_timer = 0;
    lv_timer_delete(t);
    shell_rebuild();
}
static void shell_rebuild_soon(void) {
    if (s_rebuild_timer) return;   /* zaten sirada */
    s_rebuild_timer = lv_timer_create(s_rebuild_timer_cb, 1, 0);
}


void settings_set_int(const char *key, int val) {
    char b[16];
    /* 0.7: gecikme 0..30 tam sayidir; 31 yazilmaz.      */
    if (s_eq(key, "update_delay_days")) {
        if (val < 0) val = 0;
        if (val > 30) val = 30;
    }
    itoa_buf(val, b, sizeof(b));
    kv_set(key, b);
    settings_apply_values();

    /* Erisilebilirlik tercihleri: aninda uygula + kabugu yeniden kur.
     * Yaz olcegi/disgorunum herkesi etkiledigi icin tum kabuk tazelenir
     * (cozunurluk degisikligindeki akis ile ayni). */
    int rebuild = 0;
    if (s_eq(key, "fontscale")) { adl_set_font_scale(val); adl_font_apply_scale(); rebuild = 1; }
    else if (s_eq(key, "contrast"))    { adl_set_contrast(val); rebuild = 1; }
    else if (s_eq(key, "light"))       { adl_set_light(val); rebuild = 1; }
    else if (s_eq(key, "focusring"))   { adl_set_focus_ring(val); rebuild = 1; }
    else if (s_eq(key, "bigicons"))    { g_icon_big = val ? 1 : 0; ui_scale_refresh(); rebuild = 1; }
    else if (s_eq(key, "ws_count")) {
        /* 0.6: alan sayisi degisince gorev cubugundaki noktalar ve etkin
         * alan hemen guncellenir (yeniden baslatma gerekmez). */
        int cnt = val;
        if (cnt < 1) cnt = 1;
        if (cnt > 4) cnt = 4;
        if (g_ws >= cnt) { g_ws = cnt - 1; workspace_apply(); }
        tb_ws_refresh();
    }
    else if (s_eq(key, "kbdlayout")) {
        /* 0.6: klavye duzeni tek kaynaktan ("kbd" dizesi) yonetilir. */
        static const char *codes[3] = { "trq", "trf", "enq" };
        int i = (val < 0 || val > 2) ? 0 : val;
        kv_set("kbd", codes[i]);
        if (g_asi && g_asi->input && g_asi->input->kbd_layout)
            g_asi->input->kbd_layout(i);
    }

    settings_apply_runtime();
    if (rebuild) shell_rebuild_soon();
}


void settings_accent_set(int i) {
    if (i < 0 || i >= g_accent_n) return;
    char b[8];
    itoa_buf(i, b, sizeof(b));
    kv_set("accent", b);
    settings_apply_values();
    settings_apply_runtime();
}


int settings_accent_count(void) { return g_accent_n; }

const char *settings_accent_name(int i) { return (i >= 0 && i < g_accent_n) ? g_accents[i].name : ""; }

uint32_t settings_accent_color(int i) { return (i >= 0 && i < g_accent_n) ? g_accents[i].a : 0; }

uint32_t settings_accent_color2(int i) { return (i >= 0 && i < g_accent_n) ? g_accents[i].a2 : 0; }

int settings_accent_current(void) { return g_accent_idx; }

int settings_get_int(const char *key, int def) {
    /* 0.6: klavye duzeni tek kaynaktan ("kbd" dizesi) turetilir. */
    if (s_eq(key, "kbdlayout")) {
        const char *k = kv_get("kbd", 0);
        if (!k) return def;
        return (k[0] == 'e') ? 2 : (k[2] == 'f' ? 1 : 0);
    }
    return kv_get_int(key, def);
}


void shell_rebuild(void) {
    ui_scale_refresh();

    for (int i = 0; i < MAX_WINS; i++) {
        if (g_recs[i] && g_recs[i]->win) win_close_rec(g_recs[i]);
        g_recs[i] = 0;   /* kapanan kayit win_close_rec'te serbest listeye gecti */
    }

    lv_obj_clean(lv_screen_active());

    g_start_menu = 0; g_taskbar = 0; g_wallpaper = 0;
    tb_reset_objects();   /* gorev cubugu statik isaretcileri (0.6) */
    g_shell_gen++;   /* asistan gibi ust katman panelleri yeniden kurulur */
    g_icon_layer = 0; g_dicon_count = 0; g_sel_count = 0;
    g_tb_clock = 0; g_date_label = 0; g_clock_label = 0; g_power_label = 0;
    g_tb_logo = 0; g_tb_search = 0; g_tb_sep1 = 0; g_tb_sep2 = 0; g_tb_sep3 = 0;
    g_tb_ai = 0;
    g_tb_apps_cont = 0; g_tb_status = 0; g_tb_menu = 0;
    g_net_panel = 0; g_net_body = 0; g_bat_panel = 0; g_cal_overlay = 0;
    g_rubber = 0; g_context_menu = 0; g_dialog = 0;
    g_launch_overlay = 0; g_launch_win = 0; g_launch_ctx = 0;
    for (int i = 0; i < TB_MAX_ICONS; i++) g_tb[i].btn = 0;
    g_tb_n = 0; g_modal = 0;

    build_wallpaper();
    build_cursor();
    build_desktop_icons();
    build_start_menu(lv_screen_active());
    build_taskbar(lv_screen_active());
    if (g_taskbar && kv_get_int("safe_mode", 0))
        lv_obj_set_style_blur_backdrop(g_taskbar, false, 0);
    assistant_init();   /* panel yeniden kurulur */
    update_app_indicators();
    qc_reinit();   /* hizli ayarlar/OSD/perde katmanlari yeniden kurulur */
}


void shell_apply_resolution(int idx) {
    if (idx < 0 || idx >= RES_OPT_N) return;
    if (!g_asi || !g_asi->gfx || !g_asi->gfx->set_mode) return;
    int ow = g_asi->gfx->width(), oh = g_asi->gfx->height();
    if (g_asi->gfx->set_mode(g_res_opts[idx].w, g_res_opts[idx].h) != 0) return;
    int aw = g_asi->gfx->width(), ah = g_asi->gfx->height();
    if (aw == ow && ah == oh) return;
    lv_port_set_resolution(aw, ah);
    shell_rebuild();
}


void boot_apply_saved_resolution(void) {
    int idx = kv_get_int("res", RES_DEFAULT_IDX);
    if (idx < 0 || idx >= RES_OPT_N) idx = RES_DEFAULT_IDX;
    if (!g_asi || !g_asi->gfx || !g_asi->gfx->set_mode) return;
    if (g_res_opts[idx].w == g_asi->gfx->width() &&
        g_res_opts[idx].h == g_asi->gfx->height()) return;
    if (g_asi->gfx->set_mode(g_res_opts[idx].w, g_res_opts[idx].h) != 0) return;
    lv_port_set_resolution(g_asi->gfx->width(), g_asi->gfx->height());
    lv_obj_clean(lv_screen_active());
    build_cursor();
}


int settings_resolution_count(void) { return RES_OPT_N; }

const char *settings_resolution_name(int i) {
    return (i >= 0 && i < RES_OPT_N) ? g_res_opts[i].label : "";
}

int settings_resolution_current(void) {
    int w = g_asi->gfx->width(), h = g_asi->gfx->height();
    for (int i = 0; i < RES_OPT_N; i++)
        if (g_res_opts[i].w == w && g_res_opts[i].h == h) return i;
    return -1;
}

int settings_resolution_set(int i) {
    if (i < 0 || i >= RES_OPT_N) return -1;
    char b[8];
    itoa_buf(i, b, sizeof(b));
    kv_set("res", b);
    s_res_pending = i;
    return 0;
}

