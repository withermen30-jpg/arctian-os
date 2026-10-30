/**
 * Arctian Emulator — dsk.h masaustu karsiligi (shim).
 *
 * desktop/wlf_host.c, desktop/wlf_vm.c ve desktop/port/lv_port.c OS buildinde
 * dsk.h'i dahil eder. Emulator ayni dosyalari KAYNAK DEGISTEN derleyebilmek
 * icin bu shimi kullanir: gercek alt basliklar aynen gonderilir, OS'un kabuk
 * (shell/win) tarafindan gelen birkaç sembol ise emu_shim.c icinde
 * sadelistirilmis halleriyle saglanir.
 */
#ifndef ARCTIAN_EMU_DSK_H
#define ARCTIAN_EMU_DSK_H

#include <stdint.h>
#include <stddef.h>

#include "lvgl.h"
#include "asi.h"
#include "png.h"
#include "adl_wolf.h"
#include "wlf.h"
#include "sig.h"
#include "wolf.h"

/* OS: extern asi_t *g_asi (dsk.h:255). */
extern asi_t *g_asi;

/* ---- OS kabugunun pencere kaydi (win.h) — emu tek pencere yonetir. ----
 * wlf_host.c yalnizca r->title alani yazar ve guncelleme ister. */
typedef struct {
    lv_obj_t *body;
    char      title[64];
} winrec_t;

winrec_t *winrec_of(lv_obj_t *body);
void      update_app_indicators(void);

/* ---- Izin kapilari (OS: perm.c, onay deposu) ----
 * Emulatorde karar bayraklarla verilir (--allow-net / --allow-exec). */
int perm_policy(const char *path, int is_write);
int perm_exec(const char *cmd);
int perm_net(void);

/* ---- Yardimcilar (OS: dsk_util.c) ---- */
void bin_copy(char *dst, int cap, const char *src);
int  slen2(const char *s);
void canvas_buf_free_cb(lv_event_t *e);

/* ---- Sistem servisleri (OS: shell/auth/clipboard) ---- */
void        arctian_notify(const char *title, const char *msg);
const char *arctian_auth_current(void);
int         arctian_shell_launch(const char *q);
void        arctian_clipboard_set(const char *text);
const char *arctian_clipboard_get(void);

/* ---- Wolf host + VM (desktop/wlf_host.c, desktop/wlf_vm.c) ---- */
wolf_api_t *wlf_api_get(lv_obj_t *body);
void        wlf_host_reset(lv_obj_t *body);
void        wlf_host_error(const char *msg);
void        wlf_host_set_sink(void (*fn)(const char *s));
void        wlf_host_set_arg(const char *arg);
lv_obj_t   *wolf_host_obj(wolf_id id);
int         wlf_vm_run(const uint8_t *code, uint32_t len, lv_obj_t *body,
                       asi_t *asi);

#endif /* ARCTIAN_EMU_DSK_H */
