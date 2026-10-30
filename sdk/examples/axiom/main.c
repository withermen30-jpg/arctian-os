/**
 * axiom — Axiom tasarim dilinin Arctian portu (ABI v2 vitrini)
 * =============================================================
 * axiom.css (light tema) birebir cevrildi: OKLCH palet sRGB'ye dusuruldu,
 * CSS durum secicileri WOLF_SEL()'e, `transition` wolf_transition()'a,
 * @keyframes fadeUp/softPulse wolf_anim_in()/wolf_anim_pulse()'a karsilik
 * gelir. Tema dugmesi WOLF_SF_USER1 durumu + koyu palet stilleriyle
 * calisir; degisim gecislerle 250 ms'de akar.
 *
 * Ozet esleme:
 *   :hover / :active        -> WOLF_SF_HOVER / WOLF_SF_PRESSED stilleri
 *   [data-theme="dark"]     -> WOLF_SF_USER1 stilleri + wolf_state_set()
 *   transition: all .2s     -> wolf_transition(WOLF_TP_ALL, 200, ..., IN_OUT)
 *   animation: fadeUp .5s   -> wolf_anim_in(w, 12, 500, delay)  (kademeli)
 *   softPulse               -> wolf_anim_pulse(dot, 1800, 75)
 *   font-size: 1.6rem       -> WOLF_SP_FONT_PX (gomulu Inter, tiny_ttf)
 *   backdrop-blur (nav)     -> opaklige yakin karisim (SW render: blur yok)
 *   btn shine (::after)     -> atlandi (yerine golge/bosluk geri bildirimi)
 */
#include "wolf.h"

/* ---- Palet: axiom.css OKLCH -> sRGB (hesaplanan sabitler) -----------
 * A*L: light tema  A*D: dark tema                                     */
#define AL_BG        0xFDFAF4
#define AL_BG_SUB    0xF5EFE7
#define AL_SURFACE   0xFFFFFF
#define AL_BORDER    0xE5DCD4
#define AL_BORDER_S  0xCEC2B7
#define AL_TEXT      0x221812
#define AL_MUTED     0x675B53
#define AL_SUBTLE    0x8D847D
#define AL_ACCENT    0x924353
#define AL_ACC_SOFT  0xFFE7E8
#define AL_OK        0x3E8343
#define AL_WARN      0xD58D25
#define AL_BAD       0xC53637
#define AL_BTN_BG    0xF8F4F5
#define AL_BTN_FG    0x151011
#define AL_BTN_BD    0xDBD6D7
#define AL_BTN_HOV   0xFDFBFC
#define AD_BG        0x0E0D0D
#define AD_BG_SUB    0x211E1F
#define AD_SURFACE   0x131111
#define AD_BORDER    0x312D2E
#define AD_BORDER_S  0x4C4647
#define AD_TEXT      0xF3F1F2
#define AD_MUTED     0xBAB6B7
#define AD_SUBTLE    0x898586
#define AD_ACCENT    0xE8B5BC
#define AD_ACC_SOFT  0x322527
#define AD_OK        0x85B286
#define AD_WARN      0xD9AF7F
#define AD_BAD       0xD6857F
#define AD_BTN_BG    0x2C2829
#define AD_BTN_FG    0xF3F1F2
#define AD_BTN_BD    0x464142
#define AD_BTN_HOV   0x373133

/* golge rengi = metin rengi (CSS oklch(0.2 .02 50) / siyah) */
#define AL_SHADOW    AL_TEXT
#define AD_SHADOW    0x000000

/* ---- tema kaydi ----------------------------------------------------- */
static wolf_id g_reg[160];
static int     g_reg_n;
static int     g_dark;
static wolf_id g_toggle_lbl;

static wolf_id reg(wolf_id w) {
    if (!w) return w;
    for (int i = 0; i < g_reg_n; i++)
        if (g_reg[i] == w) return w;              /* cift kayit yok */
    if (g_reg_n < (int)(sizeof g_reg / sizeof g_reg[0]))
        g_reg[g_reg_n++] = w;
    return w;
}

/* ayni ozelligi hem light (durum yok) hem dark (USER1) haline yaz;
 * tema gecisi state_set gerektirdiginden widget'i tema listesine al */
