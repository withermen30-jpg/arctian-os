#include "dsk.h"
#include <stdint.h>
#include <string.h>
/* Secim araligini gormek icin LVGL ozel basligi (Ctrl+C/X/A): altyapi
 * derleme yolunun parcasidir. */
#include "src/widgets/textarea/lv_textarea_private.h"


/* Odakli textarea'nin etiketi (secim cizimi icin); ta'nin ilk cocugu. */
static lv_obj_t *ta_label(lv_obj_t *ta) {
    return (ta && lv_obj_get_child_count(ta) > 0) ? lv_obj_get_child(ta, 0) : 0;
}


static int ta_has_sel(lv_obj_t *ta) {
    return ta && lv_textarea_text_is_selected(ta);
}


/* Secili metni al (yoksa 0 doner). */
static int ta_sel_text(lv_obj_t *ta, char *out, int cap) {
    if (!ta_has_sel(ta) || !out || cap < 1) return 0;
    lv_obj_t *lab = ta_label(ta);
    const char *txt = lv_textarea_get_text(ta);
    if (!lab || !txt) return 0;
    uint32_t a = lv_label_get_text_selection_start(lab);
    uint32_t b = lv_label_get_text_selection_end(lab);
    if (a > b) { uint32_t t = a; a = b; b = t; }
    uint32_t len = (uint32_t)strlen(txt);
    if (b > len) b = len;
    if (a >= b) return 0;
    uint32_t n = b - a;
    if (n >= (uint32_t)cap) n = (uint32_t)cap - 1;
    for (uint32_t i = 0; i < n; i++) out[i] = txt[a + i];
    out[n] = 0;
    return (int)n;
}


/* Tum metni sec (Ctrl+A): LVGL icin iki yer guncellenir — silme islemlerinin
 * baktigi ta->sel_* ve cizim/text_is_selected'in baktigi etiket. */
static void ta_select_all(lv_obj_t *ta) {
    if (!ta) return;
    lv_textarea_set_text_selection(ta, true);
    const char *txt = lv_textarea_get_text(ta);
    uint32_t len = txt ? (uint32_t)strlen(txt) : 0;
    lv_textarea_t *t = (lv_textarea_t *)ta;
    t->sel_start = 0;
    t->sel_end = len;
    lv_obj_t *lab = ta_label(ta);
    if (lab) {
        lv_label_set_text_selection_start(lab, 0);
        lv_label_set_text_selection_end(lab, len);
    }
    lv_obj_invalidate(ta);
}


/* Secimi sil: LVGL BACKSPACE olayi secim varsa tumunu siler. */
static void ta_delete_selection(lv_obj_t *ta) {
    if (!ta) return;
    uint32_t k = (uint32_t)LV_KEY_BACKSPACE;
    lv_obj_send_event(ta, LV_EVENT_KEY, &k);
}


/* Odakli textarea'ya pano islemi uygula. 1 = islendi (textarea odakta);
 * 0 = odakli textarea yok -> kisayol en ust pencereye yonlendirilir. */
static int clip_action(int code) {
    lv_indev_t *kp = lv_port_get_keypad();
    lv_group_t *g = kp ? lv_indev_get_group(kp) : 0;
    lv_obj_t *foc = g ? lv_group_get_focused(g) : 0;
    if (!foc || !lv_obj_check_type(foc, &lv_textarea_class)) return 0;

    static char sel[4096];

    if (code == 3) {                                  /* Ctrl+C kopyala */
        if (ta_has_sel(foc)) {
            ta_sel_text(foc, sel, sizeof(sel));
            arctian_clipboard_set(sel);
        } else {
            arctian_clipboard_set(lv_textarea_get_text(foc));
        }
        return 1;
    }
    if (code == 4) {                                  /* Ctrl+X kes */
        if (ta_has_sel(foc)) {
            ta_sel_text(foc, sel, sizeof(sel));
            arctian_clipboard_set(sel);
            ta_delete_selection(foc);
        } else {
            arctian_clipboard_set(lv_textarea_get_text(foc));
            lv_textarea_set_text(foc, "");
        }
        return 1;
    }
    if (code == 5) {                                  /* Ctrl+V yapistir */
        const char *s = arctian_clipboard_get();
        if (s && s[0]) {
            if (ta_has_sel(foc)) ta_delete_selection(foc);   /* secimi ezer */
            lv_textarea_add_text(foc, s);
        }
        return 1;
    }
    if (code == 6) { ta_select_all(foc); return 1; }  /* Ctrl+A tumunu sec */
    if (code == 7) { arctian_shell_text_undo(0); return 1; } /* Ctrl+Z */
    if (code == 8) { arctian_shell_text_undo(1); return 1; } /* Ctrl+Y */
    return 1;
}

