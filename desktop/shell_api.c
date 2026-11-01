#include "dsk.h"

static char s_clip[4096];

/* Pano kaliciligi: pano C:/etc/clip.txt'e yazilir, acilista geri yuklenir.
 * Onceden yalniz RAM'deydi ve oturum kapaninca kayboluyordu. */
#define CLIP_FILE "C:/etc/clip.txt"

static void clip_persist(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in || !g_asi->fs->write) return;
    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/etc");
    void *h = g_asi->fs->create_in("C:/etc", "clip.txt", 0);
    if (!h) return;
    uint32_t n = 0;
    while (s_clip[n]) n++;
    if (n) g_asi->fs->write(h, s_clip, n);
}

static void clip_load_once(void) {
    static int done;
    if (done) return;
    done = 1;
    if (!g_asi || !g_asi->fs || !g_asi->fs->read_path) return;
    uint32_t n = g_asi->fs->read_path(CLIP_FILE, s_clip, sizeof(s_clip) - 1);
    if (n < sizeof(s_clip)) s_clip[n] = 0;
    else s_clip[sizeof(s_clip) - 1] = 0;
}

void arctian_clipboard_set(const char *s) {
    int i = 0;
    if (!s) { s_clip[0] = 0; clip_persist(); return; }
    for (; s[i] && i < (int)sizeof(s_clip) - 1; i++) s_clip[i] = s[i];
    s_clip[i] = 0;
    clip_persist();
}

const char *arctian_clipboard_get(void) { clip_load_once(); return s_clip; }

int arctian_clipboard_len(void) {
    clip_load_once();
    int n = 0;
    while (s_clip[n]) n++;
    return n;
}

void arctian_clipboard_clear(void) { s_clip[0] = 0; clip_persist(); }

/* ---- Cop Kutusu: silinen dosya once C:/Cop'a tasinir ----
 * Dosya Yoneticisi ve masaustu silme akisi bu tek noktayi kullanir; boylece
 * Cop Kutusu uygulamasi gercekten dolu bir kutu gorur ve geri alinabilir.
 * Donus: 0 = cpe tasindi, -1 = tasinamadi, -2 = dosya cok buyuk. */
#define TRASH_DIR     "C:/Cop"
#define TRASH_BUF_SZ  (128u * 1024u)

int arctian_shell_trash(const char *path) {
    if (!g_asi || !g_asi->fs || !path || !path[0]) return -1;
    asi_fs_t *fs = g_asi->fs;
    if (!fs->read_path || !fs->create_in || !fs->write || !fs->remove ||
        !fs->find_in || !fs->stat) return -1;
    if (fs->mkdir) fs->mkdir(TRASH_DIR);

    /* yolun son bileseni */
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    if (!base[0]) return -1;

    /* boyut siniri: tampon tek seferde okunur; dizinler kopyalanamaz,
     * cagiran taraf kendi yoluna dussun (dosya yoneticisi dogrudan siler). */
    asi_stat_t st;
    if (fs->stat(path, &st) != 0 || st.is_dir || st.size > TRASH_BUF_SZ) return -2;

    /* ad catismasi: "ad (2).ext" */
    char cand[80];
    int bl = 0;
    while (base[bl] && bl < 40) bl++;
    int dot = -1;
    for (int i = 0; i < bl; i++) if (base[i] == '.') dot = i;
    for (int k = 1; k < 1000; k++) {
        int o = 0;
        for (int i = 0; i < bl; i++) cand[o++] = base[i];
        cand[o] = 0;
        if (k > 1) {
            /* (k) ekle: ad + " (k)" + uzanti */
            o = dot > 0 ? dot : bl;
            cand[o] = 0;
            char suf[12];
            int s = 0;
            suf[s++] = ' ';
            suf[s++] = '(';
            if (k >= 100) suf[s++] = (char)('0' + k / 100);
            if (k >= 10)  suf[s++] = (char)('0' + (k / 10) % 10);
            suf[s++] = (char)('0' + k % 10);
            suf[s++] = ')';
            int p = o;
            for (int i = 0; i < s && p < (int)sizeof(cand) - 1; i++) cand[p++] = suf[i];
            if (dot > 0)
                for (int i = dot; base[i] && p < (int)sizeof(cand) - 1; i++) cand[p++] = base[i];
            cand[p] = 0;
        }
        if (!fs->find_in(TRASH_DIR, cand)) break;
        if (k == 999) return -1;
    }

    uint8_t *buf = (uint8_t *)g_asi->mem->alloc(TRASH_BUF_SZ);
    if (!buf) return -1;
    uint32_t n = fs->read_path(path, buf, TRASH_BUF_SZ);
    void *h = fs->create_in(TRASH_DIR, cand, 0);
    if (!h) { g_asi->mem->free(buf); return -1; }
    int w = fs->write(h, buf, n);
    g_asi->mem->free(buf);
    if (w != 0) {
        /* yarim kalan kopyayi temizle; tam yoldan silelim, cand yalinz
         * ad tasir ve CWD gecerli alana gore cozulup baska bir dosyayi
         * silebilirdi. */
        char full[160];
        int o = 0;
        const char *d = TRASH_DIR "/";
        while (*d && o < (int)sizeof(full) - 1) full[o++] = *d++;
        for (int i = 0; cand[i] && o < (int)sizeof(full) - 1; i++) full[o++] = cand[i];
        full[o] = 0;
        fs->remove(full);
        return -1;
    }
    return (fs->remove(path) == 0) ? 0 : -1;
}

