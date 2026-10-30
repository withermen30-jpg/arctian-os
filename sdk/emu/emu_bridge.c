/**
 * emu_bridge.c — SysV ABI koprusu (Windows).
 *
 * Tum .wlf native kodlari SysV x86-64 cagri sozlesmesiyle uretilir
 * (derleme WSL gcc ile yapilir; isletim sistemi de SysV'dir). Windows
 * emulatoru mingw ile MS x64 ABI'de derlenir. Bu iki sozlesme karisirsa
 * wolf_main(api) RCX ile gelir, uygulama RDI'yi okur -> sihir bozulur.
 *
 * Copozum: uygulamanin gordugu wolf_api_t tablosunu buradaki SARMALAYICILAR
 * doldurur. Her sarmalayici `sysv_abi` niteligidir (gcc bunu mingw'de
 * destekler): uygulama SysV sozlesmesiyle girer, sarmalayici MS sozlesmesiyle
 * asil OS islevini cagrir. Giris (wolf_main) ve geri cagrilar (on_click /
 * on_change) icin ayni nitelik emu_native.c'de ve asagidaki trampetlerde
 * kullanilir.
 *
 * Linux'ta host zaten SysV'dir; tablo dogrudan geri doner (no-op).
 *
 * SINIRLAR: yalniz tamsayi/isaretci parametreli API fonksiyonlari (wolf.h
 * sozlesmesinde float/varargs yok); 6'dan fazla parametre SysV istifiyle
 * tasinir (gcc sysv_abi bunu dogru uretir — canvas_grad 8 parametre).
 * callback havuzu 64 kayittir; asilirsa kayit dogrudan gecirilir (uyari basilir).
 */
#include "emu.h"

#include <stdio.h>
#include <string.h>

#ifndef _WIN32

const wolf_api_t *emu_bridge_api(const wolf_api_t *host) { return host; }

#else /* _WIN32 */

#define SYSV __attribute__((sysv_abi))

static const wolf_api_t *H;          /* asil (MS ABI) tablo */
static wolf_api_t        s_tab;      /* uygulamaya verilen tablo */
static const wolf_api_t *s_built_for;

/* SysV nitelikli sarmalayici isareteleri, MS sozlesmeli yapida saklanir:
 * cagri ABI'sini cagri noktasindaki tip belirler; tabloyu yalniz uygulama
 * (SysV tarafindan) okur. Atamalardaki tip farki bu yuzden bilinclidir. */

/* ---- callback havuzu: MS trampeti -> SysV uygulama fonksiyonu ---- */
typedef void (*evt_sysv)(wolf_id, void *) SYSV;

#define EMU_TRAMP_N 64
static wolf_event_fn s_evt[EMU_TRAMP_N];
static int           s_evt_n;

#define TRAMP(k) \
    static void tr##k(wolf_id w, void *u) { \
        if (s_evt[k]) ((evt_sysv)s_evt[k])(w, u); \
    }
TRAMP(0)   TRAMP(1)   TRAMP(2)   TRAMP(3)   TRAMP(4)   TRAMP(5)   TRAMP(6)   TRAMP(7)
TRAMP(8)   TRAMP(9)   TRAMP(10)  TRAMP(11)  TRAMP(12)  TRAMP(13)  TRAMP(14)  TRAMP(15)
TRAMP(16)  TRAMP(17)  TRAMP(18)  TRAMP(19)  TRAMP(20)  TRAMP(21)  TRAMP(22)  TRAMP(23)
TRAMP(24)  TRAMP(25)  TRAMP(26)  TRAMP(27)  TRAMP(28)  TRAMP(29)  TRAMP(30)  TRAMP(31)
TRAMP(32)  TRAMP(33)  TRAMP(34)  TRAMP(35)  TRAMP(36)  TRAMP(37)  TRAMP(38)  TRAMP(39)
TRAMP(40)  TRAMP(41)  TRAMP(42)  TRAMP(43)  TRAMP(44)  TRAMP(45)  TRAMP(46)  TRAMP(47)
TRAMP(48)  TRAMP(49)  TRAMP(50)  TRAMP(51)  TRAMP(52)  TRAMP(53)  TRAMP(54)  TRAMP(55)
TRAMP(56)  TRAMP(57)  TRAMP(58)  TRAMP(59)  TRAMP(60)  TRAMP(61)  TRAMP(62)  TRAMP(63)

