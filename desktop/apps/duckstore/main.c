/* ==================================================================
 * DuckStore - Arctian uygulama magazasi
 *
 *   - Microsoft Store benzeri arayuz (sol navigasyon + icerik)
 *   - Sunucudan feed ceker, uygulama detaylarini gosterir, arar
 *   - Tek tikla kurar (.wlf paketi -> AFS), acar, kaldirir
 *   - Hesap yok, oturum yok.
 *
 * Ag protokolu (DSF - line tabanli feed):
 *     #duckstore-feed 1
 *     @app
 *     id=hesapmakinesi
 *     title=Hesap%20Makinesi
 *     category=Araclar
 *     version=1.0
 *     publisher=Arctian
 *     size=24576
 *     desc=...
 *     package=/packages/hesapmakinesi.wlf
 *     icon=<percent-encoded SVG>
 *
 * Taban adres sabittir (DS_DEFAULT_URL).
 * ================================================================== */
#include "app.h"
#include "wlf.h"
#include "adl_wolf.h"
#include "svg.h"
#include <stdint.h>

typedef struct { uint32_t hash; int size; uint32_t *buf; } ds_icon_cache_t;
static ds_icon_cache_t g_icon_cache[96];
static int g_icon_cache_n;
static uint32_t ds_icon_hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; s && *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h ? h : 1u;
}

#define DS_MAX_APPS   32
#define DS_MAX_WALLS  24
#define DS_URL_MAX    160
#define DS_ICON_MAX   1200
#define DS_DESC_MAX   220
#define DS_PKG_MAX    160
#define DS_IMG_MAX    160
#define DS_QUERY_MAX  64
#define DS_WP_MAX     (6u * 1024u * 1024u)   /* arka plan PNG ust siniri (~6 MB) */
#define DS_WP_SLACK   (8u * 1024u)           /* HTTP basliklari icin pay */
#define DS_DEFAULT_URL "http://45.143.99.75:8090"

/* ---------------- Durum ---------------- */
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
    char perm[64];
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

/* Yuklu (.wp) arka plan dosya adlari (sayfa cizimi icin) */
static char g_inst_wp[DS_MAX_WALLS][40];
static int  g_inst_wp_count = 0;

/* Buyuk indirmeler icin yeniden kullanilan tampon */
static uint8_t  *g_dl_buf = 0;
static uint32_t  g_dl_cap = 0;

static char     g_base_url[DS_URL_MAX];
static char     g_query[DS_QUERY_MAX];
static char     g_filter_cat[32];

static ds_page_t g_page = PAGE_HOME;
static int       g_detail_idx = -1;
static int       g_confirm_idx = -1;

static lv_obj_t *g_win;
static lv_obj_t *g_toolbar;
static lv_obj_t *g_body;
static lv_obj_t *g_nav;
static lv_obj_t *g_content;
static lv_obj_t *g_status;
static lv_obj_t *g_nav_btns[6];
static lv_obj_t *g_nav_rod[6];
static lv_obj_t *g_nav_lbl[6];
static lv_obj_t *g_search_ta;
static lv_obj_t *g_back_btn;
static lv_obj_t *g_fwd_btn;

/* Sayfa gecmisi (geri / ileri dugmeleri icin tek adim) */
typedef struct { ds_page_t page; int detail; } ds_hist_t;
static ds_hist_t g_back_st  = { PAGE_HOME, -1 };
static ds_hist_t g_fwd_st   = { PAGE_HOME, -1 };
static int       g_has_back = 0;
static int       g_has_fwd  = 0;

/* Spec basliklari icin ek tiny-TTF boyutlari (tema fontu15 px'tir) */
extern const unsigned char font_ttf[];
extern const unsigned int  font_ttf_len;
static lv_font_t *s_font_big = 0;     /*22 px — hero basligi */
static lv_font_t *s_font_small = 0;   /*11 px — etiket / rozet */
static lv_font_t *s_font_sec = 0;     /*15 px — bolum basligi */

/* Nav ikonlari (spec:24 viewBox,1.8 stroke,16 px, currentColor ≈ #C9CED8) */
static const char *DS_NAV_IC[6][2] = {
    /* pasif / aktif ayni; aktiflik zemin + sol cubuk + metin rengi ile */
    { "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#C9CED8\" stroke-width=\"1.8\"><path d=\"M3 11l9-7 9 7v9a1 1 0 0 1-1 1h-5v-6H9v6H4a1 1 0 0 1-1-1z\"/></svg>", 0 },
    { "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#C9CED8\" stroke-width=\"1.8\"><circle cx=\"11\" cy=\"11\" r=\"7\"/><path d=\"M20 20l-4-4\"/></svg>", 0 },
    { "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#C9CED8\" stroke-width=\"1.8\"><rect x=\"4\" y=\"4\" width=\"6\" height=\"6\" rx=\"1\"/><rect x=\"14\" y=\"4\" width=\"6\" height=\"6\" rx=\"1\"/><rect x=\"4\" y=\"14\" width=\"6\" height=\"6\" rx=\"1\"/><rect x=\"14\" y=\"14\" width=\"6\" height=\"6\" rx=\"1\"/></svg>", 0 },
    { "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#C9CED8\" stroke-width=\"1.8\"><path d=\"M4 4h16v12H4zM8 20h8M12 16v4\"/></svg>", 0 },
    { "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#C9CED8\" stroke-width=\"1.8\"><path d=\"M12 4v12m0 0l-5-5m5 5l5-5M4 20h16\"/></svg>", 0 },
    { "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#C9CED8\" stroke-width=\"1.8\"><rect x=\"3\" y=\"4\" width=\"18\" height=\"16\" rx=\"2\"/><circle cx=\"9\" cy=\"10\" r=\"2\"/><path d=\"M21 17l-5-5-9 9\"/></svg>", 0 }
};

/* ---------------- Kucuk string yardimcilari ---------------- */
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

/* ---------------- Renk / ikon ---------------- */
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
    uint32_t h = ds_icon_hash(svg);
    for (int i = 0; i < g_icon_cache_n; i++) {
        if (g_icon_cache[i].hash == h && g_icon_cache[i].size == size) {
            lv_obj_t *cc = lv_canvas_create(parent);
            lv_obj_remove_style_all(cc);
            lv_canvas_set_buffer(cc, g_icon_cache[i].buf, size, size, LV_COLOR_FORMAT_ARGB8888);
            return cc;
        }
    }
    uint32_t *buf = (uint32_t *)s_asi->mem->alloc((size_t)size * size * 4u);
    if (!buf) return 0;
    if (svg_render(svg, size, size, buf) != 0) return 0;
    if (g_icon_cache_n < 96) {
        g_icon_cache[g_icon_cache_n].hash = h;
        g_icon_cache[g_icon_cache_n].size = size;
        g_icon_cache[g_icon_cache_n].buf = buf;
        g_icon_cache_n++;
    }
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

