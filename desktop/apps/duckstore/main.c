#include "app.h"
#include "dpk.h"
#include "adl_wolf.h"
#include "svg.h"
#include <stdint.h>

#define DS_MAX_APPS   32
#define DS_MAX_WALLS  24
#define DS_URL_MAX    160
#define DS_ICON_MAX   1200
#define DS_DESC_MAX   220
#define DS_PKG_MAX    160
#define DS_IMG_MAX    160
#define DS_QUERY_MAX  64
#define DS_WP_MAX     (6u * 1024u * 1024u)
#define DS_WP_SLACK   (8u * 1024u)
#define DS_DEFAULT_URL "http://45.143.99.75:8090"

typedef struct {
    char id[32];
    char title[48];
    char category[32];
    char version[16];
    char publisher[32];
    char size[16];
    char desc[DS_DESC_MAX];
    char pkg[DS_PKG_MAX];
    char icon[DS_ICON_MAX];
} ds_app_t;

typedef enum {
    PAGE_HOME = 0,
    PAGE_SEARCH,
    PAGE_CATS,
    PAGE_LIB,
    PAGE_UPDATES,
    PAGE_WALLS,
    PAGE_DETAIL
} ds_page_t;

typedef struct {
    char id[32];
    char title[48];
    char author[32];
    char size[16];
    char img[DS_IMG_MAX];
} ds_wall_t;

static asi_t *s_asi;

static ds_app_t g_apps[DS_MAX_APPS];
static int      g_app_count = 0;
static int      g_feed_ok = 0;

static ds_wall_t g_walls[DS_MAX_WALLS];
static int       g_wall_count = 0;
static int       g_wall_ok = 0;

static char g_inst_wp[DS_MAX_WALLS][40];
static int  g_inst_wp_count = 0;

static uint8_t  *g_dl_buf = 0;
static uint32_t  g_dl_cap = 0;

static char     g_base_url[DS_URL_MAX];
static char     g_query[DS_QUERY_MAX];
static char     g_filter_cat[32];

static ds_page_t g_page = PAGE_HOME;
static int       g_detail_idx = -1;

static lv_obj_t *g_win;
static lv_obj_t *g_nav;
static lv_obj_t *g_content;
static lv_obj_t *g_status;
static lv_obj_t *g_nav_btns[6];
static lv_obj_t *g_search_ta;

static int dlen(const char *s) { int n = 0; if (s) while (s[n]) n++; return n; }

static void dcpy(char *d, int cap, const char *s) {
    int i = 0;
    if (cap <= 0) return;
    if (!s) { d[0] = 0; return; }
    for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}

static void dcat(char *d, int cap, const char *s) {
    int i = dlen(d);
    int j = 0;
    for (; s[j] && i < cap - 1; i++, j++) d[i] = s[j];
    d[i] = 0;
}

static int deq(const char *a, const char *b) {
    int i = 0;
    for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}

static int dlow(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int deq_ci(const char *a, const char *b) {
    int i = 0;
    for (; a[i] && b[i]; i++) if (dlow(a[i]) != dlow(b[i])) return 0;
    return a[i] == b[i];
}

static int dhas_ci(const char *hay, const char *needle) {
    if (!needle || !needle[0]) return 1;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && dlow(*h) == dlow(*n)) { h++; n++; }
        if (!*n) return 1;
    }
    return 0;
}

static int dstarts(const char *s, const char *p) {
    while (*p) { if (*s != *p) return 0; s++; p++; }
    return 1;
}

static int hex_nib2(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void pct_dec(char *dst, int cap, const char *src, int len) {
    int o = 0;
    for (int i = 0; i < len && o < cap - 1; i++) {
        char c = src[i];
        if (c == '%' && i + 2 < len) {
            int hi = hex_nib2(src[i + 1]), lo = hex_nib2(src[i + 2]);
            if (hi >= 0 && lo >= 0) { dst[o++] = (char)((hi << 4) | lo); i += 2; continue; }
        }
        dst[o++] = c;
    }
    dst[o] = 0;
}

static int key_is(const char *k, int klen, const char *want) {
    int i = 0;
    for (; want[i]; i++) {
        if (i >= klen) return 0;
        if (dlow(k[i]) != want[i]) return 0;
    }
    return i == klen;
}

static uint32_t ds_color_for(const char *id) {
    unsigned h = 2166136261u;
    for (; id && *id; id++) { h ^= (unsigned char)*id; h *= 16777619u; }
    static const uint32_t pal[8] = {
        0x3B82F6, 0x8B5CF6, 0xEC4899, 0xF59E0B,
        0x10B981, 0xEF4444, 0x06B6D4, 0x6366F1
    };
    return pal[h & 7];
}

static lv_obj_t *ds_svg_icon(lv_obj_t *parent, const char *svg, int size) {
    if (!svg || !svg[0] || !s_asi || !s_asi->mem) return 0;
    uint32_t *buf = (uint32_t *)s_asi->mem->alloc((size_t)size * size * 4u);
    if (!buf) return 0;
    if (svg_render(svg, size, size, buf) != 0) return 0;
    lv_obj_t *c = lv_canvas_create(parent);
    lv_obj_remove_style_all(c);
    lv_canvas_set_buffer(c, buf, size, size, LV_COLOR_FORMAT_ARGB8888);
    return c;
}

static lv_obj_t *ds_tile(lv_obj_t *parent, const char *text, uint32_t color, int size, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    char one[3];
    one[0] = text && text[0] ? text[0] : '?';
    int n = 1;
    if (text && text[0] && text[1]) { one[1] = text[1]; n = 2; }
    one[n] = 0;
    lv_obj_t *l = lv_label_create(o);
    lv_label_set_text(l, one);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l);
    return o;
}

static lv_obj_t *ds_app_icon(lv_obj_t *parent, const ds_app_t *a, int size) {
    lv_obj_t *ic = ds_svg_icon(parent, a->icon, size);
    if (ic) return ic;
    return ds_tile(parent, a->title[0] ? a->title : a->id, ds_color_for(a->id), size, size / 5);
}