static const wolf_event_fn s_tramp[EMU_TRAMP_N] = {
    tr0,   tr1,   tr2,   tr3,   tr4,   tr5,   tr6,   tr7,
    tr8,   tr9,   tr10,  tr11,  tr12,  tr13,  tr14,  tr15,
    tr16,  tr17,  tr18,  tr19,  tr20,  tr21,  tr22,  tr23,
    tr24,  tr25,  tr26,  tr27,  tr28,  tr29,  tr30,  tr31,
    tr32,  tr33,  tr34,  tr35,  tr36,  tr37,  tr38,  tr39,
    tr40,  tr41,  tr42,  tr43,  tr44,  tr45,  tr46,  tr47,
    tr48,  tr49,  tr50,  tr51,  tr52,  tr53,  tr54,  tr55,
    tr56,  tr57,  tr58,  tr59,  tr60,  tr61,  tr62,  tr63,
};

static wolf_event_fn wrap_evt(wolf_event_fn cb) {
    if (!cb) return 0;
    if (s_evt_n < EMU_TRAMP_N) {
        s_evt[s_evt_n] = cb;
        return s_tramp[s_evt_n++];
    }
    fprintf(stderr, "[emu] uyari: callback havuzu doldu (%d), geri cagri "
                    " ABI korumasiz\n", EMU_TRAMP_N);
    return cb;
}

/* ---- API sarmalayicilari (SysV giris, MS cikis) ----
 * W(ad, donus, (parametre bildirimi), (aktarilan argumanlar))
 * V(ad, (parametre bildirimi), (aktarilan argumanlar))  -> void */
#define W(name, ret, params, args) \
    static ret SYSV w_##name params { return H->name args; }
#define V(name, params, args) \
    static void SYSV w_##name params { H->name args; }

/* --- pencere --- */
static wolf_id SYSV w_root(void)  { return H->root(); }
V(win_title, (const char *title), (title))
V(win_size,  (int w, int h), (w, h))

/* --- cizim --- */
W(panel,     wolf_id, (wolf_id p, int x, int y, int ww, int hh), (p, x, y, ww, hh))
W(label,     wolf_id, (wolf_id p, int x, int y, const char *t), (p, x, y, t))
W(button,    wolf_id, (wolf_id p, int x, int y, int ww, int hh, const char *t), (p, x, y, ww, hh, t))
W(input,     wolf_id, (wolf_id p, int x, int y, int ww, int hh, const char *ph), (p, x, y, ww, hh, ph))
W(listbox,   wolf_id, (wolf_id p, int x, int y, int ww, int hh), (p, x, y, ww, hh))
W(image,     wolf_id, (wolf_id p, int x, int y, int ww, int hh, const char *pa), (p, x, y, ww, hh, pa))
W(progress,  wolf_id, (wolf_id p, int x, int y, int ww, int hh, int pc), (p, x, y, ww, hh, pc))
W(slider,    wolf_id, (wolf_id p, int x, int y, int ww, int hh, int pc), (p, x, y, ww, hh, pc))
W(separator, wolf_id, (wolf_id p, int x, int y, int ww), (p, x, y, ww))
W(checkbox,  wolf_id, (wolf_id p, int x, int y, const char *t, int ck), (p, x, y, t, ck))

/* --- widget ozellikleri --- */
V(set_text,   (wolf_id w, const char *t), (w, t))
W(get_text,   int, (wolf_id w, char *out, int cap), (w, out, cap))
V(set_value,  (wolf_id w, int v), (w, v))
W(get_value,  int, (wolf_id w), (w))
V(set_pos,    (wolf_id w, int x, int y), (w, x, y))
V(set_size,   (wolf_id w, int ww, int hh), (w, ww, hh))
V(set_color,  (wolf_id w, uint32_t rgb), (w, rgb))
V(set_visible,(wolf_id w, int v), (w, v))
V(set_enabled,(wolf_id w, int v), (w, v))
V(clear,      (wolf_id w), (w))
V(destroy,    (wolf_id w), (w))

/* --- olaylar (callback sarilir) --- */
static void SYSV w_on_click(wolf_id w, wolf_event_fn cb, void *user) {
    H->on_click(w, wrap_evt(cb), user);
}
static void SYSV w_on_change(wolf_id w, wolf_event_fn cb, void *user) {
    H->on_change(w, wrap_evt(cb), user);
}

