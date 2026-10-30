#include "app.h"
#include "adl_wolf.h"
#include <stdint.h>

#define MP_MAX   64
#define MP_PATH  200
#define MP_NAME  96

static asi_t    *A;
static lv_obj_t *g_win;
static lv_obj_t *g_list;
static lv_obj_t *g_meta;
static lv_obj_t *g_time;
static lv_obj_t *g_bar;
static lv_obj_t *g_slider;
static lv_obj_t *g_status;
static lv_obj_t *g_empty;
static lv_timer_t *g_timer;

static char      g_path[MP_MAX][MP_PATH];
static char      g_name[MP_MAX][MP_NAME];
static lv_obj_t *g_rows[MP_MAX];
static int       g_n;
static int       g_sel;

static uint8_t  *g_buf;
static uint32_t  g_buflen;
static uint32_t  g_dur_ms;
static uint32_t  g_start_ms;
static int       g_playing;

static int slen(const char *s) { int n = 0; if (s) while (s[n]) n++; return n; }

static void scpy(char *d, int cap, const char *s) {
    int i = 0;
    if (cap <= 0) return;
    if (!s) { d[0] = 0; return; }
    for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}

static void scat(char *d, int cap, const char *s) {
    int i = slen(d), j = 0;
    for (; s && s[j] && i < cap - 1; i++, j++) d[i] = s[j];
    d[i] = 0;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void mmss(char *out, uint32_t ms) {
    uint32_t s = ms / 1000u;
    uint32_t m = s / 60u;
    s %= 60u;
    if (m > 99u) m = 99u;
    out[0] = (char)('0' + (m / 10u));
    out[1] = (char)('0' + (m % 10u));
    out[2] = ':';
    out[3] = (char)('0' + (s / 10u));
    out[4] = (char)('0' + (s % 10u));
    out[5] = 0;
}

static uint32_t wav_duration_ms(const uint8_t *d, uint32_t len) {
    if (len < 44) return 0;
    uint16_t ch = 0, bits = 0;
    uint32_t rate = 0, data = 0;
    uint32_t off = 12;
    while (off + 8u <= len) {
        uint32_t sz = rd32(d + off + 4);
        uint32_t body = off + 8u;
        if (d[off] == 'f' && d[off + 1] == 'm' && d[off + 2] == 't' && d[off + 3] == ' ') {
            if (body + 16u <= len) {
                ch = rd16(d + body + 2);
                rate = rd32(d + body + 4);
                bits = rd16(d + body + 14);
            }
        } else if (d[off] == 'd' && d[off + 1] == 'a' && d[off + 2] == 't' && d[off + 3] == 'a') {
            data = sz;
            if (data > len - body) data = len - body;
            break;
        }
        uint32_t adv = sz + (sz & 1u);
        if (adv < 8u) break;
        off = body + adv;
    }
    uint32_t bps = rate * (uint32_t)ch * (uint32_t)(bits / 8);
    if (!bps) return 0;
    return (uint32_t)(((uint64_t)data * 1000u) / bps);
}

static int is_wav(const char *name) {
    int L = slen(name);
    if (L < 5) return 0;
    char a = name[L - 4], b = name[L - 3], c = name[L - 2], d = name[L - 1];
    if (a != '.') return 0;
    if (!(b == 'w' || b == 'W')) return 0;
    if (!(c == 'a' || c == 'A')) return 0;
    if (!(d == 'v' || d == 'V')) return 0;
    return 1;
}

static void join_path(char *out, int cap, const char *dir, const char *name) {
    scpy(out, cap, dir);
    int L = slen(out);
    if (L == 0) { scpy(out, cap, name); return; }
    char last = out[L - 1];
    if (last != '/' && last != ':') scat(out, cap, "/");
    scat(out, cap, name);
}

static void add_file(const char *dir, const char *name) {
    if (g_n >= MP_MAX) return;
    if (!is_wav(name)) return;
    join_path(g_path[g_n], MP_PATH, dir, name);
    scpy(g_name[g_n], MP_NAME, name);
    g_n++;
}

static void scan_dir(const char *dir) {
    if (!A->fs || !A->fs->count_dir || !A->fs->list_dir) return;
    int n = A->fs->count_dir(dir);
    if (n <= 0) return;
    for (int i = 0; i < n && g_n < MP_MAX; i++) {
        char nm[MP_NAME];
        int isd = 0;
        if (A->fs->list_dir(dir, i, nm, sizeof(nm), &isd) != 0) continue;
        if (isd) continue;
        add_file(dir, nm);
    }
}

static void player_stop(void) {
    if (A->audio && A->audio->stop) A->audio->stop();
    if (g_buf && A->mem && A->mem->free) A->mem->free(g_buf);
    g_buf = 0;
    g_buflen = 0;
    g_playing = 0;
}

static void set_status(const char *s) {
    if (g_status) lv_label_set_text(g_status, s ? s : "");
}

static void play_path(int i) {
    if (i < 0 || i >= g_n) return;
    if (!A->audio) { set_status("Ses surucusu yok"); return; }
    player_stop();

    asi_stat_t st;
    if (A->fs->stat(g_path[i], &st) != 0 || st.is_dir || st.size == 0) {
        set_status("Dosya okunamadi");
        return;
    }

    uint32_t sz = st.size;
    uint8_t *buf = (uint8_t *)A->mem->alloc(sz);
    if (!buf) { set_status("Bellek yetersiz"); return; }

    uint32_t rd = A->fs->read_path(g_path[i], buf, sz);
    if (rd == 0) {
        A->mem->free(buf);
        set_status("Dosya okunamadi");
        return;
    }

    if (A->audio->play_wav(buf, rd) != 0) {
        const char *er = A->audio->last_error ? A->audio->last_error() : "Oynatilamadi";
        A->mem->free(buf);
        set_status(er);
        return;
    }

    g_buf = buf;
    g_buflen = rd;
    g_dur_ms = wav_duration_ms(buf, rd);
    g_start_ms = (A->time && A->time->ticks_ms) ? A->time->ticks_ms() : 0;
    g_playing = 1;

    if (g_meta) lv_label_set_text(g_meta, g_name[i]);
    if (g_bar) lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);
    set_status("Calıyor...");
}