static lv_obj_t *ds_button(lv_obj_t *parent, const char *txt, uint32_t bg,
                           uint32_t fg, int w, int h) {
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

static void ds_status(const char *msg, uint32_t col) {
    if (!g_status) return;
    lv_label_set_text(g_status, msg);
    lv_obj_set_style_text_color(g_status, lv_color_hex(col), 0);
}

static void ds_render_now(void) {
    if (!s_asi || !s_asi->gfx) return;
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(lv_display_get_default());
    if (s_asi->gfx->present) s_asi->gfx->present();
}

static const char *ds_http_get(const char *url, uint32_t *len_out) {
    if (!s_asi || !s_asi->net || !s_asi->net->http_get) return 0;
    if (s_asi->net->link_type && s_asi->net->link_type() == ASI_LINK_NONE) return 0;
    return s_asi->net->http_get(url, len_out);
}

static void ds_make_url(char *out, int cap, const char *path) {
    if (!path || !path[0]) { out[0] = 0; return; }
    if (dstarts(path, "http://") || dstarts(path, "https://")) dcpy(out, cap, path);
    else { dcpy(out, cap, g_base_url); dcat(out, cap, path); }
}

static int ds_installed(const char *id) {
    return (arctian_store && arctian_store->installed) ? arctian_store->installed(id) : 0;
}

static void ds_cfg_load(void) {
    dcpy(g_base_url, sizeof(g_base_url), DS_DEFAULT_URL);
}

static void ds_feed_parse(const char *t, uint32_t n) {
    g_app_count = 0;
    ds_app_t *cur = 0;
    uint32_t i = 0;
    while (i < n) {
        uint32_t ls = i;
        while (i < n && t[i] != '\n' && t[i] != '\r') i++;
        uint32_t le = i;
        while (i < n && (t[i] == '\n' || t[i] == '\r')) i++;
        int ll = (int)(le - ls);
        if (ll <= 0) continue;
        if (t[ls] == '#') continue;

        if (ll == 4 && t[ls] == '@' && t[ls + 1] == 'a' && t[ls + 2] == 'p' && t[ls + 3] == 'p') {
            if (g_app_count >= DS_MAX_APPS) { cur = 0; continue; }
            cur = &g_apps[g_app_count++];
            for (unsigned k = 0; k < sizeof(*cur); k++) ((char *)cur)[k] = 0;
            continue;
        }
        if (!cur) continue;

        uint32_t eq = ls;
        while (eq < le && t[eq] != '=') eq++;
        if (eq >= le) continue;
        int klen = (int)(eq - ls);
        const char *key = t + ls;
        char val[DS_ICON_MAX];
        pct_dec(val, sizeof(val), t + eq + 1, (int)(le - eq - 1));

        if (key_is(key, klen, "id")) dcpy(cur->id, sizeof(cur->id), val);
        else if (key_is(key, klen, "title")) dcpy(cur->title, sizeof(cur->title), val);
        else if (key_is(key, klen, "category")) dcpy(cur->category, sizeof(cur->category), val);
        else if (key_is(key, klen, "version")) dcpy(cur->version, sizeof(cur->version), val);
        else if (key_is(key, klen, "publisher")) dcpy(cur->publisher, sizeof(cur->publisher), val);
        else if (key_is(key, klen, "size")) dcpy(cur->size, sizeof(cur->size), val);
        else if (key_is(key, klen, "desc")) dcpy(cur->desc, sizeof(cur->desc), val);
        else if (key_is(key, klen, "package")) dcpy(cur->pkg, sizeof(cur->pkg), val);
        else if (key_is(key, klen, "icon")) dcpy(cur->icon, sizeof(cur->icon), val);
    }
}

static int ds_feed_load(void) {
    char url[DS_URL_MAX + 16];
    dcpy(url, sizeof(url), g_base_url);
    dcat(url, sizeof(url), "/v1/feed");
    uint32_t len = 0;
    const char *body = ds_http_get(url, &len);
    if (!body || len == 0) { g_feed_ok = 0; return -1; }
    ds_feed_parse(body, len);
    g_feed_ok = g_app_count > 0;
    return g_feed_ok ? 0 : -1;
}

static void ds_url_encode(char *dst, int cap, const char *src) {
    static const char hx[] = "0123456789ABCDEF";
    int o = 0;
    for (; *src && o < cap - 3; src++) {
        unsigned char c = (unsigned char)*src;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[o++] = (char)c;
        } else {
            dst[o++] = '%'; dst[o++] = hx[(c >> 4) & 0xF]; dst[o++] = hx[c & 0xF];
        }
    }
    dst[o] = 0;
}

static int ds_search_server(const char *q) {
    char enc[DS_QUERY_MAX * 3];
    ds_url_encode(enc, sizeof(enc), q);
    char url[DS_URL_MAX + sizeof(enc) + 24];
    dcpy(url, sizeof(url), g_base_url);
    dcat(url, sizeof(url), "/v1/search?q=");
    dcat(url, sizeof(url), enc);
    uint32_t len = 0;
    const char *body = ds_http_get(url, &len);
    if (!body || len == 0) return -1;
    ds_feed_parse(body, len);
    return g_app_count > 0 ? 0 : -1;
}