/* Bir uygulama icin ikon (SVG varsa) veya renkli harf kutucugu uretir. */
static lv_obj_t *ds_app_icon(lv_obj_t *parent, const ds_app_t *a, int size) {
    lv_obj_t *ic = ds_svg_icon(parent, a->icon, size);
    if (ic) return ic;
    return ds_tile(parent, a->title[0] ? a->title : a->id, ds_color_for(a->id), size, size / 5);
}

/* ---------------- Yardimci widget'lar ---------------- */
/* Spec: ana dugme30-32 px,6 px yaricap, vurgu zemin, beyaz; ikincil
 * dugme koyu zemin + kenarlik. */
static lv_obj_t *ds_button(lv_obj_t *parent, const char *txt, uint32_t bg,
                           uint32_t fg, int w, int h) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
    return b;
}

/* Ikincil dugme: koyu zemin +1 px kenarlik (spec: #1e2129 / #333a47) */
static lv_obj_t *ds_button_ol(lv_obj_t *parent, const char *txt, int w, int h) {
    lv_obj_t *b = ds_button(parent, txt, ADL_SURFACE_2, ADL_TEXT, w, h);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(ADL_BORDER), 0);
    return b;
}

/*16 px stroke ikon (spec:24 viewBox,1.8 stroke) */
static lv_obj_t *ds_icon(lv_obj_t *parent, const char *svg, int size) {
    lv_obj_t *ic = ds_svg_icon(parent, svg, size);
    if (ic) return ic;
    return ds_tile(parent, "?", ADL_SURFACE_2, size, 4);
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

/* ---------------- Ag ---------------- */
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

/* ---------------- Yapilandirma (sabit sunucu) ---------------- */
static void ds_cfg_load(void) {
    dcpy(g_base_url, sizeof(g_base_url), DS_DEFAULT_URL);
}

/* ---------------- Feed ayristirma ---------------- */
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
        else if (key_is(key, klen, "permissions")) dcpy(cur->perm, sizeof(cur->perm), val);
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

/* ---------------- Arama (sunucu, yoksa yerel) ---------------- */
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

/* ---------------- Arka plan (wallpaper) feed ---------------- */
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

/* ---------------- Islemler ---------------- */
static void ds_rebuild(void);

/* ---------------- Arka plan (fiber) ag isleri ----------------
 * Ag islemleri kendi yigini uzerinde calisir; arayuz donmaz. Is biter
 * UI karesinde done() cagrilir. Ayni anda tek is calisir, fazlasi kuyruga alinir. */
typedef struct {
    void (*work)(void *user);
    void (*done)(void *user);
    void *user;
} ds_job_t;

static ds_job_t g_job;
static ds_job_t g_pending;
static int      g_has_pending = 0;

static void ds_job_run(void *arg) {
    ds_job_t *j = (ds_job_t *)arg;
    j->work(j->user);
}

static void ds_job_finish(int status, void *arg) {
    (void)status; (void)arg;
    ds_job_t j = g_job;
    if (j.done) j.done(j.user);
    if (g_has_pending) {
        g_has_pending = 0;
        g_job = g_pending;
        if (s_asi->net->submit(ds_job_run, ds_job_finish, &g_job) != 0) g_job.done = 0;
    }
}

/* Isi arka plana alir (1) veya fiber yoksa senkron icin 0 dondurur. */
static int ds_async(void (*work)(void *), void (*done)(void *), void *user) {
    if (!s_asi || !s_asi->net || !s_asi->net->submit) return 0;
    if (s_asi->net->busy && s_asi->net->busy()) {
        g_pending.work = work; g_pending.done = done; g_pending.user = user;
        g_has_pending = 1;
        return 1;
    }
    g_job.work = work; g_job.done = done; g_job.user = user;
    return s_asi->net->submit(ds_job_run, ds_job_finish, &g_job) == 0 ? 1 : 0;
}

/* Feed (uygulama listesi) */
static int g_feed_rc;
static void ds_feed_work(void *u) { (void)u; g_feed_rc = ds_feed_load(); }
static void ds_feed_done(void *u) {
    (void)u;
    if (g_feed_rc == 0) ds_status("", ADL_OK);
    else ds_status("Sunucuya ulasilamadi", ADL_DANGER);
    ds_rebuild();
}
static void ds_feed_fetch(void) {
    if (!ds_async(ds_feed_work, ds_feed_done, 0)) {
        g_feed_rc = ds_feed_load();
        ds_feed_done(0);
    }
}

/* Sunucu arama */
static int g_search_rc;
static void ds_search_work(void *u) { (void)u; g_search_rc = ds_search_server(g_query); }
static void ds_search_done(void *u) {
    (void)u;
    if (g_search_rc != 0 && g_app_count == 0) { ds_feed_fetch(); return; }
    ds_status("", ADL_OK);
    ds_rebuild();
}
static void ds_search_fetch(void) {
    if (!ds_async(ds_search_work, ds_search_done, 0)) {
        g_search_rc = ds_search_server(g_query);
        ds_search_done(0);
    }
}

/* Arka plan (wallpaper) listesi */
static int g_walls_rc;
static void ds_walls_work(void *u) { (void)u; g_walls_rc = ds_walls_load(); }
static void ds_walls_done(void *u) {
    (void)u;
    if (g_walls_rc == 0) ds_status("", ADL_OK);
    else ds_status("Sunucuya ulasilamadi", ADL_DANGER);
    ds_rebuild();
}
static void ds_walls_fetch(void) {
    if (!ds_async(ds_walls_work, ds_walls_done, 0)) {
        g_walls_rc = ds_walls_load();
        ds_walls_done(0);
    }
}

/* Uygulama kurulumu: indirme fiber'de, kurulum (UI) done'da */
static char g_inst_url[DS_PKG_MAX + DS_URL_MAX];
static const char *g_inst_body;
static uint32_t g_inst_len;

static void ds_install_work(void *u) {
    (void)u;
    g_inst_body = 0;
    g_inst_len = 0;
    uint32_t len = 0;
    const char *body = ds_http_get(g_inst_url, &len);
    if (body && len > 0) { g_inst_body = body; g_inst_len = len; }
}

static void ds_install_done(void *u) {
    (void)u;
    if (!g_inst_body || g_inst_len == 0 || g_inst_len > WLF_MAX_PACKAGE) {
        ds_status("Indirilemedi (ag/sunucu)", ADL_DANGER);
        ds_render_now();
        ds_rebuild();
        return;
    }
    char err[110];
    int r = arctian_store->install(g_inst_body, g_inst_len, err, sizeof(err));
    if (r == 0) ds_status("Kuruldu", ADL_OK);
    else ds_status(err[0] ? err : "Kurulamadi", ADL_DANGER);
    ds_render_now();
    ds_rebuild();
}

static void ds_do_install(int idx) {
    if (idx < 0 || idx >= g_app_count) return;
    ds_app_t *a = &g_apps[idx];
    if (a->perm[0] && g_confirm_idx != idx) {
        g_confirm_idx = idx;
        g_detail_idx = idx;
        g_page = PAGE_DETAIL;
        ds_rebuild();
        return;
    }
    g_confirm_idx = -1;
    if (!a->pkg[0]) { ds_status("Paket adresi yok", ADL_DANGER); ds_render_now(); return; }
    if (!arctian_store || !arctian_store->install) {
        ds_status("Uygulama platformu yok", ADL_DANGER); ds_render_now(); return;
    }
    ds_make_url(g_inst_url, sizeof(g_inst_url), a->pkg);
    ds_status("Indiriliyor...", ADL_WARN);
    ds_render_now();

    if (!ds_async(ds_install_work, ds_install_done, 0)) {
        ds_install_work(0);
        ds_install_done(0);
    }
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

/* ---------------- Navigasyon ---------------- */
/* Spec: aktif oge #1e2330 zemin + sol kenarda3 px #60a5fa cubuk,
 * metin beyaz; pasif oge zeminsiz, soluk metin. */
static void ds_set_nav_active(void) {
    static const ds_page_t map[6] = {
        PAGE_HOME, PAGE_SEARCH, PAGE_CATS, PAGE_LIB, PAGE_UPDATES, PAGE_WALLS
    };
    for (int i = 0; i < 6; i++) {
        if (!g_nav_btns[i]) continue;
        int active = (g_page == map[i]);
        lv_obj_set_style_bg_color(g_nav_btns[i],
            lv_color_hex(active ? 0x1E2330 : ADL_SURFACE), 0);
        if (g_nav_rod[i]) {
            if (active) lv_obj_remove_flag(g_nav_rod[i], LV_OBJ_FLAG_HIDDEN);
            else        lv_obj_add_flag(g_nav_rod[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (g_nav_lbl[i]) lv_obj_set_style_text_color(g_nav_lbl[i],
            lv_color_hex(active ? 0xFFFFFF : 0xB4BAC5), 0);
    }
}

typedef struct { int page; } ds_navctx_t;
static ds_navctx_t g_navctx[6];

static void ds_nav_cb(lv_event_t *e) {
    ds_navctx_t *c = (ds_navctx_t *)lv_event_get_user_data(e);
    if (!c) return;
    if ((ds_page_t)c->page != g_page) {
        g_back_st.page = g_page; g_back_st.detail = g_detail_idx;
        g_has_back = 1; g_has_fwd = 0;
    }
    g_page = (ds_page_t)c->page;
    g_detail_idx = -1;
    ds_rebuild();
    if (g_page == PAGE_HOME) ds_feed_fetch();
}

/* Arac satiri geri / ileri (tek adimlik sayfa gecmisi) */
static void ds_tb_back_cb(lv_event_t *e) {
    (void)e;
    if (!g_has_back) return;
    g_fwd_st.page = g_page; g_fwd_st.detail = g_detail_idx; g_has_fwd = 1;
    g_page = g_back_st.page;
    g_detail_idx = g_back_st.detail;
    g_has_back = 0;
    g_confirm_idx = -1;
    ds_rebuild();
}

static void ds_tb_fwd_cb(lv_event_t *e) {
    (void)e;
    if (!g_has_fwd) return;
    g_back_st.page = g_page; g_back_st.detail = g_detail_idx; g_has_back = 1;
    g_page = g_fwd_st.page;
    g_detail_idx = g_fwd_st.detail;
    g_has_fwd = 0;
    g_confirm_idx = -1;
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

static void ds_confirm_yes_cb(lv_event_t *e) {
    (void)e;
    int idx = g_confirm_idx;
    if (idx >= 0) ds_do_install(idx);
}

static void ds_confirm_no_cb(lv_event_t *e) {
    (void)e;
    g_confirm_idx = -1;
    ds_rebuild();
}

static void ds_detail_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_back_st.page = g_page; g_back_st.detail = g_detail_idx;
    g_has_back = 1; g_has_fwd = 0;
    g_detail_idx = idx;
    g_confirm_idx = -1;
    g_page = PAGE_DETAIL;
    ds_rebuild();
}

static void ds_back_cb(lv_event_t *e) {
    (void)e;
    if (g_has_back) { ds_tb_back_cb(e); return; }
    g_detail_idx = -1;
    g_confirm_idx = -1;
    g_page = PAGE_HOME;
    ds_rebuild();
}

static void ds_uninstall_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ds_do_uninstall(idx);
}

/* Kitapliktaki kurulu uygulamalar icin id tabanli islemler (feed'de olmasa da calisir). */
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
    ds_feed_fetch();
}

static void ds_search_cb(lv_event_t *e) {
    (void)e;
    const char *q = g_search_ta ? lv_textarea_get_text(g_search_ta) : "";
    dcpy(g_query, sizeof(g_query), q);
    g_page = PAGE_SEARCH;
    if (g_query[0]) {
        ds_status("Araniyor...", ADL_WARN);
        ds_render_now();
        ds_search_fetch();
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
    g_win = 0; g_toolbar = 0; g_body = 0;
    g_nav = 0; g_content = 0; g_status = 0;
    g_search_ta = 0; g_back_btn = 0; g_fwd_btn = 0;
    g_has_back = 0; g_has_fwd = 0;
    for (int i = 0; i < 6; i++) {
        g_nav_btns[i] = 0; g_nav_rod[i] = 0; g_nav_lbl[i] = 0;
    }
    if (s_font_big)   { lv_tiny_ttf_destroy(s_font_big);   s_font_big = 0; }
    if (s_font_small) { lv_tiny_ttf_destroy(s_font_small); s_font_small = 0; }
    if (s_font_sec)   { lv_tiny_ttf_destroy(s_font_sec);   s_font_sec = 0; }
}

/* ---------------- Kartlar ---------------- */
static int ds_app_matches(const ds_app_t *a) {
    if (!g_query[0] && !g_filter_cat[0]) return 1;
    int ok = 1;
    if (g_query[0])
        ok = dhas_ci(a->title, g_query) || dhas_ci(a->publisher, g_query) ||
             dhas_ci(a->category, g_query) || dhas_ci(a->id, g_query);
    if (ok && g_filter_cat[0]) ok = deq_ci(a->category, g_filter_cat);
    return ok;
}

/* Spec kartlari:1 px #262b35 kenarlik, #181b22 zemin,8 px yaricap,
 *12 px ic bosluk;36 px ikon, ad,12 px meta,26 px kucuk dugme. */
static lv_obj_t *ds_card(lv_obj_t *parent, int idx) {
    ds_app_t *a = &g_apps[idx];
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 208, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(c, 8, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    lv_obj_set_style_pad_row(c, 6, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *ic = ds_app_icon(c, a, 36);

    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, a->title[0] ? a->title : a->id);
    lv_obj_set_width(t, LV_PCT(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);

    lv_obj_t *cat = lv_label_create(c);
    lv_label_set_text_fmt(cat, "%s  -  %s",
        a->category[0] ? a->category : "Genel",
        a->version[0] ? a->version : "-");
    lv_obj_set_width(cat, LV_PCT(100));
    lv_label_set_long_mode(cat, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(cat, lv_color_hex(ADL_TEXT_DIM), 0);
    if (s_font_small) lv_obj_set_style_text_font(cat, s_font_small, 0);

    int inst = ds_installed(a->id);
    lv_obj_t *b = ds_button(c, inst ? "Ac" : "Yukle",
                            inst ? ADL_OK : ADL_ACCENT, 0xFFFFFF, 76, 26);
    if (s_font_small) lv_obj_set_style_text_font(b, s_font_small, 0);
    if (inst) lv_obj_add_event_cb(b, ds_open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    else      lv_obj_add_event_cb(b, ds_install_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_add_event_cb(c, ds_detail_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    return c;
}

static lv_obj_t *ds_section(lv_obj_t *parent, const char *title) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, title);
    lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
    if (s_font_sec) lv_obj_set_style_text_font(l, s_font_sec, 0);
    lv_obj_set_style_pad_top(l, 14, 0);
    lv_obj_set_style_pad_bottom(l, 6, 0);
    return l;
}

static lv_obj_t *ds_grid(lv_obj_t *parent) {
    lv_obj_t *g = lv_obj_create(parent);
    lv_obj_remove_style_all(g);
    lv_obj_set_width(g, LV_PCT(100));
    lv_obj_set_height(g, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(g, 10, 0);
    lv_obj_set_style_pad_row(g, 10, 0);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
    return g;
}

/* Spec:150 px one cikan kart — koyu panel + kenarlik,88 px ikon kutusu,
 * "BU HAFTA SECILDI" etiketi,22 px baslik, meta ve iki dugme. */
static lv_obj_t *ds_hero(lv_obj_t *parent, int idx) {
    ds_app_t *a = &g_apps[idx];
    lv_obj_t *hero = lv_obj_create(parent);
    lv_obj_remove_style_all(hero);
    lv_obj_set_size(hero, LV_PCT(100), 150);
    lv_obj_set_style_radius(hero, 10, 0);
    lv_obj_set_style_bg_color(hero, lv_color_hex(0x1A2233), 0);
    lv_obj_set_style_bg_opa(hero, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hero, 1, 0);
    lv_obj_set_style_border_color(hero, lv_color_hex(0x243047), 0);
    lv_obj_set_style_pad_all(hero, 20, 0);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hero, 20, 0);
    lv_obj_remove_flag(hero, LV_OBJ_FLAG_SCROLLABLE);

    /*88 px ikon kutusu (20 px yaricap, #2b3a5c) */
    lv_obj_t *ibox = lv_obj_create(hero);
    lv_obj_remove_style_all(ibox);
    lv_obj_set_size(ibox, 88, 88);
    lv_obj_set_style_radius(ibox, 20, 0);
    lv_obj_set_style_bg_color(ibox, lv_color_hex(0x2B3A5C), 0);
    lv_obj_set_style_bg_opa(ibox, LV_OPA_COVER, 0);
    lv_obj_t *ic = ds_app_icon(ibox, a, 44);
    if (ic) lv_obj_center(ic);

    lv_obj_t *col = lv_obj_create(hero);
    lv_obj_remove_style_all(col);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *kick = lv_label_create(col);
    lv_label_set_text(kick, "BU HAFTA SECILDI");
    lv_obj_set_style_text_color(kick, lv_color_hex(0x7FA8F0), 0);
    if (s_font_small) lv_obj_set_style_text_font(kick, s_font_small, 0);
    lv_obj_set_style_text_letter_space(kick, 2, 0);

    lv_obj_t *ttl = lv_label_create(col);
    lv_label_set_text(ttl, a->title[0] ? a->title : a->id);
    lv_obj_set_width(ttl, LV_PCT(100));
    lv_label_set_long_mode(ttl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(ttl, lv_color_hex(ADL_TEXT), 0);
    if (s_font_big) lv_obj_set_style_text_font(ttl, s_font_big, 0);

    lv_obj_t *meta = lv_label_create(col);
    lv_label_set_text_fmt(meta, "%s  -  %s  -  %s KB",
        a->publisher[0] ? a->publisher : "Bilinmiyor",
        a->category[0] ? a->category : "Genel",
        a->size[0] ? a->size : "?");
    lv_obj_set_style_text_color(meta, lv_color_hex(ADL_TEXT_DIM), 0);
    if (s_font_small) lv_obj_set_style_text_font(meta, s_font_small, 0);

    int inst = ds_installed(a->id);
    lv_obj_t *br = lv_obj_create(col);
    lv_obj_remove_style_all(br);
    lv_obj_set_size(br, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(br, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(br, 8, 0);
    lv_obj_set_style_pad_top(br, 6, 0);
    lv_obj_remove_flag(br, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *b1 = ds_button(br, inst ? "Ac" : "Al",
                             inst ? ADL_OK : ADL_ACCENT, 0xFFFFFF, 92, 32);
    if (inst) lv_obj_add_event_cb(b1, ds_open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    else      lv_obj_add_event_cb(b1, ds_install_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    lv_obj_t *b2 = ds_button_ol(br, "Ayrintilar", 92, 32);
    lv_obj_add_event_cb(b2, ds_detail_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    return hero;
}

/* Spec "Onerilenler" karti: esnek genislik,36 px ikon, ad, meta,26 px Al */
static lv_obj_t *ds_rec_card(lv_obj_t *parent, int idx) {
    ds_app_t *a = &g_apps[idx];
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_radius(c, 8, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    lv_obj_set_style_pad_row(c, 6, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *ic = ds_app_icon(c, a, 36);

    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, a->title[0] ? a->title : a->id);
    lv_obj_set_width(t, LV_PCT(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);

    lv_obj_t *mt = lv_label_create(c);
    lv_label_set_text_fmt(mt, "%s  -  %s",
        a->category[0] ? a->category : "Genel",
        a->version[0] ? a->version : "-");
    lv_obj_set_width(mt, LV_PCT(100));
    lv_label_set_long_mode(mt, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(mt, lv_color_hex(ADL_TEXT_DIM), 0);
    if (s_font_small) lv_obj_set_style_text_font(mt, s_font_small, 0);

    int inst = ds_installed(a->id);
    lv_obj_t *b = ds_button(c, inst ? "Ac" : "Al",
                            inst ? ADL_OK : ADL_ACCENT, 0xFFFFFF, 76, 26);
    if (s_font_small) lv_obj_set_style_text_font(b, s_font_small, 0);
    lv_obj_set_style_pad_top(b, 4, 0);
    if (inst) lv_obj_add_event_cb(b, ds_open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    else      lv_obj_add_event_cb(b, ds_install_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    lv_obj_add_event_cb(c, ds_detail_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    return c;
}

/* Spec "Populer" listesi:50 px satir,1 px alt cizgi, sira no,30 px ikon,
 * ad + meta, boyut sutunu (70 px),28 px Al dugmesi. */
static void ds_pop_row(lv_obj_t *parent, int idx, int rank) {
    ds_app_t *a = &g_apps[idx];
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 50);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x1F232B), 0);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *no = lv_label_create(row);
    lv_label_set_text_fmt(no, "%d", rank);
    lv_obj_set_style_text_color(no, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_set_width(no, 20);
    if (s_font_small) lv_obj_set_style_text_font(no, s_font_small, 0);

    lv_obj_t *ic = ds_app_icon(row, a, 30);

    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 1, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(col);
    lv_label_set_text(t, a->title[0] ? a->title : a->id);
    lv_obj_set_width(t, LV_PCT(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);

    lv_obj_t *mt = lv_label_create(col);
    lv_label_set_text_fmt(mt, "%s  -  %s",
        a->category[0] ? a->category : "Genel",
        a->publisher[0] ? a->publisher : "-");
    lv_obj_set_width(mt, LV_PCT(100));
    lv_label_set_long_mode(mt, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(mt, lv_color_hex(ADL_TEXT_DIM), 0);
    if (s_font_small) lv_obj_set_style_text_font(mt, s_font_small, 0);

    lv_obj_t *sz = lv_label_create(row);
    lv_label_set_text_fmt(sz, "%s KB", a->size[0] ? a->size : "?");
    lv_obj_set_width(sz, 70);
    lv_label_set_long_mode(sz, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(sz, lv_color_hex(ADL_TEXT_DIM), 0);
    if (s_font_small) lv_obj_set_style_text_font(sz, s_font_small, 0);

    int inst = ds_installed(a->id);
    lv_obj_t *b = ds_button(row, inst ? "Ac" : "Al",
                            inst ? ADL_OK : ADL_ACCENT, 0xFFFFFF, 74, 28);
    if (s_font_small) lv_obj_set_style_text_font(b, s_font_small, 0);
    if (inst) lv_obj_add_event_cb(b, ds_open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    else      lv_obj_add_event_cb(b, ds_install_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_add_event_cb(row, ds_detail_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
}

/* ---------------- Sayfalar ---------------- */
static void ds_page_home(void) {
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

    if (g_app_count == 0) {
        ds_section(g_content, "MAGAZA");
        lv_obj_t *m = lv_label_create(g_content);
        lv_label_set_text(m, "Henuz uygulama yok.");
        lv_obj_set_style_text_color(m, lv_color_hex(ADL_TEXT_DIM), 0);
        return;
    }

    /*1. One cikan uygulama (spec hero) */
    ds_hero(g_content, 0);

    /*2. Onerilenler: uc esit kart + "Tumunu gor" */
    if (g_app_count > 1) {
        lv_obj_t *hdr = lv_obj_create(g_content);
        lv_obj_remove_style_all(hdr);
        lv_obj_set_size(hdr, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_top(hdr, 10, 0);
        lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *ht = lv_label_create(hdr);
        lv_label_set_text(ht, "Onerilenler");
        lv_obj_set_style_text_color(ht, lv_color_hex(ADL_TEXT), 0);
        if (s_font_sec) lv_obj_set_style_text_font(ht, s_font_sec, 0);
        lv_obj_t *more = lv_label_create(hdr);
        lv_label_set_text(more, "Tumunu gor >");
        lv_obj_set_style_text_color(more, lv_color_hex(ADL_TEXT_DIM), 0);
        if (s_font_small) lv_obj_set_style_text_font(more, s_font_small, 0);

        lv_obj_t *row = lv_obj_create(g_content);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        int n = g_app_count < 4 ? g_app_count : 4;
        for (int i = 1; i < n; i++) ds_rec_card(row, i);
    }

    /*3. Populer: uygulamalarin sirali listesi (ilk alti) */
    if (g_app_count > 0) {
        ds_section(g_content, "Populer");
        lv_obj_t *col = lv_obj_create(g_content);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        int rank = 1;
        int lim = g_app_count < 6 ? g_app_count : 6;
        for (int i = 0; i < lim; i++) ds_pop_row(col, i, rank++);
    }
}

static void ds_page_search(void) {
    /* Arama kutusu arac satirinda yasar (spec: tek arama kutusu);
     * bu sayfa yalnizca sonuclari gosterir. */
    if (g_search_ta && g_query[0]) lv_textarea_set_text(g_search_ta, g_query);

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

    /* Kategorileri feed'den topla (tekil). */
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
        /* user_data: kalici isaretci gerekir; g_apps icindeki kategori dizisi sabit. */
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
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < cnt; i++) {
        const char *id = arctian_store->installed_id(i);
        const char *title = arctian_store->installed_title(i);
        const char *ver = arctian_store->installed_version_at(i);
        ds_app_t *fa = ds_find_feed(id);

        /* Spec satiri:50 px,1 px alt cizgi,30 px ikon, ad + meta,28 px dugmeler */
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 50);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x1F232B), 0);
        lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);

        lv_obj_t *ic = fa ? ds_app_icon(row, fa, 30)
                          : ds_tile(row, title, ds_color_for(id), 30, 7);
    
        lv_obj_t *col = lv_obj_create(row);
        lv_obj_remove_style_all(col);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_style_pad_row(col, 1, 0);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *tl = lv_label_create(col);
        lv_label_set_text(tl, title);
        lv_obj_set_style_text_color(tl, lv_color_hex(ADL_TEXT), 0);
        lv_obj_set_width(tl, LV_PCT(100));
        lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);

        lv_obj_t *vl = lv_label_create(col);
        lv_label_set_text_fmt(vl, "Surum %s", ver && ver[0] ? ver : "-");
        lv_obj_set_style_text_color(vl, lv_color_hex(ADL_TEXT_DIM), 0);
        if (s_font_small) lv_obj_set_style_text_font(vl, s_font_small, 0);

        lv_obj_t *ob = ds_button(row, "Ac", ADL_OK, 0xFFFFFF, 60, 28);
        if (s_font_small) lv_obj_set_style_text_font(ob, s_font_small, 0);
        lv_obj_add_event_cb(ob, ds_open_id_cb, LV_EVENT_CLICKED, (void *)id);

        lv_obj_t *db = ds_button_ol(row, "Kaldir", 74, 28);
        if (s_font_small) lv_obj_set_style_text_font(db, s_font_small, 0);
        lv_obj_add_event_cb(db, ds_uninstall_id_cb, LV_EVENT_CLICKED, (void *)id);
    }
}

static void ds_page_updates(void) {
    lv_obj_t *hdr = lv_obj_create(g_content);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_bottom(hdr, 6, 0);
    lv_obj_t *ht = lv_label_create(hdr);
    lv_label_set_text(ht, "Guncellemeler");
    lv_obj_set_style_text_color(ht, lv_color_hex(ADL_TEXT), 0);
    lv_obj_t *rb = ds_button_ol(hdr, "Yenile", 76, 28);
    if (s_font_small) lv_obj_set_style_text_font(rb, s_font_small, 0);
    lv_obj_add_event_cb(rb, ds_refresh_cb, LV_EVENT_CLICKED, 0);

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

/* ---------------- Arka plan islemleri ---------------- */
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
/* Arka plan indirme: ag + disk yazimi fiber'de, uygulama (UI) done'da. */
static int  g_wall_dl_idx = -1;
static char g_wall_dl_url[DS_IMG_MAX + DS_URL_MAX];
static char g_wall_dl_fn[40];
static uint32_t g_wall_dl_len = 0;
static int  g_wall_dl_rc = -1;

static void ds_wall_dl_work(void *u) {
    (void)u;
    g_wall_dl_rc = -1;
    g_wall_dl_len = 0;
    g_wall_dl_fn[0] = 0;

    uint32_t len = 0;
    int r = s_asi->net->download(g_wall_dl_url, g_dl_buf, g_dl_cap, &len);
    if (r != 0 || len < 8 || len > DS_WP_MAX) return;

    if (!s_asi->fs || !s_asi->fs->create) return;
    ds_wall_fname(g_walls[g_wall_dl_idx].id, g_wall_dl_fn, sizeof(g_wall_dl_fn));
    void *h = s_asi->fs->create(0, g_wall_dl_fn, 0);
    if (!h || s_asi->fs->write(h, g_dl_buf, len) != 0) { g_wall_dl_fn[0] = 0; return; }

    g_wall_dl_len = len;
    g_wall_dl_rc = 0;
}

static void ds_wall_dl_done(void *u) {
    (void)u;
    if (g_wall_dl_fn[0] && g_wall_dl_rc == 0) {
        ds_wall_apply_file(g_wall_dl_fn);
        return;
    }
    if (!s_asi->fs || !s_asi->fs->create) ds_status("AFS yok", ADL_DANGER);
    else if (!g_dl_buf) ds_status("Bellek yok", ADL_DANGER);
    else ds_status("Indirilemedi veya cok buyuk", ADL_DANGER);
    ds_render_now();
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

    g_wall_dl_idx = idx;
    ds_make_url(g_wall_dl_url, sizeof(g_wall_dl_url), w->img);
    g_wall_dl_rc = -1;
    g_wall_dl_len = 0;
    g_wall_dl_fn[0] = 0;
    ds_status("Indiriliyor...", ADL_WARN);
    ds_render_now();

    if (!ds_async(ds_wall_dl_work, ds_wall_dl_done, 0)) {
        ds_wall_dl_work(0);
        ds_wall_dl_done(0);
    }
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
    ds_walls_fetch();
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

    /* Yuklu (.wp) arka planlar (AFS) */
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

    /* Magazadan indirilebilir arka planlar */
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

    lv_obj_t *back = ds_button_ol(g_content, "< Geri", 78, 30);
    if (s_font_small) lv_obj_set_style_text_font(back, s_font_small, 0);
    lv_obj_add_event_cb(back, ds_back_cb, LV_EVENT_CLICKED, 0);

    /* Baslik paneli (spec hero dili: koyu panel,88 px ikon kutusu,22 px ad) */
    lv_obj_t *head = lv_obj_create(g_content);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), 140);
    lv_obj_set_style_radius(head, 10, 0);
    lv_obj_set_style_bg_color(head, lv_color_hex(0x1A2233), 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(head, 1, 0);
    lv_obj_set_style_border_color(head, lv_color_hex(0x243047), 0);
    lv_obj_set_style_pad_all(head, 20, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 20, 0);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ibox = lv_obj_create(head);
    lv_obj_remove_style_all(ibox);
    lv_obj_set_size(ibox, 88, 88);
    lv_obj_set_style_radius(ibox, 20, 0);
    lv_obj_set_style_bg_color(ibox, lv_color_hex(0x2B3A5C), 0);
    lv_obj_set_style_bg_opa(ibox, LV_OPA_COVER, 0);
    lv_obj_t *ic = ds_app_icon(ibox, a, 44);
    if (ic) lv_obj_center(ic);

    lv_obj_t *col = lv_obj_create(head);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(col, 3, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(col);
    lv_label_set_text(t, a->title[0] ? a->title : a->id);
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);
    if (s_font_big) lv_obj_set_style_text_font(t, s_font_big, 0);
    lv_obj_set_width(t, LV_PCT(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);

    lv_obj_t *pub = lv_label_create(col);
    lv_label_set_text_fmt(pub, "%s  -  %s", a->publisher[0] ? a->publisher : "Bilinmiyor",
                          a->category[0] ? a->category : "Genel");
    lv_obj_set_style_text_color(pub, lv_color_hex(ADL_TEXT_DIM), 0);
    if (s_font_small) lv_obj_set_style_text_font(pub, s_font_small, 0);

    lv_obj_t *meta = lv_label_create(col);
    lv_label_set_text_fmt(meta, "Surum %s   Boyut %s KB",
                          a->version[0] ? a->version : "-",
                          a->size[0] ? a->size : "?");
    lv_obj_set_style_text_color(meta, lv_color_hex(ADL_TEXT_DIM), 0);
    if (s_font_small) lv_obj_set_style_text_font(meta, s_font_small, 0);

    lv_obj_t *br = lv_obj_create(col);
    lv_obj_remove_style_all(br);
    lv_obj_set_size(br, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(br, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(br, 8, 0);
    lv_obj_set_style_pad_top(br, 6, 0);
    lv_obj_remove_flag(br, LV_OBJ_FLAG_SCROLLABLE);

    int inst = ds_installed(a->id);
    lv_obj_t *ob = ds_button(br, inst ? "Ac" : "Yukle",
                             inst ? ADL_OK : ADL_ACCENT, 0xFFFFFF, 92, 32);
    if (inst) lv_obj_add_event_cb(ob, ds_open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    else      lv_obj_add_event_cb(ob, ds_install_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    if (inst) {
        lv_obj_t *db = ds_button_ol(br, "Kaldir", 92, 32);
        lv_obj_add_event_cb(db, ds_uninstall_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    }

    ds_section(g_content, "Aciklama");
    lv_obj_t *d = lv_label_create(g_content);
    lv_obj_set_width(d, LV_PCT(96));
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    lv_label_set_text(d, a->desc[0] ? a->desc : "Aciklama yok.");
    lv_obj_set_style_text_color(d, lv_color_hex(ADL_TEXT), 0);

    if (a->perm[0]) {
        ds_section(g_content, "Istenen izinler");
        lv_obj_t *p = lv_label_create(g_content);
        lv_obj_set_width(p, LV_PCT(96));
        lv_label_set_long_mode(p, LV_LABEL_LONG_WRAP);
        lv_label_set_text_fmt(p, "%s", a->perm);
        lv_obj_set_style_text_color(p, lv_color_hex(ADL_WARN), 0);
    }

    if (g_confirm_idx == idx) {
        ds_section(g_content, "Kurulum onayi");
        lv_obj_t *warn = lv_label_create(g_content);
        lv_obj_set_width(warn, LV_PCT(96));
        lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
        lv_label_set_text(warn, "Bu uygulama yukaridaki izinleri istiyor. Kurulumu onayliyor musun?");
        lv_obj_set_style_text_color(warn, lv_color_hex(ADL_TEXT), 0);
        lv_obj_t *yes = ds_button(g_content, "Onayla ve Kur", ADL_OK, 0xFFFFFF, 170, 40);
        lv_obj_add_event_cb(yes, ds_confirm_yes_cb, LV_EVENT_CLICKED, 0);
        lv_obj_t *no = ds_button(g_content, "Vazgec", ADL_SURFACE_2, 0xFFFFFF, 130, 40);
        lv_obj_add_event_cb(no, ds_confirm_no_cb, LV_EVENT_CLICKED, 0);
    }
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

/* ---------------- Arayuz kurulumu ---------------- */
/* Spec DuckStore: sol kenar cubugu184 px, ogeler34 px +6 px yaricap,
 *16 px ikon +10 px bosluk, aktif ogede sol3 px vurgu cubugu,
 * bolum basliklari11 px, sayac rozeti mavi daire. */
static const char *g_nav_labels[6] = {
    "Ana Sayfa", "Ara", "Kategoriler", "Kitapligim", "Guncellemeler", "Arka Planlar"
};

static void ds_build_nav(void) {
    g_nav = lv_obj_create(g_body);
    lv_obj_remove_style_all(g_nav);
    lv_obj_set_size(g_nav, 184, LV_PCT(100));
    lv_obj_set_style_bg_color(g_nav, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(g_nav, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_nav, 10, 0);
    lv_obj_set_style_pad_row(g_nav, 2, 0);
    lv_obj_set_flex_flow(g_nav, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(g_nav, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(g_nav, 1, 0);
    lv_obj_set_style_border_side(g_nav, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(g_nav, lv_color_hex(0x262A33), 0);
    lv_obj_set_style_border_opa(g_nav, LV_OPA_COVER, 0);

    /* Bolum basligi (spec:11 px, #7d8491) */
    lv_obj_t *sec = lv_label_create(g_nav);
    lv_label_set_text(sec, "MAGAZA");
    lv_obj_set_style_text_color(sec, lv_color_hex(0x7D8491), 0);
    if (s_font_small) lv_obj_set_style_text_font(sec, s_font_small, 0);
    lv_obj_set_style_pad_top(sec, 2, 0);
    lv_obj_set_style_pad_bottom(sec, 4, 0);

    for (int i = 0; i < 6; i++) {
        g_navctx[i].page = i;
        lv_obj_t *b = lv_obj_create(g_nav);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, LV_PCT(100), 34);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(ADL_SURFACE), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 10, 0);
        lv_obj_set_style_pad_left(b, 10, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, ds_nav_cb, LV_EVENT_CLICKED, &g_navctx[i]);
        adl_focus_ring_attach(b);

        /* Sol aktiflik cubugu (3 px, #60a5fa; sol kenara yapisik, ilk anda gizli).
         * Flex akisindan cikarilir, aksi halde ic bosluga duser. */
        lv_obj_t *rod = lv_obj_create(b);
        lv_obj_remove_style_all(rod);
        lv_obj_set_size(rod, 3, 16);
        lv_obj_set_style_radius(rod, 2, 0);
        lv_obj_set_style_bg_color(rod, lv_color_hex(0x60A5FA), 0);
        lv_obj_set_style_bg_opa(rod, LV_OPA_COVER, 0);
        lv_obj_add_flag(rod, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(rod, LV_ALIGN_LEFT_MID, 1, 0);
        lv_obj_add_flag(rod, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *ic = ds_svg_icon(b, DS_NAV_IC[i][0], 16);
    
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, g_nav_labels[i]);
        lv_obj_set_style_text_color(l, lv_color_hex(0xB4BAC5), 0);
        if (i == 5) lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);

        g_nav_btns[i] = b;
        g_nav_rod[i] = rod;
        g_nav_lbl[i] = l;
    }

    /* Durum satiri: normalde bos; yalnizca islem geri bildirimi yazar
     * (spec'te ayri durum cubugu yok, bu yuzden sessiz ve soluk). */
    g_status = lv_label_create(g_nav);
    lv_obj_set_width(g_status, LV_PCT(100));
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_status, "");
    lv_obj_set_style_text_color(g_status, lv_color_hex(ADL_TEXT_DIM), 0);
    if (s_font_small) lv_obj_set_style_text_font(g_status, s_font_small, 0);
    lv_obj_set_style_pad_top(g_status, 12, 0);
}

/* Spec:50 px arac satiri — sol iki30 px gezinme dugmesi, sagda300x32
 * arama kutusu ve30 px daire avatar. */
static lv_obj_t *ds_toolbar_icon_btn(lv_obj_t *parent, const char *svg) {
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 30, 30);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x2C313B), 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ic = ds_icon(b, svg, 16);
    if (ic) lv_obj_center(ic);
    return b;
}

static void ds_build_toolbar(void) {
    g_toolbar = lv_obj_create(g_win);
    lv_obj_remove_style_all(g_toolbar);
    lv_obj_set_size(g_toolbar, LV_PCT(100), 50);
    lv_obj_set_style_bg_color(g_toolbar, lv_color_hex(ADL_BG), 0);
    lv_obj_set_style_bg_opa(g_toolbar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(g_toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_toolbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_toolbar, 10, 0);
    lv_obj_set_style_pad_left(g_toolbar, 14, 0);
    lv_obj_set_style_pad_right(g_toolbar, 14, 0);
    lv_obj_remove_flag(g_toolbar, LV_OBJ_FLAG_SCROLLABLE);

    g_back_btn = ds_toolbar_icon_btn(g_toolbar,
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#C9CED8\" stroke-width=\"2\"><path d=\"M15 18l-6-6 6-6\"/></svg>");
    lv_obj_add_event_cb(g_back_btn, ds_tb_back_cb, LV_EVENT_CLICKED, 0);
    g_fwd_btn = ds_toolbar_icon_btn(g_toolbar,
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#C9CED8\" stroke-width=\"2\"><path d=\"M9 18l6-6-6-6\"/></svg>");
    lv_obj_add_event_cb(g_fwd_btn, ds_tb_fwd_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *sp = lv_obj_create(g_toolbar);
    lv_obj_remove_style_all(sp);
    lv_obj_set_size(sp, 0, 1);
    lv_obj_set_flex_grow(sp, 1);

    /* Arama kutusu:300x32, #0f1115 zemin, lupe + yer tutucu */
    g_search_ta = lv_textarea_create(g_toolbar);
    lv_textarea_set_one_line(g_search_ta, true);
    lv_textarea_set_placeholder_text(g_search_ta, "Uygulamalar, oyunlar, daha fazlasi");
    lv_obj_set_size(g_search_ta, 300, 32);
    lv_obj_set_style_radius(g_search_ta, 6, 0);
    lv_obj_set_style_bg_color(g_search_ta, lv_color_hex(0x0F1115), 0);
    lv_obj_set_style_border_width(g_search_ta, 1, 0);
    lv_obj_set_style_border_color(g_search_ta, lv_color_hex(0x2C313B), 0);
    lv_obj_set_style_pad_all(g_search_ta, 6, 0);
    lv_obj_add_event_cb(g_search_ta, ds_search_cb, LV_EVENT_READY, 0);

    /* Avatar:30 px daire, "K" (spec: #2a3550 / #dbe7ff) */
    lv_obj_t *av = lv_obj_create(g_toolbar);
    lv_obj_remove_style_all(av);
    lv_obj_set_size(av, 30, 30);
    lv_obj_set_style_radius(av, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(av, lv_color_hex(0x2A3550), 0);
    lv_obj_set_style_bg_opa(av, LV_OPA_COVER, 0);
    lv_obj_t *al = lv_label_create(av);
    lv_label_set_text(al, "K");
    lv_obj_set_style_text_color(al, lv_color_hex(0xDBE7FF), 0);
    lv_obj_center(al);
}

static void ds_build_content(void) {
    g_content = lv_obj_create(g_body);
    lv_obj_remove_style_all(g_content);
    lv_obj_set_height(g_content, LV_PCT(100));
    lv_obj_set_flex_grow(g_content, 1);
    lv_obj_set_style_bg_color(g_content, lv_color_hex(ADL_BG), 0);
    lv_obj_set_style_bg_opa(g_content, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_content, 16, 0);
    lv_obj_set_style_pad_left(g_content, 20, 0);
    lv_obj_set_style_pad_right(g_content, 20, 0);
    lv_obj_set_style_pad_row(g_content, 6, 0);
    lv_obj_set_flex_flow(g_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_content, LV_DIR_VER);
}

/* ---------------- Giris noktasi ---------------- */
void app_entry(lv_obj_t *win, asi_t *asi) {
    s_asi = asi;

    g_win = win;
    g_toolbar = 0; g_body = 0;
    g_nav = 0; g_content = 0; g_status = 0;
    g_search_ta = 0; g_back_btn = 0; g_fwd_btn = 0;
    g_has_back = 0; g_has_fwd = 0;
    for (int i = 0; i < 6; i++) {
        g_nav_btns[i] = 0; g_nav_rod[i] = 0; g_nav_lbl[i] = 0;
    }

    g_app_count = 0;
    g_feed_ok = 0;
    g_query[0] = 0;
    g_filter_cat[0] = 0;
    g_detail_idx = -1;
    g_page = PAGE_HOME;

    /* Spec: dikey akis —36 px pencere basligi (iskelet) +50 px arac
     * satiri ustte, altta govde (184 px yan cubuk + ana alan). */
    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(win, 0, 0);
    lv_obj_set_style_pad_row(win, 0, 0);

    /* Spec basliklari icin yerel fontlar (tema fontu15 px'tir) */
    if (!s_font_big)   s_font_big   = lv_tiny_ttf_create_data(font_ttf, font_ttf_len, 22);
    if (!s_font_small) s_font_small = lv_tiny_ttf_create_data(font_ttf, font_ttf_len, 11);
    if (!s_font_sec)   s_font_sec   = lv_tiny_ttf_create_data(font_ttf, font_ttf_len, 15);

    ds_cfg_load();
    ds_build_toolbar();

    g_body = lv_obj_create(win);
    lv_obj_remove_style_all(g_body);
    lv_obj_set_size(g_body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(g_body, 1);
    lv_obj_set_flex_flow(g_body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(g_body, 0, 0);
    lv_obj_remove_flag(g_body, LV_OBJ_FLAG_SCROLLABLE);

    ds_build_nav();
    ds_build_content();
    lv_obj_add_event_cb(win, ds_win_del_cb, LV_EVENT_DELETE, 0);

    ds_set_nav_active();
    ds_page_home();

    ds_status("Sunucuya baglaniliyor...", ADL_WARN);
    ds_render_now();
    ds_feed_fetch();
    ds_walls_fetch();

    ds_rebuild();
}
