#include "dsk.h"


void net_panel_close(void) {
    if (g_net_panel) { lv_obj_delete(g_net_panel); g_net_panel = 0; g_net_body = 0; }
    g_net_stage = 0;
}

void net_close_cb(lv_event_t *e) { (void)e; net_panel_close(); }

void net_overlay_click_cb(lv_event_t *e) {
    if (lv_event_get_target(e) == g_net_panel) net_panel_close();
}


void wifi_pick_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_scan_n) return;
    int i = 0;
    for (; g_scan[idx].ssid[i] && i < 32; i++) g_net_ssid[i] = g_scan[idx].ssid[i];
    g_net_ssid[i] = 0;
    g_net_pass[0] = 0;
    if (!g_scan[idx].secure) {
        if (g_asi->net && g_asi->net->wifi_connect) g_asi->net->wifi_connect(g_net_ssid, "");
        g_net_stage = 0;
    } else {
        g_net_stage = 1;
    }
    net_panel_refresh();
}

void wifi_back_cb(lv_event_t *e) { (void)e; g_net_stage = 0; net_panel_refresh(); }

void wifi_connect_cb(lv_event_t *e) {
    (void)e;
    if (g_asi->net && g_asi->net->wifi_connect) g_asi->net->wifi_connect(g_net_ssid, g_net_pass);
    g_net_stage = 0;
    net_panel_refresh();
}

void wifi_disc_cb(lv_event_t *e) {
    (void)e;
    if (g_asi->net && g_asi->net->wifi_disconnect) g_asi->net->wifi_disconnect();
    net_panel_refresh();
}

void pass_ta_cb(lv_event_t *e) {
    const char *t = lv_textarea_get_text(lv_event_get_target(e));
    int i = 0; for (; t[i] && i < 63; i++) g_net_pass[i] = t[i];
    g_net_pass[i] = 0;
}


lv_obj_t *net_row(lv_obj_t *parent, const char *ssid, int secure, int rssi) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 288, 46);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_t *nm = label(b, ssid, ADL_TEXT);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 12, secure ? -8 : 0);
    if (secure) {
        lv_obj_t *lk = label(b, "sifreli", ADL_WARN);
        lv_obj_align(lk, LV_ALIGN_LEFT_MID, 12, 10);
    }
    int bars = (rssi > -50) ? 4 : (rssi > -62) ? 3 : (rssi > -74) ? 2 : 1;
    char sig[8]; int i = 0;
    for (; i < bars; i++) sig[i] = '|';
    sig[i] = 0;
    lv_obj_t *sg = label(b, sig, ADL_OK);
    lv_obj_align(sg, LV_ALIGN_RIGHT_MID, -12, 0);
    return b;
}