/* --- liste --- */
V(list_add,    (wolf_id w, const char *item), (w, item))
V(list_clear,  (wolf_id w), (w))
W(list_count,    int, (wolf_id w), (w))
W(list_item,     int, (wolf_id w, int i, char *out, int cap), (w, i, out, cap))
W(list_selected, int, (wolf_id w), (w))

/* --- dosya sistemi --- */
W(fs_read,   int, (const char *p, void *buf, int cap), (p, buf, cap))
W(fs_write,  int, (const char *p, const void *buf, int len), (p, buf, len))
W(fs_list,   int, (const char *d, int i, char *name, int cap, int *is_dir), (d, i, name, cap, is_dir))
W(fs_count,  int, (const char *d), (d))
W(fs_exists, int, (const char *p), (p))
W(fs_remove, int, (const char *p), (p))
W(fs_mkdir,  int, (const char *p), (p))
W(fs_rename, int, (const char *a, const char *b), (a, b))

/* --- sistem --- */
V(log,     (const char *t), (t))
V(notify,  (const char *t, const char *m), (t, m))
V(dialog,  (const char *t, const char *m, const char *ok), (t, m, ok))
W(exec,    int, (const char *cmd, char *out, int cap), (cmd, out, cap))
static uint64_t SYSV w_ticks_ms(void)  { return H->ticks_ms(); }
V(sleep_ms, (uint32_t ms), (ms))
W(now,        int, (int *y, int *mo, int *d, int *h, int *mi, int *s), (y, mo, d, h, mi, s))
W(os_version, int, (char *out, int cap), (out, cap))
W(open_app,   int, (const char *name), (name))

/* --- pano --- */
V(clip_set, (const char *t), (t))
W(clip_get, int, (char *out, int cap), (out, cap))

/* --- ag --- */
W(fetch, int, (const char *url, void *buf, int cap, int *len_out), (url, buf, cap, len_out))

/* --- calisma zamani argumani --- */
static const char *SYSV w_arg(void) { return H->arg(); }

/* --- ABI v2 --- */
V(style,      (wolf_id w, int prop, int val, int fl), (w, prop, val, fl))
W(line,       wolf_id, (wolf_id p, int x, int y), (p, x, y))
V(line_reset, (wolf_id w), (w))
W(line_to,    int, (wolf_id w, int x, int y), (w, x, y))
W(arc,        wolf_id, (wolf_id p, int x, int y, int r), (p, x, y, r))
V(arc_set,    (wolf_id w, int s, int e, int rot), (w, s, e, rot))
W(canvas,     wolf_id, (wolf_id p, int x, int y, int ww, int hh), (p, x, y, ww, hh))
V(canvas_px,  (wolf_id w, int x, int y, uint32_t argb), (w, x, y, argb))
V(canvas_rect,(wolf_id w, int x, int y, int cw, int ch, uint32_t argb, int rad),
              (w, x, y, cw, ch, argb, rad))
V(canvas_line,(wolf_id w, int x1, int y1, int x2, int y2, uint32_t argb, int wd),
              (w, x1, y1, x2, y2, argb, wd))
V(canvas_circle,(wolf_id w, int cx, int cy, int r, uint32_t argb, int fill),
                (w, cx, cy, r, argb, fill))
V(canvas_grad,(wolf_id w, int x, int y, int cw, int ch, uint32_t c0, uint32_t c1,
               int dir), (w, x, y, cw, ch, c0, c1, dir))
V(canvas_clear,(wolf_id w, uint32_t argb), (w, argb))
W(canvas_getpx, uint32_t, (wolf_id w, int x, int y), (w, x, y))
W(audio_ok,     int, (void), ())
W(audio_play,   int, (const char *path), (path))
W(audio_play_mem, int, (const void *data, int len), (data, len))
static void SYSV w_audio_stop(void) { H->audio_stop(); }
W(audio_playing, int, (void), ())
W(audio_vol,     int, (void), ())
V(audio_set_vol, (int percent), (percent))
static wolf_id SYSV w_timer(uint32_t period_ms, wolf_event_fn cb, void *user) {
    return H->timer(period_ms, wrap_evt(cb), user);
}
V(timer_cancel, (wolf_id t), (t))
V(to_front,     (wolf_id w), (w))
V(to_back,      (wolf_id w), (w))
V(transition,   (wolf_id w, unsigned mask, int dur, int dly, int ease),
                (w, mask, dur, dly, ease))
V(state_set,    (wolf_id w, int fl, int on), (w, fl, on))
V(anim_in,      (wolf_id w, int dy, int dur, int dly), (w, dy, dur, dly))
V(anim_pulse,   (wolf_id w, int period, int low), (w, period, low))

