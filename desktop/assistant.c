#include "dsk.h"
#include "version.h"

/* ==================================================================== */
/* Arctian Asistani                                                     */
/*                                                                      */
/* Yerel, cevrimdisi bir yardimci. Buyuk dil modeli cagrisi yapmaz;      */
/* sistem API'lerini (uygulama acma, saat, pil, ag, dosya) dogal dil     */
/* kaliplariyla kullanir. Amac: kullanici "terminal ac" gibi bir cumle   */
/* yazinca gercekten terminalin acilmasi. Yanit uretemezse komut yardimi */
/* onerir. Gecmis C:/etc/asistan.log dosyasina yazilir.                  */
/* ==================================================================== */

#define AS_MAX   48          /* sohbet satiri */
#define AS_TXT   192
#define AS_LOG   "C:/etc/asistan.log"

static lv_obj_t *s_panel;    /* sag kenar paneli */
static lv_obj_t *s_list;     /* mesaj listesi */
static lv_obj_t *s_input;    /* giris alani */
static lv_obj_t *s_hint;
static lv_group_t *s_group;
static lv_group_t *s_prev_group;   /* panel acilmadan onceki klavye grubu */
static int        s_n;
static int        s_gen;     /* panelin kuruldugu kabuk nesli */

/* ---- kucuk yardimcilar ---- */
static int as_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static int as_has_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !needle[0]) return 0;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && hay[i + j]) {
            char x = hay[i + j], y = needle[j];
            if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
            if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
            if (x != y) break;
            j++;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static void as_copy(char *d, int cap, const char *s) {
    int i = 0;
    if (!s) { if (cap > 0) d[0] = 0; return; }
    for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}

/* Kisa ad eslesmesi: "dosya" -> "Dosya Yoneticisi". Sorgu, hedefin bir
 * kelimesinin oneki ise (en az 3 harf) eslesir.
 * Not: hedefteki Turkce karakterler (UTF-8 iki bayt) tek bayt gibi
 * karsilastirilir; kullanici ASCII yazsa da eslesir. */
static int as_word_prefix(const char *target, const char *q) {
    if (!target || !q) return 0;
    char word[32];
    int n = 0;
    while (*q == ' ') q++;
    for (int i = 0; q[i] && q[i] != ' ' && n < (int)sizeof(word) - 1; i++) {
        char c = q[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        word[n++] = c;
    }
    word[n] = 0;
    if (n < 3) return 0;

    int i = 0;
    while (target[i]) {
        while (target[i] == ' ') i++;
        char tw[40];
        int k = 0;
        while (target[i] && target[i] != ' ' && k < (int)sizeof(tw) - 1) {
            char c = target[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            tw[k++] = c;
            i++;
        }
        tw[k] = 0;
        if (k >= n) {
            int m = 0;
            while (m < n && tw[m] == word[m]) m++;
            if (m == n) return 1;
        }
    }
    return 0;
}

static void as_num(char *out, int cap, unsigned v) {
    char t[12];
    int n = 0, k = 0;
    if (!v) t[n++] = '0';
    while (v && n < (int)sizeof(t)) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n && k < cap - 1) out[k++] = t[--n];
    out[k] = 0;
}

/* ---- gunluk ---- */
static void as_log_line(const char *who, const char *txt) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in || !g_asi->fs->write) return;
    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/etc");
    void *h = g_asi->fs->create_in("C:/etc", "asistan.log", 0);
    if (!h) return;
    g_asi->fs->write(h, who, (uint32_t)slen2(who));
    g_asi->fs->write(h, ": ", 2);
    g_asi->fs->write(h, txt, (uint32_t)slen2(txt));
    g_asi->fs->write(h, "\n", 1);
}

