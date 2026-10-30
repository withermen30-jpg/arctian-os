/**
 * emu_main.c — Arctian Emulator girisi.
 *
 * Kullanim:
 *   arctian-emu uygulama.wlf [secenekler]
 *     --info              paket bilgisi + imza durumu, calistirmaz
 *     --headless          pencere acmadan PNG ekran goruntusu
 *     --shot out.png      N kare sonra ekran goruntusu al (headless)
 *     --frames N          goruntu/kare sayisi (varsayilan 90)
 *     --seconds N         suresi bitince cik (etkilesimli mod)
 *     --width W --height H pencere boyutunu asgile
 *     --fs-root DIR       uygulamanin C:/ koku (varsayilan ./emu-data)
 *     --arg METIN         wolf_arg() ile gorunen deger
 *     --allow-net         ag cagrilarina izin ver (curl)
 *     --allow-exec        exec/ac cagrilarina izin ver
 *     --light             acik tema
 *     --version / -v
 */
#include "emu.h"
#include "dsk.h"
#include "lv_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================== */
/* ortak durum (plat_* buradan beslenir)                                 */
/* ===================================================================== */

static uint8_t *s_fb;
static int s_fb_w, s_fb_h;

static int s_mx, s_my, s_down;
static int s_ctrl, s_shift, s_scroll;
static uint32_t s_press_seq;

#define KEYQ 64
static int s_keys[KEYQ];
static int s_khead, s_ktail;

static int s_quit;

uint8_t *emu_fb(void) { return s_fb; }
int emu_fb_w(void) { return s_fb_w; }
int emu_fb_h(void) { return s_fb_h; }

int emu_fb_realloc(int w, int h) {
    if (w < 1 || h < 1) return -1;
    uint8_t *n = (uint8_t *)realloc(s_fb, (size_t)w * h * 4);
    if (!n) return -1;
    memset(n, 0, (size_t)w * h * 4);
    s_fb = n;
    s_fb_w = w;
    s_fb_h = h;
    return 0;
}

void emu_set_mouse(int x, int y, int down) {
    if (down && !s_down) s_press_seq++;      /* basma kenari */
    s_down = down;
    if (x >= 0) s_mx = x;
    if (y >= 0) s_my = y;
    if (s_mx < 0) s_mx = 0;
    if (s_my < 0) s_my = 0;
    if (s_mx >= emu_fb_w()) s_mx = emu_fb_w() - 1;
    if (s_my >= emu_fb_h()) s_my = emu_fb_h() - 1;
}
void emu_press_edge(void) { s_press_seq++; }
void emu_scroll(int d) { s_scroll += d; }
void emu_mods(int ctrl, int shift) { s_ctrl = ctrl; s_shift = shift; }

void emu_push_key(int c) {
    int nt = (s_khead + 1) % KEYQ;
    if (nt != s_ktail) { s_keys[s_khead] = c; s_khead = nt; }
}
int emu_pop_key(void) {
    if (s_ktail == s_khead) return -1;
    int c = s_keys[s_ktail];
    s_ktail = (s_ktail + 1) % KEYQ;
    return c;
}
int emu_key_avail(void) { return s_ktail != s_khead; }

int emu_mouse_x(void) { return s_mx; }
int emu_mouse_y(void) { return s_my; }
int emu_mouse_down(void) { return s_down; }
int emu_scroll_delta(void) { int d = s_scroll; s_scroll = 0; return d; }
int emu_ctrl(void) { return s_ctrl; }
int emu_shift(void) { return s_shift; }
uint32_t emu_press_seq(void) { return s_press_seq; }

void emu_request_quit(void) { s_quit = 1; }

static void emu_log_sink(const char *s) {
    fputs(s ? s : "", stdout);
    fflush(stdout);
}

int emu_set_mode(int w, int h) {
    if (emu_fb_realloc(w, h) != 0) return -1;
    asi_gfx_size(w, h);
    lv_port_set_resolution(w, h);
    plat_set_size(w, h);
    plat_present_rect(0, 0, w, h);
    return 0;
}

/* ===================================================================== */
/* CLI                                                                   */
/* ===================================================================== */

static void usage(void) {
    fprintf(stderr,
        "Arctian Emulator %s — .wlf uygulamarini masaustunde calistirir\n"
        "kullanim: arctian-emu uygulama.wlf [--info|--headless|--shot out.png]\n"
        "          [--frames N] [--seconds N] [--width W] [--height H]\n"
        "          [--fs-root DIR] [--arg METIN] [--allow-net] [--allow-exec]\n"
        "          [--click KARE:X:Y ...] [--light]\n"
        "          [--deterministic] sanal saat: ayni girdi -> bit-bir kare\n", EMU_VERSION);
}

static const char *role_name(uint16_t r) {
    return r == WLF_ROLE_CLI ? "cli" : r == WLF_ROLE_RUNTIME ? "runtime" : "ui";
}