static void toast_del_cb(lv_timer_t *t) {
    lv_obj_t *o = (lv_obj_t *)lv_timer_get_user_data(t);
    if (o) lv_obj_delete(o);
    lv_timer_delete(t);
}

void arctian_notify(const char *title, const char *msg) {
    qc_notify_add(title, msg);   /* bildirim merkezi yigini */
    lv_obj_t *box = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, 320, 84);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(box, 245, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_shadow_width(box, 18, 0);
    lv_obj_set_style_shadow_color(box, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(box, 120, 0);
    lv_obj_align(box, LV_ALIGN_BOTTOM_RIGHT, -24, -(TB_BH + TB_Y_OFF + 16));

    lv_obj_t *t1 = label(box, title ? title : "Bildirim", ADL_TEXT);
    lv_obj_set_pos(t1, 14, 12);
    lv_obj_t *t2 = label(box, msg ? msg : "", ADL_TEXT_DIM);
    lv_obj_set_pos(t2, 14, 40);
    lv_obj_set_width(t2, 290);
    lv_label_set_long_mode(t2, LV_LABEL_LONG_DOT);

    lv_obj_move_foreground(box);
    lv_timer_t *tm = lv_timer_create(toast_del_cb, 4000, box);
    lv_timer_set_repeat_count(tm, 1);
}


void arctian_shell_network_panel(void) { open_network_panel(); }

void arctian_shell_power_dialog(void) { open_power_dialog(); }

void arctian_shell_lock_screen(void) { g_lock_request = 1; }

/* Ctrl/Shift anlik durumu (coklu secim). Sürücu yoksa guvenli 0.    */
int arctian_shell_ctrl_down(void) {
    return (g_asi && g_asi->input && g_asi->input->ctrl_down)
           ? g_asi->input->ctrl_down() : 0;
}

int arctian_shell_shift_down(void) {
    return (g_asi && g_asi->input && g_asi->input->shift_down)
           ? g_asi->input->shift_down() : 0;
}


void lock_hide_desktop(int hide) {
    lv_obj_t *objs[4 + MAX_WINS];
    int n = 0;
    if (g_icon_layer) objs[n++] = g_icon_layer;
    if (g_taskbar) objs[n++] = g_taskbar;
    if (g_start_menu) objs[n++] = g_start_menu;
    for (int i = 0; i < MAX_WINS; i++)
        if (g_recs[i] && g_recs[i]->win) objs[n++] = g_recs[i]->win;
    for (int i = 0; i < n; i++) {
        if (hide) lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        else if (!(g_start_menu && objs[i] == g_start_menu)) lv_obj_remove_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
    }
}


void rebuild_desktop_ui(void) {
    if (g_icon_layer) { lv_obj_delete(g_icon_layer); g_icon_layer = 0; }
    g_dicon_count = 0;
    build_desktop_icons();
    /* Ikon katmani duvar kagidinin HEMEN ustune tasinir (diger her seyin
     * altinda kalir). Sabit indek kullanilamaz: rebuild_shell_ui() sonunda
     * qc_reinit() s_dim/s_night perdelerini lv_obj_move_background() ile 0/1'e
     * alir ve duvar kagidi indek 2'ye iner; sabit indeksli ikon katmani o
     * durumda kagidin ALTINDA kalip masaustu ikonlari gorunmez oluyordu. */
    if (g_icon_layer) {
        int wi = 0;
        if (g_wallpaper && lv_obj_get_parent(g_wallpaper) == lv_screen_active())
            wi = lv_obj_get_index(g_wallpaper);
        lv_obj_move_to_index(g_icon_layer, wi + 1);
    }
}


void rebuild_menu_ui(void) {
    if (g_start_menu) { lv_obj_delete(g_start_menu); g_start_menu = 0; }
    g_clock_last_sec = -1;
    build_start_menu(lv_screen_active());
    for (int i = 0; i < MAX_WINS; i++)
        if (g_recs[i] && g_recs[i]->win) lv_obj_move_foreground(g_recs[i]->win);
}


void rebuild_shell_ui(void) {
    g_shell_dirty = 3;
}


int shell_match(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}


int arctian_shell_app_count(void) { return app_total(); }


int arctian_shell_app_info(int i, char *title, int tcap,
                           char *name, int ncap, char *cat, int ccap) {
    const arctian_app_t *a = app_get(i);
    if (!a) return -1;
    if (title && tcap > 0) bin_copy(title, tcap, a->title);
    if (name && ncap > 0) bin_copy(name, ncap, a->name);
    if (cat && ccap > 0) bin_copy(cat, ccap, a->category);
    return 0;
}


int arctian_shell_launch(const char *q) {
    if (!q || !q[0]) return -1;
    int total = app_total();
    for (int i = 0; i < total; i++) {
        const arctian_app_t *a = app_get(i);
        if (!a) continue;
        if (shell_match(a->title, q) || shell_match(a->name, q)) {
            open_registry_app(i);
            return 0;
        }
    }
    return -1;
}


int arctian_shell_close(const char *q) {
    if (!q || !q[0]) return -1;
    char target[48];
    target[0] = 0;
    int total = app_total();
    for (int i = 0; i < total; i++) {
        const arctian_app_t *a = app_get(i);
        if (!a) continue;
        if (shell_match(a->title, q) || shell_match(a->name, q)) {
            bin_copy(target, sizeof(target), a->title);
            break;
        }
    }
    if (!target[0]) bin_copy(target, sizeof(target), q);
    for (int j = 0; j < MAX_WINS; j++) {
        winrec_t *r = g_recs[j];
        if (!r || !r->win || !shell_match(r->title, target)) continue;
        if (r->btn) lv_obj_delete(r->btn);
        lv_obj_delete(r->win);
        r->btn = 0; r->win = 0;
        update_app_indicators();
        return 0;
    }
    return -1;
}


int arctian_shell_window_count(void) {
    int n = 0;
    for (int j = 0; j < MAX_WINS; j++) if (g_recs[j] && g_recs[j]->win) n++;
    return n;
}


int arctian_shell_window_info(int i, char *title, int tcap, int *visible) {
    int n = 0;
    for (int j = 0; j < MAX_WINS; j++) {
        winrec_t *r = g_recs[j];
        if (!r || !r->win) continue;
        if (n == i) {
            if (title && tcap > 0) bin_copy(title, tcap, r->title);
            if (visible) *visible = lv_obj_has_flag(r->win, LV_OBJ_FLAG_HIDDEN) ? 0 : 1;
            return 0;
        }
        n++;
    }
    return -1;
}


/* Wolf Engine ayrik yigin (slab) tahsisati icin buyuk arena.
 * Rust tarafi bu blok icinde kendi serbest listelerini yonetir. */
void *arctian_js_arena(unsigned long *out_size) {
    static unsigned char *buf = 0;
    static unsigned long cap = 0;
    if (!buf) {
        unsigned long sz = 64UL * 1024UL * 1024UL;
        void *p = (g_asi && g_asi->mem && g_asi->mem->alloc) ? g_asi->mem->alloc(sz) : 0;
        if (!p) { if (out_size) *out_size = 0; return 0; }
        buf = (unsigned char *)p;
        cap = sz;
    }
    if (out_size) *out_size = cap;
    return buf;
}

