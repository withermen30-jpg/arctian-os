/**
 * emu_shim.c — OS'un ASI servislerinin masaustu karsiligi + kabuk stublari.
 *
 * desktop/wlf_host.c yalnizca asi_t uzerinden dosya/cevre/saat/ag ister;
 * burada bu tablo stdio + emu_main durum uzerinden doldurulur. Boylece
 * wlf_host.c KAYNAGINA DOKUNMADAN derlenir ve OS ile ayni davranir.
 */
#include "emu.h"
#include "dsk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>

#ifdef _WIN32
#  include <direct.h>
#  define EMU_MKDIR(p) _mkdir(p)
#else
#  include <unistd.h>
#  include <dirent.h>
#  define EMU_MKDIR(p) mkdir((p), 0777)
#endif

/* ===================================================================== */
/* ortak durum                                                           */
/* ===================================================================== */

static char g_root[512]   = "./emu-data";
static int  g_allow_net   = 0;
static int  g_allow_exec  = 0;

void asi_fs_root(const char *root) {
    if (root && root[0]) {
        snprintf(g_root, sizeof g_root, "%s", root);
#ifdef _WIN32
        _mkdir(g_root);
#else
        mkdir(g_root, 0777);
#endif
    }
}
void asi_flags(int allow_net, int allow_exec) {
    g_allow_net = allow_net;
    g_allow_exec = allow_exec;
}

/* ===================================================================== */
/* konsol                                                                */
/* ===================================================================== */

static void con_clear(void) {}
static void con_putc(char c) { fputc(c, stdout); }
static void con_puts(const char *s) { fputs(s ? s : "", stdout); fflush(stdout); }
static void con_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}
static asi_console_t s_console = {con_clear, con_putc, con_puts, con_printf};

/* ===================================================================== */
/* bellek                                                                */
/* ===================================================================== */

static void *mem_alloc(size_t n) { return malloc(n ? n : 1); }
static void  mem_free(void *p) { free(p); }
static void  mem_memset(void *d, int c, size_t n) { memset(d, c, n); }
static void  mem_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
static void  mem_info(asi_meminfo_t *o) {
    if (!o) return;
    o->total = 64u << 20; o->used = 0; o->free = 64u << 20;
    o->heap_total = 64u << 20; o->heap_used = 0;
}
static asi_mem_t s_mem = {mem_alloc, mem_free, mem_memset, mem_memcpy, mem_info};

/* ===================================================================== */
/* zaman                                                                 */
/* ===================================================================== */

static uint64_t t_ticks(void) { return plat_ticks_ms(); }
static void     t_sleep(uint32_t ms) { plat_wait_ms((int)ms); }
static int      t_now(asi_datetime_t *out) {
    if (!out) return -1;
    time_t t = emu_det_active() ? emu_det_unix() : time(NULL);
#ifdef _WIN32
    struct tm tmv;
    if (localtime_s(&tmv, &t) != 0) return -1;
#else
    struct tm tmv_s, *tmv_p = localtime_r(&t, &tmv_s);
    struct tm tmv;
    if (!tmv_p) return -1;
    tmv = *tmv_p;
#endif
    out->year = tmv.tm_year + 1900;
    out->month = tmv.tm_mon + 1;
    out->day = tmv.tm_mday;
    out->hour = tmv.tm_hour;
    out->minute = tmv.tm_min;
    out->second = tmv.tm_sec;
    out->weekday = tmv.tm_wday;
    return 0;
}
static int      t_ntp(uint32_t to) { (void)to; return -1; }
static uint64_t t_unix(void) { return (uint64_t)(emu_det_active() ? emu_det_unix() : time(NULL)); }
static void     t_set_unix(uint32_t u) { (void)u; }
static void     t_set_tz(int m) { (void)m; }
static asi_time_t s_time = {t_ticks, t_sleep, t_now, t_ntp, t_unix,
                            t_set_unix, t_set_tz};

/* ===================================================================== */
/* cizim                                                                 */
/* ===================================================================== */

