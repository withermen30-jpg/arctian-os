#include "app.h"
#include "adl_wolf.h"
#include <stdint.h>

static asi_t *A;
static lv_obj_t *g_out;

static void put(char *b, int cap, int *n, const char *s) {
    if (!s) return;
    for (int i = 0; s[i] && *n < cap - 1; i++) b[(*n)++] = s[i];
    b[*n] = 0;
}

static void put_u(char *b, int cap, int *n, uint32_t v) {
    char t[12];
    int k = 0;
    if (v == 0) t[k++] = '0';
    while (v && k < 11) { t[k++] = (char)('0' + (v % 10)); v /= 10; }
    while (k && *n < cap - 1) b[(*n)++] = t[--k];
    b[*n] = 0;
}

static void set_out(const char *title, const char *body) {
    static char buf[8192];
    int n = 0;
    put(buf, sizeof(buf), &n, title);
    put(buf, sizeof(buf), &n, "\n\n");
    put(buf, sizeof(buf), &n, body);
    if (g_out) lv_label_set_text(g_out, buf);
}

static void ntp_cb(lv_event_t *e) {
    (void)e;
    if (!A || !A->time || !A->time->ntp_sync) { set_out("NTP", "NTP servisi yok."); return; }
    int rc = A->time->ntp_sync(5000);
    if (rc != 0) {
        char b[64];
        int n = 0;
        put(b, sizeof(b), &n, "Senkron basarisiz, kod=");
        put_u(b, sizeof(b), &n, (uint32_t)(-rc));
        set_out("NTP", b);
        arctian_notify("Saat", "NTP senkronu basarisiz");
        return;
    }
    asi_datetime_t dt;
    char b[96];
    int n = 0;
    if (A->time->now && A->time->now(&dt)) {
        put_u(b, sizeof(b), &n, (uint32_t)dt.day); put(b, sizeof(b), &n, ".");
        put_u(b, sizeof(b), &n, (uint32_t)dt.month); put(b, sizeof(b), &n, ".");
        put_u(b, sizeof(b), &n, (uint32_t)dt.year); put(b, sizeof(b), &n, "  ");
        put_u(b, sizeof(b), &n, (uint32_t)dt.hour); put(b, sizeof(b), &n, ":");
        if (dt.minute < 10) put(b, sizeof(b), &n, "0");
        put_u(b, sizeof(b), &n, (uint32_t)dt.minute); put(b, sizeof(b), &n, ":");
        if (dt.second < 10) put(b, sizeof(b), &n, "0");
        put_u(b, sizeof(b), &n, (uint32_t)dt.second);
    }
    set_out("Saat esitlendi (NTP)", b);
    arctian_notify("Saat", "NTP ile esitlendi");
}

static void print_cb(lv_event_t *e) {
    (void)e;
    if (!A || !A->spool || !A->spool->submit) { set_out("Yazdirma", "Spool servisi yok."); return; }
    static const char page[] =
        "Arctian test sayfasi\n"
        "--------------------\n"
        "Bu belge sanal yaziciya gonderildi.\n"
        "C:/logs/print/ altinda .txt olarak saklanir.\n";
    int id = A->spool->submit("test-sayfasi", page, (uint32_t)(sizeof(page) - 1));
    if (id < 0) { set_out("Yazdirma", "Kuyruk dolu."); return; }
    A->spool->process();
    char path[96];
    path[0] = 0;
    if (A->spool->last_path) A->spool->last_path(path, sizeof(path));
    char b[160];
    int n = 0;
    put(b, sizeof(b), &n, "Is #");
    put_u(b, sizeof(b), &n, (uint32_t)id);
    put(b, sizeof(b), &n, " yazdirildi.\n");
    put(b, sizeof(b), &n, path);
    set_out("Yazdirma", b);
    arctian_notify("Yazici", "Test sayfasi yazdirildi");
}

static void log_cb(lv_event_t *e) {
    (void)e;
    if (!A || !A->log) { set_out("Sistem gunlugu", "Log servisi yok."); return; }
    int cnt = A->log->count();
    static char b[8192];
    int n = 0;
    for (int i = 0; i < cnt; i++) {
        klog_entry_t en;
        if (A->log->get(i, &en) != 0) continue;
        const char *lv = "???";
        switch (en.level) {
            case KLOG_DEBUG: lv = "DBG"; break;
            case KLOG_INFO:  lv = "INF"; break;
            case KLOG_WARN:  lv = "WRN"; break;
            case KLOG_ERR:   lv = "ERR"; break;
            case KLOG_FATAL: lv = "FTL"; break;
        }
        put(b, sizeof(b), &n, "[");
        put(b, sizeof(b), &n, lv);
        put(b, sizeof(b), &n, "] ");
        put(b, sizeof(b), &n, en.tag);
        put(b, sizeof(b), &n, ": ");
        put(b, sizeof(b), &n, en.msg);
        put(b, sizeof(b), &n, "\n");
    }
    if (n == 0) put(b, sizeof(b), &n, "(kayit yok)\n");
    if (A->log->flush) A->log->flush();
    set_out("Sistem gunlugu (C:/logs/sys.log'a yazildi)", b);
}

static void dev_cb(lv_event_t *e) {
    (void)e;
    if (!A || !A->dev) { set_out("Cihazlar", "Cihaz servisi yok."); return; }
    int cnt = A->dev->count();
    static char b[4096];
    int n = 0;
    for (int i = 0; i < cnt; i++) {
        put(b, sizeof(b), &n, "  ");
        put(b, sizeof(b), &n, A->dev->name(i));
        put(b, sizeof(b), &n, " : ");
        put(b, sizeof(b), &n, A->dev->status(i));
        put(b, sizeof(b), &n, "\n");
    }
    set_out("Cihazlar / suruculer", b);
}

static lv_obj_t *mkbtn(lv_obj_t *p, int y, const char *t, lv_event_cb_t cb) {
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, 176, 36);
    lv_obj_set_pos(b, 8, y);
    lv_obj_set_style_radius(b, 9, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(ADL_BORDER), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, t);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, 0);
    return b;
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    A = asi;
    int W = lv_obj_get_width(win);
    int H = lv_obj_get_height(win);
    if (W < 400) W = 760;
    if (H < 300) H = 560;

    lv_obj_set_style_bg_color(win, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, 0);

    lv_obj_t *bar = lv_obj_create(win);
    lv_obj_remove_style_all(bar);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, W, 42);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, "Sistem Araclari");
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 6, 0);

    lv_obj_t *left = lv_obj_create(win);
    lv_obj_remove_style_all(left);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(left, 0, 48);
    lv_obj_set_size(left, 192, H - 48);

    mkbtn(left, 0,  "Saati esitle (NTP)", ntp_cb);
    mkbtn(left, 44, "Test sayfasi yazdir", print_cb);
    mkbtn(left, 88, "Sistem gunlugu", log_cb);
    mkbtn(left, 132, "Cihazlar", dev_cb);

    lv_obj_t *box = lv_obj_create(win);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, 198, 48);
    lv_obj_set_size(box, W - 206, H - 56);
    lv_obj_set_style_bg_color(box, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_pad_all(box, 12, 0);

    g_out = lv_label_create(box);
    lv_label_set_text(g_out, "Soldaki araclardan birini secin.");
    lv_obj_set_style_text_color(g_out, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_width(g_out, W - 240);
    lv_label_set_long_mode(g_out, LV_LABEL_LONG_WRAP);
}