/* ------------------------------------------------------------------ */
/* Alt+Tab pencere degistirici - baslikli onizleme kartlari           */
/* Panel acikken her Alt+Tab bir ilerletir; 1.6 sn dokunulmazsa       */
/* vurgulanan pencere one cikar ve panel kapanir.                     */
/* ------------------------------------------------------------------ */
static lv_obj_t *s_sw_panel;
static int s_sw_idx = -1;
static uint64_t s_sw_last;

static uint64_t switcher_now(void) {
    return (g_asi->time && g_asi->time->ticks_ms) ? g_asi->time->ticks_ms() : 0;
}

static void switcher_close(void) {
    if (s_sw_panel) lv_obj_delete(s_sw_panel);
    s_sw_panel = 0;
    s_sw_idx = -1;
}

static void switcher_activate(void) {
    lv_obj_t *wins[MAX_WINS];
    int n = 0;
    for (int i = 0; i < MAX_WINS; i++)
        if (g_recs[i] && g_recs[i]->win) wins[n++] = g_recs[i]->win;
    if (n > 0 && s_sw_idx >= 0) {
        lv_obj_t *w = wins[s_sw_idx % n];
        lv_obj_remove_flag(w, LV_OBJ_FLAG_HIDDEN);  /* kucultulmusse geri ac */
        lv_obj_move_foreground(w);
    }
    switcher_close();
}

static void switcher_paint(int n) {
    for (int i = 0; i < n && s_sw_panel; i++) {
        lv_obj_t *card = lv_obj_get_child(s_sw_panel, i);
        if (!card) continue;
        int on = (i == s_sw_idx);
        lv_obj_set_style_bg_color(card, lv_color_hex(on ? 0x3A4152 : ADL_SURFACE), 0);
        lv_obj_set_style_border_width(card, on ? 2 : 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(on ? ADL_ACCENT
                                                            : ADL_BORDER), 0);
    }
}

static void switcher_card_cb(lv_event_t *e) {
    s_sw_idx = (int)(intptr_t)lv_event_get_user_data(e);
    switcher_activate();
}

static void switcher_tick(void) {
    if (!s_sw_panel) return;
    if (switcher_now() - s_sw_last < 1600) return;
    switcher_activate();
}