static void ds_wall_parse(const char *t, uint32_t n) {
    g_wall_count = 0;
    ds_wall_t *cur = 0;
    uint32_t i = 0;
    while (i < n) {
        uint32_t ls = i;
        while (i < n && t[i] != '\n' && t[i] != '\r') i++;
        uint32_t le = i;
        while (i < n && (t[i] == '\n' || t[i] == '\r')) i++;
        int ll = (int)(le - ls);
        if (ll <= 0) continue;
        if (t[ls] == '#') continue;
        if (ll == 5 && t[ls] == '@' && t[ls + 1] == 'w' && t[ls + 2] == 'a' &&
            t[ls + 3] == 'l' && t[ls + 4] == 'l') {
            if (g_wall_count >= DS_MAX_WALLS) { cur = 0; continue; }
            cur = &g_walls[g_wall_count++];
            for (unsigned k = 0; k < sizeof(*cur); k++) ((char *)cur)[k] = 0;
            continue;
        }
        if (!cur) continue;
        uint32_t eq = ls;
        while (eq < le && t[eq] != '=') eq++;
        if (eq >= le) continue;
        int klen = (int)(eq - ls);
        const char *key = t + ls;
        char val[DS_IMG_MAX];
        pct_dec(val, sizeof(val), t + eq + 1, (int)(le - eq - 1));
        if (key_is(key, klen, "id")) dcpy(cur->id, sizeof(cur->id), val);
        else if (key_is(key, klen, "title")) dcpy(cur->title, sizeof(cur->title), val);
        else if (key_is(key, klen, "author")) dcpy(cur->author, sizeof(cur->author), val);
        else if (key_is(key, klen, "size")) dcpy(cur->size, sizeof(cur->size), val);
        else if (key_is(key, klen, "image")) dcpy(cur->img, sizeof(cur->img), val);
    }
}

static int ds_walls_load(void) {
    char url[DS_URL_MAX + 24];
    dcpy(url, sizeof(url), g_base_url);
    dcat(url, sizeof(url), "/v1/wallpapers");
    uint32_t len = 0;
    const char *body = ds_http_get(url, &len);
    if (!body || len == 0) { g_wall_ok = 0; return -1; }
    ds_wall_parse(body, len);
    g_wall_ok = g_wall_count > 0;
    return g_wall_ok ? 0 : -1;
}

static void ds_rebuild(void);

static void ds_do_install(int idx) {
    if (idx < 0 || idx >= g_app_count) return;
    ds_app_t *a = &g_apps[idx];
    if (!a->pkg[0]) { ds_status("Paket adresi yok", ADL_DANGER); ds_render_now(); return; }
    if (!arctian_store || !arctian_store->install) {
        ds_status("Uygulama platformu yok", ADL_DANGER); ds_render_now(); return;
    }
    char url[DS_PKG_MAX + DS_URL_MAX];
    ds_make_url(url, sizeof(url), a->pkg);
    ds_status("Indiriliyor...", ADL_WARN);
    ds_render_now();

    uint32_t len = 0;
    const char *body = ds_http_get(url, &len);
    if (!body || len == 0 || len > DPK_MAX_PACKAGE) {
        ds_status("Indirilemedi (ag/sunucu)", ADL_DANGER);
        ds_render_now();
        return;
    }
    char err[110];
    int r = arctian_store->install(body, len, err, sizeof(err));
    if (r == 0) ds_status("Kuruldu", ADL_OK);
    else ds_status(err[0] ? err : "Kurulamadi", ADL_DANGER);
    ds_render_now();
    ds_rebuild();
}

static void ds_do_uninstall(int idx) {
    if (idx < 0 || idx >= g_app_count) return;
    ds_app_t *a = &g_apps[idx];
    if (!arctian_store || !arctian_store->uninstall) return;
    int r = arctian_store->uninstall(a->id);
    if (r == 0) ds_status("Kaldirildi", ADL_OK);
    else if (r == -2) ds_status("Uygulama acik; once kapat", ADL_WARN);
    else ds_status("Kaldirilamadi", ADL_DANGER);
    ds_render_now();
    ds_rebuild();
}

static void ds_do_open(int idx) {
    if (idx < 0 || idx >= g_app_count) return;
    ds_app_t *a = &g_apps[idx];
    if (!arctian_store || !arctian_store->launch) return;
    if (arctian_store->launch(a->id) != 0) {
        ds_status("Acilamadi", ADL_DANGER);
        ds_render_now();
    }
}

static void ds_set_nav_active(void) {
    static const ds_page_t map[6] = {
        PAGE_HOME, PAGE_SEARCH, PAGE_CATS, PAGE_LIB, PAGE_UPDATES, PAGE_WALLS
    };
    for (int i = 0; i < 6; i++) {
        if (!g_nav_btns[i]) continue;
        int active = (g_page == map[i]);
        lv_obj_set_style_bg_color(g_nav_btns[i],
            lv_color_hex(active ? ADL_ACCENT : ADL_SURFACE_2), 0);
    }
}

typedef struct { int page; } ds_navctx_t;
static ds_navctx_t g_navctx[6];

static void ds_nav_cb(lv_event_t *e) {
    ds_navctx_t *c = (ds_navctx_t *)lv_event_get_user_data(e);
    if (!c) return;
    g_page = (ds_page_t)c->page;
    g_detail_idx = -1;
    ds_rebuild();
}

static void ds_open_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ds_do_open(idx);
}

static void ds_install_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ds_do_install(idx);
}

static void ds_detail_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_detail_idx = idx;
    g_page = PAGE_DETAIL;
    ds_rebuild();
}

static void ds_back_cb(lv_event_t *e) {
    (void)e;
    g_detail_idx = -1;
    g_page = PAGE_HOME;
    ds_rebuild();
}

static void ds_uninstall_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ds_do_uninstall(idx);
}

static void ds_open_id_cb(lv_event_t *e) {
    const char *id = (const char *)lv_event_get_user_data(e);
    if (arctian_store && arctian_store->launch) arctian_store->launch(id);
}

static void ds_uninstall_id_cb(lv_event_t *e) {
    const char *id = (const char *)lv_event_get_user_data(e);
    if (!arctian_store || !arctian_store->uninstall) return;
    int r = arctian_store->uninstall(id);
    if (r == 0) ds_status("Kaldirildi", ADL_OK);
    else if (r == -2) ds_status("Uygulama acik; once kapat", ADL_WARN);
    else ds_status("Kaldirilamadi", ADL_DANGER);
    ds_render_now();
    ds_rebuild();
}