static void row_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    for (int k = 0; k < g_n; k++)
        lv_obj_set_style_bg_color(g_rows[k], lv_color_hex(k == i ? ADL_SURFACE_2 : ADL_SURFACE), 0);
    g_sel = i;
    play_path(i);
}

static void play_cb(lv_event_t *e) { (void)e; play_path(g_sel); }

static void stop_cb(lv_event_t *e) {
    (void)e;
    player_stop();
    if (g_bar) lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);
    set_status("Durdu");
}

static void vol_cb(lv_event_t *e) {
    lv_obj_t *s = lv_event_get_target(e);
    int v = (int)lv_slider_get_value(s);
    if (A->audio && A->audio->set_volume) A->audio->set_volume(v);
}

static void tick_cb(lv_timer_t *t) {
    (void)t;
    if (!A->audio) return;

    if (g_buf && A->audio->is_playing && !A->audio->is_playing()) {
        if (A->mem && A->mem->free) A->mem->free(g_buf);
        g_buf = 0;
        g_playing = 0;
        if (g_bar) lv_bar_set_value(g_bar, 1000, LV_ANIM_OFF);
        set_status("Bitti");
        return;
    }

    if (g_playing && g_buf) {
        uint32_t now = (A->time && A->time->ticks_ms) ? A->time->ticks_ms() : 0;
        uint32_t el = now - g_start_ms;
        if (g_dur_ms && el > g_dur_ms) el = g_dur_ms;
        if (g_bar && g_dur_ms) {
            uint32_t v = (uint32_t)(((uint64_t)el * 1000u) / g_dur_ms);
            if (v > 1000u) v = 1000u;
            lv_bar_set_value(g_bar, (int32_t)v, LV_ANIM_OFF);
        }
        if (g_time) {
            char a[8], b[8], line[24];
            mmss(a, el);
            mmss(b, g_dur_ms);
            scpy(line, sizeof(line), a);
            scat(line, sizeof(line), " / ");
            scat(line, sizeof(line), b);
            lv_label_set_text(g_time, line);
        }
    }
}