static void switcher_show(void) {
    winrec_t *recs[MAX_WINS];
    int n = 0;
    for (int i = 0; i < MAX_WINS; i++)
        if (g_recs[i] && g_recs[i]->win) recs[n++] = g_recs[i];
    if (n < 1) return;

    if (!s_sw_panel) {
        const int cw = 210, ch = 150, gap = 14, pad = 10;
        s_sw_panel = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(s_sw_panel);
        lv_obj_remove_flag(s_sw_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_sw_panel, n * (cw + gap) - gap + 2 * pad, ch + 2 * pad);
        lv_obj_align(s_sw_panel, LV_ALIGN_TOP_MID, 0, 96);
        lv_obj_set_style_bg_color(s_sw_panel, lv_color_hex(0x0E1015), 0);
        lv_obj_set_style_bg_opa(s_sw_panel, 216, 0);
        lv_obj_set_style_radius(s_sw_panel, 14, 0);
        lv_obj_set_style_pad_all(s_sw_panel, pad, 0);
        for (int i = 0; i < n; i++) {
            lv_obj_t *c = lv_obj_create(s_sw_panel);
            lv_obj_remove_style_all(c);
            lv_obj_set_size(c, cw, ch);
            lv_obj_set_style_radius(c, 8, 0);
            lv_obj_set_style_bg_color(c, lv_color_hex(ADL_SURFACE), 0);
            lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(c, 1, 0);
            lv_obj_set_style_border_color(c, lv_color_hex(ADL_BORDER), 0);
            /* mini baslik cubugu (pencere rengi temsili) */
            lv_obj_t *bar = lv_obj_create(c);
            lv_obj_remove_style_all(bar);
            lv_obj_set_size(bar, cw - 16, 18);
            lv_obj_set_pos(bar, 8, 8);
            lv_obj_set_style_radius(bar, 4, 0);
            lv_obj_set_style_bg_color(bar, lv_color_hex(ADL_ACCENT), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            /* icerik satirlari temsili */
            for (int k = 0; k < 3; k++) {
                lv_obj_t *ln = lv_obj_create(c);
                lv_obj_remove_style_all(ln);
                lv_obj_set_size(ln, cw - 44 - k * 16, 6);
                lv_obj_set_pos(ln, 14, 44 + k * 15);
                lv_obj_set_style_radius(ln, 3, 0);
                lv_obj_set_style_bg_color(ln, lv_color_hex(ADL_SURFACE_2), 0);
                lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
            }
            lv_obj_t *tl = label(c, recs[i]->title, ADL_TEXT);
            lv_obj_set_width(tl, cw - 20);
            lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
            lv_obj_align(tl, LV_ALIGN_BOTTOM_MID, 0, -9);
            lv_obj_add_event_cb(c, switcher_card_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)i);
        }
        /* ilk acilista one cikandan sonraki pencere secili gelsin */
        int top = 0;
        winrec_t *tr = winrec_top();
        for (int i = 0; i < n; i++)
            if (recs[i] == tr) { top = i; break; }
        s_sw_idx = (top + 1) % n;
    } else {
        s_sw_idx = (s_sw_idx + 1) % n;
    }
    s_sw_last = switcher_now();
    switcher_paint(n);
}

/* Giris: duz binary modul olarak yuklenir — ilk bayt .text.entry olmali
 * (linker.ld: *(.text.entry) en onde). Ozellik yoksa yanlis fonksiyon
 * cagirilir ve masaustu hic baslamaz. */
/* ---- Kare hizi HUD'u (0.6) ----
 * Ctrl+Alt+F ile acilir; sag ustte gercek fps ve kare suresini gosterir.
 * Performans calismasinin etkisini kullanici da gorebilsin. */
static lv_obj_t *s_hud;
static int       s_hud_on;