static void pkg_info(const emu_pkg_t *p) {
    const wlf_header_t *h = p->hdr;
    char name[64], title[128], ver[32], author[64], cat[48];
#define CS(dst, src, n) do { int i = 0; while (i < n && src[i] && i < (int)sizeof(dst) - 1) { dst[i] = src[i]; i++; } dst[i] = 0; } while (0)
    CS(name, h->name, WLF_NAME_LEN);
    CS(title, h->title, WLF_TITLE_LEN);
    CS(ver, h->ver, WLF_VER_LEN);
    CS(author, h->author, WLF_AUTHOR_LEN);
    CS(cat, h->category, WLF_CATEGORY_LEN);
#undef CS
    printf("Ad        : %s\n", name);
    printf("Baslik    : %s\n", title);
    printf("Surum     : %s\n", ver);
    printf("Yazar     : %s\n", author);
    printf("Kategori  : %s\n", cat);
    printf("Tur       : %s\n", h->kind == WLF_KIND_NATIVE ? "native" : "bytecode");
    printf("Rol       : %s\n", role_name(p->meta.role));
    printf("Pencere   : %ux%u\n", (uint32_t)h->win_w, (uint32_t)h->win_h);
    if (h->kind == WLF_KIND_NATIVE)
        printf("Yukleme   : 0x%08X  image=%u bss=%u entry=0x%X\n",
               h->load_addr, h->image_size, h->bss_size, h->entry_off);
    printf("Izinler   :%s%s%s\n",
           (h->flags & WLF_FLAG_FS) ? " fs" : "",
           (h->flags & WLF_FLAG_NET) ? " net" : "",
           (h->flags & WLF_FLAG_EXEC) ? " exec" : "");
    printf("Payload   : %u bayt, ikon %u bayt\n", p->payload_len, p->icon_len);
    printf("Checksum  : OK\n");
    printf("Imza      : %s\n",
           p->sig_ok == 0 ? "YOK (gelistirme paketi; OS kurmaz, market imzalar)"
                          : p->sig_ok == 1 ? "GECERLI (magaza anahtari)"
                                           : "GECERSIZ!");
}

/* --click KARE:X:Y — kare dongusunda deterministik fare tiklamasi.
 * KARE = en az kare numarasi. Tiklama TIK tabanli makineyle uretilir
 * (basili ~120ms + birakma ~60ms gercek zaman): boylece LVGL indev
 * (33ms) her platformda ayni sekilde yakalar; Windows'ta Sleep(4) 15ms
 * kuantumla calistigi icin kare-basli enjeksiyon tik dusuruyordu. */
typedef struct { int frame, x, y, state; uint64_t t0; } emu_click_t;

