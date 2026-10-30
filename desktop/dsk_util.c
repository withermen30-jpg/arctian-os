#include "dsk.h"


/* ---------------------------------------------------------------- */
/* Yardimcilar                                                       */
/* ---------------------------------------------------------------- */

void scopy(char *d, const char *s, int max) {
    int i = 0;
    for (; s[i] && i < max; i++) d[i] = s[i];
    d[i] = '\0';
}


void scpy(char *d, int cap, const char *s) { scopy(d, s, cap - 1); }


void base_obj(lv_obj_t *o, uint32_t bg, int radius, uint32_t border) {
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}


lv_obj_t *label(lv_obj_t *parent, const char *txt, uint32_t col) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    return l;
}


/* SVG ikonunu canvas olarak cizer (seffaf zemin). */
void canvas_buf_free_cb(lv_event_t *e) {
    void *p = lv_event_get_user_data(e);
    if (p && g_asi && g_asi->mem && g_asi->mem->free) g_asi->mem->free(p);
}


int ui_s(int v) { return (v * g_ui_s + 500) / 1000; }


/* Masaustu simge olcegi: kabuktan ayri. Kabuk 0.7 tabanli (yazi boyu
 * sabit oldugu icin), ama simgeler saf orana daha yakin kuculur; 720p'de
 * 0.55. 1080p'de iki olcek de 0.75 oldugundan gorunum degismez. */
static int s_ic_s = 1000;

int ic_s(int v) { return (v * s_ic_s + 500) / 1000; }


/* Dock olcek birimi: 1600x900 mantiksal referansa gore.
 * 1080p'de katsayi 1.2 olur (60 -> 72 px). */
int dk_s(int v) {
    int h = (g_asi && g_asi->gfx && g_asi->gfx->height) ? g_asi->gfx->height() : DK_REF_H;
    if (h < 1) h = DK_REF_H;
    return (v * h + DK_REF_H / 2) / DK_REF_H;
}


void ui_scale_refresh(void) {
    int h = (g_asi && g_asi->gfx && g_asi->gfx->height) ? g_asi->gfx->height() : UI_REF_H;
    if (h < 1) h = UI_REF_H;
    int s = (h * 1000) / UI_REF_H;
    /* Taban 0.7: yazi tipi ui_s ile olceklenmiyor (sabit 15 px). 720p'de
     * saf oran 0.5 kaliyordu; baslat menusu gibi sabit satir yuksekligi
     * kullanan yerlerde etiketler ikonlara ve alt satirlara tasiyordu. */
    if (s < 700) s = 700;
    if (s > 1700) s = 1700;
    g_ui_s = s;
    /* Masaustu simgeleri: saf orana yakin (720p'de 0.55 taban). */
    int is = (h * 1000) / UI_REF_H;
    if (is < 550) is = 550;
    if (is > 1700) is = 1700;
    s_ic_s = is;
    /* Simge boyutlari — buyuk simge modu ayariyla birlikte hesaplanir     */
    g_icon_w = ic_s(g_icon_big ? 220 : 176);
    g_icon_h = ic_s(g_icon_big ? 240 : 190);
}