static int      g_w = 480, g_h = 300;
static int      gfx_w(void) { return g_w; }
static int      gfx_h(void) { return g_h; }
static uint32_t *gfx_fb(void) { return (uint32_t *)emu_fb(); }
static uint32_t  gfx_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static void gfx_present(void) { plat_present_rect(0, 0, g_w, g_h); }
static int  gfx_set_mode(int w, int h) { return emu_set_mode(w, h); }
static void gfx_present_rect(int x, int y, int w, int hh) {
    plat_present_rect(x, y, w, hh);
}
static asi_gfx_t s_gfx = {gfx_w, gfx_h, gfx_fb, gfx_rgb, gfx_present,
                          gfx_set_mode, gfx_present_rect};

/* ===================================================================== */
/* girdi                                                                 */
/* ===================================================================== */

static int  in_mx(void) { return emu_mouse_x(); }
static int  in_my(void) { return emu_mouse_y(); }
static int  in_btn(void) { return emu_mouse_down() ? 1 : 0; }
static int  in_kav(void) { return emu_key_avail(); }
static int  in_kpop(void) { return emu_pop_key(); }
static int  in_scroll(void) { return emu_scroll_delta(); }
static int  in_hot(void) { return 0; }
static void in_kbd(int l) { (void)l; }
static int  in_ctrl(void) { return emu_ctrl(); }
static int  in_shift(void) { return emu_shift(); }
static uint32_t in_seq(void) { return emu_press_seq(); }
static asi_input_t s_input = {in_mx, in_my, in_btn, in_kav, in_kpop, in_scroll,
                              in_hot, in_kbd, in_ctrl, in_shift, in_seq};

/* ===================================================================== */
/* sistem                                                                */
/* ===================================================================== */

static void sys_shutdown(void) {}
static void sys_reboot(void) {}
static void sys_writeback(void) {}
static void sys_rb(arctian_recover_t *c, uint32_t t) { (void)c; (void)t; }
static void sys_re(void) {}
static asi_system_t s_sys = {sys_shutdown, sys_reboot, sys_writeback,
                             sys_rb, sys_re};

/* ===================================================================== */
/* dosya sistemi (C:/ -> fs_root kumu)                                   */
/* ===================================================================== */

typedef struct {
    FILE *f;
} emu_fh_t;

/* "C:/dir/ad" veya "dir/ad" -> "<root>/dir/ad". 0 = OK. */
static int resolve(const char *ospath, char *out, int cap) {
    if (!ospath || !ospath[0]) return -1;
    const char *p = ospath;
    if ((p[0] == 'C' || p[0] == 'c') && p[1] == ':') p += 2;
    while (*p == '/' || *p == '\\') p++;
    char norm[600];
    int n = 0;
    for (const char *s = p; *s && n < (int)sizeof(norm) - 1; s++) {
        norm[n++] = (*s == '\\') ? '/' : *s;
    }
    norm[n] = 0;
    while (n > 0 && norm[n - 1] == '/') norm[--n] = 0;
    if (n == 0) norm[0] = 0;
    /* '..' kacligini engelle */
    for (const char *s = norm; *s;) {
        if (s[0] == '.' && s[1] == '.' && (s[2] == '/' || s[2] == 0)) return -1;
        while (*s && *s != '/') s++;
        if (*s == '/') s++;
    }
    if (snprintf(out, cap, "%s/%s", g_root, norm) >= cap) return -1;
    return 0;
}

static int dir_mkpath(const char *ospath) {
    char full[600];
    if (resolve(ospath, full, sizeof full) != 0) return -1;
    char acc[600];
    snprintf(acc, sizeof acc, "%s", g_root);
    EMU_MKDIR(acc);
    const char *rel = full + strlen(g_root);
    for (char *s = (char *)rel; *s; s++) {
        if (*s == '/') {
            *s = 0;
            EMU_MKDIR(acc);
            *s = '/';
        }
    }
    EMU_MKDIR(acc);
    struct stat st;
    return stat(acc, &st) == 0 ? 0 : -1;
}