/* ---- sohbet balonu ---- */
static void as_bubble(const char *txt, int mine) {
    if (!s_list) return;
    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, mine ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *b = lv_obj_create(row);
    lv_obj_remove_style_all(b);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(b, LV_PCT(84));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(mine ? ADL_ACCENT : ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(b, 10, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(ADL_BORDER), 0);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_color(l, lv_color_hex(mine ? 0xFFFFFF : ADL_TEXT), 0);
    lv_obj_set_style_text_font(l, adl_font(), 0);

    s_n++;
    lv_obj_update_layout(s_list);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_scroll_to_y(s_list, lv_obj_get_scroll_bottom(s_list) + 100000, LV_ANIM_OFF);
}

/* ---- niyet cozumleme ---- */
/* Asistan 0.7'de hem kenar paneli hem ayri uygulama penceresi olarak
 * kullanilir. Yanit hedefi degistirilebilir: varsayilan kenar paneli,
 * pencere acikken pencere kendi balonunu cizer (assistant_set_sink). */
static void as_status(void);
static void as_handle(const char *q);
static void (*s_sink)(const char *txt) = 0;

void assistant_set_sink(void (*fn)(const char *txt)) { s_sink = fn; }

static void as_reply(const char *txt) {
    if (s_sink) s_sink(txt);
    else        as_bubble(txt, 0);
    as_log_line("asistan", txt);
}

/* Uygulama penceresinden cagrilan giris noktasi. */
void assistant_ask(const char *q) {
    if (!q || !q[0]) return;
    as_log_line("siz", q);
    as_handle(q);
}

/* Uygulama penceresi acilirken kisa durum ozeti. */
void assistant_status_report(void) { as_status(); }

/* Ekran okuyucu benzeri kisa durum ozeti. */
static void as_status(void) {
    char b[AS_TXT];
    as_copy(b, sizeof(b), "Sistem: Arctian " ARCTIAN_VERSION);

    /* saat */
    asi_datetime_t dt;
    if (g_asi && g_asi->time && g_asi->time->now && g_asi->time->now(&dt) == 0) {
        char t[40];
        int n = 0;
        t[n++] = (char)('0' + (dt.hour / 10) % 10);
        t[n++] = (char)('0' + dt.hour % 10);
        t[n++] = ':';
        t[n++] = (char)('0' + (dt.minute / 10) % 10);
        t[n++] = (char)('0' + dt.minute % 10);
        t[n] = 0;
        as_copy(b + slen2(b), sizeof(b) - slen2(b), "  |  Saat ");
        as_copy(b + slen2(b), sizeof(b) - slen2(b), t);
    }

    /* pil */
    if (g_asi && g_asi->power && g_asi->power->percent) {
        int p = g_asi->power->percent();
        if (p >= 0) {
            char num[8];
            as_num(num, sizeof(num), (unsigned)p);
            as_copy(b + slen2(b), sizeof(b) - slen2(b), "  |  Pil %");
            as_copy(b + slen2(b), sizeof(b) - slen2(b), num);
        }
    }

    /* pencere sayisi */
    {
        char num[8];
        as_num(num, sizeof(num), (unsigned)arctian_shell_window_count());
        as_copy(b + slen2(b), sizeof(b) - slen2(b), "  |  Acik pencere ");
        as_copy(b + slen2(b), sizeof(b) - slen2(b), num);
    }

    as_reply(b);
}

/* Uygulama adi eslemesi: "terminal", "dosya yoneticisi" vb.
 * Hem sorgu basligi icerebilir ("terminal ac") hem de sorgu basligin oneki
 * olabilir ("dosya" -> "Dosya Yoneticisi"). Tek yonlu eslesme kisa adlarla
 * calismiyordu (0.6 duzeltmesi). */
static int as_try_launch(const char *q) {
    int n = arctian_shell_app_count();
    for (int i = 0; i < n; i++) {
        char title[48], name[48];
        title[0] = name[0] = 0;
        arctian_shell_app_info(i, title, sizeof(title), name, sizeof(name), 0, 0);
        int hit = 0;
        if (title[0] && as_has_ci(q, title)) hit = 1;              /* tam baslik */
        if (!hit && name[0] && as_has_ci(q, name)) hit = 1;        /* teknik ad  */
        if (!hit && as_word_prefix(title, q)) hit = 1;             /* kisa ad    */
        if (!hit && as_word_prefix(name, q)) hit = 1;
        if (hit) {
            char b[AS_TXT];
            as_copy(b, sizeof(b), "\"");
            as_copy(b + slen2(b), sizeof(b) - slen2(b), title);
            as_copy(b + slen2(b), sizeof(b) - slen2(b), "\" aciliyor.");
            as_reply(b);
            /* Dogrudan kayit indeksiyle ac: arctian_shell_launch tam eslesme
             * aradigi icin "terminal ac" gibi cumleleri bulamiyordu. */
            open_registry_app(i);
            /* 0.6: komut yerine getirildi; paneli sessizce kapat ki klavye
             * odagi yeni acilan pencereye gecebilsin (kabuk ust pencere
             * degisince grubu kendisi devralir). */
            if (s_panel) lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
            return 1;
        }
    }
    return 0;
}

/* Bir cumleyi yorumlar. */
static void as_handle(const char *q) {
    if (!q || !q[0]) return;

    if (as_has_ci(q, "merhaba") || as_has_ci(q, "selam")) {
        as_reply("Merhaba. Uygulama acabilir, saat/pil/ag durumunu soyleyebilir, "
                 "not tutabilirim. Ornek: \"not al: yarin sunum\".");
        return;
    }
    if (as_has_ci(q, "saat") && (as_has_ci(q, "kac") || as_has_ci(q, "nedir"))) {
        asi_datetime_t dt;
        if (g_asi && g_asi->time && g_asi->time->now && g_asi->time->now(&dt) == 0) {
            char b[64];
            char num[8];
            as_num(num, sizeof(num), (unsigned)dt.hour);
            as_copy(b, sizeof(b), "Saat ");
            as_copy(b + slen2(b), sizeof(b) - slen2(b), num);
            as_copy(b + slen2(b), sizeof(b) - slen2(b), ":");
            as_num(num, sizeof(num), (unsigned)dt.minute);
            if (dt.minute < 10) as_copy(b + slen2(b), sizeof(b) - slen2(b), "0");
            as_copy(b + slen2(b), sizeof(b) - slen2(b), num);
            as_reply(b);
        } else as_reply("Saat servisine ulasamiyorum.");
        return;
    }
    if (as_has_ci(q, "pil") || as_has_ci(q, "sarj") || as_has_ci(q, "batarya")) {
        int p = (g_asi && g_asi->power && g_asi->power->percent) ? g_asi->power->percent() : -1;
        if (p < 0) { as_reply("Pil bilgisi yok (masaustu / AC gucu)."); return; }
        char b[64], num[8];
        as_num(num, sizeof(num), (unsigned)p);
        as_copy(b, sizeof(b), "Pil seviyesi %");
        as_copy(b + slen2(b), sizeof(b) - slen2(b), num);
        if (g_asi->power->charging && g_asi->power->charging())
            as_copy(b + slen2(b), sizeof(b) - slen2(b), " (sarj oluyor)");
        as_reply(b);
        return;
    }
    if (as_has_ci(q, "ag") || as_has_ci(q, "internet") || as_has_ci(q, "wifi")) {
        int lt = (g_asi && g_asi->net && g_asi->net->link_type) ? g_asi->net->link_type() : 0;
        if (lt == 0) { as_reply("Ag baglantisi yok."); return; }
        char b[AS_TXT];
        as_copy(b, sizeof(b), lt == 2 ? "Wi-Fi bagli" : "Ethernet bagli");
        if (g_asi->net->ip) {
            uint8_t a[4];
            g_asi->net->ip(a);
            char ip[24];
            int n = 0;
            for (int i = 0; i < 4; i++) {
                char num[5];
                as_num(num, sizeof(num), a[i]);
                for (int k = 0; num[k] && n < (int)sizeof(ip) - 2; k++) ip[n++] = num[k];
                if (i < 3) ip[n++] = '.';
            }
            ip[n] = 0;
            as_copy(b + slen2(b), sizeof(b) - slen2(b), "  IP ");
            as_copy(b + slen2(b), sizeof(b) - slen2(b), ip);
        }
        as_reply(b);
        return;
    }
    if (as_has_ci(q, "durum") || as_has_ci(q, "sistem")) { as_status(); return; }

    if (as_has_ci(q, "not al")) {
        const char *c = q;
        for (int i = 0; q[i]; i++) if (q[i] == ':') { c = q + i + 1; break; }
        while (*c == ' ') c++;
        if (*c) {
            if (g_asi && g_asi->fs && g_asi->fs->mkdir) g_asi->fs->mkdir("C:/belgeler");
            void *h = (g_asi && g_asi->fs && g_asi->fs->create_in)
                      ? g_asi->fs->create_in("C:/belgeler", "notlar.txt", 0) : 0;
            if (h && g_asi->fs->write) {
                g_asi->fs->write(h, c, (uint32_t)slen2(c));
                g_asi->fs->write(h, "\n", 1);
                as_reply("Not kaydedildi: C:/belgeler/notlar.txt");
                return;
            }
            as_reply("Not kaydedilemedi (dosya sistemi hatasi).");
            return;
        }
    }
    if (as_has_ci(q, "yardim") || as_has_ci(q, "ne yapabilir")) {
        as_reply("Yapabildiklerim: uygulama acmak (\"terminal ac\"), durum "
                 "(\"sistem durumu\"), saat/pil/ag sorgusu, not almak, "
                 "kilitlemek (\"ekrani kilitle\").");
        return;
    }
    if (as_has_ci(q, "kilitle")) {
        as_reply("Ekran kilitleniyor.");
        g_lock_request = 1;
        return;
    }

    /* komut niyeti: "... ac" / "ac ..." / "baslat" */
    if (as_has_ci(q, "ac ") || as_has_ci(q, " ac") || as_has_ci(q, "baslat") ||
        as_has_ci(q, "ba\xc5\x9flat")) {
        as_try_launch(q);
        return;
    }

    /* Niyet belirtilmemis olabilir: dogrudan uygulama adi da kabul edilir
     * ("dosya", "ayarlar"). Once dene, bulunamazsa yardim oner. */
    if (as_try_launch(q)) return;

    as_reply("Bunu tam anlayamadim. Uygulama adi soyleyin (\"terminal\"), "
             "ya da \"yardim\" yazin.");
}

/* ---- olaylar ---- */
static void as_send(void) {
    if (!s_input) return;
    const char *t = lv_textarea_get_text(s_input);
    if (!t || !t[0]) return;
    char q[AS_TXT];
    as_copy(q, sizeof(q), t);
    lv_textarea_set_text(s_input, "");
    as_bubble(q, 1);
    as_log_line("siz", q);
    as_handle(q);
}

static void as_send_cb(lv_event_t *e) { (void)e; as_send(); }

static void as_chip_cb(lv_event_t *e) {
    const char *q = (const char *)lv_event_get_user_data(e);
    if (!q) return;
    as_bubble(q, 1);
    as_log_line("siz", q);
    as_handle(q);
}

static void as_toggle_cb(lv_event_t *e) {
    (void)e;
    if (!s_panel) return;
    if (lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_panel);
        lv_indev_t *kp = lv_port_get_keypad();
        if (kp) {
            s_prev_group = lv_indev_get_group(kp);   /* geri donus icin sakla */
            if (s_group && s_input) {
                lv_indev_set_group(kp, s_group);
                lv_group_focus_obj(s_input);
            }
        }
    } else {
        /* Panel gizlenince klavye grubunu BIRAK: aksi halde tuslar gorunmez
         * panelde kaliyor ve aktif pencereye hicbir sey yazilamiyordu. */
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_indev_t *kp = lv_port_get_keypad();
        if (kp) {
            lv_group_t *back = s_prev_group;
            if (!back || lv_group_get_obj_count(back) == 0) {
                winrec_t *top = winrec_top();
                back = (top && top->fgroup && lv_group_get_obj_count(top->fgroup) > 0)
                       ? top->fgroup : g_desktop_group;
            }
            lv_indev_set_group(kp, back);
        }
    }
}

