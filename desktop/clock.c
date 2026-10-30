#include "dsk.h"


void clock_tick(void) {
    if (!g_clock_label) return;
    if (!g_opt_clockshow) {
        if (g_tb_clock && !lv_obj_has_flag(g_tb_clock, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(g_tb_clock, LV_OBJ_FLAG_HIDDEN);
        g_clock_state = 0;
        return;
    }
    if (g_clock_state != 1) {
        g_clock_state = 1;
        g_clock_last_sec = -1;
        if (g_tb_clock) lv_obj_remove_flag(g_tb_clock, LV_OBJ_FLAG_HIDDEN);
    }
    if (!g_asi->time || !g_asi->time->now) return;
    asi_datetime_t dt;
    if (!g_asi->time->now(&dt)) return;
    /* H3: yalnizca saniye karsilastirmak dakika atlamasina yol aciyordu
     * (12:00:05 -> 12:01:05 ayni saniye sayilip guncellenmiyordu).
     * Saat/dakika/saniye uclusu birlikte karsilastirilir. */
    static int last_h = -1, last_m = -1;
    if (dt.hour == last_h && dt.minute == last_m && dt.second == g_clock_last_sec)
        return;
    last_h = dt.hour;
    last_m = dt.minute;
    g_clock_last_sec = dt.second;

    int h = dt.hour;
    if (!g_opt_clock24) { if (h == 0) h = 12; else if (h > 12) h -= 12; }

    char tb[12];
    int n = 0;
    tb[n++] = (char)('0' + (h / 10) % 10);
    tb[n++] = (char)('0' + (h % 10));
    tb[n++] = ':';
    tb[n++] = (char)('0' + (dt.minute / 10) % 10);
    tb[n++] = (char)('0' + (dt.minute % 10));
    if (!g_opt_clock24) {
        tb[n++] = ' ';
        tb[n++] = (dt.hour < 12) ? 'A' : 'P';
        tb[n++] = 'M';
    }
    tb[n] = 0;
    lv_label_set_text(g_clock_label, tb);

    if (g_date_label) {
        char db[14];
        int k = 0;
        db[k++] = (char)('0' + (dt.day / 10) % 10);
        db[k++] = (char)('0' + dt.day % 10);
        db[k++] = '.';
        db[k++] = (char)('0' + (dt.month / 10) % 10);
        db[k++] = (char)('0' + dt.month % 10);
        db[k++] = '.';
        int y = dt.year;
        db[k++] = (char)('0' + (y / 1000) % 10);
        db[k++] = (char)('0' + (y / 100) % 10);
        db[k++] = (char)('0' + (y / 10) % 10);
        db[k++] = (char)('0' + y % 10);
        db[k] = 0;
        lv_label_set_text(g_date_label, db);
    }
}


int cal_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}


int cal_days(int y, int m) {
    static const int dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && cal_leap(y)) return 29;
    return dm[m - 1];
}


int cal_first_wday(int y, int m) {
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + 1) % 7;
    return (w + 6) % 7;
}


void cal_nav_cb(lv_event_t *e) {
    int dm = (int)(intptr_t)lv_event_get_user_data(e);
    g_cal_m += dm;
    if (g_cal_m < 1) { g_cal_m = 12; g_cal_y -= 1; }
    if (g_cal_m > 12) { g_cal_m = 1; g_cal_y += 1; }
    cal_render();
}


void cal_out_cb(lv_event_t *e) {
    (void)e;
    if (g_cal_overlay) { lv_obj_delete(g_cal_overlay); g_cal_overlay = 0; }
}