static void th(wolf_id w, int prop, int light, int dark) {
    wolf_style(w, prop, light, 0);
    wolf_style(w, prop, dark, WOLF_SEL(WOLF_SF_USER1, 0));
    reg(w);
}
static void hv(wolf_id w, int prop, int v) {
    wolf_style(w, prop, v, WOLF_SEL(WOLF_SF_HOVER, 0));
}
/* hover + gecis + tema kaydi */
static void tw(wolf_id w, unsigned mask, int dur) {
    wolf_transition(w, mask, dur, 0, WOLF_EASE_IN_OUT);
    reg(w);
}
/* fadeUp girisi (CSS `both`: gecikme boyunca gizli); delay<0 = animasyon yok */
static void an(wolf_id w, int delay) {
    if (delay >= 0) wolf_anim_in(w, 12, 500, delay);
}
static void noop(wolf_id w, void *u) { (void)w; (void)u; }

/* ---- bilesen yardimcilari ------------------------------------------- */

/* kart/panel govdesi: surface + border + radius + sm golge + hover */
static wolf_id ax_card(wolf_id p, int x, int y, int w, int h, int delay) {
    wolf_id c = wolf_panel(p, x, y, w, h);
    wolf_style(c, WOLF_SP_RADIUS, 12, 0);
    wolf_style(c, WOLF_SP_CLIP_CORNER, 1, 0);
    th(c, WOLF_SP_BG_COLOR, AL_SURFACE, AD_SURFACE);
    th(c, WOLF_SP_BG_OPA, 100, 100);
    wolf_style(c, WOLF_SP_BORDER_WIDTH, 1, 0);
    th(c, WOLF_SP_BORDER_COLOR, AL_BORDER, AD_BORDER);
    wolf_style(c, WOLF_SP_SHADOW_WIDTH, 6, 0);
    wolf_style(c, WOLF_SP_SHADOW_OFS_Y, 2, 0);
    wolf_style(c, WOLF_SP_SHADOW_COLOR, AL_SHADOW, AD_SHADOW);
    wolf_style(c, WOLF_SP_SHADOW_OPA, 4, 8);
    hv(c, WOLF_SP_BORDER_COLOR, AL_BORDER_S);
    hv(c, WOLF_SP_SHADOW_WIDTH, 12);
    hv(c, WOLF_SP_SHADOW_OPA, 8);
    /* hover/pressed durumlarinin koyu halleri de tanimli olsun */
    wolf_style(c, WOLF_SP_BORDER_COLOR, AD_BORDER_S, WOLF_SEL(WOLF_SF_HOVER | WOLF_SF_USER1, 0));
    tw(c, WOLF_TP_ALL, 250);
    wolf_on_click(c, noop);              /* hover icin tiklanabilir yapar */
    an(c, delay);
    return c;
}

/* etiket: font + es renk; renk verilmezse govde rengi (kalitsal degil,
 * acikça yazilir — LVGL varsayilan temasi koyu OS temasinindan gelir) */
static wolf_id ax_text(wolf_id p, int x, int y, int px, int l, int d,
                       const char *s, int delay) {
    wolf_id t = wolf_label(p, x, y, s);
    wolf_style(t, WOLF_SP_FONT_PX, px, 0);
    if (l >= 0) th(t, WOLF_SP_TEXT_COLOR, l, d);
    else        th(t, WOLF_SP_TEXT_COLOR, AL_TEXT, AD_TEXT);
    an(t, delay);
    return t;
}

