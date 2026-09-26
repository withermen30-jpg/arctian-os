#include "app.h"
#include <stdint.h>

static asi_t    *A;
static lv_obj_t *list;
static lv_obj_t *preview;
static lv_obj_t *status;
static int       sel = -1;

static char pbuf[1024];

static void refresh(void);
static void row_cb(lv_event_t *e);

static void set_status(const char *s) {
    if (status) lv_label_set_text(status, s ? s : "");
}

static void read_selected(void) {
    pbuf[0] = 0;
    if (!A || !A->fs || !A->fs->name || !A->fs->find || !A->fs->read) return;
    const char *nm = A->fs->name(sel);
    if (!nm) return;
    void *root = A->fs->root ? A->fs->root() : 0;
    void *h = A->fs->find(root, nm);
    if (!h) return;
    uint32_t n = A->fs->read(h, pbuf, sizeof(pbuf) - 1);
    pbuf[n] = 0;
}

static void refresh(void) {
    if (!list || !preview) return;
    lv_obj_clean(list);

    if (!A || !A->fs || !A->fs->count) {
        set_status("Dosya sistemi yok");
        lv_label_set_text(preview, "");
        return;
    }

    int n = A->fs->count();
    set_status("Kalici depo (AFS)");

    if (n == 0) {
        lv_obj_t *e = lv_label_create(list);
        lv_label_set_text(e, "Depo bos. 'Yeni Dosya' ile olustur.");
        lv_obj_set_style_text_color(e, lv_color_hex(0x9AA0A6), 0);
    }

    for (int i = 0; i < n; i++) {
        const char *nm = A->fs->name(i);
        if (!nm) continue;
        uint32_t sz = A->fs->size ? A->fs->size(i) : 0;

        lv_obj_t *b = lv_button_create(list);
        lv_obj_set_size(b, LV_PCT(100), 30);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(i == sel ? 0x3B82F6 : 0x1E2027), 0);

        char row[80];
        int k = 0;
        for (int j = 0; nm[j] && k < 50; j++) row[k++] = nm[j];
        row[k++] = ' '; row[k++] = '(';
        char num[12];
        int q = 0;
        if (sz == 0) num[q++] = '0';
        while (sz) { num[q++] = (char)('0' + sz % 10); sz /= 10; }
        while (q) row[k++] = num[--q];
        row[k++] = 'B'; row[k++] = ')'; row[k] = 0;

        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, row);
        lv_obj_set_style_text_color(l, lv_color_hex(0xF5F5F7), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_add_event_cb(b, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    if (sel >= 0) read_selected();
    lv_label_set_text(preview, pbuf);
}

static void row_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    sel = i;
    refresh();
}

static void refresh_cb(lv_event_t *e) { (void)e; refresh(); }

static void new_cb(lv_event_t *e) {
    (void)e;
    if (!A || !A->fs || !A->fs->create) { set_status("Olusturma yok"); return; }

    char name[24];
    for (int n = 1; n < 1000; n++) {
        int p = 0;
        const char *pre = "dosya";
        while (*pre) name[p++] = *pre++;
        if (n >= 100) name[p++] = (char)('0' + (n / 100) % 10);
        if (n >= 10) name[p++] = (char)('0' + (n / 10) % 10);
        name[p++] = (char)('0' + n % 10);
        name[p++] = '.'; name[p++] = 't'; name[p++] = 'x'; name[p++] = 't';
        name[p] = 0;
        void *root = A->fs->root ? A->fs->root() : 0;
        if (A->fs->find && !A->fs->find(root, name)) break;
    }

    void *h = A->fs->create(0, name, 0);
    if (h && A->fs->write) A->fs->write(h, "Merhaba Arctian!", 16);
    sel = -1;
    set_status("Olusturuldu");
    refresh();
}

static void del_cb(lv_event_t *e) {
    (void)e;
    if (!A || !A->fs || !A->fs->remove || sel < 0) { set_status("Once dosya secin"); return; }
    const char *nm = A->fs->name ? A->fs->name(sel) : 0;
    if (nm) A->fs->remove(nm);
    sel = -1;
    set_status("Silindi");
    refresh();
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    A = asi;
    sel = -1;

    int w = lv_obj_get_width(win);
    int h = lv_obj_get_height(win);
    if (h < 160) h = 320;

    lv_obj_t *bar = lv_obj_create(win);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), 34);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *bn = lv_button_create(bar);
    lv_obj_set_size(bn, 100, 28);
    lv_obj_set_style_radius(bn, 6, 0);
    lv_obj_set_style_bg_color(bn, lv_color_hex(0x3B82F6), 0);
    lv_obj_align(bn, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_t *bnl = lv_label_create(bn); lv_label_set_text(bnl, "Yeni Dosya");
    lv_obj_set_style_text_color(bnl, lv_color_hex(0xFFFFFF), 0); lv_obj_center(bnl);
    lv_obj_add_event_cb(bn, new_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *bd = lv_button_create(bar);
    lv_obj_set_size(bd, 70, 28);
    lv_obj_set_style_radius(bd, 6, 0);
    lv_obj_set_style_bg_color(bd, lv_color_hex(0xFF5F57), 0);
    lv_obj_align(bd, LV_ALIGN_LEFT_MID, 108, 0);
    lv_obj_t *bdl = lv_label_create(bd); lv_label_set_text(bdl, "Sil");
    lv_obj_set_style_text_color(bdl, lv_color_hex(0xFFFFFF), 0); lv_obj_center(bdl);
    lv_obj_add_event_cb(bd, del_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *be = lv_button_create(bar);
    lv_obj_set_size(be, 70, 28);
    lv_obj_set_style_radius(be, 6, 0);
    lv_obj_set_style_bg_color(be, lv_color_hex(0x1E2027), 0);
    lv_obj_align(be, LV_ALIGN_LEFT_MID, 184, 0);
    lv_obj_t *bel = lv_label_create(be); lv_label_set_text(bel, "Yenile");
    lv_obj_set_style_text_color(bel, lv_color_hex(0xF5F5F7), 0); lv_obj_center(bel);
    lv_obj_add_event_cb(be, refresh_cb, LV_EVENT_CLICKED, 0);

    status = lv_label_create(bar);
    lv_label_set_text(status, "");
    lv_obj_set_style_text_color(status, lv_color_hex(0x9AA0A6), 0);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -4, 0);

    list = lv_obj_create(win);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, w * 52 / 100, h - 40);
    lv_obj_align(list, LV_ALIGN_BOTTOM_LEFT, 2, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x0E0E12), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    preview = lv_label_create(win);
    lv_label_set_long_mode(preview, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(preview, w * 44 / 100);
    lv_obj_set_style_text_color(preview, lv_color_hex(0x7CFC9A), 0);
    lv_obj_align(preview, LV_ALIGN_BOTTOM_RIGHT, -4, 0);

    refresh();
}