void cal_render(void) {
    if (g_cal_overlay) { lv_obj_delete(g_cal_overlay); g_cal_overlay = 0; }

    int ty = 0, tm = 0, td = 0;
    asi_datetime_t dt;
    if (g_asi->time && g_asi->time->now && g_asi->time->now(&dt)) {
        ty = dt.year; tm = dt.month; td = dt.day;
    }
    if (g_cal_y == 0) { g_cal_y = ty; g_cal_m = tm; }

    g_cal_overlay = lv_obj_create(lv_screen_active());
    base_obj(g_cal_overlay, 0x000000, 0, 0x000000);
    lv_obj_set_style_bg_opa(g_cal_overlay, 0, 0);
    lv_obj_set_style_border_width(g_cal_overlay, 0, 0);
    lv_obj_set_size(g_cal_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_align(g_cal_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(g_cal_overlay, cal_out_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *panel = lv_obj_create(g_cal_overlay);
    base_obj(panel, ADL_SURFACE, 14, ADL_BORDER);
    lv_obj_set_style_bg_opa(panel, 245, 0);
    lv_obj_set_style_shadow_width(panel, 30, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(panel, 170, 0);
    lv_obj_set_style_shadow_ofs_y(panel, 10, 0);
    lv_obj_set_size(panel, 256, 240);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_RIGHT, -24, -92);

    lv_obj_t *prev = lv_obj_create(panel);
    base_obj(prev, ADL_SURFACE_2, 8, ADL_BORDER);
    lv_obj_set_style_border_width(prev, 0, 0);
    lv_obj_set_size(prev, 28, 24);
    lv_obj_set_pos(prev, 8, 8);
    lv_obj_add_flag(prev, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(prev, cal_nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(-1));
    lv_obj_t *pl = label(prev, "<", ADL_TEXT);
    lv_obj_center(pl);

    lv_obj_t *next = lv_obj_create(panel);
    base_obj(next, ADL_SURFACE_2, 8, ADL_BORDER);
    lv_obj_set_style_border_width(next, 0, 0);
    lv_obj_set_size(next, 28, 24);
    lv_obj_set_pos(next, 256 - 36, 8);
    lv_obj_add_flag(next, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(next, cal_nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(1));
    lv_obj_t *nl = label(next, ">", ADL_TEXT);
    lv_obj_center(nl);

    char hm[24];
    int i = 0;
    const char *mn = g_month_tr[(g_cal_m >= 1 && g_cal_m <= 12) ? g_cal_m - 1 : 0];
    for (const char *p = mn; *p && i < 15; p++) hm[i++] = *p;
    hm[i++] = ' ';
    hm[i++] = (char)('0' + (g_cal_y / 1000) % 10);
    hm[i++] = (char)('0' + (g_cal_y / 100) % 10);
    hm[i++] = (char)('0' + (g_cal_y / 10) % 10);
    hm[i++] = (char)('0' + (g_cal_y % 10));
    hm[i] = 0;
    lv_obj_t *title = label(panel, hm, ADL_TEXT);
    lv_obj_set_style_text_font(title, adl_font(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    int cellw = 34, cellh = 27, gx = 8, gy = 66;
    for (int c = 0; c < 7; c++) {
        lv_obj_t *wl = label(panel, g_wday_tr[c], ADL_TEXT_DIM);
        lv_obj_set_style_text_font(wl, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(wl, gx + c * cellw, 42);
    }

    int fw = cal_first_wday(g_cal_y, g_cal_m);
    int nd = cal_days(g_cal_y, g_cal_m);
    for (int d = 1; d <= nd; d++) {
        int idx = fw + d - 1;
        int col = idx % 7, row = idx / 7;
        int x = gx + col * cellw;
        int y = gy + row * cellh;
        int is_today = (g_cal_y == ty && g_cal_m == tm && d == td);
        lv_obj_t *cell = lv_obj_create(panel);
        lv_obj_remove_style_all(cell);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(cell, cellw - 6, cellh - 5);
        lv_obj_set_pos(cell, x, y);
        if (is_today) {
            lv_obj_set_style_bg_color(cell, lv_color_hex(ADL_ACCENT_2), 0);
            lv_obj_set_style_bg_opa(cell, 255, 0);
            lv_obj_set_style_radius(cell, 12, 0);
        }
        char ds[4];
        if (d < 10) { ds[0] = (char)('0' + d); ds[1] = 0; }
        else { ds[0] = (char)('0' + d / 10); ds[1] = (char)('0' + d % 10); ds[2] = 0; }
        lv_obj_t *dl = label(cell, ds, is_today ? 0xFFFFFF : ADL_TEXT);
        lv_obj_set_style_text_font(dl, &lv_font_montserrat_14, 0);
        lv_obj_center(dl);
    }
}


void clock_btn_cb(lv_event_t *e) {
    (void)e;
    if (g_cal_overlay) { lv_obj_delete(g_cal_overlay); g_cal_overlay = 0; return; }
    cal_render();
}

