/**
 * libWolf — istemci tarafi (uygulama icine linklenir)
 *
 * Tum `wolf_*` islevleri, calisma zamaninin verdigi `wolf_api_t` tablosundaki
 * isaretcilere yonlendirir. Boylece uygulama binary'si LVGL/ASI/cekirdek
 * sembollerine ihtiyac duymaz: yalnizca bu tablo ve kendi kodu.
 *
 * Freestanding ortam icin derlenir: libc yok, -nostdlib ile linklenir.
 * Bu yuzden kucuk yardimcilar (strlen/strcpy/itoa) burada tanimlidir.
 *
 * Lisans: MIT (Arctian SDK)
 */
#include "wolf.h"

/* Bagli tablo. Uygulama giriste wolf_bind() cagirir. */
static const wolf_api_t *g_api = 0;

void wolf_bind(const wolf_api_t *api) {
    if (!api) return;
    /* Uyumluluk: sihirli sayi tutmuyorsa baglamayi reddet. */
    if (api->magic != WOLF_API_MAGIC) return;
    if (api->version > WOLF_ABI_VERSION) return;
    if (api->size < 12u) return;
    g_api = api;
}

const wolf_api_t *wolf_bound(void) { return g_api; }

int wolf_ready(void) { return g_api != 0; }

/* Guvenli cagri: tablo yoksa veya alan bos ise zararsiz doner. */
#define API (g_api)
#define HAS(f) (g_api && g_api->f)

/* ---------------- Pencere ---------------- */
wolf_id wolf_root(void) { return HAS(root) ? API->root() : 0; }
void wolf_win_title(const char *title) { if (HAS(win_title)) API->win_title(title); }
void wolf_win_size(int w, int h) { if (HAS(win_size)) API->win_size(w, h); }

/* ---------------- Cizim ---------------- */
wolf_id wolf_panel(wolf_id p, int x, int y, int w, int h) {
    return HAS(panel) ? API->panel(p, x, y, w, h) : 0;
}
wolf_id wolf_label(wolf_id p, int x, int y, const char *text) {
    return HAS(label) ? API->label(p, x, y, text) : 0;
}
wolf_id wolf_button(wolf_id p, int x, int y, int w, int h, const char *text) {
    return HAS(button) ? API->button(p, x, y, w, h, text) : 0;
}
wolf_id wolf_input(wolf_id p, int x, int y, int w, int h, const char *ph) {
    return HAS(input) ? API->input(p, x, y, w, h, ph) : 0;
}
wolf_id wolf_listbox(wolf_id p, int x, int y, int w, int h) {
    return HAS(listbox) ? API->listbox(p, x, y, w, h) : 0;
}
wolf_id wolf_image(wolf_id p, int x, int y, int w, int h, const char *path) {
    return HAS(image) ? API->image(p, x, y, w, h, path) : 0;
}
wolf_id wolf_progress(wolf_id p, int x, int y, int w, int h, int pct) {
    return HAS(progress) ? API->progress(p, x, y, w, h, pct) : 0;
}
wolf_id wolf_slider(wolf_id p, int x, int y, int w, int h, int pct) {
    return HAS(slider) ? API->slider(p, x, y, w, h, pct) : 0;
}
wolf_id wolf_separator(wolf_id p, int x, int y, int w) {
    return HAS(separator) ? API->separator(p, x, y, w) : 0;
}
wolf_id wolf_checkbox(wolf_id p, int x, int y, const char *text, int checked) {
    return HAS(checkbox) ? API->checkbox(p, x, y, text, checked) : 0;
}

/* ---------------- Ozellikler ---------------- */
void wolf_text(wolf_id w, const char *text) { if (HAS(set_text)) API->set_text(w, text); }
int wolf_text_of(wolf_id w, char *out, int cap) {
    return HAS(get_text) ? API->get_text(w, out, cap) : -1;
}
void wolf_value(wolf_id w, int v) { if (HAS(set_value)) API->set_value(w, v); }
int wolf_value_of(wolf_id w) { return HAS(get_value) ? API->get_value(w) : 0; }
void wolf_move(wolf_id w, int x, int y) { if (HAS(set_pos)) API->set_pos(w, x, y); }
void wolf_resize(wolf_id w, int width, int height) {
    if (HAS(set_size)) API->set_size(w, width, height);
}
void wolf_color(wolf_id w, uint32_t rgb) { if (HAS(set_color)) API->set_color(w, rgb); }
void wolf_visible(wolf_id w, int v) { if (HAS(set_visible)) API->set_visible(w, v); }
void wolf_enabled(wolf_id w, int e) { if (HAS(set_enabled)) API->set_enabled(w, e); }
void wolf_clear(wolf_id w) { if (HAS(clear)) API->clear(w); }
void wolf_destroy(wolf_id w) { if (HAS(destroy)) API->destroy(w); }