static void ds_refresh_cb(lv_event_t *e) {
    (void)e;
    ds_status("Guncelleniyor...", ADL_WARN);
    ds_render_now();
    if (ds_feed_load() == 0) ds_status("Hazir", ADL_OK);
    else ds_status("Sunucuya ulasilamadi", ADL_DANGER);
    ds_rebuild();
}

static void ds_search_cb(lv_event_t *e) {
    (void)e;
    const char *q = g_search_ta ? lv_textarea_get_text(g_search_ta) : "";
    dcpy(g_query, sizeof(g_query), q);
    g_page = PAGE_SEARCH;
    if (g_query[0]) {
        ds_status("Araniyor...", ADL_WARN);
        ds_render_now();
        if (ds_search_server(g_query) != 0) {
            if (g_app_count == 0) ds_feed_load();
        }
        ds_status("Hazir", ADL_OK);
    }
    ds_rebuild();
}

static void ds_cat_cb(lv_event_t *e) {
    const char *cat = (const char *)lv_event_get_user_data(e);
    dcpy(g_filter_cat, sizeof(g_filter_cat), cat);
    g_page = PAGE_CATS;
    ds_rebuild();
}

static void ds_win_del_cb(lv_event_t *e) {
    (void)e;
    g_win = 0; g_nav = 0; g_content = 0; g_status = 0;
    g_search_ta = 0;
    for (int i = 0; i < 6; i++) g_nav_btns[i] = 0;
}

static int ds_app_matches(const ds_app_t *a) {
    if (!g_query[0] && !g_filter_cat[0]) return 1;
    int ok = 1;
    if (g_query[0])
        ok = dhas_ci(a->title, g_query) || dhas_ci(a->publisher, g_query) ||
             dhas_ci(a->category, g_query) || dhas_ci(a->id, g_query);
    if (ok && g_filter_cat[0]) ok = deq_ci(a->category, g_filter_cat);
    return ok;
}

static lv_obj_t *ds_card(lv_obj_t *parent, int idx) {
    ds_app_t *a = &g_apps[idx];
    lv_obj_t *c = lv_button_create(parent);
    lv_obj_set_size(c, 208, 244);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_pad_all(c, 10, 0);

    lv_obj_t *ic = ds_app_icon(c, a, 64);
    if (ic) lv_obj_align(ic, LV_ALIGN_TOP_LEFT, 4, 4);

    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, a->title[0] ? a->title : a->id);
    lv_obj_set_width(t, 184);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 82);

    lv_obj_t *cat = lv_label_create(c);
    lv_label_set_text(cat, a->category[0] ? a->category : "Genel");
    lv_obj_set_width(cat, 184);
    lv_label_set_long_mode(cat, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(cat, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_align(cat, LV_ALIGN_TOP_LEFT, 0, 108);

    int inst = ds_installed(a->id);
    lv_obj_t *b = ds_button(c, inst ? "Ac" : "Yukle",
                            inst ? ADL_OK : ADL_ACCENT, 0xFFFFFF, 96, 34);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    if (inst) lv_obj_add_event_cb(b, ds_open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    else      lv_obj_add_event_cb(b, ds_install_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_add_event_cb(c, ds_detail_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    return c;
}

static lv_obj_t *ds_section(lv_obj_t *parent, const char *title) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, title);
    lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_pad_top(l, 10, 0);
    lv_obj_set_style_pad_bottom(l, 4, 0);
    return l;
}

static lv_obj_t *ds_grid(lv_obj_t *parent) {
    lv_obj_t *g = lv_obj_create(parent);
    lv_obj_remove_style_all(g);
    lv_obj_set_width(g, LV_PCT(100));
    lv_obj_set_height(g, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(g, 12, 0);
    lv_obj_set_style_pad_row(g, 12, 0);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
    return g;
}

static void ds_page_home(void) {
    lv_obj_t *hero = lv_obj_create(g_content);
    lv_obj_remove_style_all(hero);
    lv_obj_set_size(hero, LV_PCT(100), 120);
    lv_obj_set_style_radius(hero, 14, 0);
    lv_obj_set_style_bg_color(hero, lv_color_hex(0x1D4ED8), 0);
    lv_obj_set_style_bg_grad_color(hero, lv_color_hex(0x7C3AED), 0);
    lv_obj_set_style_bg_grad_dir(hero, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(hero, LV_OPA_COVER, 0);
    lv_obj_remove_flag(hero, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ht = lv_label_create(hero);
    lv_label_set_text(ht, "DuckStore");
    lv_obj_set_style_text_color(ht, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ht, LV_ALIGN_TOP_LEFT, 20, 18);
    lv_obj_t *hs = lv_label_create(hero);
    lv_label_set_text(hs, "Arctian icin uygulamalar - tek tikla kur, hemen kullan.");
    lv_obj_set_style_text_color(hs, lv_color_hex(0xE0E7FF), 0);
    lv_obj_align(hs, LV_ALIGN_TOP_LEFT, 20, 46);

    lv_obj_t *rb = ds_button(hero, "Yenile", 0xFFFFFF, 0x1D4ED8, 96, 34);
    lv_obj_align(rb, LV_ALIGN_BOTTOM_RIGHT, -16, -14);
    lv_obj_add_event_cb(rb, ds_refresh_cb, LV_EVENT_CLICKED, 0);

    if (!g_feed_ok) {
        ds_section(g_content, "Baglanti bekleniyor");
        lv_obj_t *m = lv_label_create(g_content);
        lv_obj_set_width(m, LV_PCT(96));
        lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
        lv_label_set_text_fmt(m,
            "Sunucuya ulasilamadi: %s\n\n"
            "Ag baglantinizi kontrol edip 'Yenile' deneyin.",
            g_base_url);
        lv_obj_set_style_text_color(m, lv_color_hex(ADL_TEXT_DIM), 0);
        return;
    }

    ds_section(g_content, "One Cikanlar");
    lv_obj_t *row = lv_obj_create(g_content);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 260);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);

    int n = g_app_count < 8 ? g_app_count : 8;
    for (int i = 0; i < n; i++) ds_card(row, i);

    ds_section(g_content, "Tum Uygulamalar");
    lv_obj_t *grid = ds_grid(g_content);
    for (int i = 0; i < g_app_count; i++) ds_card(grid, i);
}

static void ds_page_search(void) {
    lv_obj_t *bar = lv_obj_create(g_content);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), 44);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    g_search_ta = lv_textarea_create(bar);
    lv_textarea_set_one_line(g_search_ta, true);
    lv_textarea_set_placeholder_text(g_search_ta, "Uygulama ara...");
    lv_obj_set_size(g_search_ta, 100, 40);
    lv_obj_set_flex_grow(g_search_ta, 1);
    if (g_query[0]) lv_textarea_set_text(g_search_ta, g_query);

    lv_obj_t *sb = ds_button(bar, "Ara", ADL_ACCENT, 0xFFFFFF, 90, 40);
    lv_obj_add_event_cb(sb, ds_search_cb, LV_EVENT_CLICKED, 0);
    lv_obj_add_event_cb(g_search_ta, ds_search_cb, LV_EVENT_READY, 0);

    ds_section(g_content, g_query[0] ? "Sonuclar" : "Tum Uygulamalar");
    lv_obj_t *grid = ds_grid(g_content);
    int shown = 0;
    for (int i = 0; i < g_app_count; i++) {
        if (!ds_app_matches(&g_apps[i])) continue;
        ds_card(grid, i);
        shown++;
    }
    if (shown == 0) {
        lv_obj_t *m = lv_label_create(g_content);
        lv_label_set_text(m, "Sonuc bulunamadi.");
        lv_obj_set_style_text_color(m, lv_color_hex(ADL_TEXT_DIM), 0);
    }
}

static void ds_page_cats(void) {
    ds_section(g_content, "Kategoriler");
    lv_obj_t *row = lv_obj_create(g_content);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 46);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);

    lv_obj_t *all = ds_button(row, "Tumu", g_filter_cat[0] ? ADL_SURFACE_2 : ADL_ACCENT,
                              0xFFFFFF, 90, 36);
    lv_obj_add_event_cb(all, ds_cat_cb, LV_EVENT_CLICKED, (void *)"");

    for (int i = 0; i < g_app_count; i++) {
        const char *cat = g_apps[i].category[0] ? g_apps[i].category : "Genel";
        int seen = 0;
        for (int j = 0; j < i; j++) {
            const char *cj = g_apps[j].category[0] ? g_apps[j].category : "Genel";
            if (deq_ci(cj, cat)) { seen = 1; break; }
        }
        if (seen) continue;
        lv_obj_t *b = ds_button(row, cat,
            deq_ci(g_filter_cat, cat) ? ADL_ACCENT : ADL_SURFACE_2, 0xFFFFFF, 120, 36);
        lv_obj_add_event_cb(b, ds_cat_cb, LV_EVENT_CLICKED, (void *)cat);
    }

    ds_section(g_content, g_filter_cat[0] ? g_filter_cat : "Tum Uygulamalar");
    lv_obj_t *grid = ds_grid(g_content);
    for (int i = 0; i < g_app_count; i++) {
        if (!ds_app_matches(&g_apps[i])) continue;
        ds_card(grid, i);
    }
}

