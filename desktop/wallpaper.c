#include "dsk.h"


/* Yuksek kaliteli yeniden olcekleme: buyutmede bilinear, kucultmede
 * alan ortalamasi (kutu). Eskiden saf nearest-neighbor kullaniliyordu. */
uint32_t *scale_hq(const uint32_t *src, int sw, int sh, int dw, int dh) {
    uint32_t *d = (uint32_t *)g_asi->mem->alloc((size_t)dw * dh * 4u);
    if (!d) return 0;
    if (dw >= sw && dh >= sh) {
        for (int y = 0; y < dh; y++) {
            int fy = ((2 * y + 1) * sh * 128) / dh - 128;
            if (fy < 0) fy = 0;
            if (fy > (sh - 1) * 256) fy = (sh - 1) * 256;
            int y0 = fy >> 8, ty = fy & 255;
            int y1 = y0 + 1 < sh ? y0 + 1 : y0;
            for (int x = 0; x < dw; x++) {
                int fx = ((2 * x + 1) * sw * 128) / dw - 128;
                if (fx < 0) fx = 0;
                if (fx > (sw - 1) * 256) fx = (sw - 1) * 256;
                int x0 = fx >> 8, tx = fx & 255;
                int x1 = x0 + 1 < sw ? x0 + 1 : x0;
                uint32_t top = mix2(src[y0 * sw + x0], src[y0 * sw + x1], tx);
                uint32_t bot = mix2(src[y1 * sw + x0], src[y1 * sw + x1], tx);
                d[y * dw + x] = mix2(top, bot, ty);
            }
        }
        return d;
    }
    for (int y = 0; y < dh; y++) {
        int sy0 = (int)((long)y * sh / dh);
        int sy1 = (int)((long)(y + 1) * sh / dh);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > sh) sy1 = sh;
        for (int x = 0; x < dw; x++) {
            int sx0 = (int)((long)x * sw / dw);
            int sx1 = (int)((long)(x + 1) * sw / dw);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > sw) sx1 = sw;
            uint32_t r = 0, g = 0, b = 0, al = 0, n = 0;
            for (int sy2 = sy0; sy2 < sy1; sy2++) {
                const uint32_t *srow = src + (size_t)sy2 * sw;
                for (int sx2 = sx0; sx2 < sx1; sx2++) {
                    uint32_t c = srow[sx2];
                    al += (c >> 24) & 0xFF;
                    r += (c >> 16) & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += c & 0xFF;
                    n++;
                }
            }
            if (!n) n = 1;
            d[y * dw + x] = ((al / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
    return d;
}


int afs_index_of(const char *name) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->count || !g_asi->fs->name) return -1;
    int n = g_asi->fs->count();
    for (int i = 0; i < n; i++) {
        const char *nm = g_asi->fs->name(i);
        if (nm && s_eq(nm, name)) return i;
    }
    return -1;
}