/* CSS .btn: panel + etiket kompoziti (alt agac animasyonu tam kontrol) */
static wolf_id ax_btn(wolf_id p, int x, int y, int w, int h, const char *s,
                      int primary, int delay, wolf_event_fn cb, wolf_id *lbl_out) {
    wolf_id b = wolf_panel(p, x, y, w, h);
    wolf_style(b, WOLF_SP_RADIUS, 8, 0);
    wolf_style(b, WOLF_SP_BORDER_WIDTH, 1, 0);
    if (primary) {
        th(b, WOLF_SP_BG_COLOR, AL_BTN_BG, AD_BTN_BG);
        wolf_style(b, WOLF_SP_BG_OPA, 100, 100);
        th(b, WOLF_SP_BORDER_COLOR, AL_BTN_BD, AD_BTN_BD);
        wolf_style(b, WOLF_SP_SHADOW_WIDTH, 4, 0);
        wolf_style(b, WOLF_SP_SHADOW_OFS_Y, 1, 0);
        wolf_style(b, WOLF_SP_SHADOW_COLOR, AL_SHADOW, AD_SHADOW);
        wolf_style(b, WOLF_SP_SHADOW_OPA, 4, 12);
        hv(b, WOLF_SP_BG_COLOR, AL_BTN_HOV);
        wolf_style(b, WOLF_SP_BG_COLOR, AD_BTN_HOV, WOLF_SEL(WOLF_SF_HOVER | WOLF_SF_USER1, 0));
        hv(b, WOLF_SP_SHADOW_WIDTH, 12);
        hv(b, WOLF_SP_SHADOW_OPA, 10);
        th(b, WOLF_SP_TEXT_COLOR, AL_BTN_FG, AD_BTN_FG);
    } else { /* .btn-secondary */
        wolf_style(b, WOLF_SP_BG_OPA, 0, 0);
        th(b, WOLF_SP_BG_COLOR, AL_BG_SUB, AD_BG_SUB);
        th(b, WOLF_SP_BORDER_COLOR, AL_BORDER_S, AD_BORDER_S);
        wolf_style(b, WOLF_SP_BG_OPA, 0, 0);
        hv(b, WOLF_SP_BG_OPA, 100);
        th(b, WOLF_SP_TEXT_COLOR, AL_TEXT, AD_TEXT);
    }
    tw(b, WOLF_TP_ALL, 200);
    int tl = (w - (wolf_strlen(s) * (h > 30 ? 13 : 11) / 2 + 6)) / 2;
    if (tl < 6) tl = 6;
    wolf_id lb = wolf_label(b, tl, (h - (h > 30 ? 16 : 14)) / 2, s);
    wolf_style(lb, WOLF_SP_FONT_PX, h > 30 ? 13 : 11, 0);
    if (primary) th(lb, WOLF_SP_TEXT_COLOR, AL_BTN_FG, AD_BTN_FG);
    else         th(lb, WOLF_SP_TEXT_COLOR, AL_TEXT, AD_TEXT);
    an(b, delay);
    an(lb, delay);
    if (cb) wolf_on_click(b, cb); else wolf_on_click(b, noop);
    if (lbl_out) *lbl_out = lb;
    return b;
}

/* alert: bg_elevated + sol serit */
static void ax_alert(wolf_id p, int x, int y, int w, int stripe_l, int stripe_d,
                     const char *s, int delay) {
    wolf_id a = wolf_panel(p, x, y, w, 26);
    wolf_style(a, WOLF_SP_RADIUS, 8, 0);
    wolf_style(a, WOLF_SP_CLIP_CORNER, 1, 0);
    wolf_style(a, WOLF_SP_BORDER_WIDTH, 1, 0);
    th(a, WOLF_SP_BG_COLOR, AL_SURFACE, AD_SURFACE);
    wolf_style(a, WOLF_SP_BG_OPA, 100, 100);
    th(a, WOLF_SP_BORDER_COLOR, AL_BORDER, AD_BORDER);
    tw(a, WOLF_TP_ALL, 250);
    an(a, delay);
    wolf_id st = wolf_panel(a, 0, 0, 3, 26);      /* stripe (::before) */
    wolf_style(st, WOLF_SP_BORDER_WIDTH, 0, 0);
    th(st, WOLF_SP_BG_COLOR, stripe_l, stripe_d);
    wolf_style(st, WOLF_SP_BG_OPA, 100, 0);
    wolf_style(st, WOLF_SP_RADIUS, 0, 0);
    an(st, delay);
    wolf_id lb = wolf_label(a, 12, 6, s);
    wolf_style(lb, WOLF_SP_FONT_PX, 11, 0);
    th(lb, WOLF_SP_TEXT_COLOR, AL_MUTED, AD_MUTED);
    wolf_transition(lb, WOLF_TP_TEXT_COLOR, 250, 0, WOLF_EASE_IN_OUT);
    reg(lb);
    an(lb, delay);
}