void net_panel_refresh(void) {
    if (!g_net_body) return;
    lv_obj_clean(g_net_body);
    asi_net_t *n = g_asi ? g_asi->net : 0;
    if (!n) {
        lv_obj_t *l = label(g_net_body, "Ag servisi yok", ADL_TEXT_DIM);
        lv_obj_set_pos(l, 4, 4);
        return;
    }

    int lt = n->link_type ? n->link_type() : 0;
    uint8_t ip[4] = {0, 0, 0, 0};
    if (n->ip) n->ip(ip);

    if (g_net_stage == 1) {
        lv_obj_t *t = label(g_net_body, "Sifre gir", ADL_TEXT);
        lv_obj_set_pos(t, 4, 2);
        lv_obj_t *s = label(g_net_body, g_net_ssid, ADL_TEXT_DIM);
        lv_obj_set_pos(s, 4, 24);

        lv_obj_t *ta = lv_textarea_create(g_net_body);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, true);
        lv_textarea_set_placeholder_text(ta, "Wi-Fi sifresi");
        lv_obj_set_width(ta, 288);
        lv_obj_set_pos(ta, 4, 48);
        lv_obj_add_event_cb(ta, pass_ta_cb, LV_EVENT_VALUE_CHANGED, 0);

        lv_obj_t *kb = lv_keyboard_create(g_net_body);
        lv_obj_set_size(kb, 288, 150);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_keyboard_set_textarea(kb, ta);
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);

        lv_obj_t *b = lv_button_create(g_net_body);
        lv_obj_set_size(b, 140, 36);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(ADL_ACCENT), 0);
        lv_obj_align_to(b, kb, LV_ALIGN_OUT_TOP_LEFT, 0, -6);
        lv_obj_t *bl = label(b, "Baglan", ADL_TEXT); lv_obj_center(bl);
        lv_obj_add_event_cb(b, wifi_connect_cb, LV_EVENT_CLICKED, 0);

        lv_obj_t *bb = lv_button_create(g_net_body);
        lv_obj_set_size(bb, 140, 36);
        lv_obj_set_style_radius(bb, 8, 0);
        lv_obj_set_style_bg_color(bb, lv_color_hex(ADL_SURFACE_2), 0);
        lv_obj_align_to(bb, kb, LV_ALIGN_OUT_TOP_RIGHT, 0, -6);
        lv_obj_t *bbl = label(bb, "Geri", ADL_TEXT); lv_obj_center(bbl);
        lv_obj_add_event_cb(bb, wifi_back_cb, LV_EVENT_CLICKED, 0);
        return;
    }

    lv_obj_t *card = lv_obj_create(g_net_body);
    lv_obj_remove_style_all(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_size(card, 288, 64);
    lv_obj_set_pos(card, 4, 2);

    if (lt == ASI_LINK_ETHERNET) {
        lv_obj_t *t = label(card, "Ethernet ile bagli", ADL_OK);
        lv_obj_set_pos(t, 12, 8);
        lv_obj_t *s = label(card, "", ADL_TEXT_DIM);
        lv_obj_set_pos(s, 12, 34);
        lv_label_set_text_fmt(s, "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    } else if (lt == ASI_LINK_WIFI) {
        const char *ssid = (n->wifi_ssid) ? n->wifi_ssid() : "";
        lv_obj_t *t = label(card, "Wi-Fi ile bagli", ADL_OK);
        lv_obj_set_pos(t, 12, 8);
        lv_obj_t *s = label(card, "", ADL_TEXT_DIM);
        lv_obj_set_pos(s, 12, 34);
        lv_label_set_text_fmt(s, "%s  IP: %d.%d.%d.%d", ssid, ip[0], ip[1], ip[2], ip[3]);
        lv_obj_t *d = lv_button_create(card);
        lv_obj_set_size(d, 90, 28);
        lv_obj_set_style_radius(d, 6, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(ADL_DANGER), 0);
        lv_obj_align(d, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_t *dl = label(d, "Kes", ADL_TEXT); lv_obj_center(dl);
        lv_obj_add_event_cb(d, wifi_disc_cb, LV_EVENT_CLICKED, 0);
    } else {
        lv_obj_t *t = label(card, "Baglanti yok", ADL_WARN);
        lv_obj_set_pos(t, 12, 8);
        lv_obj_t *s = label(card, "Bir ag secin", ADL_TEXT_DIM);
        lv_obj_set_pos(s, 12, 34);
    }

    if (lt != ASI_LINK_ETHERNET && lt != ASI_LINK_WIFI) {
        lv_obj_t *h = label(g_net_body, "Kullanilabilir aglar", ADL_TEXT_DIM);
        lv_obj_set_pos(h, 4, 76);

        int wifi_hw = (n->wifi_hw) ? n->wifi_hw() : 0;
        g_scan_n = (n->wifi_scan) ? n->wifi_scan(g_scan, 16) : 0;
        if (g_scan_n == 0) {
            const char *msg;
            if (!wifi_hw)       msg = "Wi-Fi donanimi bulunamadi";
            else                msg = "Wi-Fi karti var, surucu/beyan yok";
            lv_obj_t *e = label(g_net_body, msg, ADL_TEXT_DIM);
            lv_obj_set_pos(e, 4, 100);
        }
        for (int i = 0; i < g_scan_n; i++) {
            lv_obj_t *b = net_row(g_net_body, g_scan[i].ssid, g_scan[i].secure, g_scan[i].rssi);
            lv_obj_set_pos(b, 4, 100 + i * 52);
            lv_obj_add_event_cb(b, wifi_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    }
}


void open_network_panel(void) {
    if (g_net_panel) { net_panel_close(); return; }
    g_net_stage = 0;

    g_net_panel = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(g_net_panel);
    lv_obj_set_size(g_net_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(g_net_panel, 0, 0);
    lv_obj_add_flag(g_net_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_net_panel, net_overlay_click_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *panel = lv_obj_create(g_net_panel);
    base_obj(panel, ADL_SURFACE, 14, ADL_BORDER);
    lv_obj_set_style_bg_opa(panel, 250, 0);
    lv_obj_set_style_shadow_width(panel, 26, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(panel, 180, 0);
    lv_obj_set_size(panel, 320, 420);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_RIGHT, -30, -92);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = label(panel, "Ag", ADL_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 10);

    lv_obj_t *x = lv_button_create(panel);
    lv_obj_set_size(x, 28, 28);
    lv_obj_set_style_radius(x, 14, 0);
    lv_obj_set_style_bg_color(x, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, -10, 8);
    lv_obj_t *xl = label(x, "x", ADL_TEXT); lv_obj_center(xl);
    lv_obj_add_event_cb(x, net_close_cb, LV_EVENT_CLICKED, 0);

    g_net_body = lv_obj_create(panel);
    lv_obj_remove_style_all(g_net_body);
    lv_obj_set_size(g_net_body, 296, 354);
    lv_obj_set_pos(g_net_body, 12, 44);
    lv_obj_remove_flag(g_net_body, LV_OBJ_FLAG_SCROLLABLE);

    net_panel_refresh();
}


void wifi_btn_cb(lv_event_t *e) { (void)e; open_network_panel(); }