static ds_app_t *ds_find_feed(const char *id) {
    for (int i = 0; i < g_app_count; i++) if (deq(g_apps[i].id, id)) return &g_apps[i];
    return 0;
}

static void ds_page_lib(void) {
    int cnt = arctian_store && arctian_store->count_installed
              ? arctian_store->count_installed() : 0;
    ds_section(g_content, "Kitapligim");
    if (cnt == 0) {
        lv_obj_t *m = lv_label_create(g_content);
        lv_label_set_text(m, "Henuz yuklu uygulama yok.");
        lv_obj_set_style_text_color(m, lv_color_hex(ADL_TEXT_DIM), 0);
        return;
    }
    lv_obj_t *list = lv_obj_create(g_content);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < cnt; i++) {
        const char *id = arctian_store->installed_id(i);
        const char *title = arctian_store->installed_title(i);
        const char *ver = arctian_store->installed_version_at(i);
        ds_app_t *fa = ds_find_feed(id);

        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 72);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(ADL_SURFACE_2), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *ic = fa ? ds_app_icon(row, fa, 48)
                          : ds_tile(row, title, ds_color_for(id), 48, 10);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 12, 0);

        lv_obj_t *tl = lv_label_create(row);
        lv_label_set_text(tl, title);
        lv_obj_set_style_text_color(tl, lv_color_hex(ADL_TEXT), 0);
        lv_obj_align(tl, LV_ALIGN_LEFT_MID, 74, -10);

        lv_obj_t *vl = lv_label_create(row);
        lv_label_set_text_fmt(vl, "Surum %s", ver && ver[0] ? ver : "-");
        lv_obj_set_style_text_color(vl, lv_color_hex(ADL_TEXT_DIM), 0);
        lv_obj_align(vl, LV_ALIGN_LEFT_MID, 74, 12);

        lv_obj_t *ob = ds_button(row, "Ac", ADL_OK, 0xFFFFFF, 84, 34);
        lv_obj_align(ob, LV_ALIGN_RIGHT_MID, -108, 0);
        lv_obj_add_event_cb(ob, ds_open_id_cb, LV_EVENT_CLICKED, (void *)id);

        lv_obj_t *db = ds_button(row, "Kaldir", ADL_DANGER, 0xFFFFFF, 90, 34);
        lv_obj_align(db, LV_ALIGN_RIGHT_MID, -12, 0);
        lv_obj_add_event_cb(db, ds_uninstall_id_cb, LV_EVENT_CLICKED, (void *)id);
    }
}