static lv_obj_t *mk_panel(lv_obj_t *parent, uint32_t bg) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_style_bg_color(c, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(c, 14, 0);
    lv_obj_set_style_pad_row(c, 8, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *mk_button(lv_obj_t *parent, const char *txt, uint32_t bg) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, 40);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
    lv_obj_center(l);
    return b;
}

static void build_list(void) {
    g_list = lv_obj_create(g_win);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_width(g_list, 250);
    lv_obj_set_height(g_list, LV_PCT(100));
    lv_obj_set_style_bg_color(g_list, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_list, 10, 0);
    lv_obj_set_style_pad_row(g_list, 6, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);

    lv_obj_t *hdr = lv_label_create(g_list);
    lv_label_set_text(hdr, "Parcalar");
    lv_obj_set_style_text_color(hdr, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_set_style_pad_bottom(hdr, 4, 0);

    for (int i = 0; i < g_n; i++) {
        lv_obj_t *b = lv_button_create(g_list);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_height(b, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(ADL_SURFACE_2), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 8, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, g_name[i]);
        lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, LV_PCT(100));
        lv_obj_add_event_cb(b, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        g_rows[i] = b;
    }

    if (g_n == 0) {
        g_empty = lv_label_create(g_list);
        lv_label_set_text(g_empty, "WAV dosyasi bulunamadi.\nUSB bellege veya Belgeler'e .wav koyun.");
        lv_obj_set_style_text_color(g_empty, lv_color_hex(ADL_TEXT_DIM), 0);
        lv_label_set_long_mode(g_empty, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(g_empty, LV_PCT(100));
    }
}

static void build_side(void) {
    lv_obj_t *side = mk_panel(g_win, ADL_BG);
    lv_obj_set_flex_grow(side, 1);
    lv_obj_set_height(side, LV_PCT(100));
    lv_obj_set_style_pad_all(side, 18, 0);
    lv_obj_set_style_pad_row(side, 12, 0);

    lv_obj_t *title = lv_label_create(side);
    lv_label_set_text(title, "Medya Oynatıcı");
    lv_obj_set_style_text_color(title, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_text_font(title, adl_font(), 0);

    g_meta = lv_label_create(side);
    lv_label_set_text(g_meta, "Parca secin");
    lv_obj_set_style_text_color(g_meta, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_text_font(g_meta, adl_font(), 0);
    lv_label_set_long_mode(g_meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_meta, LV_PCT(100));

    g_time = lv_label_create(side);
    lv_label_set_text(g_time, "00:00 / 00:00");
    lv_obj_set_style_text_color(g_time, lv_color_hex(ADL_TEXT_DIM), 0);

    g_bar = lv_bar_create(side);
    lv_obj_set_width(g_bar, LV_PCT(100));
    lv_obj_set_height(g_bar, 8);
    lv_bar_set_range(g_bar, 0, 1000);
    lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(ADL_SURFACE_2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(ADL_ACCENT), LV_PART_INDICATOR);

    lv_obj_t *ctl = lv_obj_create(side);
    lv_obj_remove_style_all(ctl);
    lv_obj_set_width(ctl, LV_PCT(100));
    lv_obj_set_height(ctl, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(ctl, 10, 0);
    lv_obj_set_flex_flow(ctl, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(ctl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bplay = mk_button(ctl, "Cal", ADL_ACCENT);
    lv_obj_set_width(bplay, 110);
    lv_obj_add_event_cb(bplay, play_cb, LV_EVENT_CLICKED, 0);
    lv_obj_t *bstop = mk_button(ctl, "Durdur", ADL_SURFACE_2);
    lv_obj_set_width(bstop, 110);
    lv_obj_add_event_cb(bstop, stop_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *vrow = lv_obj_create(side);
    lv_obj_remove_style_all(vrow);
    lv_obj_set_width(vrow, LV_PCT(100));
    lv_obj_set_height(vrow, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(vrow, 12, 0);
    lv_obj_set_flex_flow(vrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(vrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *vl = lv_label_create(vrow);
    lv_label_set_text(vl, "Ses");
    lv_obj_set_style_text_color(vl, lv_color_hex(ADL_TEXT), 0);
    g_slider = lv_slider_create(vrow);
    lv_obj_set_flex_grow(g_slider, 1);
    lv_obj_set_height(g_slider, 10);
    lv_slider_set_range(g_slider, 0, 100);
    lv_slider_set_value(g_slider, (A->audio && A->audio->volume) ? A->audio->volume() : 80, LV_ANIM_OFF);
    lv_obj_add_event_cb(g_slider, vol_cb, LV_EVENT_VALUE_CHANGED, 0);

    g_status = lv_label_create(side);
    lv_label_set_text(g_status, "");
    lv_obj_set_style_text_color(g_status, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_status, LV_PCT(100));
}

static void win_del_cb(lv_event_t *e) {
    (void)e;
    player_stop();
    if (g_timer) { lv_timer_delete(g_timer); g_timer = 0; }
    g_win = 0;
    g_list = 0;
    g_meta = 0;
    g_time = 0;
    g_bar = 0;
    g_slider = 0;
    g_status = 0;
    g_empty = 0;
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    A = asi;
    g_win = win;
    g_list = 0;
    g_meta = 0;
    g_time = 0;
    g_bar = 0;
    g_slider = 0;
    g_status = 0;
    g_empty = 0;
    g_timer = 0;
    g_n = 0;
    g_sel = -1;
    g_buf = 0;
    g_buflen = 0;
    g_dur_ms = 0;
    g_start_ms = 0;
    g_playing = 0;

    if (A->fs) {
        scan_dir("C:");
        scan_dir("C:/belgeler");
        scan_dir("C:/indirilenler");
        scan_dir("C:/masaustu");
        scan_dir("D:");
        scan_dir("D:/belgeler");
    }
    if (g_n > 0) g_sel = 0;

    /* Hedef dosya verildiyse (orn. masaustunde .wav cift tiki) onu
     * oynaticiya yukle ve baslat */
    int autoplay = -1;
    if (arctian_open_target && arctian_open_target[0] && is_wav(arctian_open_target)) {
        for (int i = 0; i < g_n; i++) {
            const char *a = g_path[i], *b = arctian_open_target;
            int same = 1;
            while (*a && *b) { if (*a != *b) { same = 0; break; } a++; b++; }
            if (same && !*a && !*b) { autoplay = i; break; }
        }
        if (autoplay < 0 && g_n < MP_MAX) {
            scpy(g_path[g_n], MP_PATH, arctian_open_target);
            const char *nm = arctian_open_target;
            for (const char *p = nm; *p; p++) if (*p == '/') nm = p + 1;
            scpy(g_name[g_n], MP_NAME, nm);
            autoplay = g_n++;
        }
        arctian_open_target = "";       /* hedefi tuket (tekrar oynatma engeli) */
    }
    if (autoplay >= 0) g_sel = autoplay;

    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(win, 0, 0);
    lv_obj_set_style_pad_column(win, 0, 0);
    lv_obj_remove_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    build_list();
    build_side();

    if (autoplay >= 0 && A->audio) play_path(autoplay);   /* Otomatik oynat */

    g_timer = lv_timer_create(tick_cb, 250, 0);
    lv_obj_add_event_cb(win, win_del_cb, LV_EVENT_DELETE, 0);
}
