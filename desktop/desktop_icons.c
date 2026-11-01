#include "dsk.h"


int has_ext(const char *name, char ext[], int extsz) {
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


int app_handles_ext(int i, const char *ext) {
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


void icon_apply_pos(int i) {
    dicon_t *d = &g_dicons[i];
    if (!d->cell) return;
    lv_obj_set_pos(d->cell, GRID_X + d->gx * ICON_W, GRID_Y + d->gy * ICON_H);
}

void icon_set_sel(int i, int sel) {
    dicon_t *d = &g_dicons[i];
    d->selected = sel;
    if (!d->cell) return;
    lv_obj_set_style_bg_opa(d->cell, sel ? 45 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(d->cell, sel ? 255 : LV_OPA_TRANSP, 0);
}

void clear_selection(void) {
    for (int i = 0; i < g_dicon_count; i++) icon_set_sel(i, 0);
    g_sel_count = 0;
}


/* Klavye gezinme: Enter ile odaktaki simgeyi ac      */
static void icon_key_cb(lv_event_t *e) {
    if (lv_event_get_key(e) != LV_KEY_ENTER) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < g_dicon_count && g_dicons[idx].cell) open_dicon(idx);
}


void icon_build(int i) {
    dicon_t *d = &g_dicons[i];
    lv_obj_t *c = lv_obj_create(g_icon_layer);
    lv_obj_remove_style_all(c);
    /* remove_style_all tema odak halkasi stilini de siler; geri ekle      */
    adl_focus_ring_attach(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(c, ICON_W - ic_s(8), ICON_H - ic_s(8));
    lv_obj_set_style_radius(c, ic_s(14), 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_border_width(c, ic_s(1), 0);
    lv_obj_set_style_border_color(c, lv_color_hex(ADL_ACCENT), 0);

    lv_obj_t *ic = d->app ? make_app_icon(c, d->app, ic_s(88)) : make_icon(c, d->svg, ic_s(88));
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, ic_s(14));

    lv_obj_t *lab = label(c, d->name, ADL_TEXT);
    if (!s_f_dlabel) s_f_dlabel = lv_tiny_ttf_create_data(font_ttf, font_ttf_len, ic_s(22));
    if (s_f_dlabel) lv_tiny_ttf_set_size(s_f_dlabel, ic_s(22));
    lv_obj_set_style_text_font(lab, s_f_dlabel ? s_f_dlabel : adl_font(), 0);
    lv_obj_set_width(lab, ICON_W - ic_s(4));
    lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
    lv_obj_align(lab, LV_ALIGN_BOTTOM_MID, 0, -ic_s(6));

    d->cell = c;
    icon_set_sel(i, 0);
    icon_apply_pos(i);

    /* Klavye ile tam gezinme: simge masaustu grubuna eklenir.
     * (lv_obj_delete grubu otomatik temizler; yeniden kurulumda tekrar eklenir.) */
    if (g_desktop_group) {
        lv_group_add_obj(g_desktop_group, c);
        lv_obj_add_event_cb(c, icon_key_cb, LV_EVENT_KEY, (void *)(intptr_t)i);
    }
}


void dicon_add(const char *name, int is_file, int app_idx, const char *svg, const arctian_app_t *app) {
    if (g_dicon_count >= MAX_DICONS) return;
    dicon_t *d = &g_dicons[g_dicon_count];
    scopy(d->name, name, 47);
    d->is_file = is_file; d->app_idx = app_idx; d->svg = svg; d->app = app;
    d->gx = 0; d->gy = g_dicon_count;
    g_dicon_count++;
    icon_build(g_dicon_count - 1);
}


int has_suffix_ci(const char *nm, const char *ext) {
    int n = slen2(nm), l = slen2(ext);
    if (n < l) return 0;
    for (int i = 0; i < l; i++)
        if (chr_low(nm[n - l + i]) != chr_low(ext[i])) return 0;
    return 1;
}


/* AFS'te masaustunde dosya olarak GOSTERILMEYECEK ozel dosyalar.
 * C:/ kullanici diski agaci (klasorler ve dosyalar) de gizlenir; bunlar
 * Dosya Yoneticisi ile gezilir. */
int afs_name_is_hidden(const char *nm) {
    if (!nm) return 1;
    for (int i = 0; nm[i]; i++) if (nm[i] == '/') return 1;
    return has_suffix_ci(nm, ".dpk") || has_suffix_ci(nm, ".wp") ||
           has_suffix_ci(nm, ".cfg") || has_suffix_ci(nm, ".log");
}


/* Masaustunde yalnizca Dosya Yoneticisi + Cop Kovasi + kullanici
 * kisayollari (baslat menususu sag tik ile eklenenler) gosterilir. */
void dicons_fill(void) {
    dshort_load();

    int fi = app_idx_by_name("filemanager");
    if (fi >= 0) {
        const arctian_app_t *a = app_get(fi);
        if (a) dicon_add(a->title, 0, fi, a->icon_svg ? a->icon_svg : "", a);
    }
    int ti = app_idx_by_name("trash");
    if (ti >= 0) {
        const arctian_app_t *a = app_get(ti);
        if (a) dicon_add(a->title, 0, ti, a->icon_svg ? a->icon_svg : "", a);
    }
    for (int i = 0; i < g_dshort_n; i++) {
        int ai = app_idx_by_name(g_dshort[i]);
        if (ai < 0 || ai == fi || ai == ti) continue;
        const arctian_app_t *a = app_get(ai);
        if (a) dicon_add(a->title, 0, ai, a->icon_svg ? a->icon_svg : "", a);
    }

    /* Kalici depodaki (AFS) gercek dosyalar masaustunde gorunur. Uygulama
     * paketleri (.dpk), arka planlar (.wp) ve ayar dosyalari (.cfg) gizlenir. */
    if (g_asi->fs && g_asi->fs->count && g_asi->fs->name) {
        int fc = g_asi->fs->count();
        for (int i = 0; i < fc && g_dicon_count < MAX_DICONS; i++) {
            const char *nm = g_asi->fs->name(i);
            if (nm && nm[0] && !afs_name_is_hidden(nm)) dicon_add(nm, 1, -1, SVG_FILE, 0);
        }
    }

    int H = g_asi->gfx->height();
    int rows = (H - GRID_Y - ui_s(130)) / ICON_H; if (rows < 1) rows = 1;
    for (int i = 0; i < g_dicon_count; i++) {
        g_dicons[i].gx = i / rows;
        g_dicons[i].gy = i % rows;
        icon_apply_pos(i);
    }
}


void build_desktop_icons(void) {
    g_icon_layer = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(g_icon_layer);
    lv_obj_set_size(g_icon_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(g_icon_layer, 0, 0);
    lv_obj_remove_flag(g_icon_layer, LV_OBJ_FLAG_SCROLLABLE);

    if (!g_opt_dicons) return;
    dicons_fill();
}


void dicons_rebuild(void) {
    for (int i = 0; i < g_dicon_count; i++) {
        if (g_dicons[i].cell) { lv_obj_delete(g_dicons[i].cell); g_dicons[i].cell = 0; }
    }
    g_dicon_count = 0;
    g_sel_count = 0;
    if (g_icon_layer && g_opt_dicons) dicons_fill();
}


int icon_hit(int x, int y, int *out) {
    for (int i = 0; i < g_dicon_count; i++) {
        lv_obj_t *c = g_dicons[i].cell; if (!c) continue;
        int cx = lv_obj_get_x(c), cy = lv_obj_get_y(c);
        int w = lv_obj_get_width(c), h = lv_obj_get_height(c);
        if (x >= cx && x < cx + w && y >= cy && y < cy + h) { if (out) *out = i; return 1; }
    }
    return 0;
}


int rect_at(lv_obj_t *o, int x, int y) {
    if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return 0;
    int ox = lv_obj_get_x(o), oy = lv_obj_get_y(o);
    int w = lv_obj_get_width(o), h = lv_obj_get_height(o);
    return (x >= ox && x < ox + w && y >= oy && y < oy + h);
}

int point_over_ui(int x, int y) {
    if (g_modal) return 1;
    if (g_net_panel) return 1;
    if (window_at(x, y)) return 1;
    if (g_taskbar && rect_at(g_taskbar, x, y)) return 1;
    if (g_start_menu && !lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN) && rect_at(g_start_menu, x, y)) return 1;
    return 0;
}


void open_arcapp_file(const char *file) {
    lv_obj_t *w = create_window("ArcApp", 520, 320);
    lv_obj_t *t = label(w, "ArcApp Goruntuleyici", ADL_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t *b = label(w, file, ADL_TEXT_DIM);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 44);
}


void dialog_close(void) {
    if (g_dialog) { lv_obj_delete(g_dialog); g_dialog = 0; }
    g_modal = 0;
}

void dialog_cancel_cb(lv_event_t *e) { (void)e; dialog_close(); }

void dialog_pick_cb(lv_event_t *e) {
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


void open_with_dialog(int idx) {
    dicon_t *d = &g_dicons[idx];
    char ext[16];
    int isfile = d->is_file && has_ext(d->name, ext, sizeof(ext));
    scopy(g_ow_file, d->name, 47);

    int opts[16]; int n = 0;
    if (isfile) {
        int total = app_total();
        for (int i = 0; i < total && n < 15; i++) {
            /* 0.7: yalnizca uzantiyi ilan eden (.wlf) uygulamalar listelenir. */
            if (app_handles_ext(i, ext)) opts[n++] = i;
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


/* ---- Sag tik menusu ---- */
void menu_close(void) { if (g_context_menu) { lv_obj_delete(g_context_menu); g_context_menu = 0; } g_modal = 0; }


void menu_delete_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < g_dicon_count && g_dicons[idx].cell) {
        dicon_t *d = &g_dicons[idx];
        if (d->is_file) {
            /* Dosyanin kendisi de silinir: yalniz widget silinirse simge
             * sonraki yeniden kurmada depodan yeniden listelenip geri
             * geliyordu. */
            if (g_asi && g_asi->fs && g_asi->fs->remove)
                g_asi->fs->remove(d->name);
        } else if (d->app && dshort_find(d->app->name) >= 0) {
            /* Kisayol: kalici listeden cikar; dshort_toggle yeniden
             * kurmayi da kendisi yapar. Sabit simgeler (Dosya Yoneticisi,
             * Cop Kovasi) listede degildir; onlar silinemez, ekranda kalir. */
            dshort_toggle(d->app->name);
            menu_close();
            return;
        }
        /* Diger durumlarda da tablo yeniden kurulur: tek hucreyi silmek
         * diger butonlarin user_data indekslerini kaydirirdi. */
        dicons_rebuild();
    }
    menu_close();
}

void menu_openwith_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    menu_close();
    open_with_dialog(idx);
}


void open_context_menu(int x, int y, int idx) {
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


/* ---- Cift tik acma ---- */
static void set_open_target(const char *path);   /* asagida tanimli */

void open_dicon(int idx) {
    dicon_t *d = &g_dicons[idx];
    if (!d->is_file) { open_registry_app(d->app_idx); return; }

    char ext[16];
    if (!has_ext(d->name, ext, sizeof(ext))) { open_arcapp_file(d->name); return; }

    /* 0.7: uzantiyi isleyen .wlf varsa dogrudan onunla ac. Birden fazla
     * aday varsa "Birlikte ac" secimi gosterilir. */
    int first = -1, matches = 0;
    int total = app_total();
    for (int i = 0; i < total; i++) {
        if (app_handles_ext(i, ext)) {
            if (first < 0) first = i;
            matches++;
        }
    }

    if (matches == 0) { arctian_shell_open_file(d->name); return; }
    if (matches == 1) {
        set_open_target(d->name);
        open_registry_app(first);
        return;
    }
    open_with_dialog(idx);
}


/* ---- Uzanti -> uygulama eslemesi (Dosya deneyimi) --------- */

static int ext_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

/* Metin ailesi -> Editor */
static int ext_is_text(const char *e) {
    static const char *const T[] = {
        "txt","md","c","h","cpp","hpp","cc","js","mjs","py","json","cfg","ini",
        "log","sh","bash","css","scss","html","htm","xml","csv","rs","toml",
        "yaml","yml","conf","desktop","asm","s","lua","sql","text"
    };
    for (int i = 0; i < (int)(sizeof(T) / sizeof(T[0])); i++)
        if (ext_eq(e, T[i])) return 1;
    return 0;
}

static int ext_is_image(const char *e) {   /* png -> kutu goruntuleyici */
    return ext_eq(e, "png");
}

static int ext_is_audio(const char *e) {   /* -> Medya oynatici */
    return ext_eq(e, "wav") || ext_eq(e, "mp3") ||
           ext_eq(e, "ogg") || ext_eq(e, "flac");
}

/* PNG goruntuleyici: dosyayi okur, ekrana sigdirir, pencerede gosterir. */
static void img_viewer_open(const char *path) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->find || !g_asi->fs->read) {
        open_arcapp_file(path); return;
    }
    void *h = g_asi->fs->find(0, path);
    if (!h) { open_arcapp_file(path); return; }
    uint32_t sz = 0;
    if (g_asi->fs->stat_handle) {
        asi_stat_t st;
        if (g_asi->fs->stat_handle(h, &st) == 0) sz = st.size;
    }
    if (sz == 0 || sz > 4u * 1024u * 1024u) { open_arcapp_file(path); return; }
    unsigned char *data = (unsigned char *)g_asi->mem->alloc(sz);
    if (!data) { open_arcapp_file(path); return; }
    uint32_t n = g_asi->fs->read(h, data, sz);
    uint32_t *px = 0; int w = 0, hh = 0;
    if (png_decode_argb(data, n ? n : sz, g_asi->mem->alloc, g_asi->mem->free,
                        &px, &w, &hh) != 0 || !px || w < 1 || hh < 1) {
        g_asi->mem->free(data);
        arctian_notify("Goruntu", "PNG cozulemedi");
        return;
    }
    g_asi->mem->free(data);

    /* ekrana sigdir (baslik cubugu + pay icin alan) */
    int maxw = (g_asi->gfx && g_asi->gfx->width) ? g_asi->gfx->width() - 80 : 900;
    int maxh = (g_asi->gfx && g_asi->gfx->height) ? g_asi->gfx->height() - 220 : 700;
    int dw = w, dh = hh;
    if (dw > maxw || dh > maxh) {
        if (dw * maxh > dh * maxw) { dh = dh * maxw / dw; dw = maxw; }
        else { dw = dw * maxh / dh; dh = maxh; }
        if (dw < 1) dw = 1; if (dh < 1) dh = 1;
    }
    uint32_t *out = px;
    if (dw != w || dh != hh) {
        uint32_t *s = (uint32_t *)g_asi->mem->alloc((size_t)dw * (size_t)dh * 4u);
        if (s) {
            for (int y = 0; y < dh; y++) {
                int sy = (int)((long)y * hh / dh); if (sy >= hh) sy = hh - 1;
                for (int x = 0; x < dw; x++) {
                    int sx = (int)((long)x * w / dw); if (sx >= w) sx = w - 1;
                    s[(long)y * dw + x] = px[(long)sy * w + sx];
                }
            }
            g_asi->mem->free(px);
            out = s;
        } else { dw = w; dh = hh; }
    }

    char nm[48];
    int li = 0;
    for (int i = 0; path[i] && i < 47; i++) nm[i] = path[i], li = i + 1;
    nm[li] = 0;

    lv_obj_t *win = create_window("Goruntu", dw + 24, dh + 60);
    if (!win) { g_asi->mem->free(out); return; }
    lv_obj_t *c = lv_canvas_create(win);
    lv_obj_remove_style_all(c);
    lv_canvas_set_buffer(c, out, dw, dh, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(c, 12, 48);
    lv_obj_add_event_cb(c, canvas_buf_free_cb, LV_EVENT_DELETE, out);
}


/* Dosya hedefi tamponu: Medya/DuckStore uygulamalari arctian_open_target
 * uzerinden hedef dosyayi okur (ikili esleme). */
static char s_open_tgt_buf[200];
static void set_open_target(const char *path) {
    int j = 0;
    for (; path[j] && j < (int)sizeof(s_open_tgt_buf) - 1; j++)
        s_open_tgt_buf[j] = path[j];
    s_open_tgt_buf[j] = 0;
    arctian_open_target = s_open_tgt_buf;
}

/* Dosyayi uzantisina gore uygun uygulamada ac (ikili esleme).
 * Her zaman 1 doner; bilinmeyen tur icin ArcApp onizlemesi acilir. */
int arctian_shell_open_file(const char *path) {
    if (!path || !path[0]) return 1;

    char ext[16];
    if (!has_ext(path, ext, sizeof(ext))) { open_arcapp_file(path); return 1; }

    /* 0) 0.7: uygulama paketi. .wlf -> kur ve rolune gore calistir.
     *    .dpk 0.7'de kaldirildi; sessizce yok saymak yerine acik soyle. */
    if (ext_eq(ext, "wlf")) {
        char full[176];
        int k = 0;
        int abs = 0;
        for (int i = 0; path[i]; i++) if (path[i] == ':') { abs = 1; break; }
        if (!abs) { full[k++] = 'C'; full[k++] = ':'; full[k++] = '/'; }
        for (int i = 0; path[i] && k < (int)sizeof(full) - 1; i++) full[k++] = path[i];
        full[k] = 0;

        char err[96];
        if (wlf_install_file(full, err, sizeof(err)) != 0) {
            show_error_window("Uygulama kurulamadi", err[0] ? err : "Gecersiz .wlf");
            return 1;
        }
        /* yeni kurulan uygulamayi bul: dosya adi (<id>.wlf) = uygulama adi */
        char id[40];
        int j = 0, n = slen2(full);
        int start = 0;
        for (int i = 0; i < n; i++) if (full[i] == '/') start = i + 1;
        for (int i = start; i < n - 4 && j < (int)sizeof(id) - 1; i++) id[j++] = full[i];
        id[j] = 0;
        int ai = app_idx_by_name(id);
        if (ai >= 0) open_registry_app(ai);
        else arctian_term_run("wf list");
        return 1;
    }
    if (ext_eq(ext, "dpk")) {
        show_error_window("Desteklenmeyen paket",
                          "0.7'de uygulamalar yalnizca .wlf bicimindedir. "
                          "Eski .dpk paketleri kurulamaz.");
        return 1;
    }

    /* 1) Kurulu .wlf uygulamalari once: uzantiyi handles ile ilan ettilerse.
     * 0.7: rol=cli/runtime uygulamalar da buraya girer; terminal acilir. */
    int total = app_total();
    for (int i = 0; i < total; i++) {
        if (app_handles_ext(i, ext)) {
            set_open_target(path);
            open_registry_app(i);
            return 1;
        }
    }

    /* 2) Yerlesik esleme tablosu */
    if (ext_is_text(ext)) { arctian_shell_editor(path); return 1; }
    if (ext_is_image(ext)) { img_viewer_open(path); return 1; }
    if (ext_is_audio(ext)) {
        set_open_target(path);
        arctian_shell_launch("medya");
        return 1;
    }

    /* 3) Bilinmeyen tur: ArcApp dosya onizlemesi */
    open_arcapp_file(path);
    return 1;
}


/* (x,y) masaustu mu? (gorev cubugu/menuler/pencereler disi) - surukle-birak    */
int arctian_shell_over_desktop(int x, int y) {
    return point_over_ui(x, y) ? 0 : 1;
}


/* ---- Lastik bant (rubber band) ---- */
void rubber_show(int x0, int y0, int x1, int y1) {
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

void rubber_hide(void) { if (g_rubber) lv_obj_add_flag(g_rubber, LV_OBJ_FLAG_HIDDEN); }


void poll_icons(void) {
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
                    /* Ayni simgeye ikinci tik: cift tik -> ac */
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
                /* surukle-birak: dosya pencereye birakildiysa kopyala
                 * (pencere kabul ederse dosya oraya kopyalanir; masaustu
                 * ikonu yerinde kalir) */
                if (d->is_file && !arctian_shell_over_desktop(mx, my)) {
                    winrec_t *r = win_at(mx, my);
                    if (r && r->drop) r->drop(d->name);
                }
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