static void ds_page_updates(void) {
    ds_section(g_content, "Guncellemeler");
    int cnt = arctian_store && arctian_store->count_installed
              ? arctian_store->count_installed() : 0;
    int found = 0;
    lv_obj_t *grid = ds_grid(g_content);
    for (int i = 0; i < cnt; i++) {
        const char *id = arctian_store->installed_id(i);
        const char *iver = arctian_store->installed_version_at(i);
        ds_app_t *fa = ds_find_feed(id);
        if (!fa) continue;
        if (fa->version[0] && !deq(iver, fa->version)) {
            ds_card(grid, (int)(fa - g_apps));
            found++;
        }
    }
    if (!found) {
        lv_obj_t *m = lv_label_create(g_content);
        lv_label_set_text(m, "Tum uygulamalar guncel.");
        lv_obj_set_style_text_color(m, lv_color_hex(ADL_OK), 0);
    }
}

static void ds_wall_fname(const char *id, char *out, int cap) {
    dcpy(out, cap, id);
    dcat(out, cap, ".wp");
}
static int ds_wall_installed(const char *id) {
    if (!s_asi || !s_asi->fs || !s_asi->fs->find) return 0;
    char fn[40];
    ds_wall_fname(id, fn, sizeof(fn));
    return s_asi->fs->find(0, fn) != 0;
}
static const char *ds_wall_active_name(void) {
    return (arctian_store && arctian_store->wallpaper_name)
           ? arctian_store->wallpaper_name() : "";
}
static void ds_wall_apply_file(const char *fn) {
    if (!arctian_store || !arctian_store->set_wallpaper) return;
    int r = arctian_store->set_wallpaper(fn);
    ds_status(r == 0 ? "Arka plan uygulandi" : "Arka plan uygulanamadi",
              r == 0 ? ADL_OK : ADL_DANGER);
    ds_render_now();
    ds_rebuild();
}
static void ds_wall_default(void) {
    if (arctian_store && arctian_store->set_wallpaper) {
        arctian_store->set_wallpaper("");
        ds_status("Varsayilan arka plan", ADL_OK);
    }
    ds_render_now();
    ds_rebuild();
}
static void ds_wall_download(int idx) {
    if (idx < 0 || idx >= g_wall_count) return;
    ds_wall_t *w = &g_walls[idx];
    if (!w->img[0]) { ds_status("Gorsel adresi yok", ADL_DANGER); ds_render_now(); return; }
    if (!s_asi || !s_asi->net || !s_asi->net->download) {
        ds_status("Ag indirme API yok", ADL_DANGER); ds_render_now(); return;
    }
    if (!g_dl_buf) {
        g_dl_buf = (uint8_t *)s_asi->mem->alloc(DS_WP_MAX + DS_WP_SLACK);
        g_dl_cap = g_dl_buf ? (DS_WP_MAX + DS_WP_SLACK) : 0;
    }
    if (!g_dl_buf) { ds_status("Bellek yok", ADL_DANGER); ds_render_now(); return; }

    char url[DS_IMG_MAX + DS_URL_MAX];
    ds_make_url(url, sizeof(url), w->img);
    ds_status("Indiriliyor...", ADL_WARN);
    ds_render_now();

    uint32_t len = 0;
    int r = s_asi->net->download(url, g_dl_buf, g_dl_cap, &len);
    if (r != 0 || len < 8 || len > DS_WP_MAX) {
        ds_status("Indirilemedi veya cok buyuk", ADL_DANGER);
        ds_render_now();
        return;
    }
    if (!s_asi->fs || !s_asi->fs->create) {
        ds_status("AFS yok", ADL_DANGER); ds_render_now(); return;
    }
    char fn[40];
    ds_wall_fname(w->id, fn, sizeof(fn));
    void *h = s_asi->fs->create(0, fn, 0);
    if (!h || s_asi->fs->write(h, g_dl_buf, len) != 0) {
        ds_status("Diske yazilamadi", ADL_DANGER); ds_render_now(); return;
    }
    ds_wall_apply_file(fn);
}
static void ds_wall_remove_file(const char *fn) {
    if (!s_asi || !s_asi->fs || !s_asi->fs->remove) return;
    const char *act = ds_wall_active_name();
    int was_active = (act && act[0] && deq(act, fn));
    s_asi->fs->remove(fn);
    if (was_active && arctian_store && arctian_store->set_wallpaper)
        arctian_store->set_wallpaper("");
    ds_status("Kaldirildi", ADL_OK);
    ds_render_now();
    ds_rebuild();
}

static void ds_wall_default_cb(lv_event_t *e) { (void)e; ds_wall_default(); }
static void ds_wall_inst_apply_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < g_inst_wp_count) ds_wall_apply_file(g_inst_wp[idx]);
}
static void ds_wall_inst_remove_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < g_inst_wp_count) ds_wall_remove_file(g_inst_wp[idx]);
}
static void ds_wall_download_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ds_wall_download(idx);
}
static void ds_wall_store_apply_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_wall_count) return;
    char fn[40];
    ds_wall_fname(g_walls[idx].id, fn, sizeof(fn));
    ds_wall_apply_file(fn);
}
static void ds_wall_store_remove_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_wall_count) return;
    char fn[40];
    ds_wall_fname(g_walls[idx].id, fn, sizeof(fn));
    ds_wall_remove_file(fn);
}
static void ds_wall_refresh_cb(lv_event_t *e) {
    (void)e;
    ds_status("Yenileniyor...", ADL_WARN);
    ds_render_now();
    if (ds_walls_load() == 0) ds_status("Hazir", ADL_OK);
    else ds_status("Sunucuya ulasilamadi", ADL_DANGER);
    ds_rebuild();
}