static void *fs_root_fn(void) { return (void *)(size_t)1; }
static void *fs_find(void *d, const char *n) { (void)d; (void)n; return 0; }
static void *fs_create(void *d, const char *n, int is_dir) {
    (void)d; (void)n; (void)is_dir; return 0;
}
static int fs_write(void *h, const void *data, uint32_t size) {
    emu_fh_t *fh = (emu_fh_t *)h;
    if (!fh || !fh->f) return -1;
    size_t w = fwrite(data, 1, size, fh->f);
    fflush(fh->f);
    return w == size ? 0 : -1;
}
static uint32_t fs_read(void *h, void *buf, uint32_t size) {
    emu_fh_t *fh = (emu_fh_t *)h;
    if (!fh || !fh->f) return 0;
    return (uint32_t)fread(buf, 1, size, fh->f);
}
static int         fs_count(void) { return 0; }
static const char *fs_name(int i) { (void)i; return ""; }
static uint32_t    fs_size(int i) { (void)i; return 0; }

static int fs_remove(const char *path) {
    char full[600];
    if (resolve(path, full, sizeof full) != 0) return -1;
    if (remove(full) == 0) return 0;
    return -1;
}
static int fs_mkdir(const char *path) { return dir_mkpath(path); }
static void *fs_create_in(const char *dir, const char *name, int is_dir) {
    if (!name || !name[0]) return 0;
    char rel[600];
    if (dir && dir[0]) {
        if (snprintf(rel, sizeof rel, "%s/%s", dir, name) >= (int)sizeof rel)
            return 0;
    } else {
        snprintf(rel, sizeof rel, "%s", name);
    }
    char full[600];
    if (resolve(rel, full, sizeof full) != 0) return 0;
    if (is_dir) return dir_mkpath(rel) == 0 ? (void *)(size_t)1 : 0;
    /* ebeveyn klasoru garanti et */
    char parent[600];
    snprintf(parent, sizeof parent, "%s", rel);
    char *sl = strrchr(parent, '/');
    if (sl) { *sl = 0; dir_mkpath(parent); }
    FILE *f = fopen(full, "w+b");
    if (!f) return 0;
    emu_fh_t *fh = (emu_fh_t *)malloc(sizeof(emu_fh_t));
    fh->f = f;
    return fh;
}
static void *fs_find_in(const char *dir, const char *name) {
    char rel[600];
    if (snprintf(rel, sizeof rel, "%s/%s", dir ? dir : "", name ? name : "")
        >= (int)sizeof rel) return 0;
    char full[600];
    if (resolve(rel, full, sizeof full) != 0) return 0;
    struct stat st;
    return stat(full, &st) == 0 ? (void *)(size_t)1 : 0;
}
static uint32_t fs_read_path(const char *path, void *buf, uint32_t size) {
    char full[600];
    if (resolve(path, full, sizeof full) != 0) return 0;
    FILE *f = fopen(full, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    return (uint32_t)n;
}

#ifdef _WIN32
/* Windows: FindFirstFile tablli listeleme (dirent mingw'de var ama
 * is_dir bayragi icin FindData daha saglam). */
#include <windows.h>
static int dir_count(const char *full) {
    char pat[700];
    snprintf(pat, sizeof pat, "%s\\*", full);
    for (char *s = pat; *s; s++) if (*s == '/') *s = '\\';
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..")) n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}
static int dir_list(const char *full, int idx, char *out, int cap, int *is_dir) {
    char pat[700];
    snprintf(pat, sizeof pat, "%s\\*", full);
    for (char *s = pat; *s; s++) if (*s == '/') *s = '\\';
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int n = 0, rc = -1;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (n == idx) {
            snprintf(out, cap, "%s", fd.cFileName);
            if (is_dir) *is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
            rc = 0;
            break;
        }
        n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return rc;
}
#else
static int dir_count(const char *full) {
    DIR *d = opendir(full);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
    closedir(d);
    return n;
}
static int dir_list(const char *full, int idx, char *out, int cap, int *is_dir) {
    DIR *d = opendir(full);
    if (!d) return -1;
    int n = 0, rc = -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (n == idx) {
            snprintf(out, cap, "%s", e->d_name);
            if (is_dir) {
                char p[700];
                struct stat st;
                snprintf(p, sizeof p, "%s/%s", full, e->d_name);
                *is_dir = (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
            }
            rc = 0;
            break;
        }
        n++;
    }
    closedir(d);
    return rc;
}
#endif

static int fs_count_dir(const char *path) {
    char full[600];
    if (resolve(path, full, sizeof full) != 0) return 0;
    return dir_count(full);
}
static int fs_list_dir(const char *path, int idx, char *out, int cap,
                       int *is_dir) {
    char full[600];
    if (resolve(path, full, sizeof full) != 0) return -1;
    return dir_list(full, idx, out, cap, is_dir);
}
static int fs_stat(const char *path, asi_stat_t *o) {
    char full[600];
    if (!o) return -1;
    if (resolve(path, full, sizeof full) != 0) return -1;
    struct stat st;
    if (stat(full, &st) != 0) return -1;
    memset(o, 0, sizeof *o);
    o->size = (uint32_t)st.st_size;
    o->mode = (uint32_t)st.st_mode;
    o->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
    return 0;
}
static int fs_stat_handle(void *h, asi_stat_t *o) {
    emu_fh_t *fh = (emu_fh_t *)h;
    if (!fh || !fh->f || !o) return -1;
    long p = ftell(fh->f);
    fseek(fh->f, 0, SEEK_END);
    long e = ftell(fh->f);
    fseek(fh->f, p, SEEK_SET);
    memset(o, 0, sizeof *o);
    o->size = (uint32_t)e;
    return 0;
}
static int fs_chmod(const char *p, uint32_t m) { (void)p; (void)m; return 0; }
static int fs_chown(const char *p, int u, int g) { (void)p; (void)u; (void)g; return 0; }
static void fs_subject(int u, int g) { (void)u; (void)g; }
static int fs_rename(const char *a, const char *b) {
    char fa[600], fb[600];
    if (resolve(a, fa, sizeof fa) || resolve(b, fb, sizeof fb)) return -1;
    char parent[600];
    snprintf(parent, sizeof parent, "%s", b);
    char *sl = strrchr(parent, '/');
    if (sl) { *sl = 0; dir_mkpath(parent); }
    return rename(fa, fb) == 0 ? 0 : -1;
}
static int fs_fsck(int fix, int *pr, int *fi) {
    (void)fix;
    if (pr) *pr = 0;
    if (fi) *fi = 0;
    return 0;
}
static asi_fs_t s_fs = {
    fs_root_fn, fs_create, fs_find, fs_write, fs_read,
    fs_count, fs_name, fs_size, fs_remove,
    fs_mkdir, fs_create_in, fs_find_in, fs_read_path,
    fs_count_dir, fs_list_dir, fs_stat, fs_stat_handle,
    fs_chmod, fs_chown, fs_subject, fs_rename, fs_fsck,
};

/* ===================================================================== */
/* ag (yalnizca --allow-net; curl ile)                                   */
/* ===================================================================== */

static int net_present(void) { return g_allow_net ? 1 : 0; }
static int net_send(const void *d, uint32_t n) { (void)d; (void)n; return -1; }
static int net_recv(void *b, uint32_t m) { (void)b; (void)m; return -1; }
static int net_has(void) { return 0; }
static void net_mac(uint8_t o[6]) { memset(o, 0, 6); }
static int  net_link_type(void) { return g_allow_net ? ASI_LINK_ETHERNET : ASI_LINK_NONE; }
static int  net_link_up(void) { return g_allow_net; }
static void net_ip(uint8_t o[4]) { o[0] = 10; o[1] = 0; o[2] = 0; o[3] = 5; }
static void net_mask(uint8_t o[4]) { o[0] = 255; o[1] = 255; o[2] = 255; o[3] = 0; }
static void net_gw(uint8_t o[4]) { o[0] = 10; o[1] = 0; o[2] = 0; o[3] = 1; }

static int url_safe(const char *u) {
    if (!u) return 0;
    int n = 0;
    for (const char *s = u; *s; s++, n++) {
        int ok = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
                 (*s >= '0' && *s <= '9') ||
                 strchr(":/?&=%._-+", *s) != 0;
        if (!ok || n > 1000) return 0;
    }
    return strncmp(u, "http://", 7) == 0 || strncmp(u, "https://", 8) == 0;
}

static int net_download(const char *url, void *dst, uint32_t cap,
                        uint32_t *len_out) {
    if (len_out) *len_out = 0;
    if (!g_allow_net || !url_safe(url) || !dst || !cap) return -1;
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s/emu-dl.tmp", g_root);
    char cmd[2200];
    snprintf(cmd, sizeof cmd, "curl -sL --max-time 60 -o \"%s\" \"%s\"",
             tmp, url);
    int rc = system(cmd);
    if (rc != 0) { remove(tmp); return -1; }
    FILE *f = fopen(tmp, "rb");
    if (!f) return -1;
    size_t n = fread(dst, 1, cap, f);
    fclose(f);
    remove(tmp);
    if (len_out) *len_out = (uint32_t)n;
    return 0;
}

/* NOT: adlandirma `s_net` OLAMAZ — winsock.h `#define s_net S_un.S_net`
 * makrosu yuzunden Windows derlemesinde cakisma cikarildi. */
static asi_net_t s_netab;
static void net_fill(void) {
    s_netab.present = net_present;
    s_netab.send = net_send;
    s_netab.recv = net_recv;
    s_netab.has = net_has;
    s_netab.mac = net_mac;
    s_netab.link_type = net_link_type;
    s_netab.link_up = net_link_up;
    s_netab.ip = net_ip;
    s_netab.netmask = net_mask;
    s_netab.gateway = net_gw;
    s_netab.download = net_download;
}

/* ===================================================================== */
/* ses simulasyonu (WAV sureci sanal sacta ilerler; gercek cikti yok)    */
/* ===================================================================== */

static int      s_aud_vol = 80;
static uint64_t s_aud_end;        /* plat_ticks_ms tabanlı bitis ani */
static uint32_t s_aud_rate = 44100;

static uint32_t rd32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16le(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* WAV basligindan sure (ms) ve ornek hizini cikar; -1 gecersiz. */
static long wav_duration_ms(const uint8_t *d, uint32_t len) {
    if (len < 44) return -1;
    if (!(d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' &&
          d[8] == 'W' && d[9] == 'A' && d[10] == 'V' && d[11] == 'E'))
        return -1;
    uint32_t off = 12, rate = 44100, bytes_per_sec = 0, data_sz = 0;
    int have_fmt = 0;
    while (off + 8 <= len) {
        const uint8_t *id = d + off;
        uint32_t sz = rd32le(d + off + 4), body = off + 8;
        if (id[0] == 'f' && id[1] == 'm' && id[2] == 't' && id[3] == ' ') {
            if (body + 16 <= len) {
                uint16_t ch = rd16le(d + body + 2), bits = rd16le(d + body + 14);
                rate = rd32le(d + body + 4);
                if (rate == 0) rate = 44100;
                if (ch && bits) bytes_per_sec = rate * ch * (uint32_t)bits / 8u;
                have_fmt = 1;
            }
        } else if (id[0] == 'd' && id[1] == 'a' && id[2] == 't' && id[3] == 'a') {
            data_sz = sz;
            if (data_sz > len - body) data_sz = len - body;
            break;
        }
        uint32_t adv = sz + (sz & 1u);
        if (adv < 8) break;
        off = body + adv;
    }
    if (!have_fmt || !bytes_per_sec || !data_sz) return -1;
    return (long)((uint64_t)data_sz * 1000ull / bytes_per_sec);
}

static int  aud_present(void) { return 1; }   /* her zaman sanal surucu */
static int  aud_play_wav(const void *data, uint32_t len) {
    long ms = wav_duration_ms((const uint8_t *)data, len);
    if (ms < 0) return -1;
    s_aud_end = (uint64_t)plat_ticks_ms() + (uint64_t)ms;
    return 0;
}
static void aud_stop(void) { s_aud_end = 0; }
static int  aud_is_playing(void) {
    return (uint64_t)plat_ticks_ms() < s_aud_end;
}
static int  aud_set_volume(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_aud_vol = pct;
    return pct;
}
static int         aud_volume(void) { return s_aud_vol; }
static void        aud_service(void) { }
static const char *aud_last_error(void) { return ""; }
static uint32_t    aud_sample_rate(void) { return s_aud_rate; }

static asi_audio_t s_audio = {
    aud_present, aud_play_wav, aud_stop, aud_is_playing,
    aud_set_volume, aud_volume, aud_service, aud_last_error,
    aud_sample_rate
};

/* ===================================================================== */
/* asi_t                                                                 */
/* ===================================================================== */

asi_t g_asi_store;
asi_t *g_asi = &g_asi_store;

void asi_attach(void) {
    net_fill();
    g_asi_store.version = ASI_VERSION;
    g_asi_store.console = &s_console;
    g_asi_store.mem = &s_mem;
    g_asi_store.time = &s_time;
    g_asi_store.gfx = &s_gfx;
    g_asi_store.input = &s_input;
    g_asi_store.system = &s_sys;
    g_asi_store.fs = &s_fs;
    g_asi_store.net = &s_netab;
    g_asi_store.audio = &s_audio;
}

void asi_gfx_size(int w, int h) {
    if (w > 0) g_w = w;
    if (h > 0) g_h = h;
}

/* ===================================================================== */
/* kabuk stublari (wlf_host.c'in bekledikleri)                           */
/* ===================================================================== */

static winrec_t s_winrec;

winrec_t *winrec_of(lv_obj_t *body) {
    if (!body) return 0;
    if (!s_winrec.body) s_winrec.body = body;
    return s_winrec.body == body ? &s_winrec : 0;
}

void update_app_indicators(void) {
    if (s_winrec.title[0]) plat_set_title(s_winrec.title);
}

int perm_policy(const char *path, int is_write) {
    (void)path; (void)is_write;
    return 0;               /* emu kumu guvenli: pano/dosya serbest */
}
int perm_exec(const char *cmd) {
    (void)cmd;
    if (g_allow_exec) return 0;
    fprintf(stderr, "[emu] exec reddedildi (--allow-exec ile acin)\n");
    return 1;
}
int perm_net(void) {
    if (g_allow_net) return 0;
    fprintf(stderr, "[emu] ag reddedildi (--allow-net ile acin)\n");
    return 1;
}

void bin_copy(char *dst, int cap, const char *src) {
    if (!dst || cap <= 0) return;
    int i = 0;
    for (; src && src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

int slen2(const char *s) {
    int n = 0;
    for (; s && s[n]; n++) { }
    return n;
}

/* OS: dsk_util.c — canvas silinince pixmap tamponunu serbest birakir. */
void canvas_buf_free_cb(lv_event_t *e) {
    void *p = lv_event_get_user_data(e);
    if (p && g_asi && g_asi->mem && g_asi->mem->free) g_asi->mem->free(p);
}

void arctian_notify(const char *title, const char *msg) {
    printf("[bildirim] %s: %s\n", title ? title : "", msg ? msg : "");
    fflush(stdout);
}

const char *arctian_auth_current(void) { return "emu@arctian"; }

int arctian_shell_launch(const char *q) {
    printf("[emu] uygulama acma istegi reddedildi: %s\n", q ? q : "");
    return -1;
}

static char s_clip[8192];
void arctian_clipboard_set(const char *text) {
    bin_copy(s_clip, sizeof s_clip, text ? text : "");
}
const char *arctian_clipboard_get(void) { return s_clip; }

/* ===================================================================== */
/* host kurulumu                                                         */
/* ===================================================================== */

void host_setup(const emu_cfg_t *cfg) {
    asi_fs_root(cfg->fs_root);
    asi_flags(cfg->allow_net, cfg->allow_exec);
    asi_attach();
    adl_set_light(cfg->light_theme);
    wlf_host_set_arg(cfg->arg ? cfg->arg : "");
}
