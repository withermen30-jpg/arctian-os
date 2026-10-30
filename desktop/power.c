#include "dsk.h"


void power_tick(void) {
    if (!g_power_label) return;
    uint64_t now = (g_asi->time && g_asi->time->ticks_ms) ? g_asi->time->ticks_ms() : 0;
    if (g_power_last && now - g_power_last < 2000u) return;
    g_power_last = now;

    int pct = -1, present = 0, chg = 0;
    if (g_asi->power) {
        if (g_asi->power->present) present = g_asi->power->present();
        if (g_asi->power->percent) pct = g_asi->power->percent();
        if (g_asi->power->charging) chg = g_asi->power->charging();
    }
    if (pct < 0 && !present) pct = 100;   /* AC: tam dolu goster */

    char b[16];
    int n = 0;
    if (pct >= 0) {
        if (pct >= 100) { b[n++] = '1'; b[n++] = '0'; b[n++] = '0'; }
        else {
            if (pct >= 10) b[n++] = (char)('0' + (pct / 10) % 10);
            b[n++] = (char)('0' + pct % 10);
        }
        b[n++] = '%';
    } else {
        b[n++] = '-'; b[n++] = '-';
    }
    if (chg) b[n++] = '+';
    b[n] = 0;
    lv_label_set_text(g_power_label, b);
    /* Pil seviyesi ikonun dolgu cubuguna yazilir (%15 altinda
     * accent rengi). Yuzde metni dock'ta gorunmez. */
    dock_battery_level(pct);

    /* Pil uyarilari (0.6): %15 ve %5 esiklerinde bir kez bildir.
     * Sarj olurken uyari verilmez; esik yukselince yeniden kurulur. */
    static int warned15, warned5;
    if (pct > 20) { warned15 = 0; warned5 = 0; }
    if (present && !chg && pct >= 0) {
        if (pct <= 5 && !warned5) {
            warned5 = 1;
            arctian_notify("Pil kritik", "Pil %5 veya altinda: hemen sarj edin");
        } else if (pct <= 15 && !warned15) {
            warned15 = 1;
            arctian_notify("Pil azaliyor", "Pil %15: sarj cihazini baglayin");
        }
    }
}

void bat_panel_close(void) {
    if (g_bat_panel) { lv_obj_delete(g_bat_panel); g_bat_panel = 0; }
}


void bat_overlay_cb(lv_event_t *e) { (void)e; bat_panel_close(); }