/* ---- olaylar --------------------------------------------------------- */
static void theme_cb(wolf_id w, void *u) {
    (void)w; (void)u;
    g_dark = !g_dark;
    for (int i = 0; i < g_reg_n; i++)
        wolf_state_set(g_reg[i], WOLF_SF_USER1, g_dark);
    wolf_text(g_toggle_lbl, g_dark ? "Tema: Koyu" : "Tema: Acik");
}
static void start_cb(wolf_id w, void *u) {
    (void)w; (void)u;
    wolf_notify("Axiom", "Ayni tasarim dili, yerel hiz.");
}

/* ---- ana ------------------------------------------------------------- */
int wolf_main(const wolf_api_t *api) {
    wolf_bind(api);
    if (!wolf_ready()) return 1;
    wolf_win_title("Axiom UI");
    wolf_id root = wolf_root();

    /* body/ root: CSS body { background, color, transition .2s } */
    th(root, WOLF_SP_BG_COLOR, AL_BG, AD_BG);
    wolf_style(root, WOLF_SP_BG_OPA, 100, 0);
    th(root, WOLF_SP_TEXT_COLOR, AL_TEXT, AD_TEXT);
    wolf_transition(root, WOLF_TP_BG_COLOR | WOLF_TP_TEXT_COLOR, 250, 0,
                    WOLF_EASE_IN_OUT);
    reg(root);

    /* ---- nav (sticky, blur yerine yuksek opak karisim) ---- */
    wolf_id nav = wolf_panel(root, 0, 0, 680, 52);
    th(nav, WOLF_SP_BG_COLOR, AL_BG, AD_BG);
    wolf_style(nav, WOLF_SP_BG_OPA, 96, 96);
    wolf_style(nav, WOLF_SP_BORDER_WIDTH, 1, 0);
    wolf_style(nav, WOLF_SP_BORDER_SIDE, 2, 0);        /* sadece alt */
    th(nav, WOLF_SP_BORDER_COLOR, AL_BORDER, AD_BORDER);
    tw(nav, WOLF_TP_ALL, 250);
    ax_text(nav, 20, 15, 17, -1, -1, "Axiom", -1);    /* .nav-brand */

    /* nav-links: .btn-ghost */
    static const char *links[] = { "Urunler", "Ozellikler", "Blog" };
    static const int lw[] = { 62, 72, 46 };
    int lx = 350;
    for (int i = 0; i < 3; i++) {
        wolf_id g = wolf_panel(nav, lx, 12, lw[i], 28);
        wolf_style(g, WOLF_SP_RADIUS, 6, 0);
        wolf_style(g, WOLF_SP_BORDER_WIDTH, 0, 0);   /* ghost: cercevesiz */
        wolf_style(g, WOLF_SP_BG_OPA, 0, 0);
        th(g, WOLF_SP_BG_COLOR, AL_BG_SUB, AD_BG_SUB);
        hv(g, WOLF_SP_BG_OPA, 100);
        tw(g, WOLF_TP_ALL, 150);
        wolf_id gt = wolf_label(g, 10, 7, links[i]);
        wolf_style(gt, WOLF_SP_FONT_PX, 11, 0);
        th(gt, WOLF_SP_TEXT_COLOR, AL_MUTED, AD_MUTED);
        hv(gt, WOLF_SP_TEXT_COLOR, AL_TEXT);
        wolf_style(gt, WOLF_SP_TEXT_COLOR, AD_TEXT, WOLF_SEL(WOLF_SF_HOVER | WOLF_SF_USER1, 0));
        wolf_transition(gt, WOLF_TP_TEXT_COLOR, 150, 0, WOLF_EASE_IN_OUT);
        reg(gt);
        wolf_on_click(g, noop);
        lx += lw[i] + 6;
    }

    /* tema dugmesi: etiket metni tema ile birlikte degisir */
    wolf_id tg_lbl = 0;
    ax_btn(nav, 578, 12, 84, 28, "Tema: Acik", 0, -1, theme_cb, &tg_lbl);
    g_toggle_lbl = tg_lbl;

    /* ---- hero (.section, fadeUp 60 ms) ---- */
    wolf_id badge = wolf_panel(root, 20, 66, 128, 22);
    wolf_style(badge, WOLF_SP_RADIUS, 999, 0);
    wolf_style(badge, WOLF_SP_BORDER_WIDTH, 0, 0);
    th(badge, WOLF_SP_BG_COLOR, AL_ACC_SOFT, AD_ACC_SOFT);
    wolf_style(badge, WOLF_SP_BG_OPA, 100, 100);
    tw(badge, WOLF_TP_ALL, 250);
    an(badge, 60);
    ax_text(badge, 10, 5, 10, AL_ACCENT, AD_ACCENT, "YENI - ABI v2", 60);

    wolf_id h1 = ax_text(root, 20, 96, 26, -1, -1, "Sicak, sakin arayuz", 60);
    wolf_style(h1, WOLF_SP_TEXT_LETTER, -1, 0);        /* -0.02em */

    wolf_id p1 = ax_text(root, 20, 140, 12, AL_MUTED, AD_MUTED,
        "Ayni tasarim dili artik isletim sisteminde: CSS benzeri stil motoru,", 60);
    wolf_text(p1, "Ayni tasarim dili artik isletim sisteminde: CSS benzeri stil motoru,\n"
                  "durum gecisleri, kademeli giris animasyonlari ve iki tema - tek ABI'de.");
    wolf_style(p1, WOLF_SP_TEXT_LINE, 4, 0);
    wolf_transition(p1, WOLF_TP_TEXT_COLOR, 250, 0, WOLF_EASE_IN_OUT);
    reg(p1);

    ax_btn(root, 20, 186, 118, 34, "Hemen basla", 1, 60, start_cb, 0);
    ax_btn(root, 148, 186, 96, 34, "Belgeler", 0, 60, 0, 0);

    /* softPulse canli noktasi */
    wolf_id dot = wolf_panel(root, 646, 70, 10, 10);
    wolf_style(dot, WOLF_SP_RADIUS, 999, 0);
    wolf_style(dot, WOLF_SP_BORDER_WIDTH, 0, 0);
    th(dot, WOLF_SP_BG_COLOR, AL_ACCENT, AD_ACCENT);
    wolf_style(dot, WOLF_SP_BG_OPA, 100, 0);
    reg(dot);
    wolf_anim_pulse(dot, 1800, 75);
    ax_text(root, 622, 86, 10, AL_SUBTLE, AD_SUBTLE, "canli", -1);

    /* ---- kartlar (.grid-3, kademeli 140/220/300) ---- */
    ax_text(root, 20, 240, 10, AL_SUBTLE, AD_SUBTLE, "BILESENLER", 120);
    static const char *ct[] = { "Stil motoru", "Gecisler", "Animasyon" };
    static const char *cb[] = {
        "Renk, opaklik, radius, golge, ic kenar ve durum bazli seciciler: "
        "hover, pressed, checked - hepsi tek cagri.",
        "Tema degisimi 250 ms'de akar; hover'da golgeler derinlesir, "
        "kenarliklar belirginlesir. cubic-bezier(.42,0,.58,1).",
        "fadeUp kademeli giris, softPulse canli nokta, gecikmeli baslama - "
        "hepsi sanal saatte deterministik.",
    };
    static const char *cf[] = { "wolf_style()", "wolf_transition()", "wolf_anim_in()" };
    for (int i = 0; i < 3; i++) {
        int cx = 20 + i * 220, delay = 140 + i * 80;
        wolf_id c = ax_card(root, cx, 258, 200, 110, delay);
        ax_text(c, 12, 10, 14, -1, -1, ct[i], delay);
        wolf_id body = ax_text(c, 12, 32, 11, AL_MUTED, AD_MUTED, cb[i], delay);
        wolf_resize(body, 176, 48);
        wolf_style(body, WOLF_SP_TEXT_LINE, 3, 0);
        wolf_transition(body, WOLF_TP_TEXT_COLOR, 250, 0, WOLF_EASE_IN_OUT);
        reg(body);
        ax_text(c, 12, 88, 10, AL_SUBTLE, AD_SUBTLE, cf[i], delay);
        if (i == 1) {   /* .card-accent: sol serit */
            wolf_id st = wolf_panel(c, 0, 0, 4, 110);
            wolf_style(st, WOLF_SP_BORDER_WIDTH, 0, 0);
            wolf_style(st, WOLF_SP_RADIUS, 0, 0);
            th(st, WOLF_SP_BG_COLOR, AL_ACCENT, AD_ACCENT);
            wolf_style(st, WOLF_SP_BG_OPA, 100, 0);
            an(st, delay);
        }
    }

    /* ---- alt: uyarilar + form (fadeUp 380/420) ---- */
    ax_text(root, 20, 368, 10, AL_SUBTLE, AD_SUBTLE, "DURUM BILDIRIMLERI", 380);
    ax_alert(root, 20, 386, 300, AL_OK,   AD_OK,   "Kayit tamamlandi", 380);
    ax_alert(root, 20, 416, 300, AL_WARN, AD_WARN, "Depolama %90 dolu", 410);
    ax_alert(root, 20, 446, 300, AL_BAD,  AD_BAD,  "Baglanti koptu", 440);

    wolf_id fl = ax_text(root, 360, 368, 11, AL_MUTED, AD_MUTED,
                         "E-posta adresin", 420);
    wolf_transition(fl, WOLF_TP_TEXT_COLOR, 250, 0, WOLF_EASE_IN_OUT);
    reg(fl);

    wolf_id in = wolf_input(root, 360, 386, 300, 30, "ad@ornek.com");
    wolf_style(in, WOLF_SP_RADIUS, 8, 0);
    wolf_style(in, WOLF_SP_FONT_PX, 12, 0);
    th(in, WOLF_SP_BG_COLOR, AL_SURFACE, AD_SURFACE);
    wolf_style(in, WOLF_SP_BG_OPA, 100, 0);
    wolf_style(in, WOLF_SP_BORDER_WIDTH, 1, 0);
    th(in, WOLF_SP_BORDER_COLOR, AL_BORDER, AD_BORDER);
    th(in, WOLF_SP_TEXT_COLOR, AL_TEXT, AD_TEXT);
    /* :focus { border-color muted; box-shadow halka } */
    {
        int f = WOLF_SF_FOCUSED;
        wolf_style(in, WOLF_SP_BORDER_COLOR, AL_MUTED, WOLF_SEL(f, 0));
        wolf_style(in, WOLF_SP_BORDER_COLOR, AD_MUTED, WOLF_SEL(f | WOLF_SF_USER1, 0));
        wolf_style(in, WOLF_SP_OUTLINE_WIDTH, 3, WOLF_SEL(f, 0));
        wolf_style(in, WOLF_SP_OUTLINE_COLOR, AL_ACC_SOFT, WOLF_SEL(f, 0));
        wolf_style(in, WOLF_SP_OUTLINE_COLOR, AD_ACC_SOFT, WOLF_SEL(f | WOLF_SF_USER1, 0));
        wolf_style(in, WOLF_SP_OUTLINE_OPA, 60, WOLF_SEL(f, 0));
        wolf_style(in, WOLF_SP_OUTLINE_PAD, 1, WOLF_SEL(f, 0));
    }
    wolf_transition(in, WOLF_TP_OUTLINE | WOLF_TP_BORDER_COLOR | WOLF_TP_BG_COLOR,
                    150, 0, WOLF_EASE_IN_OUT);
    reg(in);
    an(in, 420);

    ax_btn(root, 360, 430, 88, 30, "Kaydet", 1, 420, start_cb, 0);
    wolf_id mono = wolf_panel(root, 458, 434, 150, 22);
    wolf_style(mono, WOLF_SP_RADIUS, 6, 0);
    th(mono, WOLF_SP_BG_COLOR, AL_BG_SUB, AD_BG_SUB);
    wolf_style(mono, WOLF_SP_BG_OPA, 100, 0);
    wolf_style(mono, WOLF_SP_BORDER_WIDTH, 1, 0);
    th(mono, WOLF_SP_BORDER_COLOR, AL_BORDER, AD_BORDER);
    tw(mono, WOLF_TP_ALL, 250);
    an(mono, 420);
    ax_text(mono, 8, 5, 10, AL_MUTED, AD_MUTED, "wolf.h = ABI v2", 420);

    wolf_log("axiom: Axiom CSS portu (ABI v2: stil+gecis+animasyon+tema)");
    return 0;
}