const wolf_api_t *emu_bridge_api(const wolf_api_t *host) {
    if (!host) return host;
    if (s_built_for == host) return &s_tab;

    H = host;
    memset(&s_tab, 0, sizeof s_tab);
    s_evt_n = 0;
    memset(s_evt, 0, sizeof s_evt);

    s_tab.magic   = host->magic;
    s_tab.version = host->version;
    s_tab.size    = host->size;

    s_tab.root      = w_root;
    s_tab.win_title = w_win_title;
    s_tab.win_size  = w_win_size;

    s_tab.panel     = w_panel;
    s_tab.label     = w_label;
    s_tab.button    = w_button;
    s_tab.input     = w_input;
    s_tab.listbox   = w_listbox;
    s_tab.image     = w_image;
    s_tab.progress  = w_progress;
    s_tab.slider    = w_slider;
    s_tab.separator = w_separator;
    s_tab.checkbox  = w_checkbox;

    s_tab.set_text    = w_set_text;
    s_tab.get_text    = w_get_text;
    s_tab.set_value   = w_set_value;
    s_tab.get_value   = w_get_value;
    s_tab.set_pos     = w_set_pos;
    s_tab.set_size    = w_set_size;
    s_tab.set_color   = w_set_color;
    s_tab.set_visible = w_set_visible;
    s_tab.set_enabled = w_set_enabled;
    s_tab.clear       = w_clear;
    s_tab.destroy     = w_destroy;

    s_tab.on_click  = w_on_click;
    s_tab.on_change = w_on_change;

    s_tab.list_add      = w_list_add;
    s_tab.list_clear    = w_list_clear;
    s_tab.list_count    = w_list_count;
    s_tab.list_item     = w_list_item;
    s_tab.list_selected = w_list_selected;

    s_tab.fs_read   = w_fs_read;
    s_tab.fs_write  = w_fs_write;
    s_tab.fs_list   = w_fs_list;
    s_tab.fs_count  = w_fs_count;
    s_tab.fs_exists = w_fs_exists;
    s_tab.fs_remove = w_fs_remove;
    s_tab.fs_mkdir  = w_fs_mkdir;
    s_tab.fs_rename = w_fs_rename;

    s_tab.log       = w_log;
    s_tab.notify    = w_notify;
    s_tab.dialog    = w_dialog;
    s_tab.exec      = w_exec;
    s_tab.ticks_ms  = w_ticks_ms;
    s_tab.sleep_ms  = w_sleep_ms;
    s_tab.now       = w_now;
    s_tab.os_version = w_os_version;
    s_tab.open_app  = w_open_app;

    s_tab.clip_set = w_clip_set;
    s_tab.clip_get = w_clip_get;

    s_tab.fetch = w_fetch;
    s_tab.arg   = w_arg;

    /* ABI v2 */
    s_tab.style        = w_style;
    s_tab.line         = w_line;
    s_tab.line_reset   = w_line_reset;
    s_tab.line_to      = w_line_to;
    s_tab.arc          = w_arc;
    s_tab.arc_set      = w_arc_set;
    s_tab.canvas       = w_canvas;
    s_tab.canvas_px    = w_canvas_px;
    s_tab.canvas_rect  = w_canvas_rect;
    s_tab.canvas_line  = w_canvas_line;
    s_tab.canvas_circle = w_canvas_circle;
    s_tab.canvas_grad  = w_canvas_grad;
    s_tab.canvas_clear = w_canvas_clear;
    s_tab.canvas_getpx = w_canvas_getpx;
    s_tab.audio_ok     = w_audio_ok;
    s_tab.audio_play   = w_audio_play;
    s_tab.audio_play_mem = w_audio_play_mem;
    s_tab.audio_stop   = w_audio_stop;
    s_tab.audio_playing = w_audio_playing;
    s_tab.audio_vol    = w_audio_vol;
    s_tab.audio_set_vol = w_audio_set_vol;
    s_tab.timer        = w_timer;
    s_tab.timer_cancel = w_timer_cancel;
    s_tab.to_front     = w_to_front;
    s_tab.to_back      = w_to_back;
    s_tab.transition   = w_transition;
    s_tab.state_set    = w_state_set;
    s_tab.anim_in      = w_anim_in;
    s_tab.anim_pulse   = w_anim_pulse;

    s_built_for = host;
    return &s_tab;
}

#endif /* _WIN32 */