int main(int argc, char **argv) {
    const char *pkg_path = 0;
    const char *shot = 0, *fsroot = "emu-data";
    int info_only = 0, frames = 0, seconds = 0;
    int width = 0, height = 0;
    emu_click_t clicks[16];
    int nclicks = 0, cqi = 0;
    emu_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.fs_root = fsroot;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--info")) info_only = 1;
        else if (!strcmp(a, "--headless")) frames = frames ? frames : 90;
        else if (!strcmp(a, "--shot") && i + 1 < argc) { shot = argv[++i]; frames = frames ? frames : 90; }
        else if (!strcmp(a, "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(a, "--seconds") && i + 1 < argc) seconds = atoi(argv[++i]);
        else if (!strcmp(a, "--width") && i + 1 < argc) width = atoi(argv[++i]);
        else if (!strcmp(a, "--height") && i + 1 < argc) height = atoi(argv[++i]);
        else if (!strcmp(a, "--fs-root") && i + 1 < argc) cfg.fs_root = argv[++i];
        else if (!strcmp(a, "--arg") && i + 1 < argc) cfg.arg = argv[++i];
        else if (!strcmp(a, "--allow-net")) cfg.allow_net = 1;
        else if (!strcmp(a, "--allow-exec")) cfg.allow_exec = 1;
        else if (!strcmp(a, "--light")) cfg.light_theme = 1;
        else if (!strcmp(a, "--deterministic")) emu_det_set(1);
        else if (!strcmp(a, "--click") && i + 1 < argc) {
            const char *v = argv[++i];
            int cf, cx, cy;
            if (nclicks < (int)(sizeof clicks / sizeof clicks[0]) &&
                sscanf(v, "%d:%d:%d", &cf, &cx, &cy) == 3 && cf >= 0) {
                clicks[nclicks].frame = cf;
                clicks[nclicks].x = cx;
                clicks[nclicks].y = cy;
                clicks[nclicks].state = 0;
                clicks[nclicks].t0 = 0;
                nclicks++;
            } else { fprintf(stderr, "kotu --click: %s (beklenen KARE:X:Y)\n", v);
                   return 2; }
        }
        else if (!strcmp(a, "-v") || !strcmp(a, "--version")) {
            printf("arctian-emu %s (LVGL %d.%d)\n", EMU_VERSION,
                   lv_version_major(), lv_version_minor());
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "bilinmeyen secenek: %s\n", a);
            usage();
            return 2;
        } else pkg_path = a;
    }
    if (!pkg_path) { usage(); return 2; }

    emu_pkg_t pkg;
    char err[256];
    if (pkg_load(pkg_path, &pkg, err, sizeof err) != 0) {
        fprintf(stderr, "[emu] paket gecersiz: %s\n", err);
        return 1;
    }
    if (pkg.sig_ok == -1)
        fprintf(stderr, "[emu] UYARI: imza DOGRULANAMADI — paket magazadan "
                        "bozuk gelmis olabilir\n");
    if (info_only) { pkg_info(&pkg); pkg_free(&pkg); return 0; }
    if (pkg.hdr->kind == WLF_KIND_NATIVE && pkg.sig_ok == 0)
        fprintf(stderr, "[emu] not: imzasiz native paket yalnizca emulatorde "
                        "calisir (gelistirme onizlemesi)\n");

    const wlf_header_t *h = pkg.hdr;
    int w = width > 0 ? width : (h->win_w > 0 ? h->win_w : 480);
    int hh = height > 0 ? height : (h->win_h > 0 ? h->win_h : 300);
    if (w < 64) w = 64;
    if (hh < 64) hh = 64;

    char title[128];
    {
        int i = 0;
        while (i < WLF_TITLE_LEN && h->title[i] && i < (int)sizeof(title) - 1) {
            title[i] = h->title[i]; i++;
        }
        title[i] = 0;
        if (!title[0]) snprintf(title, sizeof title, "Arctian App");
    }

    /* --- kurulum: ASI -> platform -> LVGL -> tema -> host --- */
    host_setup(&cfg);
    if (emu_fb_realloc(w, hh) != 0) { fprintf(stderr, "[emu] bellek yok\n"); return 1; }
    asi_gfx_size(w, hh); /* display cozunurlugu fb boyutuyla ayni olmali —
                          * lv_port_init gfx_w/h'den yaratir; senkronsiz
                          * flush fb tasirir (uidemo 360x220'de cokmisti) */
    if (plat_init(w, hh, title) != 0) { pkg_free(&pkg); return 1; }
    lv_port_init(g_asi);
    adl_font_init();
    adl_wolf_theme_init();

    lv_obj_t *body = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    wlf_host_reset(body);
    wolf_api_t *api = wlf_api_get(body);

    int is_cli = (pkg.meta.role != WLF_ROLE_UI);
    if (is_cli) wlf_host_set_sink(emu_log_sink);

    /* --- uygulama girisi --- */
    char aerr[256];
    aerr[0] = 0;
    int rc;
    if (h->kind == WLF_KIND_NATIVE) {
        rc = emu_run_native(&pkg, api, aerr, sizeof aerr);
    } else {
        rc = wlf_vm_run(pkg.payload, pkg.payload_len, body, g_asi);
    }
    if (rc != 0) {
        fprintf(stderr, "[emu] uygulama girisi hatali (rc=%d)%s\n", rc,
                rc == -0x7FF ? " — erisim ihlali/cokme" : "");
        if (aerr[0]) fprintf(stderr, "[emu] %s\n", aerr);
    }

    /* --- kare dongusu --- */
    if (frames > 0) {
        /* headless/goruntu modu: sabit kare sayisi, hizli tick */
        for (int f = 0; f < frames && !s_quit; f++) {
            /* tiklama durum makinesi (TIK tabanli, platformdan bagimsiz) */
            if (cqi < nclicks) {
                uint64_t now = plat_ticks_ms();
                emu_click_t *ck = &clicks[cqi];
                if (ck->state == 0 && f >= ck->frame) {
                    emu_set_mouse(ck->x, ck->y, 1);
                    ck->state = 1; ck->t0 = now;
                } else if (ck->state == 1 && now - ck->t0 >= 120) {
                    emu_set_mouse(ck->x, ck->y, 0);
                    ck->state = 2; ck->t0 = now;
                } else if (ck->state == 2 && now - ck->t0 >= 60) {
                    ck->state = 3; cqi++;
                }
            }
            plat_pump();
            lv_timer_handler();
            int dx, dy, dw, dh;
            if (lv_port_take_dirty(&dx, &dy, &dw, &dh))
                plat_present_rect(dx, dy, dw, dh);
            plat_wait_ms(4);
        }
        if (shot) {
            if (emu_write_png(shot, emu_fb(), emu_fb_w(), emu_fb_h()) == 0)
                printf("ekran goruntusu: %s (%dx%d)\n", shot,
                       emu_fb_w(), emu_fb_h());
            else
                fprintf(stderr, "[emu] PNG yazilamadi: %s\n", shot);
        }
    } else {
        uint64_t t0 = plat_ticks_ms();
        while (!s_quit) {
            if (plat_pump()) break;
            lv_timer_handler();
            int dx, dy, dw, dh;
            if (lv_port_take_dirty(&dx, &dy, &dw, &dh))
                plat_present_rect(dx, dy, dw, dh);
            plat_wait_ms(12);
            if (seconds > 0 && (int)(plat_ticks_ms() - t0) > seconds * 1000)
                break;
            if (is_cli && rc == 0 &&
                (int)(plat_ticks_ms() - t0) > 2500) break;
        }
    }

    plat_shutdown();
    pkg_free(&pkg);
    return rc == 0 ? 0 : 3;
}
