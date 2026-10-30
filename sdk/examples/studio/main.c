/* Cizim Stüdyosu — ABI v2 tanitim ornegi.
 *
 * Gosterdigi v2 yetenekleri:
 *   - wolf_style: gradyan, golge, radius, kenarlik, odak/vurgu durumlar
 *   - wolf_line : nokta nokta cizgi grafik (lv_line)
 *   - wolf_arc  : kadran (yay) + indicator gradyani
 *   - wolf_canvas: piksel piksel cizim (gradyan, daire, kalin cizgi, getpx)
 *   - wolf_timer: her 100 ms'de kadreani canlandiran zamanlayici
 *   - to_front  : yigin sirasi
 *
 * Derle : wolf build .   (Windows)  /  sh build.sh  (Linux/WSL)
 * Calistir: wolf run studio.wlf
 */
#include "wolf.h"

static wolf_id g_gauge;
static wolf_id g_canvas;
static int     g_phase;

/* --- zamanlayici: kadreani ve canvas noktasini canlandirir ---------- */
static void tick_cb(wolf_id t, void *user) {
    (void)t; (void)user;
    /* ucgen dalga 0..100 (tamsayi, deterministik) */
    int ph = g_phase++ % 160;
    int tri = (ph < 80) ? (ph * 100 / 80) : ((160 - ph) * 100 / 80);
    wolf_value(g_gauge, tri);

    /* canvas sag altinda vuran nokta */
    int x = 236 - (tri * 12 / 100);
    wolf_canvas_rect(g_canvas, x, 104, 6, 6, 0xFFFFFFFFu, 2);
}