static lv_obj_t *as_chip(lv_obj_t *parent, const char *txt) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(c, LV_SIZE_CONTENT, 30);
    lv_obj_set_style_radius(c, 15, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_pad_hor(c, 12, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, as_chip_cb, LV_EVENT_CLICKED, (void *)txt);
    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_text_font(l, adl_font(), 0);
    lv_obj_center(l);
    return c;
}

/* Panel: sag kenara yasli, baslat menusunun sagindan acilir. */
void assistant_init(void) {
    /* Kabuk yeniden kurulduysa (cozunurluk/tema/olcek) eski nesneler
     * silinmistir; generation sayaci ile yeni panel kurulur. */
    if (s_panel && s_gen == g_shell_gen) return;
    s_panel = 0; s_list = 0; s_input = 0; s_n = 0;
    s_gen = g_shell_gen;
    int W = g_asi->gfx->width();
    int H = g_asi->gfx->height();

    s_panel = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, ui_s(420), ui_s(560));
    lv_obj_align(s_panel, LV_ALIGN_BOTTOM_RIGHT, -ui_s(24), -ui_s(160));
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_panel, ui_s(18), 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_shadow_width(s_panel, ui_s(30), 0);
    lv_obj_set_style_shadow_color(s_panel, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(s_panel, 140, 0);
    lv_obj_set_style_pad_all(s_panel, ui_s(16), 0);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *hdr = lv_label_create(s_panel);
    lv_label_set_text(hdr, "Arctian Asistan");
    lv_obj_set_style_text_color(hdr, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_text_font(hdr, adl_font(), 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *sub = lv_label_create(s_panel);
    lv_label_set_text(sub, "Yerel calisir; internete veri gondermez.");
    lv_obj_set_style_text_color(sub, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_set_style_text_font(sub, adl_font(), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 0, ui_s(26));

    s_list = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, LV_PCT(100), ui_s(360));
    lv_obj_align(s_list, LV_ALIGN_TOP_LEFT, 0, ui_s(56));
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, ui_s(8), 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *chips = lv_obj_create(s_panel);
    lv_obj_remove_style_all(chips);
    lv_obj_set_size(chips, LV_PCT(100), ui_s(34));
    lv_obj_align(chips, LV_ALIGN_BOTTOM_LEFT, 0, -ui_s(58));
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chips, ui_s(6), 0);
    as_chip(chips, "Sistem durumu");
    as_chip(chips, "Terminal");
    as_chip(chips, "Yardim");

    s_input = lv_textarea_create(s_panel);
    lv_obj_remove_style_all(s_input);
    lv_textarea_set_one_line(s_input, true);
    lv_textarea_set_max_length(s_input, 120);
    lv_textarea_set_placeholder_text(s_input, "Bir sey yazin: \"terminal ac\"");
    lv_obj_set_size(s_input, LV_PCT(100), ui_s(44));
    lv_obj_align(s_input, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_input, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(s_input, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_input, ui_s(12), 0);
    lv_obj_set_style_pad_all(s_input, ui_s(10), 0);
    lv_obj_set_style_text_color(s_input, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_text_font(s_input, adl_font(), 0);
    lv_obj_add_event_cb(s_input, as_send_cb, LV_EVENT_READY, 0);

    if (!s_group) s_group = lv_group_create();
    lv_group_remove_all_objs(s_group);
    lv_group_add_obj(s_group, s_input);

    as_reply("Merhaba! Bir uygulama acmami isterseniz adini yazin "
             "(ornek: \"terminal ac\"), durum sorabilir ya da not alabilirsiniz.");

    (void)W; (void)H;
    (void)s_hint;
}

/* Gorev cubugundaki asistan dugmesi buradan acar. */
void assistant_toggle(void) { as_toggle_cb(0); }