lv_obj_t *make_icon(lv_obj_t *parent, const char *svg, int size) {
    uint32_t *buf = (uint32_t *)g_asi->mem->alloc((size_t)size * size * 4u);
    if (!buf) {
        lv_obj_t *e = lv_obj_create(parent);
        lv_obj_remove_style_all(e);
        return e;
    }
    svg_render(svg, size, size, buf);
    lv_obj_t *c = lv_canvas_create(parent);
    lv_obj_remove_style_all(c);
    lv_canvas_set_buffer(c, buf, size, size, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_add_event_cb(c, canvas_buf_free_cb, LV_EVENT_DELETE, buf);
    return c;
}


lv_obj_t *make_icon_png(lv_obj_t *parent, const unsigned char *data, unsigned len, int size) {
    if (!data || len == 0 || size <= 0) return 0;
    uint32_t *px = 0;
    int w = 0, h = 0;
    if (png_decode_argb(data, len, g_asi->mem->alloc, g_asi->mem->free, &px, &w, &h) != 0 || !px)
        return 0;
    int dw = size, dh = size;
    if (w > 0 && h > 0) {
        if (w >= h) { dw = size; dh = h * size / w; if (dh < 1) dh = 1; }
        else { dh = size; dw = w * size / h; if (dw < 1) dw = 1; }
    }
    uint32_t *out = px;
    if (dw != w || dh != h) {
        /* Kaliteli kucultme: piksel atlama yerine alan ortalamasi.
         * 1024 px kaynaktan 24 px'e inerken en yakin komsu ciddi kemer
         * (aliasing) birakiyordu. */
        uint32_t *sc = scale_hq(px, w, h, dw, dh);
        if (sc) {
            g_asi->mem->free(px);
            out = sc;
        } else {
            uint32_t *s = (uint32_t *)g_asi->mem->alloc((size_t)dw * (size_t)dh * 4u);
            if (s) {
                for (int y = 0; y < dh; y++) {
                    int sy = (int)((long)y * h / dh);
                    if (sy >= h) sy = h - 1;
                    for (int x = 0; x < dw; x++) {
                        int sx = (int)((long)x * w / dw);
                        if (sx >= w) sx = w - 1;
                        s[(long)y * dw + x] = px[(long)sy * w + sx];
                    }
                }
                g_asi->mem->free(px);
                out = s;
            } else { dw = w; dh = h; }
        }
    }
    lv_obj_t *c = lv_canvas_create(parent);
    lv_obj_remove_style_all(c);
    lv_canvas_set_buffer(c, out, dw, dh, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_add_event_cb(c, canvas_buf_free_cb, LV_EVENT_DELETE, out);
    return c;
}


lv_obj_t *make_app_icon(lv_obj_t *parent, const arctian_app_t *a, int size) {
    if (a && a->icon_png && a->icon_png_len) {
        lv_obj_t *c = make_icon_png(parent, a->icon_png, a->icon_png_len, size);
        if (c) return c;
    }
    return make_icon(parent, (a && a->icon_svg) ? a->icon_svg : "", size);
}


int title_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}



lv_obj_t *make_logo(lv_obj_t *parent, int size) {
    uint32_t *native = 0;
    int nw = 0, nh = 0;
    if (png_decode_argb(logo_png, logo_png_len, g_asi->mem->alloc,
                        g_asi->mem->free, &native, &nw, &nh) == 0) {
        uint32_t *sc = scale_hq(native, nw, nh, size, size);
        g_asi->mem->free(native);
        if (sc) {
            lv_obj_t *c = lv_canvas_create(parent);
            lv_obj_remove_style_all(c);
            lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
            lv_canvas_set_buffer(c, sc, size, size, LV_COLOR_FORMAT_ARGB8888);
            lv_obj_add_event_cb(c, canvas_buf_free_cb, LV_EVENT_DELETE, sc);
            return c;
        }
    }
    return make_icon(parent, SVG_LOGO, size);
}


int tb_streq(const char *a, const char *b) {
    int i = 0;
    for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}


int slen2(const char *s) { int n = 0; while (s && s[n]) n++; return n; }

int chr_low(char c) { if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return c; }

int s_eq(const char *a, const char *b) {
    int i = 0; for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0; return a[i] == b[i];
}

int iabs2(int v) { return v < 0 ? -v : v; }


/* ---------------------------------------------------------------- */
/* Duvar kagidi + imlec                                              */
/* ---------------------------------------------------------------- */

uint32_t mix2(uint32_t a, uint32_t b, int w) { /* w: 0..256 */
    int wa = 256 - w;
    int al = ((((a >> 24) & 0xFF) * wa + ((b >> 24) & 0xFF) * w + 128) >> 8) & 0xFF;
    int r = ((((a >> 16) & 0xFF) * wa + ((b >> 16) & 0xFF) * w + 128) >> 8) & 0xFF;
    int g = ((((a >> 8) & 0xFF) * wa + ((b >> 8) & 0xFF) * w + 128) >> 8) & 0xFF;
    int bl = (((a & 0xFF) * wa + (b & 0xFF) * w + 128) >> 8) & 0xFF;
    return (uint32_t)((al << 24) | (r << 16) | (g << 8) | bl);
}


int point_in_poly(float px, float py, const float *poly, int n) {
    int inside = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = poly[i*2], yi = poly[i*2+1], xj = poly[j*2], yj = poly[j*2+1];
        if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
            inside = !inside;
    }
    return inside;
}


void bin_copy(char *dst, int cap, const char *src) {
    int i = 0;
    if (cap <= 0) return;
    if (!src) { dst[0] = 0; return; }
    for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}


int hex_nib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}


void pct_decode(char *dst, int cap, const char *src, int len) {
    int o = 0;
    for (int i = 0; i < len && o < cap - 1; i++) {
        char c = src[i];
        if (c == '%' && i + 2 < len) {
            int hi = hex_nib(src[i + 1]), lo = hex_nib(src[i + 2]);
            if (hi >= 0 && lo >= 0) { dst[o++] = (char)((hi << 4) | lo); i += 2; continue; }
        }
        dst[o++] = c;
    }
    dst[o] = 0;
}


int key_eq(const char *k, int klen, const char *want) {
    int i = 0;
    for (; want[i]; i++) {
        if (i >= klen) return 0;
        if (chr_low(k[i]) != chr_low(want[i])) return 0;
    }
    return i == klen;
}


int perm_low(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }


int itoa_buf(int val, char *out, int cap) {
    char t[16];
    int n = 0, k = 0;
    unsigned int u;
    if (val < 0) { if (k < cap - 1) out[k++] = '-'; u = (unsigned int)(-val); }
    else u = (unsigned int)val;
    if (u == 0) t[n++] = '0';
    while (u) { t[n++] = (char)('0' + (u % 10)); u /= 10; }
    while (n && k < cap - 1) out[k++] = t[--n];
    out[k] = 0;
    return k;
}

