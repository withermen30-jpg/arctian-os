/**
 * Arctian Emulator — .wlf uygulamalarini isletim sistemiyle birebir calistirir.
 *
 * Tasarim (piksel sadakati):
 *   OS'un kendi kaynaklari DOGRUDAN derlenir:
 *     desktop/wlf_host.c   wolf_api_t -> LVGL koprusu (ayni stiller)
 *     desktop/wlf_vm.c     Wolf bytecode makinesi
 *     desktop/port/lv_port.c  display/indev/tick kurulumu (ayni buffer)
 *     desktop/theme/adl_wolf.c + Inter.ttf  ayni tema ve font
 *     lib/png.c            ayni PNG cozucu (wolf_image icin)
 *     desktop/sig.c        ayni imza dogrulama (RSA-2048/SHA-256)
 *   sdk/emu/shim/dsk.h bu dosyalarin bekledigi dsk.h'in masaustu karsiligidir;
 *   sdk/emu/emu_shim.c asi_t (fs/zaman/cizim/girdi) servislerini sunar.
 *
 * Platform arka uclari: win32 (GDI), x11, headless (PNG ekran goruntusu).
 */
#ifndef ARCTIAN_EMU_H
#define ARCTIAN_EMU_H

#include <stdint.h>
#include <time.h>
#include <stddef.h>

#include "lvgl.h"
#include "asi.h"
#include "wlf.h"
#include "wolf.h"

#define EMU_VERSION "1.2"

/* ---------------- paket ---------------- */
typedef struct {
    uint8_t             *raw;      /* dosya baytlari (imza trailer dahil) */
    uint32_t             raw_len;
    uint32_t             body_len; /* imzalanan govde uzunlugu */
    const wlf_header_t  *hdr;
    const uint8_t       *icon;
    uint32_t             icon_len;
    const uint8_t       *payload;
    uint32_t             payload_len;
    wlf_meta_t           meta;
    int                  sig_ok;   /* 1 gecersiz yok, 0 imzasiz, -1 gecersiz */
} emu_pkg_t;

int  pkg_load(const char *path, emu_pkg_t *pkg, char *err, int errcap);
void pkg_free(emu_pkg_t *pkg);

/* ---------------- calistirma ---------------- */
typedef struct {
    const char *fs_root;     /* uygulamanin C:/ koku (varsayilan ./emu-data) */
    int         allow_net;
    int         allow_exec;
    const char *arg;         /* wolf_arg() ile gorunen deger */
    int         light_theme;
} emu_cfg_t;

void host_setup(const emu_cfg_t *cfg);   /* asi + font + tema + host hazir */
int  emu_run_native(emu_pkg_t *pkg, wolf_api_t *api, char *err, int errcap);

/* Windows: uygulamaya verilen api tablosunu SysV ABI sarmalayicilariyla
 * doldurur (emu_bridge.c). Linux: ayni tabloyu geri dondurur. */
const wolf_api_t *emu_bridge_api(const wolf_api_t *host);

/* ---------------- platform katmani ---------------- */
int  plat_init(int w, int h, const char *title);  /* 0 = OK */
void plat_shutdown(void);
const char *plat_name(void);
void plat_set_title(const char *title);
void plat_set_size(int w, int h);      /* pencereyi yeniden boyutlandirir */
void plat_present_rect(int x, int y, int w, int h);
int  plat_pump(void);                  /* 1 = pencere kapandi */
void plat_wait_ms(int ms);
uint32_t plat_ticks_ms(void);
/* boyut degisince emu_main.caagilir: fb yeniden ayrilir */
int  emu_fb_realloc(int w, int h);

/* --- emu_main.c tarfindan saglanan ortak durum (plat_* bunlari besler) --- */
uint8_t *emu_fb(void);
int      emu_fb_w(void);
int      emu_fb_h(void);
void     emu_request_quit(void);
void     emu_set_mouse(int x, int y, int down);
void     emu_press_edge(void);         /* sol tus basma kenari */
void     emu_scroll(int d);
void     emu_mods(int ctrl, int shift);
void     emu_push_key(int c);
int      emu_pop_key(void);            /* -1 = bos */
int      emu_key_avail(void);

/* --- ASI servis tablolari (emu_shim.c) --- */
extern asi_t g_asi_store;
void asi_fs_root(const char *root);
void asi_flags(int allow_net, int allow_exec);
void asi_attach(void);                 /* g_asi tablosunu doldurur */
void asi_gfx_size(int w, int h);       /* gfx genislik/yuksekligini guncelle */

/* --- girdi okuma (lv_port read_cb'nin ASI uzerinden ulastigi yer) --- */
int emu_mouse_x(void);
int emu_mouse_y(void);
int emu_mouse_down(void);
int emu_scroll_delta(void);            /* okuyunca sifirlanir */
int emu_ctrl(void);
int emu_shift(void);
uint32_t emu_press_seq(void);

/* --- pencere boyutu degisimi (gfx set_mode) --- */
int emu_set_mode(int w, int h);        /* plat + fb + lv_port gunceller */

/* --- ekran goruntusu (emu_png.c) --- */
int emu_write_png(const char *path, const uint8_t *argb, int w, int h);

/* --- deterministik sanal saat (--deterministic, emu_time.c) ---
 * Acikken tum zaman tek bir sanal sayactan gelir (gerçek saat/uyku yok):
 * ayni .wlf + ayni kare sayisi -> her makinede bit-bit ayni kare. */
void     emu_det_set(int on);
int      emu_det_active(void);
uint32_t emu_det_now(void);
void     emu_det_advance(uint32_t ms);   /* aktif degilse no-op */
time_t   emu_det_unix(void);             /* sabit epoch + sanal */
#endif /* ARCTIAN_EMU_H */