static void hud_update(uint64_t now) {
    static uint32_t frames;
    static uint64_t t0;
    static uint32_t last_frames;
    if (!s_hud_on) {
        if (s_hud && !lv_obj_has_flag(s_hud, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(s_hud, LV_OBJ_FLAG_HIDDEN);
        frames = 0; t0 = 0;
        return;
    }
    if (!s_hud) {
        s_hud = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(s_hud);
        lv_obj_remove_flag(s_hud, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_hud, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(s_hud, ui_s(190), ui_s(44));
        lv_obj_align(s_hud, LV_ALIGN_TOP_RIGHT, -ui_s(24), ui_s(24));
        lv_obj_set_style_bg_color(s_hud, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_hud, 170, 0);
        lv_obj_set_style_radius(s_hud, ui_s(10), 0);
        lv_obj_set_style_pad_all(s_hud, ui_s(8), 0);
        lv_obj_t *l = lv_label_create(s_hud);
        lv_label_set_text(l, "fps --");
        lv_obj_set_style_text_color(l, lv_color_hex(0x53E68A), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_center(l);
    }
    lv_obj_remove_flag(s_hud, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_hud);

    if (!t0) t0 = now;
    frames++;
    if (now - t0 >= 1000) {
        last_frames = (uint32_t)((uint64_t)frames * 1000u / (now - t0));
        frames = 0; t0 = now;
        lv_obj_t *l = lv_obj_get_child(s_hud, 0);
        if (l) {
            char b[40];
            int n = 0;
            const char *p = "fps ";
            while (*p) b[n++] = *p++;
            uint32_t v = last_frames;
            char tmp[8]; int k = 0;
            if (!v) tmp[k++] = '0';
            while (v && k < 8) { tmp[k++] = (char)('0' + v % 10); v /= 10; }
            while (k) b[n++] = tmp[--k];
            b[n++] = ' ';
            b[n++] = ' '; b[n++] = '|'; b[n++] = ' ';
            p = "kare ";
            while (*p) b[n++] = *p++;
            p = "cizim";
            while (*p) b[n++] = *p++;
            b[n] = 0;
            lv_label_set_text(l, b);
        }
    }
}

void fps_hud_toggle(void) { s_hud_on = !s_hud_on; }

void desktop_main(void *arg) __attribute__((section(".text.entry")));

/* 0.7: ayar okuyuculari (update.c bunlari cagirir).      */
static int upd_auto_setting(void)  { return kv_get_int("update_auto", 1); }
static int upd_delay_setting(void) {
    int v = kv_get_int("update_delay_days", 0);
    if (v < 0) v = 0;
    if (v > 30) v = 30;
    return v;
}

/* ------------------------------------------------------------------ */
/* 0.7: olayin hedefi bir kutulu uygulamanin govedesiyse olay           */
/* uygulama kodu sayilir; dosya/komut kapilari o an acilir.            */
/* linker `--wrap=lv_obj_send_event` ile LVGL'in tek gonderim            */
/* noktasi uzerine gelistirildi.                                        */
/* ------------------------------------------------------------------ */
extern lv_result_t __real_lv_obj_send_event(lv_obj_t *obj,
                                            lv_event_code_t event_code,
                                            void *param);

lv_result_t __wrap_lv_obj_send_event(lv_obj_t *obj,
                                     lv_event_code_t event_code,
                                     void *param) {
    int mine = perm_begin_obj(obj);
    lv_result_t r = __real_lv_obj_send_event(obj, event_code, param);
    if (mine) perm_end_obj();
    return r;
}

void desktop_main(void *arg) {
    if (s_desktop_started) {
        if (g_recover.active) {
            recover_set_reason("Uygulama gecersiz bir adres cagirdi");
            arctian_longjmp(&g_recover, 3);
        }
        return;
    }
    s_desktop_started = 1;
    g_asi = (asi_t *)arg;

    lv_port_init(g_asi);
    disk_unlock_run(g_asi);   /* disk kilitliyse ac; ayarlar disktan okunur */
    settings_load();          /* fontscale/contrast/focusring tercihleri */
    /* 0.7 (H3): RTC/NTP UTC tutar; gosterilen saat yerel olmali.
     * Ayar yoksa Europe/Istanbul (UTC+3) varsayilir. */
    if (g_asi->time && g_asi->time->set_tz_offset)
        g_asi->time->set_tz_offset(kv_get_int("tz_minutes", 180));
    perm_init();              /* 0.7: karar deposu + dosya kapisi */
    slot_init(g_asi);         /* 0.7: A/B yuva durumu */
    adl_font_init();          /* yaz olcegi ayarlanmis olur (kv -> adl_*) */
    adl_wolf_theme_init();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(ADL_BG), 0);
    lv_obj_set_style_text_font(scr, adl_font(), 0);

    /* Klavye ile tam gezinme: masaustu grubu — simgeler ve gorev
     * cubugu dugmeleri buraya eklenir; Tab/Enter ile gezinilir. */
    g_desktop_group = lv_group_create();

    build_cursor();
    if (g_asi->input->kbd_layout) {
        const char *kbd = kv_get("kbd", "trq");
        g_asi->input->kbd_layout(kbd[0] == 'e' ? 2 : (kbd[2] == 'f' ? 1 : 0));
    }
    boot_apply_saved_resolution();
    ui_scale_refresh();
    build_wallpaper();
#ifdef ARCTIAN_AUTOUSER
    /* Gelistirme (ekran goruntusu): hesap yoksa varsayilan kullanici ac. */
    if (!arctian_auth_has_user()) {
        arctian_auth_create_user("arctian", 0);
        arctian_auth_set_current("arctian");
    }
#endif
#ifdef ARCTIAN_DEBUG_BOOT
    if (!arctian_auth_has_user()) {
        arctian_auth_create_user("debug", 0);
        arctian_auth_set_current("debug");
    }
#else
    lock_screen_run(g_asi);
#endif
    dyn_scan_afs();
    update_init(g_asi);
    update_set_installer(store_install);
    update_set_notify(rebuild_shell_ui);
    update_set_settings(upd_auto_setting, upd_delay_setting);
    /* 0.7: acilis adimi. Kapanista bitmemis kurulum burada biter;
     * boot_ok vermeyen yuva bir onceki yuvaya doner. Ag cagrisi yapilmaz. */
    {
        int bad = -1;
        if (slot_boot_guard(&bad, 0) && g_asi->console && g_asi->console->puts)
            g_asi->console->puts("[yuva] yeni yuva acilamadi, onceki yuvaya donuldu\n");
        slot_mark_boot_ok();
        update_boot_step();
    }
    build_desktop_icons();
    build_start_menu(scr);
    build_taskbar(scr);
    assistant_init();   /* asistan paneli — gorev cubugu dugmesiyle acilir */

    qc_init();   /* hizli ayarlar + OSD + bildirim merkezi + perdeler */
    {
        /* Varsayilan keypad grubu: masaustu gezinme. Uygulamalar
         * kendi gruplarini kurar/kaldirir; ana dongu bosalirsa geri koyar. */
        lv_indev_t *kp = lv_port_get_keypad();
        if (kp) lv_indev_set_group(kp, g_desktop_group);
    }


#ifdef ARCTIAN_DS_TEST
    duckstore_selftest();
#endif
#ifdef ARCTIAN_UPDATE_SELFTEST
    update_selftest();
#endif
#ifdef ARCTIAN_06_TEST
    t06_selftest();
#endif
#ifdef ARCTIAN_07_TEST
    t07_selftest();
#endif

    /* Kapatilabilir karsilama penceresi */
#ifdef ARCTIAN_DEBUG_BOOT
    /* debug build: karsilama + tur atlanir */
#ifdef ARCTIAN_SETUP_TEST
    setup_wizard_show();
#endif
#else
    build_welcome_window();
    if (kv_get_int("setup_done", 0) && !kv_get_int("tour_done", 0)) tour_show();
    if (!kv_get_int("setup_done", 0)) setup_wizard_show();
#endif

    for (;;) {
        if (g_lock_request) {
            g_lock_request = 0;
            lock_hide_desktop(1);
            lock_screen_run(g_asi);
            lock_hide_desktop(0);
            rebuild_menu_ui();
        }
        /* Bosta otomatik kilit: ayar aciksa 5 dk girdisiz kalinca kilit.      */
        {
            static uint64_t s_idle_ms;
            static int s_ipx = -1, s_ipy = -1, s_ibt = -1;
            int mx = g_asi->input->mouse_x();
            int my = g_asi->input->mouse_y();
            int mb = g_asi->input->mouse_buttons();
            uint64_t now = (g_asi->time && g_asi->time->ticks_ms)
                           ? g_asi->time->ticks_ms() : 0;
            if (!s_idle_ms) s_idle_ms = now;
            if (mx != s_ipx || my != s_ipy || mb != s_ibt) {
                s_ipx = mx; s_ipy = my; s_ibt = mb;
                s_idle_ms = now;
            }
            int idle_min = kv_get_int("idle_lock_min", 0);
            if (idle_min <= 0) idle_min = kv_get_int("idle_lock", 0) ? 5 : 0;
            if (idle_min > 0 && now - s_idle_ms > (uint64_t)idle_min * 60000u) {
                s_idle_ms = now;
                g_lock_request = 1;
            }
        }
        /* Uygulamalar keypad grubunu sifirlarken masaustu gezinmesini
         * ayakta tut. */
        if (g_desktop_group) {
            lv_indev_t *kp = lv_port_get_keypad();
            if (kp && !lv_indev_get_group(kp)) lv_indev_set_group(kp, g_desktop_group);
        }
        /* Odak yonetimi: one gelen pencere degistiginde klavye grubunu o
         * pencerenin grubuna gecir. Baslat menusu aciksa dokunma (arama
         * kutusu odakli kalir). */
        {
            winrec_t *topw = winrec_top();
            static winrec_t *s_last_top;
            /* kayitlar geri donusumlu; kapanip yeni pencereye devredilmis
             * bir adres eski top sanilmasin */
            if (s_last_top && !winrec_alive(s_last_top)) s_last_top = 0;
            int sm_open = g_start_menu && !lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
            if (!sm_open && topw != s_last_top) {
                s_last_top = topw;
                lv_indev_t *kp = lv_port_get_keypad();
                if (kp) {
                    lv_group_t *want = (topw && topw->fgroup &&
                                        lv_group_get_obj_count(topw->fgroup) > 0)
                                       ? topw->fgroup : g_desktop_group;
                    if (want && lv_indev_get_group(kp) != want)
                        lv_indev_set_group(kp, want);
                }
            }
        }



        hud_update((g_asi->time && g_asi->time->ticks_ms) ? g_asi->time->ticks_ms() : 0);
        lv_port_tick_advance(16);
        if (g_asi->input && g_asi->input->hotkey_pop) {
            int hk = g_asi->input->hotkey_pop();
            if (hk >= 3 && hk <= 8) {
                /* Kisayol onceligi: kisayol isleyicisi kayitli olan on plan
                 * penceresine ilet (FM: dosya kopyala/yapistir/geri-al).
                 * Pencere yoksa odakli textarea'ya uygula (editor/panel). */
                winrec_t *r = winrec_top();
                if (r && r->hotkey) r->hotkey(hk);
                else clip_action(hk);
            } else if (hk == 1) {
                switcher_show();   /* Alt+Tab: pano acik degilse ac, degilse ilerlet */
            } else if (hk == 2) {
                static uint64_t hot_last;
                uint64_t hn = (g_asi->time && g_asi->time->ticks_ms)
                              ? g_asi->time->ticks_ms() : 0;
                if (hn - hot_last > 1200) {
                    hot_last = hn;
                    open_app("Terminal");
                }
            } else if (hk == 9) {
                help_toggle();  /* Ctrl+Alt+H: "Ne yapabilirim?" */
            } else if (hk == 10) {
                workspace_switch(-1);   /* Ctrl+Alt+, : onceki calisma alani */
            } else if (hk == 11) {
                workspace_switch(1);    /* Ctrl+Alt+. : sonraki calisma alani */
            } else if (hk == 15) {
                workspace_move_top(-1); /* Ctrl+Alt+Shift+, : pencereyi sola tasi */
            } else if (hk == 16) {
                workspace_move_top(1);  /* Ctrl+Alt+Shift+. : pencereyi saga tasi */
            } else if (hk == 12) {
                assistant_toggle();     /* Ctrl+Alt+A: asistan */
            } else if (hk == 13) {
                fps_hud_toggle();       /* Ctrl+Alt+F : kare hizi HUD'u */
            }
        }
        switcher_tick();
        poll_icons();
        clock_tick();
        power_tick();
        tb_poll();
        /* 0.7: masaustu kendi yetkisiyle calisir. Odaktaki uygulamanin
         * yetki maskesi tum donguye artik yayilmaz; kutulu uygulamaya erisim
         * yalnizca kapidan (perm.c) gecer. */
        if (g_asi->sandbox) g_asi->sandbox->leave();
        if (g_asi->net && g_asi->net->service) g_asi->net->service();
        if (g_asi->audio && g_asi->audio->service) g_asi->audio->service();
        if (g_asi->system && g_asi->system->writeback) g_asi->system->writeback();
        if (g_asi->proc && g_asi->proc->reap) g_asi->proc->reap(0);
        {
            static int spool_tick;
            if (++spool_tick >= 120) {
                spool_tick = 0;
                if (g_asi->spool && g_asi->spool->process) g_asi->spool->process();
            }
        }
        lv_timer_handler();
        if (g_asi->sandbox) g_asi->sandbox->leave();
        if (s_res_pending >= 0) {
            int ri = s_res_pending;
            s_res_pending = -1;
            shell_apply_resolution(ri);
            g_shell_dirty = 0;
        }
        if (g_shell_dirty) {
            adl_wolf_theme_init();
            if (g_shell_dirty & 2) {
                g_shell_dirty &= ~2;
                rebuild_menu_ui();
            } else if (g_shell_dirty & 1) {
                g_shell_dirty &= ~1;
                rebuild_desktop_ui();
            }
        }
        /* Kirli bolge sunumu (performans): yalnizca LVGL'in cizdigi alan
         * kopyalanir. Onceden her karede tam ekran blit yapiliyordu ve TCG'de
         * kare suresi saniyelere cikiyordu (fare/tiklama kaybi). */
        {
            int dx, dy, dw, dh;
            if (lv_port_take_dirty(&dx, &dy, &dw, &dh) && dw > 0 && dh > 0) {
                if (g_asi->gfx->present_rect)
                    g_asi->gfx->present_rect(dx, dy, dw, dh);
                else
                    g_asi->gfx->present();
            }
        }
        asm volatile("hlt");
    }
}