/* ---------------- Olaylar ---------------- */
void wolf_on_click(wolf_id w, wolf_event_fn cb) {
    if (HAS(on_click)) API->on_click(w, cb, 0);
}
void wolf_on_click_ex(wolf_id w, wolf_event_fn cb, void *user) {
    if (HAS(on_click)) API->on_click(w, cb, user);
}
void wolf_on_change(wolf_id w, wolf_event_fn cb) {
    if (HAS(on_change)) API->on_change(w, cb, 0);
}
void wolf_on_change_ex(wolf_id w, wolf_event_fn cb, void *user) {
    if (HAS(on_change)) API->on_change(w, cb, user);
}

/* ---------------- Liste ---------------- */
void wolf_list_add(wolf_id w, const char *item) { if (HAS(list_add)) API->list_add(w, item); }
void wolf_list_clear(wolf_id w) { if (HAS(list_clear)) API->list_clear(w); }
int wolf_list_count(wolf_id w) { return HAS(list_count) ? API->list_count(w) : 0; }
int wolf_list_item(wolf_id w, int i, char *out, int cap) {
    return HAS(list_item) ? API->list_item(w, i, out, cap) : -1;
}
int wolf_list_selected(wolf_id w) {
    return HAS(list_selected) ? API->list_selected(w) : -1;
}

/* ---------------- Dosya sistemi ---------------- */
int wolf_fs_read(const char *p, void *b, int c) { return HAS(fs_read) ? API->fs_read(p, b, c) : -1; }
int wolf_fs_write(const char *p, const void *b, int n) {
    return HAS(fs_write) ? API->fs_write(p, b, n) : -1;
}
int wolf_fs_list(const char *d, int i, char *name, int cap, int *isd) {
    return HAS(fs_list) ? API->fs_list(d, i, name, cap, isd) : -1;
}
int wolf_fs_count(const char *d) { return HAS(fs_count) ? API->fs_count(d) : 0; }
int wolf_fs_exists(const char *p) { return HAS(fs_exists) ? API->fs_exists(p) : 0; }
int wolf_fs_remove(const char *p) { return HAS(fs_remove) ? API->fs_remove(p) : -1; }
int wolf_fs_mkdir(const char *p) { return HAS(fs_mkdir) ? API->fs_mkdir(p) : -1; }
int wolf_fs_rename(const char *a, const char *b) {
    return HAS(fs_rename) ? API->fs_rename(a, b) : -1;
}

/* ---------------- Sistem ---------------- */
void wolf_log(const char *s) { if (HAS(log)) API->log(s); }
void wolf_notify(const char *t, const char *m) { if (HAS(notify)) API->notify(t, m); }
void wolf_dialog(const char *t, const char *m, const char *ok) {
    if (HAS(dialog)) API->dialog(t, m, ok);
}
int wolf_exec(const char *cmd, char *out, int cap) {
    return HAS(exec) ? API->exec(cmd, out, cap) : -1;
}
uint64_t wolf_ticks_ms(void) { return HAS(ticks_ms) ? API->ticks_ms() : 0; }
void wolf_sleep_ms(uint32_t ms) { if (HAS(sleep_ms)) API->sleep_ms(ms); }
int wolf_now(int *y, int *mo, int *d, int *h, int *mi, int *s) {
    return HAS(now) ? API->now(y, mo, d, h, mi, s) : -1;
}
int wolf_version(char *out, int cap) { return HAS(os_version) ? API->os_version(out, cap) : -1; }
int wolf_open_app(const char *n) { return HAS(open_app) ? API->open_app(n) : -1; }
void wolf_clip_set(const char *s) { if (HAS(clip_set)) API->clip_set(s); }
int wolf_clip_get(char *out, int cap) { return HAS(clip_get) ? API->clip_get(out, cap) : -1; }

/* ---------------- Ag ---------------- */
int wolf_fetch(const char *url, void *buf, int cap, int *len_out) {
    return HAS(fetch) ? API->fetch(url, buf, cap, len_out) : -1;
}

/* ==================================================================== */
/* Freestanding yardimcilar                                             */
/* ==================================================================== */

int wolf_strlen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }

void wolf_strcpy(char *dst, int cap, const char *src) {
    int i = 0;
    if (!dst || cap <= 0) return;
    if (!src) { dst[0] = 0; return; }
    for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

void wolf_strcat(char *dst, int cap, const char *src) {
    int i = 0, j = 0;
    if (!dst || cap <= 0) return;
    while (dst[i] && i < cap - 1) i++;
    if (!src) return;
    for (; src[j] && i < cap - 1; i++, j++) dst[i] = src[j];
    dst[i] = 0;
}

int wolf_streq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

void wolf_itoa(int value, char *out, int cap) {
    char tmp[16];
    int n = 0, k = 0, neg = 0;
    unsigned v;
    if (!out || cap <= 0) return;
    if (value < 0) { neg = 1; v = (unsigned)(-(long long)value); }
    else v = (unsigned)value;
    if (!v) tmp[n++] = '0';
    while (v && n < (int)sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    if (neg && k < cap - 1) out[k++] = '-';
    while (n && k < cap - 1) out[k++] = tmp[--n];
    out[k] = 0;
}