void battery_btn_cb(lv_event_t *e) {
    (void)e;
    if (g_bat_panel) { bat_panel_close(); return; }

    g_bat_panel = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(g_bat_panel);
    lv_obj_set_size(g_bat_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(g_bat_panel, 0, 0);
    lv_obj_add_flag(g_bat_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_bat_panel, bat_overlay_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *p = lv_obj_create(g_bat_panel);
    base_obj(p, ADL_SURFACE, ui_s(14), ADL_BORDER);
    lv_obj_set_style_bg_opa(p, 250, 0);
    lv_obj_set_style_shadow_width(p, ui_s(26), 0);
    lv_obj_set_style_shadow_color(p, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(p, 180, 0);
    lv_obj_set_size(p, ui_s(420), ui_s(260));
    lv_obj_align(p, LV_ALIGN_BOTTOM_RIGHT, -ui_s(56), -(TB_BH + TB_Y_OFF + ui_s(276)));
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = label(p, "Pil", ADL_TEXT);
    if (!s_f_dlabel) s_f_dlabel = lv_tiny_ttf_create_data(font_ttf, font_ttf_len, ui_s(22));
    if (s_f_dlabel) lv_tiny_ttf_set_size(s_f_dlabel, ui_s(22));
    lv_obj_set_style_text_font(t, s_f_dlabel ? s_f_dlabel : adl_font(), 0);
    lv_obj_set_pos(t, ui_s(24), ui_s(18));

    int pct = -1, pres = 0, chg = 0, mins = -1;
    if (g_asi->power) {
        if (g_asi->power->present) pres = g_asi->power->present();
        if (g_asi->power->percent) pct = g_asi->power->percent();
        if (g_asi->power->charging) chg = g_asi->power->charging();
        if (g_asi->power->minutes) mins = g_asi->power->minutes();
    }

    lv_obj_t *big = lv_label_create(p);
    lv_obj_set_style_text_color(big, lv_color_hex(ADL_TEXT), 0);
    char bs[8];
    if (pct >= 0) {
        int k = 0;
        if (pct >= 100) bs[k++] = '1';
        bs[k++] = (char)('0' + (pct % 100) / 10);
        bs[k++] = (char)('0' + pct % 10);
        bs[k++] = '%'; bs[k] = 0;
    } else scpy(bs, (int)sizeof(bs), "%--");
    lv_label_set_text(big, bs);
    if (s_f_time) lv_obj_set_style_text_font(big, s_f_time, 0);
    lv_obj_align(big, LV_ALIGN_TOP_LEFT, ui_s(24), ui_s(66));

    char st[56];
    if (!pres) scpy(st, (int)sizeof(st), "Pil bulunamadi (AC adaptorde)");
    else if (chg) scpy(st, (int)sizeof(st), "Sarijda");
    else if (mins >= 0) {
        scpy(st, (int)sizeof(st), "Pilde - tahmini ");
        int k = 0; while (st[k]) k++;
        char mb[8];
        int n = 0; int v = mins;
        char tv[8]; int tn = 0;
        if (v == 0) tv[tn++] = '0';
        while (v) { tv[tn++] = (char)('0' + v % 10); v /= 10; }
        while (tn) mb[n++] = tv[--tn];
        mb[n] = 0;
        scpy(st + k, (int)sizeof(st) - k, mb);
        k = 0; while (st[k]) k++;
        scpy(st + k, (int)sizeof(st) - k, " dk");
    } else scpy(st, (int)sizeof(st), "Pilde");
    lv_obj_t *sl = label(p, st, ADL_TEXT_DIM);
    lv_obj_set_style_text_font(sl, s_f_dlabel ? s_f_dlabel : adl_font(), 0);
    lv_obj_align(sl, LV_ALIGN_TOP_LEFT, ui_s(24), ui_s(150));

    lv_obj_t *ic = make_icon(p, SVG_TB_BATTERY, ui_s(92));
    lv_obj_align(ic, LV_ALIGN_TOP_RIGHT, -ui_s(34), ui_s(60));
}

/* ---------------- Guc diyalogu (Linux tarzi, ortada) ---------------- */
void power_cancel_cb(lv_event_t *e) {
    lv_obj_delete((lv_obj_t *)lv_event_get_user_data(e));
}

void power_off_cb(lv_event_t *e) {
    (void)e;
    /* 0.7: uygun ve hazirsa guncelleme yuvasi burada cevrilir;
     * ag yoksa veya indirme bitmediyse beklemeden kapanir. */
    update_shutdown_step();
    if (g_asi->system) g_asi->system->shutdown();
}

void power_reboot_cb(lv_event_t *e) {
    (void)e;
    if (g_asi->system) g_asi->system->reboot();
}


/* Ekrani karart, CPU'yu uyut, tus/fare ile uyan. */
void power_sleep_cb(lv_event_t *e) {
    lv_obj_t *dlg = (lv_obj_t *)lv_event_get_user_data(e);
    if (dlg) lv_obj_delete(dlg);

    lv_obj_t *ov = lv_obj_create(lv_screen_active());
    base_obj(ov, 0x000000, 0, 0x000000);
    lv_obj_set_style_bg_opa(ov, 255, 0);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_center(ov);
    lv_obj_t *l = label(ov, "Uyku modu\n\nDevam icin bir tusa basin veya fareyi hareket ettirin.", ADL_TEXT_DIM);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 620);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);

    lv_port_tick_advance(16);
    g_asi->gfx->present();

    if (g_asi->power && g_asi->power->sleep) g_asi->power->sleep();

    lv_obj_delete(ov);
}


void open_power_dialog(void) {
    lv_obj_t *ov = lv_obj_create(lv_screen_active());
    base_obj(ov, 0x000000, 0, 0x000000);
    lv_obj_set_style_bg_opa(ov, 150, 0);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_align(ov, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *panel = lv_obj_create(ov);
    base_obj(panel, ADL_SURFACE, 14, ADL_BORDER);
    lv_obj_set_style_shadow_width(panel, 30, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(panel, 190, 0);
    lv_obj_set_size(panel, 480, 210);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);

    lv_obj_t *ic = make_icon(panel, SVG_POWER, 34);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *title = label(panel, "Bilgisayari Kapat", ADL_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);

    lv_obj_t *msg = label(panel, "Kapatmak, uyutmak veya yeniden baslatmak istediginizden emin misiniz?", ADL_TEXT_DIM);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg, 436);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 68);

    lv_obj_t *row = lv_obj_create(panel);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 44);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t *off = lv_button_create(row);
    lv_obj_set_size(off, 96, 40);
    lv_obj_set_style_radius(off, 8, 0);
    lv_obj_set_style_bg_color(off, lv_color_hex(ADL_DANGER), 0);
    { lv_obj_t *ol = label(off, "Kapat", ADL_TEXT); lv_obj_center(ol); }
    lv_obj_add_event_cb(off, power_off_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *slp = lv_button_create(row);
    lv_obj_set_size(slp, 88, 40);
    lv_obj_set_style_radius(slp, 8, 0);
    lv_obj_set_style_bg_color(slp, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_border_width(slp, 1, 0);
    lv_obj_set_style_border_color(slp, lv_color_hex(ADL_BORDER), 0);
    { lv_obj_t *sl = label(slp, "Uyku", ADL_TEXT); lv_obj_center(sl); }
    lv_obj_add_event_cb(slp, power_sleep_cb, LV_EVENT_CLICKED, ov);

    lv_obj_t *rb = lv_button_create(row);
    lv_obj_set_size(rb, 132, 40);
    lv_obj_set_style_radius(rb, 8, 0);
    lv_obj_set_style_bg_color(rb, lv_color_hex(ADL_ACCENT), 0);
    { lv_obj_t *rl = label(rb, "Yeniden Baslat", ADL_TEXT); lv_obj_center(rl); }
    lv_obj_add_event_cb(rb, power_reboot_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *cx = lv_button_create(row);
    lv_obj_set_size(cx, 80, 40);
    lv_obj_set_style_radius(cx, 8, 0);
    lv_obj_set_style_bg_color(cx, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_border_width(cx, 1, 0);
    lv_obj_set_style_border_color(cx, lv_color_hex(ADL_BORDER), 0);
    { lv_obj_t *xl = label(cx, "Iptal", ADL_TEXT); lv_obj_center(xl); }
    lv_obj_add_event_cb(cx, power_cancel_cb, LV_EVENT_CLICKED, ov);
}


void power_btn_cb(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    open_power_dialog();
}


void settings_btn_cb(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    open_app("Ayarlar");
}


void switch_user_cb(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    arctian_shell_lock_screen();
}