static void ds_page_walls(void) {
    ds_section(g_content, "Arka Planlar");

    lv_obj_t *bar = lv_obj_create(g_content);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), 44);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *def = ds_button(bar, "Varsayilan", ADL_SURFACE_2, 0xFFFFFF, 140, 38);
    lv_obj_add_event_cb(def, ds_wall_default_cb, LV_EVENT_CLICKED, 0);
    lv_obj_t *ref = ds_button(bar, "Yenile", ADL_ACCENT, 0xFFFFFF, 110, 38);
    lv_obj_add_event_cb(ref, ds_wall_refresh_cb, LV_EVENT_CLICKED, 0);

    const char *act = ds_wall_active_name();
    lv_obj_t *actl = lv_label_create(g_content);
    lv_label_set_text_fmt(actl, "Aktif: %s",
                          (act && act[0]) ? act : "Varsayilan (gomulu)");
    lv_obj_set_style_text_color(actl, lv_color_hex(ADL_TEXT_DIM), 0);

    g_inst_wp_count = 0;
    if (s_asi && s_asi->fs && s_asi->fs->count && s_asi->fs->name) {
        int n = s_asi->fs->count();
        for (int i = 0; i < n && g_inst_wp_count < DS_MAX_WALLS; i++) {
            const char *nm = s_asi->fs->name(i);
            int l = nm ? dlen(nm) : 0;
            if (l < 4) continue;
            if (!(nm[l - 3] == '.' && dlow(nm[l - 2]) == 'w' && dlow(nm[l - 1]) == 'p'))
                continue;
            dcpy(g_inst_wp[g_inst_wp_count], 40, nm);
            g_inst_wp_count++;
        }
    }

    ds_section(g_content, "Yuklu Arka Planlar");
    if (g_inst_wp_count == 0) {
        lv_obj_t *m = lv_label_create(g_content);
        lv_label_set_text(m, "Henuz indirilmis arka plan yok.");
        lv_obj_set_style_text_color(m, lv_color_hex(ADL_TEXT_DIM), 0);
    } else {
        lv_obj_t *list = lv_obj_create(g_content);
        lv_obj_remove_style_all(list);
        lv_obj_set_width(list, LV_PCT(100));
        lv_obj_set_height(list, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(list, 6, 0);
        lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
        for (int i = 0; i < g_inst_wp_count; i++) {
            lv_obj_t *row = lv_obj_create(list);
            lv_obj_remove_style_all(row);
            lv_obj_set_size(row, LV_PCT(100), 56);
            lv_obj_set_style_radius(row, 10, 0);
            lv_obj_set_style_bg_color(row, lv_color_hex(ADL_SURFACE_2), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *tile = ds_tile(row, g_inst_wp[i], ds_color_for(g_inst_wp[i]), 40, 8);
            lv_obj_align(tile, LV_ALIGN_LEFT_MID, 10, 0);
            lv_obj_t *tl = lv_label_create(row);
            lv_label_set_text(tl, g_inst_wp[i]);
            lv_obj_set_style_text_color(tl, lv_color_hex(ADL_TEXT), 0);
            lv_obj_align(tl, LV_ALIGN_LEFT_MID, 62, 0);
            int isact = (act && act[0] && deq(act, g_inst_wp[i]));
            lv_obj_t *ab = ds_button(row, isact ? "Aktif" : "Uygula",
                                     isact ? ADL_OK : ADL_ACCENT, 0xFFFFFF, 92, 34);
            lv_obj_align(ab, LV_ALIGN_RIGHT_MID, -100, 0);
            if (!isact)
                lv_obj_add_event_cb(ab, ds_wall_inst_apply_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_t *rb = ds_button(row, "Kaldir", ADL_DANGER, 0xFFFFFF, 84, 34);
            lv_obj_align(rb, LV_ALIGN_RIGHT_MID, -10, 0);
            lv_obj_add_event_cb(rb, ds_wall_inst_remove_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    }

    ds_section(g_content, "Magazadan");
    if (!g_wall_ok) {
        lv_obj_t *m = lv_label_create(g_content);
        lv_obj_set_width(m, LV_PCT(96));
        lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
        lv_label_set_text(m, "Sunucudan arka plan listesi alinamadi. 'Yenile' deneyin.");
        lv_obj_set_style_text_color(m, lv_color_hex(ADL_TEXT_DIM), 0);
        return;
    }
    for (int i = 0; i < g_wall_count; i++) {
        ds_wall_t *w = &g_walls[i];
        lv_obj_t *row = lv_obj_create(g_content);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 64);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(ADL_SURFACE_2), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *tile = ds_tile(row, w->title[0] ? w->title : w->id,
                                 ds_color_for(w->id), 44, 8);
        lv_obj_align(tile, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_t *tl = lv_label_create(row);
        lv_label_set_text(tl, w->title[0] ? w->title : w->id);
        lv_obj_set_width(tl, 300);
        lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(tl, lv_color_hex(ADL_TEXT), 0);
        lv_obj_align(tl, LV_ALIGN_LEFT_MID, 66, -10);
        lv_obj_t *sl = lv_label_create(row);
        lv_label_set_text_fmt(sl, "%s  -  %s B",
                              w->author[0] ? w->author : "Arctian",
                              w->size[0] ? w->size : "?");
        lv_obj_set_style_text_color(sl, lv_color_hex(ADL_TEXT_DIM), 0);
        lv_obj_align(sl, LV_ALIGN_LEFT_MID, 66, 12);
        int inst = ds_wall_installed(w->id);
        lv_obj_t *ab = ds_button(row, inst ? "Uygula" : "Indir",
                                 inst ? ADL_OK : ADL_ACCENT, 0xFFFFFF, 96, 36);
        lv_obj_align(ab, LV_ALIGN_RIGHT_MID, -104, 0);
        if (inst)
            lv_obj_add_event_cb(ab, ds_wall_store_apply_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        else
            lv_obj_add_event_cb(ab, ds_wall_download_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if (inst) {
            lv_obj_t *rb = ds_button(row, "Kaldir", ADL_DANGER, 0xFFFFFF, 84, 36);
            lv_obj_align(rb, LV_ALIGN_RIGHT_MID, -12, 0);
            lv_obj_add_event_cb(rb, ds_wall_store_remove_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    }
}

static void ds_page_detail(void) {
    int idx = g_detail_idx;
    if (idx < 0 || idx >= g_app_count) { g_page = PAGE_HOME; ds_page_home(); return; }
    ds_app_t *a = &g_apps[idx];

    lv_obj_t *back = ds_button(g_content, "< Geri", ADL_SURFACE_2, 0xFFFFFF, 90, 36);
    lv_obj_add_event_cb(back, ds_back_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *head = lv_obj_create(g_content);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), 120);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ic = ds_app_icon(head, a, 96);
    if (ic) lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *t = lv_label_create(head);
    lv_label_set_text(t, a->title[0] ? a->title : a->id);
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 116, 6);

    lv_obj_t *pub = lv_label_create(head);
    lv_label_set_text_fmt(pub, "%s  -  %s", a->publisher[0] ? a->publisher : "Bilinmiyor",
                          a->category[0] ? a->category : "Genel");
    lv_obj_set_style_text_color(pub, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_align(pub, LV_ALIGN_TOP_LEFT, 116, 34);

    lv_obj_t *meta = lv_label_create(head);
    lv_label_set_text_fmt(meta, "Surum %s   Boyut %s KB",
                          a->version[0] ? a->version : "-",
                          a->size[0] ? a->size : "?");
    lv_obj_set_style_text_color(meta, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_align(meta, LV_ALIGN_TOP_LEFT, 116, 58);

    int inst = ds_installed(a->id);
    lv_obj_t *ob = ds_button(head, inst ? "Ac" : "Yukle",
                             inst ? ADL_OK : ADL_ACCENT, 0xFFFFFF, 130, 40);
    lv_obj_align(ob, LV_ALIGN_BOTTOM_LEFT, 116, -4);
    if (inst) lv_obj_add_event_cb(ob, ds_open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    else      lv_obj_add_event_cb(ob, ds_install_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    if (inst) {
        lv_obj_t *db = ds_button(head, "Kaldir", ADL_DANGER, 0xFFFFFF, 130, 40);
        lv_obj_align(db, LV_ALIGN_BOTTOM_LEFT, 258, -4);
        lv_obj_add_event_cb(db, ds_uninstall_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    }

    ds_section(g_content, "Aciklama");
    lv_obj_t *d = lv_label_create(g_content);
    lv_obj_set_width(d, LV_PCT(96));
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    lv_label_set_text(d, a->desc[0] ? a->desc : "Aciklama yok.");
    lv_obj_set_style_text_color(d, lv_color_hex(ADL_TEXT), 0);
}

static void ds_page(void) {
    switch (g_page) {
    case PAGE_HOME: ds_page_home(); break;
    case PAGE_SEARCH: ds_page_search(); break;
    case PAGE_CATS: ds_page_cats(); break;
    case PAGE_LIB: ds_page_lib(); break;
    case PAGE_UPDATES: ds_page_updates(); break;
    case PAGE_WALLS: ds_page_walls(); break;
    case PAGE_DETAIL: ds_page_detail(); break;
    }
}

static void ds_rebuild(void) {
    if (!g_content) return;
    lv_obj_clean(g_content);
    ds_set_nav_active();
    ds_page();
}

static const char *g_nav_labels[6] = {
    "Ana Sayfa", "Ara", "Kategoriler", "Kitapligim", "Guncellemeler", "Arka Planlar"
};

static void ds_build_nav(void) {
    g_nav = lv_obj_create(g_win);
    lv_obj_remove_style_all(g_nav);
    lv_obj_set_size(g_nav, 190, LV_PCT(100));
    lv_obj_set_style_bg_color(g_nav, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(g_nav, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_nav, 12, 0);
    lv_obj_set_style_pad_row(g_nav, 8, 0);
    lv_obj_set_flex_flow(g_nav, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(g_nav, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logo = lv_label_create(g_nav);
    lv_label_set_text(logo, "DuckStore");
    lv_obj_set_style_text_color(logo, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_pad_bottom(logo, 8, 0);

    for (int i = 0; i < 6; i++) {
        g_navctx[i].page = i;
        lv_obj_t *b = lv_button_create(g_nav);
        lv_obj_set_size(b, LV_PCT(100), 40);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(ADL_SURFACE_2), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, g_nav_labels[i]);
        lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_add_event_cb(b, ds_nav_cb, LV_EVENT_CLICKED, &g_navctx[i]);
        g_nav_btns[i] = b;
    }

    g_status = lv_label_create(g_nav);
    lv_obj_set_width(g_status, LV_PCT(100));
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_status, "Hazir");
    lv_obj_set_style_text_color(g_status, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_set_style_pad_top(g_status, 12, 0);
}

static void ds_build_content(void) {
    g_content = lv_obj_create(g_win);
    lv_obj_remove_style_all(g_content);
    lv_obj_set_height(g_content, LV_PCT(100));
    lv_obj_set_flex_grow(g_content, 1);
    lv_obj_set_style_bg_color(g_content, lv_color_hex(ADL_BG), 0);
    lv_obj_set_style_bg_opa(g_content, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_content, 16, 0);
    lv_obj_set_style_pad_row(g_content, 6, 0);
    lv_obj_set_flex_flow(g_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_content, LV_DIR_VER);
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    s_asi = asi;

    g_win = win;
    g_nav = 0; g_content = 0; g_status = 0;
    g_search_ta = 0;
    for (int i = 0; i < 6; i++) g_nav_btns[i] = 0;

    g_app_count = 0;
    g_feed_ok = 0;
    g_query[0] = 0;
    g_filter_cat[0] = 0;
    g_detail_idx = -1;
    g_page = PAGE_HOME;

    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(win, 0, 0);
    lv_obj_set_style_pad_column(win, 0, 0);

    ds_cfg_load();
    ds_build_nav();
    ds_build_content();
    lv_obj_add_event_cb(win, ds_win_del_cb, LV_EVENT_DELETE, 0);

    ds_set_nav_active();
    ds_page_home();

    ds_status("Sunucuya baglaniliyor...", ADL_WARN);
    ds_render_now();
    if (ds_feed_load() == 0) ds_status("Hazir", ADL_OK);
    else ds_status("Sunucuya ulasilamadi", ADL_DANGER);
    ds_walls_load();

    ds_rebuild();
}