void wallpaper_show(uint32_t *scaled, int W, int H) {
    if (g_wallpaper) { lv_obj_delete(g_wallpaper); g_wallpaper = 0; }
    g_wallpaper = lv_canvas_create(lv_screen_active());
    lv_obj_remove_style_all(g_wallpaper);
    lv_obj_remove_flag(g_wallpaper, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(g_wallpaper, scaled, W, H, LV_COLOR_FORMAT_XRGB8888);
    lv_obj_add_event_cb(g_wallpaper, canvas_buf_free_cb, LV_EVENT_DELETE, scaled);
    lv_obj_set_pos(g_wallpaper, 0, 0);
    lv_obj_move_to_index(g_wallpaper, 0);
}


int wallpaper_from(const uint8_t *data, uint32_t len) {
    uint32_t *native = 0;
    int nw = 0, nh = 0;
    if (png_decode(data, len, g_asi->mem->alloc, g_asi->mem->free, &native, &nw, &nh) != 0)
        return -1;
    int W = g_asi->gfx->width(), H = g_asi->gfx->height();
    uint32_t *scaled = scale_hq(native, nw, nh, W, H);
    g_asi->mem->free(native);
    if (!scaled) return -1;
    wallpaper_show(scaled, W, H);
    return 0;
}


int wallpaper_load_afs(const char *name) {
    int i = afs_index_of(name);
    if (i < 0) return -1;
    uint32_t sz = g_asi->fs->size(i);
    if (sz < 8 || sz > (4u * 1024u * 1024u)) return -1;
    void *h = g_asi->fs->find(0, name);
    if (!h) return -1;
    uint8_t *buf = (uint8_t *)g_asi->mem->alloc(sz);
    if (!buf) return -1;
    uint32_t n = g_asi->fs->read(h, buf, sz);
    int r = (n == 0) ? -1 : wallpaper_from(buf, n);
    g_asi->mem->free(buf);
    return r;
}


void wallpaper_cfg_read(void) {
    g_wall_active[0] = 0;
    int i = afs_index_of("wallpaper.cfg");
    if (i < 0) return;
    uint32_t sz = g_asi->fs->size(i);
    if (sz == 0 || sz > 63) return;
    void *h = g_asi->fs->find(0, "wallpaper.cfg");
    if (!h) return;
    char buf[64];
    uint32_t n = g_asi->fs->read(h, buf, sizeof(buf) - 1);
    if (!n) return;
    buf[n] = 0;
    int e = 0; while (buf[e] && buf[e] != '\n' && buf[e] != '\r') e++;
    buf[e] = 0;
    if (buf[0]) scopy(g_wall_active, buf, 39);
}


void wallpaper_cfg_write(const char *name) {
    if (!g_asi || !g_asi->fs) return;
    if (!name || !name[0]) {
        if (g_asi->fs->remove) g_asi->fs->remove("wallpaper.cfg");
        return;
    }
    void *h = g_asi->fs->create(0, "wallpaper.cfg", 0);
    if (h) g_asi->fs->write(h, name, (uint32_t)slen2(name));
}


void build_wallpaper(void) {
    wallpaper_cfg_read();
    if (g_wall_active[0] && wallpaper_load_afs(g_wall_active) == 0)
        return;
    g_wall_active[0] = 0;
    wallpaper_from(wallpaper_png, wallpaper_png_len);
}


/* Store API: arka plan secimi */
int store_set_wallpaper(const char *afs_name) {
    if (!g_asi || !g_asi->fs) return -1;
    if (!afs_name || !afs_name[0]) {
        wallpaper_cfg_write("");
        g_wall_active[0] = 0;
        return wallpaper_from(wallpaper_png, wallpaper_png_len);
    }
    if (afs_index_of(afs_name) < 0) return -1;
    if (wallpaper_load_afs(afs_name) != 0) return -2;
    scopy(g_wall_active, afs_name, 39);
    wallpaper_cfg_write(afs_name);
    return 0;
}

int store_wallpaper_active(void) { return g_wall_active[0] ? 1 : 0; }

const char *store_wallpaper_name(void) { return g_wall_active; }


/* ------------------------------------------------------------------ */
/* Imlec (0.7)                                                         */
/*                                                                     */
/* Kaynak: tasarim SVG'si (32x32 viewBox), uc katman:                  */
/*   1) dis siluet  (5,5)-(12,27)-(15,15)-(27,12)  cizgi 2.6 #EEF0F6   */
/*   2) sol yuz     (5,5)-(12,27)-(15,15)                   #262C40    */
/*   3) sag yuz     (5,5)-(15,15)-(27,12)                   #3A4260    */
/*                                                                     */
/* Kenarlar 8x8 alt ornekleme (supersampling) ile yumusatilir; eski    */
/* 16x16 piksel haritasi keskin (aliased) oldugu icin "pikselli"       */
/* gorunuyordu. Olcek dock ile ayni referanstan gelir: dk_s(32)        */
/* (1600x900 referansi, 1080p'de 38 px tuval ~29 px gorunur ok).       */
/* ------------------------------------------------------------------ */

#define CUR_SS 8   /* alt ornekleme: 8x8 = piksel basina 64 ornek */

/* Noktanin AB dogru parcasina uzakligi r'den kucuk mu? (r2 = r*r) */
static int seg_near(float px, float py, float ax, float ay,
                    float bx, float by, float r2) {
    float dx = bx - ax, dy = by - ay;
    float t = 0.0f, len2 = dx * dx + dy * dy;
    if (len2 > 0.0001f) {
        t = ((px - ax) * dx + (py - ay) * dy) / len2;
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    }
    float qx = ax + t * dx - px, qy = ay + t * dy - py;
    return (qx * qx + qy * qy) <= r2;
}

/* Kapali poligon + r yaricapli kalin cizgi (yuvarlak birlesim) alani. */
static int poly_cover(float px, float py, const float *poly, int n, float r) {
    if (point_in_poly(px, py, poly, n)) return 1;
    if (r <= 0.0f) return 0;
    float r2 = r * r;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if (seg_near(px, py, poly[j * 2], poly[j * 2 + 1],
                     poly[i * 2], poly[i * 2 + 1], r2)) return 1;
    return 0;
}


static void cursor_buf_free(lv_event_t *e) {
    uint32_t *p = (uint32_t *)lv_event_get_user_data(e);
    if (p && g_asi && g_asi->mem && g_asi->mem->free) g_asi->mem->free(p);
}


void build_cursor(void) {
    static lv_obj_t *s_cursor;
    static int s_size;

    /* Tuval 32 birim; dk_s ile ekran yuksekligine olceklenir. */
    int S = dk_s(32);
    if (S < 20) S = 20;
    if (S > 72) S = 72;

    if (s_cursor) {
        if (s_size == S) return;          /* ayni olcek: yeniden kurma */
        lv_obj_delete(s_cursor);          /* cozunurluk degisti: yenile */
        s_cursor = NULL;
    }

    static const float outer[8] = { 5, 5, 12, 27, 15, 15, 27, 12 };
    static const float left[6]  = { 5, 5, 12, 27, 15, 15 };
    static const float right[6] = { 5, 5, 15, 15, 27, 12 };
    const uint32_t c_out   = 0xFFEEF0F6u;   /* dis kontur  */
    const uint32_t c_left  = 0xFF262C40u;   /* sol yuz     */
    const uint32_t c_right = 0xFF3A4260u;   /* sag yuz     */

    uint32_t *big = (uint32_t *)g_asi->mem->alloc((size_t)S * S * 4u);
    if (!big) return;

    const float k = (float)S / 32.0f;       /* birim -> px */
    const int ns = CUR_SS * CUR_SS;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            int n = 0, sr = 0, sg = 0, sb = 0;
            for (int sy = 0; sy < CUR_SS; sy++) {
                for (int sx = 0; sx < CUR_SS; sx++) {
                    float u = ((float)x + ((float)sx + 0.5f) / CUR_SS) / k;
                    float v = ((float)y + ((float)sy + 0.5f) / CUR_SS) / k;
                    uint32_t c = 0;
                    /* SVG boyama sirasi: siluet, sol yuz, sag yuz (ustte) */
                    if (poly_cover(u, v, right, 3, 0.4f))      c = c_right;
                    else if (poly_cover(u, v, left, 3, 0.4f))  c = c_left;
                    else if (poly_cover(u, v, outer, 4, 1.3f)) c = c_out;
                    if (!c) continue;
                    n++;
                    sr += (c >> 16) & 0xFF;
                    sg += (c >> 8) & 0xFF;
                    sb += c & 0xFF;
                }
            }
            if (!n) { big[y * S + x] = 0; continue; }
            /* duz (premultiply edilmemis) alfa: renk kapli orneklerin
             * ortalamasi, alfa kapsama orani. */
            int a = (n * 255 + ns / 2) / ns;
            if (a > 255) a = 255;
            big[y * S + x] = ((uint32_t)a << 24) |
                             ((uint32_t)(sr / n) << 16) |
                             ((uint32_t)(sg / n) << 8) |
                             (uint32_t)(sb / n);
        }
    }

    /* Gorsel sinirlari bul: uc noktasi tuvale (0,0) gelsin ki isaretci
     * ucu tam imlec konumunda dursun. */
    int x0 = S, y0 = S, x1 = -1, y1 = -1;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            if (((big[y * S + x] >> 24) & 0xFF) < 8) continue;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
    if (x1 < 0 || y1 < 0) { g_asi->mem->free(big); return; }

    int W = x1 - x0 + 1, H = y1 - y0 + 1;
    uint32_t *buf = (uint32_t *)g_asi->mem->alloc((size_t)W * H * 4u);
    if (!buf) { g_asi->mem->free(big); return; }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            buf[y * W + x] = big[(y + y0) * S + (x + x0)];
    g_asi->mem->free(big);

    lv_obj_t *cur = lv_canvas_create(lv_screen_active());
    lv_obj_remove_style_all(cur);
    lv_obj_remove_flag(cur, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(cur, buf, W, H, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_add_event_cb(cur, cursor_buf_free, LV_EVENT_DELETE, buf);
    lv_indev_t *indev = lv_port_get_indev();
    if (indev) lv_indev_set_cursor(indev, cur);
    s_cursor = cur;
    s_size = S;
}