int wolf_main(const wolf_api_t *api) {
    wolf_bind(api);
    if (!wolf_ready()) return 1;
    wolf_win_title("Cizim Stüdyosu");

    wolf_id root = wolf_root();
    if (!root) return 1;

    wolf_id head = wolf_label(root, 16, 10, "libWolf ABI v2");
    wolf_style(head, WOLF_SP_TEXT_COLOR, 0xE2E8F0, 0);
    if (wolf_api_v2()) wolf_text(head, "libWolf ABI v2 - hazir");

    /* --- hero: dikey gradyan + golge + radius ----------------------- */
    wolf_id hero = wolf_panel(root, 12, 34, 596, 78);
    wolf_style(hero, WOLF_SP_RADIUS, 18, 0);
    wolf_style(hero, WOLF_SP_BG_COLOR, 0x2563EB, 0);
    wolf_style(hero, WOLF_SP_BG_GRAD_COLOR, 0x7C3AED, 0);
    wolf_style(hero, WOLF_SP_BG_GRAD_DIR, 1, 0);       /* dikey */
    wolf_style(hero, WOLF_SP_BG_OPA, 100, 0);
    wolf_style(hero, WOLF_SP_SHADOW_WIDTH, 20, 0);
    wolf_style(hero, WOLF_SP_SHADOW_OPA, 45, 0);
    wolf_style(hero, WOLF_SP_SHADOW_COLOR, 0x000000, 0);
    wolf_style(hero, WOLF_SP_SHADOW_OFS_Y, 6, 0);
    wolf_style(hero, WOLF_SP_BORDER_WIDTH, 1, 0);
    wolf_style(hero, WOLF_SP_BORDER_COLOR, 0x60A5FA, 0);
    wolf_style(hero, WOLF_SP_PAD_LEFT, 16, 0);
    wolf_style(hero, WOLF_SP_PAD_TOP, 10, 0);

    wolf_id h1 = wolf_label(hero, 16, 12, "CSS gibi stil, tuval gibi cizim");
    wolf_style(h1, WOLF_SP_TEXT_COLOR, 0xFFFFFF, 0);
    wolf_style(h1, WOLF_SP_TEXT_LETTER, 1, 0);
    wolf_id h2 = wolf_label(hero, 16, 40,
                            "gradyan - golge - durum stili - yay - grafik - piksel");
    wolf_style(h2, WOLF_SP_TEXT_COLOR, 0xC7D2FE, 0);
    wolf_to_front(hero);

    /* --- durum stilli dugmeler (hover / aktif) ---------------------- */
    wolf_id b1 = wolf_button(root, 12, 124, 132, 40, "Vurgulu");
    wolf_style(b1, WOLF_SP_BG_COLOR, 0x16A34A, 0);
    wolf_style(b1, WOLF_SP_BG_COLOR, 0x4ADE80, WOLF_SEL(WOLF_SF_HOVER, WOLF_PF_MAIN));
    wolf_style(b1, WOLF_SP_BG_COLOR, 0x166534, WOLF_SEL(WOLF_SF_PRESSED, WOLF_PF_MAIN));
    wolf_style(b1, WOLF_SP_RADIUS, 20, 0);
    wolf_style(b1, WOLF_SP_OUTLINE_COLOR, 0xBBF7D0, WOLF_SEL(WOLF_SF_FOCUSED, WOLF_PF_MAIN));
    wolf_style(b1, WOLF_SP_OUTLINE_WIDTH, 3, WOLF_SEL(WOLF_SF_FOCUSED, WOLF_PF_MAIN));

    wolf_id b2 = wolf_button(root, 152, 124, 132, 40, "Kapsul");
    wolf_style(b2, WOLF_SP_BG_COLOR, 0xF59E0B, 0);
    wolf_style(b2, WOLF_SP_BG_GRAD_COLOR, 0xEF4444, 0);
    wolf_style(b2, WOLF_SP_BG_GRAD_DIR, 2, 0);         /* yatay */
    wolf_style(b2, WOLF_SP_BG_GRAD_OPA, 100, 0);
    wolf_style(b2, WOLF_SP_RADIUS, 999, 0);            /* tam kapsul */
    wolf_style(b2, WOLF_SP_BORDER_WIDTH, 2, 0);
    wolf_style(b2, WOLF_SP_BORDER_COLOR, 0xFEF3C7, 0);
    wolf_style(b2, WOLF_SP_SHADOW_WIDTH, 10, WOLF_SEL(WOLF_SF_PRESSED, WOLF_PF_MAIN));
    wolf_style(b2, WOLF_SP_SHADOW_COLOR, 0x000000, WOLF_SEL(WOLF_SF_PRESSED, WOLF_PF_MAIN));

    wolf_id chk = wolf_checkbox(root, 296, 130, "Secili renk", 1);
    wolf_style(chk, WOLF_SP_TEXT_COLOR, 0x94A3B8, 0);
    wolf_style(chk, WOLF_SP_TEXT_COLOR, 0xFBBF24, WOLF_SEL(WOLF_SF_CHECKED, WOLF_PF_MAIN));
    wolf_style(chk, WOLF_SP_BG_COLOR, 0x1D4ED8, WOLF_SEL(WOLF_SF_CHECKED, WOLF_PF_INDICATOR));
    wolf_style(chk, WOLF_SP_BG_OPA, 100, WOLF_SEL(WOLF_SF_CHECKED, WOLF_PF_INDICATOR));

    /* --- donmus + bolunmus kart (transform) -------------------------- */
    wolf_id card = wolf_panel(root, 452, 122, 64, 64);
    wolf_style(card, WOLF_SP_BG_COLOR, 0x0EA5E9, 0);
    wolf_style(card, WOLF_SP_BG_OPA, 100, 0);
    wolf_style(card, WOLF_SP_RADIUS, 14, 0);
    wolf_style(card, WOLF_SP_ROTATE, 150, 0);          /* 15 derece */
    wolf_style(card, WOLF_SP_SCALE_X, 110, 0);         /* %110 */
    wolf_style(card, WOLF_SP_SHADOW_WIDTH, 12, 0);
    wolf_style(card, WOLF_SP_SHADOW_OPA, 50, 0);
    wolf_style(card, WOLF_SP_SHADOW_COLOR, 0x000000, 0);

    /* --- kadran (yay) ------------------------------------------------- */
    g_gauge = wolf_arc(root, 530, 112, 40);
    wolf_arc_set(g_gauge, 120, 420, 0);                /* altta bosluk */
    wolf_style(g_gauge, WOLF_SP_BG_OPA, 0, WOLF_SEL(0, WOLF_PF_MAIN));
    wolf_style(g_gauge, WOLF_SP_ARC_WIDTH, 8, WOLF_SEL(0, WOLF_PF_MAIN));
    wolf_style(g_gauge, WOLF_SP_ARC_COLOR, 0x1E293B, WOLF_SEL(0, WOLF_PF_MAIN));
    wolf_style(g_gauge, WOLF_SP_ARC_WIDTH, 8, WOLF_SEL(0, WOLF_PF_INDICATOR));
    wolf_style(g_gauge, WOLF_SP_ARC_COLOR, 0xEF4444, WOLF_SEL(0, WOLF_PF_INDICATOR));
    wolf_style(g_gauge, WOLF_SP_ARC_ROUNDED, 1, WOLF_SEL(0, WOLF_PF_INDICATOR));
    wolf_value(g_gauge, 50);
    wolf_id gl = wolf_label(root, 554, 200, "canli");
    wolf_style(gl, WOLF_SP_TEXT_COLOR, 0x94A3B8, 0);

    /* --- tuval: piksel piksel manzara --------------------------------- */
    g_canvas = wolf_canvas(root, 12, 178, 260, 116);
    if (g_canvas) {
        wolf_canvas_grad(g_canvas, 0, 0, 260, 116,
                         0xFF0EA5E9, 0xFF312E81, 0);   /* dikey gok */
        /* ay */
        wolf_canvas_circle(g_canvas, 214, 26, 16, 0xFFFFF176u, 1);
        wolf_canvas_circle(g_canvas, 220, 22, 16, 0xFF0EA5E9u, 1);
        /* tepeler (orta nokta cemberi yerine kalin cizgiler) */
        wolf_canvas_line(g_canvas, -10, 110, 90, 40, 0xFF166534u, 6);
        wolf_canvas_line(g_canvas, 70, 110, 170, 30, 0xFF15803Du, 6);
        wolf_canvas_line(g_canvas, 150, 110, 270, 46, 0xFF14532Du, 6);
        /* kart */
        wolf_canvas_rect(g_canvas, 14, 66, 120, 34, 0xCCF8FAFCu, 8);
        /* kart uzerine kirmizi kalin cizgi + beyaz taslak daire */
        wolf_canvas_line(g_canvas, 24, 88, 118, 88, 0xFFEF4444u, 3);
        wolf_canvas_circle(g_canvas, 190, 92, 14, 0xE6FFFFFFu, 0);
        /* getpx ornegi: bir pikseli oku, sag ustte 4x4 blok olarak geri yaz */
        uint32_t p = wolf_canvas_getpx(g_canvas, 90, 10);
        for (int yy = 0; yy < 4; yy++)
            for (int xx = 0; xx < 4; xx++)
                wolf_canvas_px(g_canvas, 248 + xx, 4 + yy, p);
    }
    wolf_id cl = wolf_label(root, 12, 300, "tuval: gradyan/cember/getpx");
    wolf_style(cl, WOLF_SP_TEXT_COLOR, 0x64748B, 0);

    /* --- cizgi grafik (lv_line) --------------------------------------- */
    wolf_id ln = wolf_line(root, 292, 196);
    wolf_style(ln, WOLF_SP_LINE_COLOR, 0x38BDF8, 0);
    wolf_style(ln, WOLF_SP_LINE_WIDTH, 2, 0);
    wolf_style(ln, WOLF_SP_LINE_ROUNDED, 1, 0);
    wolf_line_reset(ln);
    for (int i = 0; i <= 34; i++) {
        int x = i * 8;
        int y = 44 - ((i * 37 + (i % 5) * 13) % 45);  /* sözde veri */
        wolf_line_to(ln, x, y);
    }
    wolf_id ll = wolf_label(root, 292, 300, "wolf_line: 35 nokta");
    wolf_style(ll, WOLF_SP_TEXT_COLOR, 0x64748B, 0);

    /* --- alt bilgi ------------------------------------------------------ */
    wolf_id info = wolf_label(root, 12, 344, "");
    wolf_style(info, WOLF_SP_TEXT_COLOR, 0x94A3B8, 0);
    int ok = wolf_audio_ok();
    wolf_text(info, ok ? "ses surucusu: hazir (player ornegine bakin)"
                       : "ABI v2 stilleri calisiyor");

    /* --- zamanlayici: 100 ms -------------------------------------------- */
    wolf_timer(100, tick_cb, 0);
    return 0;
}
