/* Arctian Terminal */
#include "app.h"
#include "dsk.h"
#include "libwolf_doc_pdf.h"
#include "lv_port.h"
#include "adl_wolf.h"
#include "wolf_engine.h"
#include "version.h"        /* surum: version.json -> ARCTIAN_VERSION */
#include <stdint.h>

#define TERM_LOG_MAX 20000
#define TERM_TAB_H   34
#define TERM_BG      0x0C0C0C
#define TERM_BAR_BG  0x0C0C0C
#define TERM_TAB_BG  0x2B2B2B
#define TERM_FG      0xCCCCCC
#define TERM_PROMPT  0xE6E6E6
#define TERM_STORE_URL "http://45.143.99.75:8090"

static char  g_log[TERM_LOG_MAX];
static int   g_log_len;

static lv_obj_t   *g_scroll;
static lv_obj_t   *g_out_label;
static lv_obj_t   *g_input;
static lv_obj_t   *g_menu;
static lv_group_t *g_group;

#define SH_ENV_MAX   16
#define SH_HIST_MAX  32
#define SH_BUF_MAX   8192

static char g_cap[TERM_LOG_MAX];
static int  g_cap_len;
static int  g_cap_on;

static const char *g_stdin;
static int  g_stdin_len;

static char g_env_k[SH_ENV_MAX][24];
static char g_env_v[SH_ENV_MAX][96];
static int  g_env_n;

static char g_hist[SH_HIST_MAX][256];
static int  g_hist_n;
static int  g_hist_pos;

static char g_fbuf[SH_BUF_MAX];

static void term_putc(char c) {
    if (g_cap_on) {
        if (g_cap_len < TERM_LOG_MAX - 1) g_cap[g_cap_len++] = c;
        return;
    }
    if (g_log_len >= TERM_LOG_MAX - 1) return;
    g_log[g_log_len++] = c;
}

static void term_puts(const char *s) {
    if (!s) return;
    while (*s) term_putc(*s++);
}

static void term_put_uint(uint32_t v) {
    char b[12];
    int i = 0;
    if (v == 0) { term_putc('0'); return; }
    while (v && i < 11) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i) term_putc(b[--i]);
}

static void term_put_ip(const uint8_t ip[4]) {
    for (int i = 0; i < 4; i++) {
        term_put_uint(ip[i]);
        if (i < 3) term_putc('.');
    }
}

static void term_flush(void) {
    g_log[g_log_len] = 0;
    if (g_out_label) lv_label_set_text(g_out_label, g_log);
    if (g_scroll) {
        lv_obj_update_layout(g_scroll);
        lv_obj_scroll_to_y(g_scroll, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static int starts_with(const char *s, const char *p) {
    while (*p) { if (*s != *p) return 0; s++; p++; }
    return 1;
}

/* on ek eslesmesi (bos on ek her zaman eslesir) */
static int str_prefix(const char *s, const char *p) { return starts_with(s, p); }

static void req_append(char *buf, int *n, const char *s) {
    while (*s && *n < 580) buf[(*n)++] = *s++;
}

static void cmd_update(asi_t *asi, const char *arg) {
    (void)asi;
    int apply = 0;
    if (arg && arg[0]) {
        if (str_eq(arg, "kur") || str_eq(arg, "install") || str_eq(arg, "yukle")) apply = 1;
        else if (str_eq(arg, "kontrol") || str_eq(arg, "check")) apply = 0;
        else { term_puts("Kullanim: update [kontrol|kur]\n"); return; }
    }
    term_puts("Guncelleme sunucusu: "); term_puts(update_base_url()); term_putc('\n');
    term_puts("Mevcut surum: "); term_puts(update_applied_version());
    term_puts("  (derleme "); term_puts(update_current_version()); term_puts(")\n");
    term_puts("Baglaniyor...\n"); term_flush();

    int rc = update_check();
    const update_status_t *st = update_status();
    term_puts("Kontrol: "); term_puts(st->message); term_putc('\n');
    if (rc < 0) { term_flush(); return; }
    if (rc == 0) { term_puts("Sistem guncel.\n"); term_flush(); return; }

    term_puts("Sunucudaki surum: "); term_puts(st->version);
    term_puts("  modul: "); term_put_uint((uint32_t)update_manifest()->count); term_putc('\n');
    if (!apply) { term_puts("Kurmak icin: update kur\n"); term_flush(); return; }

    term_puts("Indiriliyor ve kuruluyor...\n"); term_flush();
    rc = update_apply();
    term_puts("Sonuc: "); term_puts(update_status()->message); term_putc('\n');
    if (rc == 0) {
        term_puts("Yeni surum: "); term_puts(update_applied_version());
        term_puts("  kurulu modul: "); term_put_uint((uint32_t)arctian_store->count_installed()); term_putc('\n');
    }
    term_flush();
}

static void cmd_curl(asi_t *asi, const char *url) {
    if (!asi || !asi->net || !asi->net->dns_resolve || !asi->net->tcp_connect) {
        term_puts("Ag servisi yok\n");
        return;
    }
    if (!url || !url[0]) { term_puts("Kullanim: curl http://site/path\n"); return; }

    if (starts_with(url, "https://")) {
        if (!asi->net->https_get) { term_puts("HTTPS API yok\n"); term_flush(); return; }
        term_puts("> TLS 1.3 GET "); term_puts(url); term_putc('\n'); term_flush();
        uint32_t hlen = 0;
        const char *hb = asi->net->https_get(url, &hlen);
        if (!hb) { term_puts("HTTPS istegi basarisiz (TLS/sertifika)\n"); term_flush(); return; }
        for (uint32_t i = 0; i < hlen && g_log_len < TERM_LOG_MAX - 2; i++) {
            char ch = hb[i];
            if (ch == '\r') continue;
            term_putc(ch);
        }
        term_putc('\n');
        term_flush();
        return;
    }

    const char *p = url;
    if (starts_with(p, "http://")) p += 7;

    char host[128];
    int hl = 0;
    while (*p && *p != '/' && *p != ':' && hl < 127) host[hl++] = *p++;
    host[hl] = 0;
    if (hl == 0) { term_puts("Gecersiz adres\n"); return; }

    uint16_t port = 80;
    if (*p == ':') {
        p++;
        uint32_t pv = 0;
        while (*p >= '0' && *p <= '9') { pv = pv * 10u + (uint32_t)(*p - '0'); p++; }
        if (pv > 0 && pv < 65536u) port = (uint16_t)pv;
    }

    char path[256];
    int pl = 0;
    if (*p == '/') { while (*p && pl < 255) path[pl++] = *p++; }
    else path[pl++] = '/';
    path[pl] = 0;

    term_puts("> GET "); term_puts(host);
    term_putc(':'); term_put_uint(port); term_puts(path); term_putc('\n');
    term_flush();

    uint8_t ip[4];
    if (asi->net->dns_resolve(host, ip) != 0) {
        term_puts("DNS cozulemedi: "); term_puts(host); term_putc('\n');
        term_flush();
        return;
    }
    term_puts("IP: "); term_put_ip(ip); term_putc('\n'); term_flush();

    int c = asi->net->tcp_connect(ip, port);
    if (c < 0) {
        term_puts("TCP baglanti kurulamadi\n"); term_flush(); return;
    }

    char req[600];
    int r = 0;
    req_append(req, &r, "GET ");  req_append(req, &r, path);
    req_append(req, &r, " HTTP/1.0\r\n");
    req_append(req, &r, "Host: "); req_append(req, &r, host); req_append(req, &r, "\r\n");
    req_append(req, &r, "User-Agent: Arctian/0.6\r\n");
    req_append(req, &r, "Accept: */*\r\nConnection: close\r\n\r\n");

    if (asi->net->tcp_send && asi->net->tcp_send(c, req, (uint32_t)r) != 0) {
        term_puts("Istek gonderilemedi\n"); term_flush();
        if (asi->net->tcp_close) asi->net->tcp_close(c);
        return;
    }

    int total = 0;
    for (int iter = 0; iter < 6000; iter++) {
        uint8_t buf[1024];
        int n = 0;
        if (asi->net->tcp_recv) n = asi->net->tcp_recv(c, buf, (uint32_t)sizeof(buf));
        if (n < 0) break;
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                char ch = (char)buf[i];
                if (ch == '\r') continue;
                term_putc(ch);
            }
            total += n;
            iter = 0;
            if (total >= 14000) { term_puts("\n... (kisaltildi)\n"); break; }
        }
        int st = asi->net->tcp_state ? asi->net->tcp_state(c) : -1;
        if (st == 8 || st == 0) break;
    }
    if (asi->net->tcp_close) asi->net->tcp_close(c);

    if (total == 0) term_puts("\n(Yanit alinamadi)\n");
    else term_putc('\n');
    term_flush();
}

static char s_bg_url[256];
static const char *s_bg_body;
static uint32_t s_bg_len;
static int s_bg_https;
static volatile int s_bg_running;

static void term_bg_work(void *user) {
    (void)user;
    if (!g_asi || !g_asi->net) return;
    if (s_bg_https) s_bg_body = g_asi->net->https_get(s_bg_url, &s_bg_len);
    else            s_bg_body = g_asi->net->http_get(s_bg_url, &s_bg_len);
}

static void term_bg_done(int status, void *user) {
    (void)status; (void)user;
    s_bg_running = 0;
    if (!s_bg_body) { term_puts("[bg] indirme basarisiz\n"); term_flush(); return; }
    term_puts("[bg] indirme tamam: "); term_put_uint(s_bg_len); term_puts(" bayt\n");

    /* Govdeyi diske yaz: onceden yalniz uzunluk yaziliyordu, veri
     * kayboluyordu. Dosya adi URL'nin son parcasi, yoksa bgfetch.bin. */
    if (g_asi && g_asi->fs && g_asi->fs->create_in && g_asi->fs->write) {
        const char *base = s_bg_url;
        for (const char *p = s_bg_url; *p; p++) if (*p == '/') base = p + 1;
        char nm[64];
        int n = 0;
        for (int i = 0; base[i] && base[i] != '?' && n < 48; i++) {
            char c = base[i];
            int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
            nm[n++] = ok ? c : '_';
        }
        if (n == 0 || nm[0] == '_') { const char *d = "bgfetch.bin"; n = 0; for (; d[n]; n++) nm[n] = d[n]; }
        nm[n] = 0;
        if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/indirilenler");
        void *h = g_asi->fs->create_in("C:/indirilenler", nm, 0);
        if (h && g_asi->fs->write(h, s_bg_body, s_bg_len) == 0) {
            term_puts("[bg] kaydedildi: C:/indirilenler/");
            term_puts(nm);
            term_putc('\n');
        } else {
            term_puts("[bg] kaydedilemedi\n");
        }
    }
    term_flush();
}

static void cmd_bgfetch(asi_t *asi, const char *url) {
    if (!asi || !asi->net || !asi->net->submit) { term_puts("Arka plan ag servisi yok\n"); return; }
    if (!url || !url[0]) url = TERM_STORE_URL;
    if (s_bg_running || (asi->net->busy && asi->net->busy())) { term_puts("Arka plan mesgul\n"); return; }
    int i = 0;
    for (; url[i] && i < (int)sizeof(s_bg_url) - 1; i++) s_bg_url[i] = url[i];
    s_bg_url[i] = 0;
    s_bg_https = starts_with(s_bg_url, "https://") ? 1 : 0;
    s_bg_body = 0;
    s_bg_len = 0;
    s_bg_running = 1;
    if (asi->net->submit(term_bg_work, term_bg_done, 0) != 0) {
        s_bg_running = 0;
        term_puts("Arka plan isi baslatilamadi\n");
        return;
    }
    term_puts("[bg] indirme arka planda; arayuz kullanilabilir\n");
}

static void cmd_ip(asi_t *asi) {
    if (!asi || !asi->net) { term_puts("Ag servisi yok\n"); return; }
    uint8_t a[4];
    int lt = asi->net->link_type ? asi->net->link_type() : 0;
    term_puts("Baglanti: ");
    term_puts(lt == ASI_LINK_ETHERNET ? "Ethernet" :
              lt == ASI_LINK_WIFI ? "Wi-Fi" : "yok");
    term_putc('\n');
    if (asi->net->ip)      { asi->net->ip(a);      term_puts("IP    : "); term_put_ip(a); term_putc('\n'); }
    if (asi->net->netmask) { asi->net->netmask(a); term_puts("Maske : "); term_put_ip(a); term_putc('\n'); }
    if (asi->net->gateway) { asi->net->gateway(a); term_puts("GW    : "); term_put_ip(a); term_putc('\n'); }
    if (asi->net->dns)     { asi->net->dns(a);     term_puts("DNS   : "); term_put_ip(a); term_putc('\n'); }
}

static void cmd_dns(asi_t *asi, const char *host) {
    if (!asi || !asi->net || !asi->net->dns_resolve) { term_puts("Ag servisi yok\n"); return; }
    if (!host || !host[0]) { term_puts("Kullanim: dns <host>\n"); return; }
    uint8_t ip[4];
    if (asi->net->dns_resolve(host, ip) == 0) {
        term_puts(host); term_puts(" -> "); term_put_ip(ip); term_putc('\n');
    } else {
        term_puts("Cozulemedi: "); term_puts(host); term_putc('\n');
    }
}

static int slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
/* 0.6 yardimcilari: terminal araclari (find/tree) icin */
static int slen_t(const char *s) { return slen(s); }
static int str_contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !needle[0]) return 0;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && hay[i + j]) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            j++;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static void term_put2(int v) {
    term_putc((char)('0' + (v / 10) % 10));
    term_putc((char)('0' + v % 10));
}

static void term_put_size(uint32_t b) {
    if (b < 1024) { term_put_uint(b); term_puts(" B"); }
    else if (b < 1024u * 1024u) { term_put_uint(b / 1024); term_puts(" KB"); }
    else { term_put_uint(b / (1024u * 1024u)); term_puts(" MB"); }
}

static int key_is(const char *k, int kl, const char *lit) {
    int i = 0;
    while (lit[i]) { if (i >= kl || k[i] != lit[i]) return 0; i++; }
    return i == kl;
}

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void pct_dec(char *out, int cap, const char *s, int n) {
    int o = 0;
    for (int i = 0; i < n && o < cap - 1; i++) {
        if (s[i] == '%' && i + 2 < n) {
            int h = hexv(s[i + 1]), l = hexv(s[i + 2]);
            if (h >= 0 && l >= 0) { out[o++] = (char)(h * 16 + l); i += 2; continue; }
        }
        out[o++] = s[i];
    }
    out[o] = 0;
}

static int eq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static void cmd_apps(void) {
    int n = arctian_shell_app_count();
    if (n <= 0) { term_puts("Kayitli uygulama yok\n"); return; }
    term_puts("Uygulamalar:\n");
    for (int i = 0; i < n; i++) {
        char title[48], name[32], cat[32];
        if (arctian_shell_app_info(i, title, sizeof(title), name, sizeof(name), cat, sizeof(cat)) != 0) continue;
        term_puts("  "); term_puts(name);
        term_puts("  "); term_puts(title);
        if (cat[0]) { term_puts("  ["); term_puts(cat); term_puts("]"); }
        term_putc('\n');
    }
}

static void cmd_run(const char *q) {
    if (!q[0]) { term_puts("Kullanim: run <uygulama>\n"); return; }
    if (arctian_shell_launch(q) == 0) { term_puts("Baslatildi: "); term_puts(q); term_putc('\n'); }
    else { term_puts("Uygulama bulunamadi: "); term_puts(q); term_puts("  ('apps' yazin)\n"); }
}

static asi_t *g_proc_asi;

static const char *proc_state_name(int s) {
    switch (s) {
        case 1: return "READY";
        case 2: return "CALIS";
        case 3: return "BEKLE";
        case 4: return "ZOMBI";
        default: return "?";
    }
}

static void proc_demo_worker(void *arg) {
    int id = (int)(intptr_t)arg;
    asi_t *a = g_proc_asi;
    if (a && a->console && a->console->printf && a->proc)
        a->console->printf("[term/proc] isci-%d calisti (pid=%d)\n", id, a->proc->getpid());
    for (volatile int i = 0; i < 400000; i++) __asm__ volatile("pause");
    if (a && a->proc) a->proc->exit(id * 3);
}

static void cmd_proctest(asi_t *asi) {
    if (!asi || !asi->proc) { term_puts("Surec servisi yok\n"); return; }
    g_proc_asi = asi;
    term_puts("Surec testi: iki isci surec olusturuluyor...\n");
    int p1 = asi->proc->spawn("isci-1", proc_demo_worker, (void *)(intptr_t)1);
    int p2 = asi->proc->spawn("isci-2", proc_demo_worker, (void *)(intptr_t)2);
    if (p1 < 0 || p2 < 0) { term_puts("Spawn basarisiz\n"); return; }
    term_puts("Bekleniyor: pid "); term_put_uint((uint32_t)p1);
    term_puts(", "); term_put_uint((uint32_t)p2); term_putc('\n');
    int c1 = -1, c2 = -1;
    asi->proc->wait(p1, &c1);
    asi->proc->wait(p2, &c2);
    term_puts("Toplandi: pid "); term_put_uint((uint32_t)p1);
    term_puts(" kod="); term_put_uint((uint32_t)c1);
    term_puts(", pid "); term_put_uint((uint32_t)p2);
    term_puts(" kod="); term_put_uint((uint32_t)c2); term_putc('\n');
    term_puts("Zombie toplama tamam (exit temiz kapandi).\n");
}

static void cmd_proc(asi_t *asi) {
    if (!asi || !asi->proc) { term_puts("Surec servisi yok\n"); return; }
    int n = asi->proc->count();
    if (n <= 0) { term_puts("Surec yok\n"); return; }
    term_puts("PID  PPID  DURUM    AD\n");
    for (int i = 0; i < 16; i++) {
        int pid = 0, st = 0, ppid = 0, code = 0;
        char nm[32];
        if (asi->proc->info(i, &pid, nm, sizeof(nm), &st, &ppid, &code) != 0) continue;
        term_put_uint((uint32_t)pid); term_puts("   ");
        term_put_uint((uint32_t)ppid); term_puts("    ");
        term_puts(proc_state_name(st));
        term_puts("  "); term_puts(nm); term_putc('\n');
    }
}

static void cmd_procwait(asi_t *asi, const char *arg) {
    if (!asi || !asi->proc) { term_puts("Surec servisi yok\n"); return; }
    while (*arg == ' ') arg++;
    int pid = 0;
    while (*arg >= '0' && *arg <= '9') { pid = pid * 10 + (*arg - '0'); arg++; }
    if (pid <= 0) { term_puts("Kullanim: wait <pid>\n"); return; }
    int code = -1;
    int r = asi->proc->wait(pid, &code);
    if (r < 0) { term_puts("Boyle bir surec yok\n"); return; }
    term_puts("Toplandi pid="); term_put_uint((uint32_t)r);
    term_puts(" kod="); term_put_uint((uint32_t)code); term_putc('\n');
}

static void cmd_procreap(asi_t *asi) {
    if (!asi || !asi->proc) { term_puts("Surec servisi yok\n"); return; }
    int code = 0, n = 0;
    while (asi->proc->reap(&code) > 0) n++;
    if (n) { term_puts("Toplanan zombie: "); term_put_uint((uint32_t)n); term_putc('\n'); }
    else term_puts("Toplanacak zombie yok\n");
}

static void cmd_labtest(asi_t *asi) {
    if (!asi || !asi->lab) { term_puts("Lab servisi yok\n"); return; }
    term_puts("sync + thread + ipc selftest calisiyor...\n");
    int r = asi->lab->selftest();
    term_puts(r == 0 ? "Sonuc: OK\n" : "Sonuc: HATA\n");
}

static void cmd_threads(asi_t *asi) {
    if (!asi || !asi->lab) { term_puts("Lab servisi yok\n"); return; }
    term_puts("Aktif thread: "); term_put_uint((uint32_t)asi->lab->thread_count());
    term_puts("  shm bolge: "); term_put_uint((uint32_t)asi->lab->shm_count());
    term_putc('\n');
}

static void cmd_close(const char *q) {
    if (!q[0]) { term_puts("Kullanim: close <uygulama>\n"); return; }
    if (arctian_shell_close(q) == 0) { term_puts("Kapatildi: "); term_puts(q); term_putc('\n'); }
    else { term_puts("Acik pencere yok: "); term_puts(q); term_putc('\n'); }
}

static void cmd_ps(void) {
    int n = arctian_shell_window_count();
    if (n <= 0) { term_puts("Acik pencere yok\n"); return; }
    term_puts("Acik pencereler:\n");
    for (int i = 0; i < n; i++) {
        char t[40]; int vis = 0;
        if (arctian_shell_window_info(i, t, sizeof(t), &vis) != 0) continue;
        term_puts("  "); term_puts(t);
        if (!vis) term_puts("  (gizli)");
        term_putc('\n');
    }
}

/* theme [light|dark|toggle] — acik/koyu temayi terminalden degistirir.
 * Ayar settings.cfg'ye yazilir ve kabuk yeniden kurulur (0.6). */
static void cmd_theme(const char *arg) {
    if (!arctian_settings || !arctian_settings->set_int) {
        term_puts("Ayar servisi yok\n"); return;
    }
    int cur = arctian_settings->get_int ? arctian_settings->get_int("light", 0) : 0;
    int on;
    if (str_eq(arg, "light") || str_eq(arg, "acik")) on = 1;
    else if (str_eq(arg, "dark") || str_eq(arg, "koyu")) on = 0;
    else if (!arg[0] || str_eq(arg, "toggle")) on = cur ? 0 : 1;
    else { term_puts("Kullanim: theme [light|dark|toggle]\n"); return; }

    arctian_settings->set_int("light", on);
    term_puts(on ? "Acik tema etkin\n" : "Koyu tema etkin\n");
}

static void cmd_wallpaper(const char *arg) {
    if (!arctian_store) { term_puts("Arka plan servisi yok\n"); return; }
    if (!arg[0] || str_eq(arg, "list")) {
        const char *active = (arctian_store->wallpaper_active && arctian_store->wallpaper_active())
                             ? arctian_store->wallpaper_name() : "";
        term_puts("Arka planlar (.wp):\n");
        if (!g_asi || !g_asi->fs || !g_asi->fs->count) { term_puts("  (dosya sistemi yok)\n"); return; }
        int n = g_asi->fs->count();
        int found = 0;
        for (int i = 0; i < n; i++) {
            const char *nm = g_asi->fs->name(i);
            int L = slen(nm);
            if (L <= 3 || nm[L - 3] != '.' || nm[L - 2] != 'w' || nm[L - 1] != 'p') continue;
            found = 1;
            term_puts("  "); term_puts(nm);
            if (active[0] && str_eq(active, nm)) term_puts("  *");
            term_putc('\n');
        }
        if (!found) term_puts("  (goruntu dosyasi yok)\n");
        if (active[0]) { term_puts("Aktif: "); term_puts(active); term_putc('\n'); }
        else term_puts("Aktif: gomulu varsayilan\n");
        return;
    }
    if (starts_with(arg, "set ")) {
        const char *f = arg + 4; while (*f == ' ') f++;
        if (!f[0]) { term_puts("Kullanim: wallpaper set <dosya.wp>\n"); return; }
        int r = arctian_store->set_wallpaper ? arctian_store->set_wallpaper(f) : -99;
        if (r == 0) { term_puts("Arka plan ayarlandi: "); term_puts(f); term_putc('\n'); }
        else if (r == -1) term_puts("Dosya bulunamadi\n");
        else term_puts("Arka plan yuklenemedi (bozuk gorsel)\n");
        return;
    }
    if (str_eq(arg, "reset")) {
        int r = arctian_store->set_wallpaper ? arctian_store->set_wallpaper("") : -99;
        term_puts(r == 0 ? "Varsayilan arka plana donuldu\n" : "Sifirlanamadi\n");
        return;
    }
    term_puts("Kullanim: wallpaper [list | set <dosya.wp> | reset]\n");
}

static char g_cwd[176];
static int cstr_copy(char *dst, int cap, const char *src);
static void path_normalize(char *s);
static void path_resolve(char *dst, int cap, const char *name);
static void path_resolve_dir(char *dst, int cap, const char *name);
static int fs_dir_exists(const char *path);
static int path_is_dir(const char *path);

static void cmd_ls(const char *arg) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->count) { term_puts("Dosya sistemi yok\n"); return; }
    /* `ls <klasor>`: verilen klasoru listeler.      */
    char dir[176];
    if (arg && arg[0]) {
        path_resolve(dir, sizeof(dir), arg);
        if (g_asi->fs->count_dir && g_asi->fs->list_dir) {
            int dn = g_asi->fs->count_dir(dir);
            if (dn < 0) { term_puts("Klasor yok: "); term_puts(dir); term_putc('\n'); return; }
            if (dn == 0) { term_puts("(bos)\n"); return; }
            for (int i = 0; i < dn; i++) {
                char nm[128]; int isd = 0;
                if (g_asi->fs->list_dir(dir, i, nm, sizeof(nm), &isd) != 0) continue;
                term_puts(isd ? "  [D] " : "  [F] ");
                term_puts(nm);
                term_putc('\n');
            }
            return;
        }
    }

    if (!str_eq(g_cwd, "C:/") && g_asi->fs->count_dir && g_asi->fs->list_dir) {
        int dn = g_asi->fs->count_dir(g_cwd);
        if (dn <= 0) { term_puts("(bos)\n"); return; }
        for (int i = 0; i < dn; i++) {
            char nm[128]; int isd = 0;
            if (g_asi->fs->list_dir(g_cwd, i, nm, sizeof(nm), &isd) != 0) continue;
            term_puts(isd ? "  [D] " : "  [F] ");
            term_puts(nm);
            term_putc('\n');
        }
        return;
    }
    int n = g_asi->fs->count();
    if (n <= 0) { term_puts("(bos)\n"); return; }
    uint32_t total = 0;
    for (int i = 0; i < n; i++) {
        const char *nm = g_asi->fs->name(i);
        uint32_t sz = g_asi->fs->size ? g_asi->fs->size(i) : 0;
        total += sz;
        term_puts("  "); term_puts(nm);
        term_puts("  "); term_put_size(sz); term_putc('\n');
    }
    term_puts("Toplam "); term_put_uint((uint32_t)n); term_puts(" oge, "); term_put_size(total); term_putc('\n');
}

static void cmd_cat(const char *name) {
    if (!name[0]) { term_puts("Kullanim: cat <dosya>\n"); return; }
    if (!g_asi || !g_asi->fs || !g_asi->fs->find || !g_asi->fs->read) { term_puts("Dosya sistemi yok\n"); return; }
    char p[176];
    path_resolve(p, sizeof(p), name);
    void *h = g_asi->fs->find(0, p);
    if (!h) h = g_asi->fs->find(0, name);
    if (!h) { term_puts("Bulunamadi: "); term_puts(p); term_putc('\n'); return; }
    char buf[1025];
    uint32_t r = g_asi->fs->read(h, buf, sizeof(buf) - 1);
    if (r == 0) { term_puts("(bos dosya)\n"); return; }
    buf[r] = 0;
    for (uint32_t i = 0; i < r; i++) {
        if (buf[i] == '\r') continue;
        term_putc(buf[i]);
    }
    if (buf[r - 1] != '\n') term_putc('\n');
}

static void cmd_rm(const char *name) {
    if (!name[0]) { term_puts("Kullanim: rm <dosya>\n"); return; }
    if (!g_asi || !g_asi->fs || !g_asi->fs->remove) { term_puts("Desteklenmiyor\n"); return; }
    if (g_asi->fs->find && !g_asi->fs->find(0, name)) { term_puts("Bulunamadi: "); term_puts(name); term_putc('\n'); return; }
    g_asi->fs->remove(name);
    term_puts("Silindi: "); term_puts(name); term_putc('\n');
}

/* mv <kaynak> <hedef> — AFS'de yerel rename yok; kopyala + sil ile yapilir.
 * Klasor hedefi verilirse dosya adi korunur ("mv a.txt Belgeler"). */
static void cmd_mv(const char *args) {
    if (!g_asi || !g_asi->fs) { term_puts("Dosya sistemi yok\n"); return; }
    asi_fs_t *fs = g_asi->fs;
    if (!fs->read_path || !fs->create_in || !fs->write || !fs->remove) {
        term_puts("Desteklenmiyor\n"); return;
    }
    char src[176], dst[176];
    int i = 0, n = 0;
    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ' && n < (int)sizeof(src) - 1) src[n++] = args[i++];
    src[n] = 0;
    while (args[i] == ' ') i++;
    n = 0;
    while (args[i] && args[i] != ' ' && n < (int)sizeof(dst) - 1) dst[n++] = args[i++];
    dst[n] = 0;
    if (!src[0] || !dst[0]) { term_puts("Kullanim: mv <kaynak> <hedef>\n"); return; }

    char sp[176], dp[176];
    path_resolve(sp, sizeof(sp), src);
    path_resolve(dp, sizeof(dp), dst);

    /* hedef klasor ise dosya adini ekle */
    if (path_is_dir(dp)) {
        const char *base = sp;
        for (const char *p = sp; *p; p++) if (*p == '/') base = p + 1;
        int k = 0;
        while (dp[k]) k++;
        if (k && dp[k - 1] != '/') { dp[k++] = '/'; dp[k] = 0; }
        for (int j = 0; base[j] && k < (int)sizeof(dp) - 1; j++) dp[k++] = base[j];
        dp[k] = 0;
    }

    static uint8_t *buf;
    const uint32_t cap = 512u * 1024u;

    /* Yerel rename varsa once onu dene: veri kopyalanmaz, buyuk dosyada da
     * aninda biter (0.6). */
    if (fs->rename && fs->rename(sp, dp) == 0) {
        term_puts("Tasindi: "); term_puts(sp); term_puts(" -> "); term_puts(dp); term_putc('\n');
        return;
    }

    if (!buf) buf = (uint8_t *)g_asi->mem->alloc(cap);
    if (!buf) { term_puts("Bellek yetersiz\n"); return; }
    uint32_t sz = fs->read_path(sp, buf, cap);
    if (sz == 0 && !fs->find_in && !fs->find) { term_puts("Kaynak okunamadi\n"); return; }

    /* hedef klasor (varsa) */
    char dir[176];
    cstr_copy(dir, sizeof(dir), dp);
    for (int k = (int)sizeof(dir) - 1; k >= 0 && dir[k]; k--) { }
    int last = -1;
    for (int k = 0; dir[k]; k++) if (dir[k] == '/') last = k;
    const char *fname = dp;
    if (last >= 0) { dir[last] = 0; fname = dp + last + 1; if (fs->mkdir) fs->mkdir(dir); }

    void *h = fs->create_in(last >= 0 ? dir : "C:/", fname, 0);
    if (!h) { term_puts("Hedef olusturulamadi\n"); return; }
    fs->write(h, buf, sz);
    fs->remove(sp);
    term_puts("Tasindi: "); term_puts(sp); term_puts(" -> "); term_puts(dp); term_putc('\n');
}

/* trash <dosya> — dosyayi Cop Kutusu'na tasir.                          */
static void cmd_trash(const char *name) {
    if (!name[0]) { term_puts("Kullanim: trash <dosya>\n"); return; }
    char p[176];
    path_resolve(p, sizeof(p), name);
    int r = arctian_shell_trash(p);
    if (r == 0) {
        term_puts("Cop Kutusu'na tasindi: "); term_puts(p); term_putc('\n');
    } else if (r == -2) {
        term_puts("Dosya cok buyuk (128 KB siniri); rm ile silin\n");
    } else {
        term_puts("Tasinamadi (dosya yok ya da FS hatasi)\n");
    }
}

/* touch <dosya> — bos dosya olustur (varsa dokunma). */
static void cmd_touch(const char *name) {
    if (!name[0]) { term_puts("Kullanim: touch <dosya>\n"); return; }
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in) { term_puts("Desteklenmiyor\n"); return; }
    char p[176];
    path_resolve(p, sizeof(p), name);
    if (g_asi->fs->find_in && g_asi->fs->find_in("C:/", p)) return;
    void *h = g_asi->fs->create_in("C:/", p, 0);
    if (!h) { term_puts("Olusturulamadi\n"); return; }
    term_puts("Olusturuldu: "); term_puts(p); term_putc('\n');
}

static void cmd_date(void) {
    if (!g_asi || !g_asi->time || !g_asi->time->now) { term_puts("Saat servisi yok\n"); return; }
    asi_datetime_t dt;
    if (!g_asi->time->now(&dt)) { term_puts("Saat okunamadi\n"); return; }
    term_put_uint((uint32_t)dt.year); term_putc('-'); term_put2(dt.month); term_putc('-'); term_put2(dt.day);
    term_putc(' '); term_put2(dt.hour); term_putc(':'); term_put2(dt.minute); term_putc(':'); term_put2(dt.second);
    term_putc('\n');
}

static void cmd_mem(void) {
    if (!g_asi || !g_asi->mem || !g_asi->mem->info) { term_puts("Bellek servisi yok\n"); return; }
    asi_meminfo_t m;
    m.total = m.used = m.free = m.heap_total = m.heap_used = 0;
    g_asi->mem->info(&m);
    term_puts("Fiziksel bellek (cekirdek cerceve havuzu):\n");
    term_puts("  toplam  : "); term_put_uint((uint32_t)(m.total / 1024u)); term_puts(" KB\n");
    term_puts("  kullanim: "); term_put_uint((uint32_t)(m.used / 1024u)); term_puts(" KB\n");
    term_puts("  bos     : "); term_put_uint((uint32_t)(m.free / 1024u)); term_puts(" KB\n");
    term_puts("Uygulama yigin alani (kmalloc):\n");
    term_puts("  toplam  : "); term_put_uint((uint32_t)(m.heap_total / 1048576u)); term_puts(" MB\n");
    term_puts("  kullanim: "); term_put_uint((uint32_t)(m.heap_used / 1048576u)); term_puts(" MB\n");
}

static void cmd_users(void) {
    int n = arctian_auth_user_count();
    if (n <= 0) { term_puts("Kayıtlı hesap yok.\n"); return; }
    const char *cur = arctian_auth_current();
    for (int i = 0; i < n; i++) {
        const char *nm = arctian_auth_user_name(i);
        term_puts((cur && str_eq(cur, nm)) ? "* " : "  ");
        term_puts(nm);
        term_puts("  uid="); term_put_uint(arctian_auth_user_uid(i));
        term_puts(arctian_auth_user_has_password(i) ? "  (parolalı)" : "  (parolasız)");
        term_puts("  ev="); term_puts(arctian_auth_home(nm));
        term_putc('\n');
    }
}

static void cmd_adduser(const char *arg) {
    char name[32], pw[64];
    int i = 0, a = 0, b = 0;
    while (arg[i] == ' ' || arg[i] == '\t') i++;
    while (arg[i] && arg[i] != ' ' && arg[i] != '\t' && a < 31) name[a++] = arg[i++];
    name[a] = 0;
    while (arg[i] == ' ' || arg[i] == '\t') i++;
    while (arg[i] && arg[i] != ' ' && arg[i] != '\t' && b < 63) pw[b++] = arg[i++];
    pw[b] = 0;
    if (!name[0]) { term_puts("Kullanım: adduser <ad> [parola]\n"); return; }
    int r = arctian_auth_create_user(name, pw[0] ? pw : 0);
    if (r == 0) {
        term_puts("Hesap oluşturuldu: "); term_puts(name);
        term_puts("\n  ev dizini: "); term_puts(arctian_auth_home(name)); term_putc('\n');
        term_puts("  uid: "); term_put_uint(arctian_auth_user_uid(arctian_auth_find(name))); term_putc('\n');
    } else if (r == -6) term_puts("Bu ad zaten var.\n");
    else if (r == -5) term_puts("Hesap sınırına ulaşıldı (en fazla 8).\n");
    else if (r == -2) term_puts("Geçersiz ad (boşluk, ':', '/' veya '\\' olmaz).\n");
    else term_puts("Hesap oluşturulamadı.\n");
}

static void cmd_passwd(const char *arg) {
    char t[3][64];
    int i = 0;
    for (int k = 0; k < 3; k++) {
        int o = 0;
        while (arg[i] == ' ' || arg[i] == '\t') i++;
        while (arg[i] && arg[i] != ' ' && arg[i] != '\t' && o < 63) t[k][o++] = arg[i++];
        t[k][o] = 0;
    }
    int r;
    if (t[0][0] && t[1][0] && t[2][0]) r = arctian_auth_set_password_for(t[0], t[1], t[2]);
    else if (t[0][0] && t[1][0]) r = arctian_auth_set_password(t[0], t[1]);
    else { term_puts("Kullanım: passwd [ad] <parola> <parola-tekrar>\n"); return; }
    if (r == 0) term_puts("Parola güncellendi.\n");
    else if (r == -3) term_puts("Parolalar eşleşmiyor.\n");
    else if (r == -4) term_puts("Parola en az 4 karakter olmalı.\n");
    else if (r == -1) term_puts("Hesap bulunamadı.\n");
    else term_puts("Parola ayarlanamadı.\n");
}

static void cmd_lock(void) {
    term_puts("Kilit ekranına dönülüyor...\n");
    term_flush();
    arctian_shell_lock_screen();
}

static void cmd_setpassword(const char *arg) {
    char p1[64], p2[64];
    int i = 0, a = 0, b = 0;
    while (arg[i] == ' ' || arg[i] == '\t') i++;
    while (arg[i] && arg[i] != ' ' && arg[i] != '\t' && a < 63) p1[a++] = arg[i++];
    p1[a] = 0;
    while (arg[i] == ' ' || arg[i] == '\t') i++;
    while (arg[i] && arg[i] != ' ' && arg[i] != '\t' && b < 63) p2[b++] = arg[i++];
    p2[b] = 0;
    if (!p1[0] || !p2[0]) {
        term_puts("Kullanım: setpassword <parola> <parola-tekrar>\n");
        return;
    }
    int r = arctian_auth_set_password(p1, p2);
    if (r == 0) term_puts("Parola güncellendi.\n");
    else if (r == -3) term_puts("Parolalar eşleşmiyor.\n");
    else if (r == -4) term_puts("Parola en az 4 karakter olmalı.\n");
    else if (r == -1) term_puts("Önce bir hesap oluşturun.\n");
    else term_puts("Parola ayarlanamadı.\n");
}

static int s_cap_hook_mode = PERM_DENY;
static int cap_hook(int type, const char *src) {
    (void)type; (void)src;
    return s_cap_hook_mode;
}

/* 0.7: yetki maskesi ve dosya kapisi birlikte denenir.
 *  - ag yetkisi olmayan surec agi kullanamaz (CAP_NET -> EPERM)
 *  - kutulu ozne + kullanici reddi  -> create NULL + SANDBOX_EPERM
 *  - kutulu ozne + kullanici izni   -> yazma basarili
 * Terminal kendisi guvenilendir; test boyunca gecici kutulu ozne acilir
 * ve sonunda terminal oznesi geri yuklenir. */
static void cmd_capstest(void) {
    if (!g_asi || !g_asi->proc || !g_asi->proc->app_open_caps || !g_asi->sandbox ||
        !g_asi->fs || !g_asi->fs->create) {
        term_puts("Sandbox servisi yok\n");
        return;
    }
    asi_sandbox_t *sb = g_asi->sandbox;
    asi_fs_t *fs = g_asi->fs;
    uint32_t len = 0;
    int net_denied = 0, fs_denied = 0, fs_allowed = 0;

    /* 1) Yetki maskesi: CAP_FS olan surec agi kullanamaz. */
    int p1 = g_asi->proc->app_open_caps("caps-ag-yok", CAP_FS);
    if (p1 > 0) {
        sb->enter(p1);
        const char *b = (g_asi->net && g_asi->net->http_get)
                        ? g_asi->net->http_get("http://example.invalid/", &len) : 0;
        net_denied = (b == 0 && sb->last_error() == SANDBOX_EPERM);
        sb->leave();
        g_asi->proc->app_close(p1, 0);
    }

    /* 2-3) Dosya kapisi: kutulu ozne + onay kancasi.      */
    {
        char old_id[64], old_title[64];
        const char *oid = (sb->subject_id) ? sb->subject_id() : "";
        const char *oti = (sb->subject_title) ? sb->subject_title() : "";
        cstr_copy(old_id, sizeof(old_id), oid[0] ? oid : "gomulu:terminal");
        cstr_copy(old_title, sizeof(old_title), oti[0] ? oti : "Terminal");

        consent_set_hook(cap_hook);
        perm_dec_drop("dpk:caps-kapi");
        subj_open("dpk:caps-kapi", "Kapi Testi", 0, "C:/apps/caps-kapi.dpk");

        s_cap_hook_mode = PERM_DENY;
        perm_dispatch_begin();
        void *h = fs->create(0, "caps_denied.tmp", 0);
        int err = sb->last_error();
        perm_dispatch_end();
        fs_denied = (h == 0 && err == SANDBOX_EPERM);
        if (h) fs->remove("caps_denied.tmp");

        s_cap_hook_mode = PERM_ALWAYS;
        perm_dispatch_begin();
        void *h2 = fs->create(0, "caps_allowed.tmp", 0);
        if (h2) fs_allowed = (fs->write(h2, "x", 1) == 0);
        perm_dispatch_end();
        fs->remove("caps_allowed.tmp");
        perm_dec_drop("dpk:caps-kapi");

        consent_set_hook(0);
        subj_open(old_id, old_title, 1, "");
    }

    term_puts("Sandbox testi (uygulama sureci):\n");
    term_puts("  ag yetkisi yok -> EPERM : "); term_puts(net_denied ? "EVET" : "HAYIR"); term_putc('\n');
    term_puts("  fs kapisi, kullanici reddi : "); term_puts(fs_denied ? "EPERM" : "GECMEDI"); term_putc('\n');
    term_puts("  fs kapisi, kullanici izni  : "); term_puts(fs_allowed ? "YAZILDI" : "GECMEDI"); term_putc('\n');
    term_puts((net_denied && fs_denied && fs_allowed) ? "  SONUC: OK\n" : "  SONUC: HATA\n");
}

/* Uctan uca: kutulu bir ozne ev dizini disina yazmak ister; gercek
 * onay penceresi acilir ve kullanici karar verir. Kanca KURULMAZ. */
static void cmd_onaytest(const char *arg) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create || !g_asi->sandbox ||
        !g_asi->sandbox->subject_begin) {
        term_puts("Onay servisi yok\n");
        return;
    }
    char dir[176];
    if (arg && arg[0]) path_resolve(dir, sizeof(dir), arg);
    else               cstr_copy(dir, sizeof(dir), "C:/belgeler");

    if (g_asi->fs->mkdir) g_asi->fs->mkdir(dir);

    char old_id[64], old_title[64];
    const char *oid = g_asi->sandbox->subject_id ? g_asi->sandbox->subject_id() : "";
    const char *oti = g_asi->sandbox->subject_title ? g_asi->sandbox->subject_title() : "";
    cstr_copy(old_id, sizeof(old_id), oid[0] ? oid : "gomulu:terminal");
    cstr_copy(old_title, sizeof(old_title), oti[0] ? oti : "Terminal");

    perm_dec_drop("dpk:onay-demo");
    consent_set_hook(0);
    subj_open("dpk:onay-demo", "Ornek Uygulama", 0, "C:/apps/onay-demo.dpk");

    term_puts("Kutulu uygulama su yola yazmak istiyor: ");
    term_puts(dir);
    term_putc('\n');
    term_puts("Onay penceresi: Bir kez / Her zaman / Reddet (Esc = Reddet)\n");
    term_flush();

    int ok = 0;
    perm_dispatch_begin();
    void *h = g_asi->fs->create_in(dir, "onay_demo.txt", 0);
    if (h) ok = (g_asi->fs->write(h, "merhaba", 7) == 0);
    int err = g_asi->sandbox->last_error ? g_asi->sandbox->last_error() : 0;
    perm_dispatch_end();

    if (ok) {
        term_puts("Sonuc: IZIN VERILDI, dosya yazildi.\n");
        g_asi->fs->remove("C:/belgeler/onay_demo.txt");
    } else {
        term_puts("Sonuc: REDDEDILDI");
        if (err == SANDBOX_EPERM) term_puts(" (SANDBOX_EPERM)");
        term_putc('\n');
    }
    perm_dec_drop("dpk:onay-demo");
    subj_open(old_id, old_title, 1, "");
}

static void cmd_permtest(void) {
    if (!g_asi || !g_asi->fs || !g_asi->sandbox || !g_asi->sandbox->set_uid) {
        term_puts("Dosya izin servisi yok\n");
        return;
    }
    asi_fs_t *fs = g_asi->fs;
    asi_sandbox_t *sb = g_asi->sandbox;
    int owner_write = 0, other_deny = 0, chown_deny = 0;

    sb->set_uid(0, 0);
    fs->remove("permtest.tmp");
    void *h = fs->create(0, "permtest.tmp", 0);
    if (!h) { term_puts("Dosya olusturulamadi\n"); return; }
    fs->write(h, "abc", 3);
    if (fs->chmod) fs->chmod("permtest.tmp", 0640u);

    sb->set_uid(1000, 1000);
    other_deny = (fs->write(h, "x", 1) != 0);
    if (fs->chown) chown_deny = (fs->chown("permtest.tmp", 1000, 1000) != 0);

    sb->set_uid(0, 0);
    owner_write = (fs->write(h, "x", 1) == 0);

    asi_stat_t st;
    int have_stat = (fs->stat && fs->stat("permtest.tmp", &st) == 0);
    fs->remove("permtest.tmp");
    sb->set_uid(0, 0);

    term_puts("Dosya izin testi (sahiplik):\n");
    term_puts("  sahibi yazabilir       : "); term_puts(owner_write ? "EVET" : "HAYIR"); term_putc('\n');
    term_puts("  sahibi olmayan yazamaz : "); term_puts(other_deny ? "EVET" : "HAYIR"); term_putc('\n');
    term_puts("  sahibi olmayan chown   : "); term_puts(chown_deny ? "ENGEL" : "SIZINTI"); term_putc('\n');
    if (have_stat) {
        term_puts("  mod                    : 0");
        term_put_uint((st.mode >> 6) & 7u);
        term_put_uint((st.mode >> 3) & 7u);
        term_put_uint(st.mode & 7u);
        term_puts("  uid="); term_put_uint(st.uid);
        term_puts(" gid="); term_put_uint(st.gid);
        term_putc('\n');
    }
    term_puts((owner_write && other_deny && chown_deny) ? "  SONUC: OK\n" : "  SONUC: HATA\n");
}

static void cmd_socktest(void) {
    if (!g_asi || !g_asi->net || !g_asi->net->sock_socket) {
        term_puts("Soket servisi yok\n");
        return;
    }
    uint8_t host[4] = { 10, 0, 2, 2 };
    if (g_asi->net->gateway) g_asi->net->gateway(host);
    if ((host[0] | host[1] | host[2] | host[3]) == 0) {
        host[0] = 10; host[1] = 0; host[2] = 2; host[3] = 2;
    }

    char rx[64];
    int tcp_ok = 0, udp_ok = 0;

    int fd = g_asi->net->sock_socket(ASI_SOCK_STREAM);
    if (fd >= 0 && g_asi->net->sock_connect(fd, host, 18081) == 0) {
        g_asi->net->sock_send(fd, "PING\n", 5);
        uint64_t deadline = g_asi->time->ticks_ms() + 4000;
        int n = 0;
        while (g_asi->time->ticks_ms() < deadline) {
            g_asi->net->poll();
            n = g_asi->net->sock_recv(fd, rx, sizeof(rx) - 1);
            if (n > 0) break;
            g_asi->time->sleep_ms(10);
        }
        if (n >= 4 && rx[0] == 'P' && rx[1] == 'O' && rx[2] == 'N' && rx[3] == 'G')
            tcp_ok = 1;
    }
    if (fd >= 0) g_asi->net->sock_close(fd);

    fd = g_asi->net->sock_socket(ASI_SOCK_DGRAM);
    if (fd >= 0 && g_asi->net->sock_bind(fd, 0, 0) == 0 &&
        g_asi->net->sock_connect(fd, host, 18082) == 0) {
        g_asi->net->sock_send(fd, "UDPPING\n", 8);
        uint64_t deadline = g_asi->time->ticks_ms() + 4000;
        int n = 0;
        while (g_asi->time->ticks_ms() < deadline) {
            g_asi->net->poll();
            n = g_asi->net->sock_recv(fd, rx, sizeof(rx) - 1);
            if (n > 0) break;
            g_asi->time->sleep_ms(10);
        }
        if (n >= 7 && rx[0] == 'U' && rx[1] == 'D' && rx[2] == 'P')
            udp_ok = 1;
    }
    if (fd >= 0) g_asi->net->sock_close(fd);

    term_puts("Soket testi (uygulama, ham TCP/UDP):\n");
    term_puts("  tcp baglanti + PING/PONG : "); term_puts(tcp_ok ? "EVET" : "HAYIR"); term_putc('\n');
    term_puts("  udp baglanti + echo      : "); term_puts(udp_ok ? "EVET" : "HAYIR"); term_putc('\n');
    term_puts((tcp_ok && udp_ok) ? "  SONUC: OK\n" : "  SONUC: HATA\n");
}

static void cmd_ntp(void) {
    if (!g_asi || !g_asi->time || !g_asi->time->ntp_sync) {
        term_puts("NTP servisi yok\n");
        return;
    }
    term_puts("NTP sunucusuna baglaniliyor...\n");
    int rc = g_asi->time->ntp_sync(5000);
    if (rc != 0) {
        term_puts("NTP basarisiz, kod=");
        term_put_uint((uint32_t)(-rc));
        term_putc('\n');
        return;
    }
    asi_datetime_t dt;
    if (g_asi->time->now && g_asi->time->now(&dt)) {
        term_puts("Saat esitlendi: ");
        term_put_uint((uint32_t)dt.day); term_putc('/');
        term_put_uint((uint32_t)dt.month); term_putc('/');
        term_put_uint((uint32_t)dt.year);
        term_putc(' ');
        term_put_uint((uint32_t)dt.hour); term_putc(':');
        if (dt.minute < 10) term_putc('0');
        term_put_uint((uint32_t)dt.minute); term_putc(':');
        if (dt.second < 10) term_putc('0');
        term_put_uint((uint32_t)dt.second);
        term_putc('\n');
    }
    if (g_asi->time->unix_now) {
        term_puts("Unix: ");
        term_put_uint((uint32_t)g_asi->time->unix_now());
        term_putc('\n');
    }
}

static const char *term_level_str(int lv) {
    switch (lv) {
        case KLOG_DEBUG: return "DBG";
        case KLOG_INFO:  return "INF";
        case KLOG_WARN:  return "WRN";
        case KLOG_ERR:   return "ERR";
        case KLOG_FATAL: return "FTL";
        default:         return "???";
    }
}

static void cmd_dmesg(void) {
    if (!g_asi || !g_asi->log || !g_asi->log->count || !g_asi->log->get) {
        term_puts("Log servisi yok\n");
        return;
    }
    int n = g_asi->log->count();
    if (n <= 0) { term_puts("(log bos)\n"); return; }
    for (int i = 0; i < n; i++) {
        klog_entry_t e;
        if (g_asi->log->get(i, &e) != 0) continue;
        term_puts("["); term_puts(term_level_str(e.level)); term_puts("] ");
        term_puts(e.tag); term_puts(": "); term_puts(e.msg); term_putc('\n');
    }
}

static void cmd_syslog(void) {
    if (!g_asi || !g_asi->log || !g_asi->log->flush) { term_puts("Log servisi yok\n"); return; }
    int n = g_asi->log->flush();
    term_puts("Log dosyaya yazildi ("); term_put_uint((uint32_t)(n < 0 ? 0 : n));
    term_puts(" kayit)\n");
}

static void cmd_copy(const char *arg) {
    arctian_clipboard_set(arg);
    term_puts("Panoya kopyalandi (");
    term_put_uint((uint32_t)arctian_clipboard_len());
    term_puts(" bayt)\n");
}

static void cmd_paste(void) {
    const char *s = arctian_clipboard_get();
    if (!s || !s[0]) { term_puts("(pano bos)\n"); return; }
    term_puts(s);
    term_putc('\n');
}

static void cmd_clip(void) {
    int n = arctian_clipboard_len();
    term_puts("Pano: "); term_put_uint((uint32_t)n); term_puts(" bayt\n");
    if (n > 0) { term_puts(arctian_clipboard_get()); term_putc('\n'); }
}

static void cmd_notify(const char *arg) {
    char t[48];
    int k = 0;
    while (arg[k] && arg[k] != ' ' && k < 47) { t[k] = arg[k]; k++; }
    t[k] = 0;
    const char *msg = arg + k;
    while (*msg == ' ') msg++;
    arctian_notify(t[0] ? t : "Bildirim", msg);
    term_puts("Bildirim gosterildi\n");
}

static void cmd_drivers(void) {
    if (!g_asi || !g_asi->dev) { term_puts("Surucu listesi yok\n"); return; }
    int n = g_asi->dev->count();
    term_puts("Cihazlar / suruculer:\n");
    for (int i = 0; i < n; i++) {
        const char *nm = g_asi->dev->name(i);
        const char *st = g_asi->dev->status(i);
        term_puts("  "); term_puts(nm ? nm : "?");
        term_puts(" : "); term_puts(st ? st : "?"); term_putc('\n');
    }
}

static void cmd_safemode(const char *arg) {
    int cur = kv_get_int("safe_mode", 0);
    if (!arg[0]) {
        term_puts("Guvenli mod: "); term_puts(cur ? "acik" : "kapali"); term_putc('\n');
        return;
    }
    int on = (arg[0] == '1' || arg[0] == 'a' || arg[0] == 'A' ||
              arg[0] == 'o' || arg[0] == 'O');
    kv_set("safe_mode", on ? "1" : "0");
    term_puts("Guvenli mod "); term_puts(on ? "acildi" : "kapatildi");
    term_puts(" (yeniden baslatinca etkin)\n");
}

static void cmd_fsck(const char *arg) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->fsck) {
        term_puts("ArctianFS denetim servisi yok\n");
        return;
    }
    int fix = 0;
    if (arg) {
        while (*arg == ' ') arg++;
        if (str_eq(arg, "-f") || str_eq(arg, "--fix") || str_eq(arg, "fix")) fix = 1;
    }
    int prob = 0, done = 0;
    int rc = g_asi->fs->fsck(fix, &prob, &done);
    term_puts("ArctianFS denetimi: ");
    if (rc == 0 && prob == 0) {
        term_puts("temiz (sorun yok)\n");
    } else if (prob > 0) {
        term_put_uint((uint32_t)prob); term_puts(" sorun");
        if (fix) {
            term_puts(", "); term_put_uint((uint32_t)done); term_puts(" duzeltildi\n");
        } else {
            term_puts(" bulundu (onarmak icin: fsck -f)\n");
        }
    } else {
        term_puts("tamamlanamadi\n");
    }
}

static void cmd_sysinfo(void) {
    term_puts("Arctian OS");
    if (g_asi && g_asi->time) {
        asi_datetime_t dt;
        if (g_asi->time->now && g_asi->time->now(&dt)) {
            term_puts("  ("); term_put_uint((uint32_t)dt.year); term_putc('-');
            term_put2(dt.month); term_putc('-'); term_put2(dt.day); term_puts(")");
        }
    }
    term_putc('\n');
    if (g_asi && g_asi->gfx) {
        term_puts("Ekran   : "); term_put_uint((uint32_t)g_asi->gfx->width());
        term_putc('x'); term_put_uint((uint32_t)g_asi->gfx->height()); term_putc('\n');
    }
    if (g_asi && g_asi->net && g_asi->net->link_type) {
        int lt = g_asi->net->link_type();
        term_puts("Ag      : ");
        term_puts(lt == ASI_LINK_ETHERNET ? "Ethernet" : lt == ASI_LINK_WIFI ? "Wi-Fi" : "yok");
        term_putc('\n');
    }
    term_puts("Uygulama: "); term_put_uint((uint32_t)arctian_shell_app_count()); term_putc('\n');
    term_puts("Pencere : "); term_put_uint((uint32_t)arctian_shell_window_count()); term_putc('\n');
    if (arctian_store && arctian_store->count_installed) {
        term_puts("Paket   : "); term_put_uint((uint32_t)arctian_store->count_installed()); term_putc('\n');
    }
}

static int feed_find(const char *body, uint32_t len, const char *id, char *out, int cap) {
    uint32_t i = 0;
    int have = 0;
    char curid[64], curpkg[512];
    curid[0] = 0; curpkg[0] = 0;
    while (i < len) {
        uint32_t ls = i;
        while (i < len && body[i] != '\n' && body[i] != '\r') i++;
        uint32_t le = i;
        while (i < len && (body[i] == '\n' || body[i] == '\r')) i++;
        int ll = (int)(le - ls);
        if (ll <= 0) continue;
        if (ll == 4 && body[ls] == '@' && body[ls + 1] == 'a' && body[ls + 2] == 'p' && body[ls + 3] == 'p') {
            if (have && eq_ci(curid, id)) {
                int c = slen(curpkg); if (c > cap - 1) c = cap - 1;
                for (int k = 0; k < c; k++) out[k] = curpkg[k];
                out[c] = 0;
                return 0;
            }
            have = 1; curid[0] = 0; curpkg[0] = 0;
            continue;
        }
        if (!have) continue;
        uint32_t eq = ls;
        while (eq < le && body[eq] != '=') eq++;
        if (eq >= le) continue;
        const char *k = body + ls;
        int kl = (int)(eq - ls);
        const char *v = body + eq + 1;
        int vl = (int)(le - eq - 1);
        if (key_is(k, kl, "id")) pct_dec(curid, sizeof(curid), v, vl);
        else if (key_is(k, kl, "package")) pct_dec(curpkg, sizeof(curpkg), v, vl);
    }
    if (have && eq_ci(curid, id)) {
        int c = slen(curpkg); if (c > cap - 1) c = cap - 1;
        for (int k = 0; k < c; k++) out[k] = curpkg[k];
        out[c] = 0;
        return 0;
    }
    return -1;
}

static void cmd_pkg_install(const char *id) {
    if (!g_asi || !g_asi->net || !g_asi->net->http_get) { term_puts("Ag servisi yok\n"); return; }
    if (!arctian_store || !arctian_store->install) { term_puts("Magaza yok\n"); return; }

    char url[160];
    int u = 0;
    const char *base = TERM_STORE_URL;
    while (*base && u < 150) url[u++] = *base++;
    const char *fp = "/v1/feed";
    while (*fp && u < 159) url[u++] = *fp++;
    url[u] = 0;

    term_puts("Paket dizini aliniyor...\n"); term_flush();
    uint32_t flen = 0;
    const char *feed = g_asi->net->http_get(url, &flen);
    if (!feed || !flen) { term_puts("Sunucuya ulasilamadi\n"); return; }

    char pkg[512];
    if (feed_find(feed, flen, id, pkg, sizeof(pkg)) != 0 || !pkg[0]) {
        term_puts("Paket bulunamadi: "); term_puts(id); term_putc('\n'); return;
    }

    char purl[640];
    int pu = 0;
    if (starts_with(pkg, "http")) {
        int k = 0; while (pkg[k] && pu < 639) purl[pu++] = pkg[k++];
    } else {
        base = TERM_STORE_URL; while (*base && pu < 600) purl[pu++] = *base++;
        int k = 0; while (pkg[k] && pu < 639) purl[pu++] = pkg[k++];
    }
    purl[pu] = 0;

    term_puts("Indiriliyor: "); term_puts(purl); term_putc('\n'); term_flush();
    uint32_t blen = 0;
    const char *body = g_asi->net->http_get(purl, &blen);
    if (!body || !blen) { term_puts("Indirme basarisiz\n"); return; }

    char err[96]; err[0] = 0;
    int r = arctian_store->install(body, blen, err, sizeof(err));
    if (r == 0) { term_puts("Kuruldu: "); term_puts(id); term_putc('\n'); }
    else { term_puts("Kurulum hatasi: "); term_puts(err[0] ? err : "bilinmeyen"); term_putc('\n'); }
}

/* 0.6: pkg install <yol|url> — magaza dizinini atlayip dogrudan kurar.
 * Yerel dosya (C:/indirilenler/x.dpk) ya da tam URL kabul edilir. Boylece
 * cevrimdisi kurulum ve test mumkun olur. */
static void cmd_pkg_install_direct(const char *src) {
    if (!arctian_store || !arctian_store->install) { term_puts("Magaza yok\n"); return; }
    static uint8_t *buf;
    const uint32_t cap = DPK_MAX_PACKAGE;
    if (!buf) buf = (uint8_t *)g_asi->mem->alloc(cap);
    if (!buf) { term_puts("Bellek yetersiz\n"); return; }

    uint32_t len = 0;
    if (starts_with(src, "http://") || starts_with(src, "https://")) {
        if (!g_asi->net || !g_asi->net->download) { term_puts("Ag servisi yok\n"); return; }
        term_puts("Indiriliyor: "); term_puts(src); term_putc('\n'); term_flush();
        if (g_asi->net->download(src, buf, cap, &len) != 0 || len < 32) {
            term_puts("Indirme basarisiz\n"); return;
        }
    } else {
        char p[176];
        path_resolve(p, sizeof(p), src);
        if (afs_read_at(p, buf, cap, &len) != 0 || len < 32) {
            term_puts("Dosya okunamadi: "); term_puts(p); term_putc('\n');
            return;
        }
    }

    char err[128]; err[0] = 0;
    int r = arctian_store->install(buf, len, err, sizeof(err));
    if (r == 0) {
        term_puts("Kuruldu: "); term_put_size(len); term_putc('\n');
    } else {
        term_puts("Kurulum hatasi: "); term_puts(err[0] ? err : "bilinmiyor");
        term_putc('\n');
    }
}

/* pkg pull <host> <port> <yol> — URL yazmadan .dpk indirip kurar.
 * ":" ve "/" karakterleri klavye duzenine gore sorun cikarabildigi icin
 * parcalardan URL kurulur (wf pull ile ayni yaklasim). */
static void cmd_pkg_pull(const char *args) {
    char host[64], port[16], path[160];
    int i = 0, n = 0;
    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ' && n < (int)sizeof(host) - 1) host[n++] = args[i++];
    host[n] = 0;
    while (args[i] == ' ') i++;
    n = 0;
    while (args[i] && args[i] != ' ' && n < (int)sizeof(port) - 1) port[n++] = args[i++];
    port[n] = 0;
    while (args[i] == ' ') i++;
    n = 0;
    while (args[i] && args[i] != ' ' && n < (int)sizeof(path) - 1) path[n++] = args[i++];
    path[n] = 0;
    if (!host[0] || !port[0] || !path[0]) {
        term_puts("Kullanim: pkg pull <host> <port> <yol>\n");
        term_puts("  ornek: pkg pull 10.0.2.2 8098 kronometre.dpk\n");
        return;
    }
    char url[300];
    int u = 0;
    const char *pre = "http://";
    for (int k = 0; pre[k] && u < (int)sizeof(url) - 1; k++) url[u++] = pre[k];
    for (int k = 0; host[k] && u < (int)sizeof(url) - 1; k++) url[u++] = host[k];
    if (u < (int)sizeof(url) - 1) url[u++] = ':';
    for (int k = 0; port[k] && u < (int)sizeof(url) - 1; k++) url[u++] = port[k];
    if (u < (int)sizeof(url) - 1) url[u++] = '/';
    for (int k = 0; path[k] && u < (int)sizeof(url) - 1; k++) url[u++] = path[k];
    url[u] = 0;

    term_puts("Indiriliyor: "); term_puts(url); term_putc('\n'); term_flush();
    cmd_pkg_install_direct(url);
}

static void cmd_pkg(const char *arg) {
    if (!arctian_store) { term_puts("Magaza servisi yok\n"); return; }
    if (!arg[0] || str_eq(arg, "list")) {
        int n = arctian_store->count_installed ? arctian_store->count_installed() : 0;
        if (n <= 0) { term_puts("Kurulu paket yok\n"); return; }
        term_puts("Kurulu paketler:\n");
        for (int i = 0; i < n; i++) {
            term_puts("  ");
            term_puts(arctian_store->installed_id ? arctian_store->installed_id(i) : "");
            term_puts("  ");
            term_puts(arctian_store->installed_title ? arctian_store->installed_title(i) : "");
            term_puts("  ");
            term_puts(arctian_store->installed_version_at ? arctian_store->installed_version_at(i) : "");
            term_putc('\n');
        }
        return;
    }
    if (starts_with(arg, "install ")) {
        const char *id = arg + 8; while (*id == ' ') id++;
        if (!id[0]) {
            term_puts("Kullanim: pkg install <id|yol|url>\n");
            return;
        }
        /* Yol/URL ise dogrudan kur; degilse magaza dizininden ara. */
        if (starts_with(id, "http://") || starts_with(id, "https://") ||
            starts_with(id, "C:/") || starts_with(id, "D:/") ||
            starts_with(id, "/") || slen(id) > 4) {
            const char *dot = id;
            int has_dpk = 0;
            for (int i = 0; id[i]; i++)
                if (id[i] == '.' && id[i + 1] == 'd' && id[i + 2] == 'p' &&
                    id[i + 3] == 'k') has_dpk = 1;
            if (has_dpk) { cmd_pkg_install_direct(id); return; }
        }
        cmd_pkg_install(id);
        return;
    }
    if (starts_with(arg, "pull ")) {
        cmd_pkg_pull(arg + 5);
        return;
    }
    if (starts_with(arg, "remove ")) {
        const char *id = arg + 7; while (*id == ' ') id++;
        if (!id[0]) { term_puts("Kullanim: pkg remove <id>\n"); return; }
        int r = arctian_store->uninstall ? arctian_store->uninstall(id) : -1;
        if (r == 0) { term_puts("Kaldirildi: "); term_puts(id); term_putc('\n'); }
        else if (r == -2) { term_puts("Uygulama acik, once kapat: "); term_puts(id); term_putc('\n'); }
        else term_puts("Paket bulunamadi\n");
        return;
    }
    term_puts("Kullanim: pkg [list | install <id|yol|url> | pull <host> <port> <yol> | remove <id>]\n");
}

static char g_cwd[176] = "C:/";

static int cstr_copy(char *dst, int cap, const char *src) {
    int i = 0;
    for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = 0;
    return i;
}

static int path_is_abs(const char *n) {
    if (!n || !n[0]) return 0;
    if (n[0] == '/' || n[0] == '\\') return 1;
    if (n[1] == ':') return 1;
    return 0;
}

static void path_normalize(char *s) {
    char out[192];
    int o = 0;
    out[o++] = 'C';
    out[o++] = ':';
    int i = 0;
    if (s[0] && s[1] == ':') {
        i = (s[2] == '/' || s[2] == '\\') ? 3 : 2;
    }
    while (s[i]) {
        if (s[i] == '/' || s[i] == '\\') { i++; continue; }
        int j = i;
        while (s[j] && s[j] != '/' && s[j] != '\\') j++;
        int len = j - i;
        if (len == 1 && s[i] == '.') { i = j; continue; }
        if (len == 2 && s[i] == '.' && s[i + 1] == '.') {
            if (o > 2) {
                o--;
                while (o > 2 && out[o - 1] != '/') o--;
            }
            i = j;
            continue;
        }
        if (out[o - 1] != '/' && o < 180) out[o++] = '/';
        for (int k = 0; k < len && o < 180; k++) out[o++] = s[i + k];
        i = j;
    }
    if (o == 2 && o < 180) out[o++] = '/';
    out[o] = 0;
    for (int k = 0; k <= o; k++) s[k] = out[k];
}

static void path_resolve(char *dst, int cap, const char *name) {
    char tmp[192];
    int o = 0;
    int abs = path_is_abs(name);
    if (abs) {
        if (name[0] == '/' || name[0] == '\\') { tmp[o++] = 'C'; tmp[o++] = ':'; }
    } else {
        for (int i = 0; g_cwd[i] && o < 180; i++) tmp[o++] = g_cwd[i];
    }
    if (name) for (int i = 0; name[i] && o < 180; i++) tmp[o++] = name[i];
    tmp[o] = 0;
    path_normalize(tmp);
    cstr_copy(dst, cap, tmp);
}

static void path_resolve_dir(char *dst, int cap, const char *name) {
    path_resolve(dst, cap, name);
    int n = slen(dst);
    if (n > 0 && dst[n - 1] != '/' && n < cap - 1) { dst[n] = '/'; dst[n + 1] = 0; }
}

static int fs_dir_exists(const char *path) {
    if (!g_asi || !g_asi->fs) return 0;
    if (str_eq(path, "C:/")) return 1;
    return (g_asi->fs->find && g_asi->fs->find(0, path)) ? 1 : 0;
}

/* Yol gercekten var olan bir klasor mu? (0.7: cp/mv hedefi ayrimi icin).
 * count_dir() var olmayan yol icin 0 dondurebildigi icin tek basina
 * guvenilir degil; once stat, yoksa find_in denenir. */
static int path_is_dir(const char *path) {
    if (!g_asi || !g_asi->fs) return 0;
    if (str_eq(path, "C:/") || str_eq(path, "C:")) return 1;
    int n = slen(path);
    if (n > 3 && path[n - 1] == '/') return fs_dir_exists(path);
    if (g_asi->fs->stat) {
        asi_stat_t st;
        for (unsigned i = 0; i < sizeof(st); i++) ((char *)&st)[i] = 0;
        if (g_asi->fs->stat(path, &st) == 0) return st.is_dir ? 1 : 0;
    }
    if (g_asi->fs->find_in && g_asi->fs->count_dir) {
        char dir[176], base[80];
        cstr_copy(dir, sizeof(dir), path);
        int last = -1;
        for (int i = 0; dir[i]; i++) if (dir[i] == '/') last = i;
        if (last > 0) {
            cstr_copy(base, sizeof(base), path + last + 1);
            dir[last] = 0;
            if (g_asi->fs->find_in(dir, base)) return 1;
        }
    }
    return 0;
}

static void cmd_pwd(void) {
    char t[176];
    cstr_copy(t, sizeof(t), g_cwd);
    int n = slen(t);
    if (n > 3 && t[n - 1] == '/') t[n - 1] = 0;
    term_puts(t);
    term_putc('\n');
}

static void cmd_cd(const char *arg) {
    if (!g_asi || !g_asi->fs) { term_puts("Dosya sistemi yok\n"); return; }
    if (!arg[0] || str_eq(arg, "~")) {
        const char *cu = arctian_auth_current();
        const char *hm = cu && cu[0] ? arctian_auth_home(cu) : 0;
        char hp[96]; int o = 0;
        if (hm) for (int i = 0; hm[i] && o < (int)sizeof(hp) - 2; i++) hp[o++] = hm[i];
        if (o > 0 && hp[o - 1] != '/') hp[o++] = '/';
        hp[o] = 0;
        if (o > 0 && fs_dir_exists(hp)) cstr_copy(g_cwd, sizeof(g_cwd), hp);
        else cstr_copy(g_cwd, sizeof(g_cwd), "C:/");
        term_puts(g_cwd);
        term_putc('\n');
        return;
    }
    char p[176];
    path_resolve_dir(p, sizeof(p), arg);
    if (!fs_dir_exists(p)) { term_puts("Dizin yok: "); term_puts(p); term_putc('\n'); return; }
    cstr_copy(g_cwd, sizeof(g_cwd), p);
    term_puts(g_cwd);
    term_putc('\n');
}

static void cmd_mkdir(const char *arg) {
    if (!arg[0]) { term_puts("Kullanim: mkdir <klasor>\n"); return; }
    if (!g_asi || !g_asi->fs || !g_asi->fs->mkdir) { term_puts("Desteklenmiyor\n"); return; }
    char p[176];
    path_resolve_dir(p, sizeof(p), arg);
    if (fs_dir_exists(p)) { term_puts("Zaten var: "); term_puts(p); term_putc('\n'); return; }
    if (g_asi->fs->mkdir(p) == 0) { term_puts("Olusturuldu: "); term_puts(p); term_putc('\n'); }
    else term_puts("Olusturulamadi\n");
}

/* stat <dosya> — boyut, tur ve izinler (0.6; FM "i" penceresinin kabuk yuzu). */
static void cmd_stat(const char *name) {
    if (!name[0]) { term_puts("Kullanim: stat <dosya>\n"); return; }
    if (!g_asi || !g_asi->fs || !g_asi->fs->stat) { term_puts("Desteklenmiyor\n"); return; }
    char p[176];
    path_resolve(p, sizeof(p), name);
    asi_stat_t st;
    if (g_asi->fs->stat(p, &st) != 0) {
        term_puts("Bulunamadi: "); term_puts(p); term_putc('\n');
        return;
    }
    term_puts("Dosya : "); term_puts(p); term_putc('\n');
    term_puts("Tur   : "); term_puts(st.is_dir ? "klasor" : "dosya"); term_putc('\n');
    term_puts("Boyut : ");
    if (st.is_dir) term_puts("-");
    else term_put_size(st.size);
    term_putc('\n');
    term_puts("Izin  : ");
    uint32_t m = st.mode & 0777u;
    term_putc('0');
    term_putc((char)('0' + ((m >> 6) & 7)));
    term_putc((char)('0' + ((m >> 3) & 7)));
    term_putc((char)('0' + (m & 7)));
    term_putc('\n');
    term_puts("Sahip : "); term_put_uint(st.uid);
    term_puts(":"); term_put_uint(st.gid); term_putc('\n');
}

/* cp <kaynak> <hedef> — dosya kopyalar; hedef klasor ise adi korunur (0.6). */
static void cmd_cp(const char *args) {
    if (!g_asi || !g_asi->fs) { term_puts("Dosya sistemi yok\n"); return; }
    asi_fs_t *fs = g_asi->fs;
    if (!fs->read_path || !fs->create_in || !fs->write) { term_puts("Desteklenmiyor\n"); return; }
    char src[176], dst[176];
    int i = 0, n = 0;
    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ' && n < (int)sizeof(src) - 1) src[n++] = args[i++];
    src[n] = 0;
    while (args[i] == ' ') i++;
    n = 0;
    while (args[i] && args[i] != ' ' && n < (int)sizeof(dst) - 1) dst[n++] = args[i++];
    dst[n] = 0;
    if (!src[0] || !dst[0]) { term_puts("Kullanim: cp <kaynak> <hedef>\n"); return; }

    char sp[176], dp[176];
    path_resolve(sp, sizeof(sp), src);
    path_resolve(dp, sizeof(dp), dst);

    asi_stat_t st;
    if (fs->stat && fs->stat(sp, &st) != 0) {
        term_puts("Kaynak yok: "); term_puts(sp); term_putc('\n'); return;
    }
    if (st.is_dir) { term_puts("Klasor kopyalama desteklenmiyor (mv kullanin)\n"); return; }

    /* hedef klasor ise dosya adini ekle */
    if (path_is_dir(dp)) {
        const char *base = sp;
        for (const char *p = sp; *p; p++) if (*p == '/') base = p + 1;
        int k = 0;
        while (dp[k]) k++;
        if (k && dp[k - 1] != '/') { dp[k++] = '/'; dp[k] = 0; }
        for (int j = 0; base[j] && k < (int)sizeof(dp) - 1; j++) dp[k++] = base[j];
        dp[k] = 0;
    }
    if (fs->find_in) {
        char d[176]; cstr_copy(d, sizeof(d), dp);
        int last = -1;
        for (int k = 0; d[k]; k++) if (d[k] == '/') last = k;
        const char *fn = dp;
        if (last >= 0) { d[last] = 0; fn = dp + last + 1; }
        if (fs->find_in(last >= 0 ? d : "C:/", fn)) {
            term_puts("Hedef zaten var: "); term_puts(dp); term_putc('\n'); return;
        }
    }

    static uint8_t *buf;
    const uint32_t cap = 512u * 1024u;
    if (!buf) buf = (uint8_t *)g_asi->mem->alloc(cap);
    if (!buf) { term_puts("Bellek yetersiz\n"); return; }
    if (st.size > cap) { term_puts("Dosya cok buyuk (512 KB siniri)\n"); return; }

    uint32_t sz = fs->read_path(sp, buf, cap);
    char dir[176];
    cstr_copy(dir, sizeof(dir), dp);
    int last = -1;
    for (int k = 0; dir[k]; k++) if (dir[k] == '/') last = k;
    const char *fname = dp;
    if (last >= 0) { dir[last] = 0; fname = dp + last + 1; }
    void *h = fs->create_in(last >= 0 ? dir : "C:/", fname, 0);
    if (!h) { term_puts("Hedef olusturulamadi\n"); return; }
    if (fs->write(h, buf, sz) != 0) { term_puts("Yazma hatasi\n"); return; }
    term_puts("Kopyalandi: "); term_puts(sp); term_puts(" -> "); term_puts(dp); term_putc('\n');
}

/* find <parca> — bulunulan klasor agacinda ad parcasi arar (0.6). */
static void find_walk(const char *dir, const char *pat, int depth, int *hits) {
    asi_fs_t *fs = g_asi->fs;
    if (!fs || !fs->count_dir || !fs->list_dir || depth > 6) return;
    int dn = fs->count_dir(dir);
    for (int i = 0; i < dn; i++) {
        char nm[128]; int isd = 0;
        if (fs->list_dir(dir, i, nm, sizeof(nm), &isd) != 0) continue;
        if (str_contains_ci(nm, pat)) {
            term_puts(isd ? "  [D] " : "  [F] ");
            term_puts(dir);
            if (slen_t(dir) && dir[slen_t(dir) - 1] != '/') term_putc('/');
            term_puts(nm);
            term_putc('\n');
            (*hits)++;
        }
        if (isd) {
            char sub[176];
            cstr_copy(sub, sizeof(sub), dir);
            int k = slen_t(sub);
            if (k && sub[k - 1] != '/') { if (k < (int)sizeof(sub) - 1) { sub[k++] = '/'; sub[k] = 0; } }
            for (int j = 0; nm[j] && k < (int)sizeof(sub) - 1; j++) sub[k++] = nm[j];
            sub[k] = 0;
            find_walk(sub, pat, depth + 1, hits);
        }
    }
}

static void cmd_find(const char *pat) {
    if (!pat[0]) { term_puts("Kullanim: find <parca>\n"); return; }
    int hits = 0;
    term_puts("Aranan: "); term_puts(pat); term_puts("  ("); term_puts(g_cwd); term_puts(")\n");
    find_walk(g_cwd, pat, 0, &hits);
    term_puts("Toplam "); term_put_uint((uint32_t)hits); term_puts(" eslesme\n");
}

/* du [klasor] — klasordeki dosyalarin toplam boyutu (0.6). */
static void cmd_du(const char *arg) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->stat) { term_puts("Desteklenmiyor\n"); return; }
    char dir[176];
    if (arg && arg[0]) path_resolve_dir(dir, sizeof(dir), arg);
    else cstr_copy(dir, sizeof(dir), g_cwd);

    int dn = g_asi->fs->count_dir ? g_asi->fs->count_dir(dir) : 0;
    uint32_t total = 0;
    int files = 0, dirs = 0;
    for (int i = 0; i < dn; i++) {
        char nm[128]; int isd = 0;
        if (g_asi->fs->list_dir(dir, i, nm, sizeof(nm), &isd) != 0) continue;
        char full[176];
        cstr_copy(full, sizeof(full), dir);
        int k = slen_t(full);
        if (k && full[k - 1] != '/') { full[k++] = '/'; full[k] = 0; }
        for (int j = 0; nm[j] && k < (int)sizeof(full) - 1; j++) full[k++] = nm[j];
        full[k] = 0;
        asi_stat_t st;
        if (g_asi->fs->stat(full, &st) == 0 && !st.is_dir) { total += st.size; files++; }
        else if (isd) dirs++;
    }
    term_puts("Klasor: "); term_puts(dir); term_putc('\n');
    term_puts("  Dosya: "); term_put_uint((uint32_t)files);
    term_puts("  Alt klasor: "); term_put_uint((uint32_t)dirs);
    term_puts("  Boyut: "); term_put_size(total); term_putc('\n');
}

/* tree [derinlik] — klasor agacini cizer (0.6). */
static void tree_walk(const char *dir, int depth, int maxd, int *shown) {
    asi_fs_t *fs = g_asi->fs;
    if (!fs || !fs->count_dir || depth >= maxd) return;
    int dn = fs->count_dir(dir);
    for (int i = 0; i < dn; i++) {
        char nm[128]; int isd = 0;
        if (fs->list_dir(dir, i, nm, sizeof(nm), &isd) != 0) continue;
        for (int k = 0; k < depth; k++) term_puts("   ");
        term_puts(isd ? "|-- " : "|-- ");
        term_puts(nm);
        if (isd) term_putc('/');
        term_putc('\n');
        (*shown)++;
        if (isd) {
            char sub[176];
            cstr_copy(sub, sizeof(sub), dir);
            int k = slen_t(sub);
            if (k && sub[k - 1] != '/') { sub[k++] = '/'; sub[k] = 0; }
            for (int j = 0; nm[j] && k < (int)sizeof(sub) - 1; j++) sub[k++] = nm[j];
            sub[k] = 0;
            tree_walk(sub, depth + 1, maxd, shown);
        }
    }
}

static void cmd_tree(const char *arg) {
    int maxd = 4;
    if (arg && arg[0] >= '1' && arg[0] <= '9') maxd = arg[0] - '0';
    int shown = 0;
    term_puts(g_cwd);
    term_puts("/\n");
    tree_walk(g_cwd, 1, maxd, &shown);
    term_puts("("); term_put_uint((uint32_t)shown); term_puts(" oge)\n");
}

/* ================================================================== */
/* wf — Wolf uygulama platformu (0.7)                                  */
/*   .wlf paketleri: proje olusturma, OS ici derleme, kurulum          */
/* ================================================================== */

int  wolfc_compile(const char *src, uint8_t *out, uint32_t cap,
                   char *errmsg, int errcap);
uint32_t wlf_pack_bytecode(const char *name, const char *title,
                           const char *version, const char *author,
                           const char *category, int win_w, int win_h,
                           uint16_t flags, const uint8_t *prog, uint32_t prog_len,
                           uint8_t *out, uint32_t cap);
uint32_t wlf_pack_bytecode_ex(const char *name, const char *title,
                              const char *version, const char *author,
                              const char *category, int win_w, int win_h,
                              uint16_t flags, const char *meta,
                              const uint8_t *prog, uint32_t prog_len,
                              uint8_t *out, uint32_t cap);

static char    s_wf_src[96 * 1024];
static uint8_t s_wf_prog[64 * 1024];
static uint8_t s_wf_pkg[160 * 1024];

static void wf_write_file(const char *dir, const char *name, const void *data, uint32_t len) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in || !g_asi->fs->write) return;
    if (g_asi->fs->mkdir) g_asi->fs->mkdir(dir);
    void *h = g_asi->fs->create_in(dir, name, 0);
    if (h) g_asi->fs->write(h, data, len);
}

static void wf_help(void) {
    term_puts("Wolf uygulama platformu (.wlf) - 0.7\n");
    term_puts("  wf list                kurulu .wlf uygulamalari (rol/handles)\n");
    term_puts("  wf run <ad|dosya> [dosya]  calistir (rol neyse o)\n");
    term_puts("  wf install <yol|url>   .wlf kur (agdan indirilebilir)\n");
    term_puts("  wf pull <host> <port> <yol>  URL yazmadan indir\n");
    term_puts("  wf remove <ad>         kaldir\n");
    term_puts("  wf info <dosya>        paket bilgisi (rol, handles)\n");
    term_puts("  wf new <ad>            proje iskeleti: C:/projler/<ad>\n");
    term_puts("  wf build <ad> [ui|cli|runtime] [uzantilar]\n");
    term_puts("                         derle -> C:/apps/<ad>.wlf (bytecode)\n");
    term_puts("  wf apps                tum uygulamalar (apps)\n");
    term_puts("  wf sdk                 libWolf basligini C:/sdk'ya yaz\n");
    term_puts("  wolfdok                libWolf_dokuman.pdf olustur\n");
    term_puts("Roller: ui = pencere, cli = terminalde calisir,\n");
    term_puts("        runtime = dosya ile calisan yorumlayici (orn. python .wlf)\n");
    term_puts("Masaustu SDK (Windows/Linux): sdk/ klasoru, wlfpack ile .wlf\n");
}

static const char *role_name(int role) {
    if (role == WLF_ROLE_CLI) return "cli";
    if (role == WLF_ROLE_RUNTIME) return "runtime";
    return "ui";
}

static void wf_list(void) {
    int any = 0;
    for (int i = 0; i < g_dyn_count; i++) {
        if (g_dyn[i].kind == 0) continue;
        term_puts("  ");
        term_puts(g_dyn[i].id);
        term_puts("  ");
        term_puts(g_dyn[i].title);
        term_puts("  [");
        term_puts(role_name(g_dyn[i].role));
        term_puts("/");
        term_puts(g_dyn[i].kind == 1 ? "native" : "bytecode");
        term_puts("]");
        if (g_dyn[i].handles[0]) {
            term_puts("  ("); term_puts(g_dyn[i].handles); term_puts(")");
        }
        term_putc('\n');
        any = 1;
    }
    if (!any) term_puts("  (.wlf uygulamasi yok)\n");
}

static void wf_launch_id(const char *id) {
    int ai = app_idx_by_name(id);
    if (ai >= 0) open_registry_app(ai);
}

/* Terminalde calisan uygulama ciktisi. */
static void term_bridge_puts(const char *s) {
    if (!s) return;
    term_puts(s);
    term_flush();
}

static void wf_run(const char *what) {
    if (!what[0]) { term_puts("Kullanim: wf run <ad|dosya> [dosya]\n"); return; }

    /* "ad [dosya]" — ikinci parca rol=cli/runtime uygulamasina arguman olur. */
    char id[40];
    int n = 0;
    while (what[n] && what[n] != ' ' && n < (int)sizeof(id) - 1) { id[n] = what[n]; n++; }
    id[n] = 0;
    const char *arg = what + n;
    while (*arg == ' ') arg++;

    int idx = dyn_find(id);
    if (idx < 0) {
        /* Dosya yolu verilmis olabilir: once kur, sonra calistir. */
        char p[176];
        path_resolve(p, sizeof(p), id);
        if (wlf_is_package_name(p)) {
            char err[96];
            if (wlf_install_file(p, err, sizeof(err)) != 0) {
                term_puts("Kurulamadi: "); term_puts(err); term_putc('\n');
                return;
            }
            for (int i = g_dyn_count - 1; i >= 0; i--) {
                if (g_dyn[i].kind == 0) continue;
                idx = i; break;
            }
        }
    }
    if (idx < 0) { term_puts("Bulunamadi: "); term_puts(what); term_putc('\n'); return; }

    dyn_app_t *d = &g_dyn[idx];

    /* 0.7: rol neyse o. cli/runtime terminalde, ui pencerede. */
    if (d->role == WLF_ROLE_CLI || d->role == WLF_ROLE_RUNTIME) {
        const char *label = d->entry_name[0] ? d->entry_name : d->title;
        if (d->role == WLF_ROLE_RUNTIME && !arg[0]) {
            term_puts(label);
            term_puts(" bir calisma zamani; dosya ile calisir.\n");
            term_puts("  ornek: wf run "); term_puts(d->id);
            term_puts(" C:/ornek.");
            if (d->handles[0]) {
                int k = 0;
                while (d->handles[k] && d->handles[k] != ',') { term_putc(d->handles[k]); k++; }
            } else term_puts("dosya");
            term_putc('\n');
            term_puts("  (dosyaya cift tiklamak da bu uygulamayi secer)\n");
            return;
        }
        term_puts("> "); term_puts(label);
        if (arg[0]) { term_puts(" "); term_puts(arg); }
        term_putc('\n');
        term_flush();
        int rc = wlf_run_cli(d, arg, term_bridge_puts);
        term_puts(rc == 0 ? "[bitti]\n" : "[hata ile bitti]\n");
        term_flush();
        return;
    }

    wf_launch_id(d->id);
    term_puts("Baslatildi: "); term_puts(d->title); term_putc('\n');
}

static void wf_info(const char *path) {
    if (!path[0]) { term_puts("Kullanim: wf info <dosya.wlf>\n"); return; }
    char p[176];
    path_resolve(p, sizeof(p), path);
    uint32_t len = 0;
    if (afs_read_at(p, s_wf_pkg, sizeof(s_wf_pkg), &len) != 0) {
        term_puts("Dosya okunamadi\n"); return;
    }
    const wlf_header_t *h = 0;
    int v = wlf_validate(s_wf_pkg, len, &h);
    if (v != 0) {
        term_puts("Gecersiz .wlf (kod ");
        int k = -v;
        if (k >= 10) term_putc((char)('0' + (k / 10) % 10));
        term_putc((char)('0' + k % 10));
        term_puts(")\n");
        return;
    }
    term_puts("Ad      : "); term_puts(h->name); term_putc('\n');
    term_puts("Baslik  : "); term_puts(h->title); term_putc('\n');
    term_puts("Surum   : "); term_puts(h->ver); term_putc('\n');
    term_puts("Yazar   : "); term_puts(h->author); term_putc('\n');
    term_puts("Tur     : ");
    term_puts(h->kind == WLF_KIND_NATIVE ? "native (makine kodu)"
                                         : "bytecode (OS ici derlendi)");
    term_putc('\n');
    wlf_meta_t wm;
    int hm = (wlf_meta_read(s_wf_pkg, len, &wm) == 0);
    term_puts("Rol     : ");
    term_puts(role_name(hm ? wm.role : h->role));
    if (hm && (wm.flags & WLF_META_TERMINAL)) term_puts(" (terminal)");
    term_putc('\n');
    if (hm && wm.handles[0]) {
        term_puts("Isler   : "); term_puts(wm.handles); term_putc('\n');
    }
    if (hm && wm.entry[0]) {
        term_puts("Giris   : "); term_puts(wm.entry); term_putc('\n');
    }
    term_puts("Boyut   : "); term_put_size(len); term_putc('\n');
    term_puts("Pencere : "); term_put_uint((uint32_t)h->win_w);
    term_puts("x"); term_put_uint((uint32_t)h->win_h); term_putc('\n');
    term_puts("Izinler : ");
    if (h->flags == 0) term_puts("tam");
    else {
        if (h->flags & WLF_FLAG_FS) term_puts("fs ");
        if (h->flags & WLF_FLAG_NET) term_puts("net ");
        if (h->flags & WLF_FLAG_EXEC) term_puts("exec");
    }
    term_putc('\n');
}

static void wf_new(const char *name) {
    if (!name[0]) { term_puts("Kullanim: wf new <ad>\n"); return; }
    int ok = 1;
    for (int i = 0; name[i]; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) ok = 0;
    }
    if (!ok || slen(name) >= 30) { term_puts("Gecersiz ad (a-z, 0-9, -, _)\n"); return; }

    char dir[128];
    cstr_copy(dir, sizeof(dir), "C:/projler/");
    int l = slen(dir);
    cstr_copy(dir + l, sizeof(dir) - l, name);

    static char src[2048];
    int n = 0;
    /* Ayni kaynak hem OS icinde (bytecode) hem SDK ile (native) derlenir:
     * giris noktasi wolf_main, baglama wolf_bind (bytecode'da islevsiz). */
    const char *pre =
        "/* Arctian Wolf uygulamasi (libWolf). LVGL gorunmez.\n"
        " * OS icinde: wf build <ad>\n"
        " * Masaustu SDK: sdk/build.bat (ayni kaynak, native .wlf)\n"
        " */\n"
        "#include \"wolf.h\"\n"
        "\n"
        "int counter = 0;\n"
        "int bar = 0;\n"
        "int durum = 0;\n"
        "\n"
        "void on_inc(int w, void *user) {\n"
        "    counter = counter + 5;\n"
        "    if (counter > 100) { counter = 100; }\n"
        "    wolf_value(bar, counter);\n"
        "    wolf_text(durum, \"Sayac artti\");\n"
        "}\n"
        "\n"
        "int wolf_main(const wolf_api_t *api) {\n"
        "    wolf_bind(api);\n"
        "    int root = wolf_root();\n"
        "    wolf_win_title(\"AD\");\n"
        "    wolf_label(root, 20, 16, \"Merhaba Arctian!\");\n"
        "    int b = wolf_button(root, 20, 48, 200, 44, \"Sayaci artir\");\n"
        "    bar = wolf_progress(root, 20, 108, 260, 18, 0);\n"
        "    durum = wolf_label(root, 20, 140, \"hazir\");\n"
        "    wolf_on_click(b, on_inc);\n"
        "    return 0;\n"
        "}\n";
    for (int i = 0; pre[i] && n < (int)sizeof(src) - 1; i++) {
        if (pre[i] == 'A' && pre[i + 1] == 'D') {
            for (int k = 0; name[k] && n < (int)sizeof(src) - 1; k++) src[n++] = name[k];
            i++;
            continue;
        }
        src[n++] = pre[i];
    }
    src[n] = 0;
    wf_write_file(dir, "main.c", src, (uint32_t)n);

    char json[256];
    int j = 0;
    const char *jp = "{\n  \"name\": \"";
    for (int i = 0; jp[i] && j < (int)sizeof(json) - 1; i++) json[j++] = jp[i];
    for (int i = 0; name[i] && j < (int)sizeof(json) - 1; i++) json[j++] = name[i];
    jp = "\",\n  \"title\": \"";
    for (int i = 0; jp[i] && j < (int)sizeof(json) - 1; i++) json[j++] = jp[i];
    for (int i = 0; name[i] && j < (int)sizeof(json) - 1; i++) json[j++] = name[i];
    jp = "\",\n  \"version\": \"1.0\",\n  \"w\": 520, \"h\": 400,\n"
         "  \"permissions\": \"fs,net,exec\"\n}\n";
    for (int i = 0; jp[i] && j < (int)sizeof(json) - 1; i++) json[j++] = jp[i];
    json[j] = 0;
    wf_write_file(dir, "wolf.json", json, (uint32_t)j);

    const char *rd =
        "Wolf uygulama projesi\n\n"
        "  main.c     uygulama kaynagi (libWolf)\n"
        "  wolf.json  ust veri (baslik, pencere, izinler)\n\n"
        "OS icinde:\n"
        "  wf build <ad>\n"
        "  wf run <ad>\n\n"
        "Masaustu SDK: sdk/windows/build.bat (gcc + ld + wlfpack.py)\n";
    wf_write_file(dir, "README.txt", rd, (uint32_t)slen(rd));

    term_puts("Proje olusturuldu: "); term_puts(dir); term_putc('\n');
    term_puts("  main.c, wolf.json, README.txt\n");
    term_puts("Derlemek icin: wf build "); term_puts(name); term_putc('\n');
}

static void wf_build(const char *args) {
    if (!args[0]) { term_puts("Kullanim: wf build <proje> [ui|cli|runtime] [uzantilar]\n"); return; }
    /* 0.7: rol ve islenen uzantilar komut satirindan verilebilir:
     *   wf build sayac cli
     *   wf build python runtime py,pyw */
    char name[40];
    int ni = 0;
    while (args[ni] && args[ni] != ' ' && ni < (int)sizeof(name) - 1) { name[ni] = args[ni]; ni++; }
    name[ni] = 0;
    const char *rest = args + ni;
    while (*rest == ' ') rest++;

    char role[12] = "ui";
    char handles[WLF_HANDLES_LEN] = "";
    if (*rest) {
        int k = 0;
        while (rest[k] && rest[k] != ' ' && k < (int)sizeof(role) - 1) { role[k] = rest[k]; k++; }
        role[k] = 0;
        rest += k;
        while (*rest == ' ') rest++;
        if (*rest) {
            int h = 0;
            while (rest[h] && rest[h] != ' ' && h < (int)sizeof(handles) - 1) { handles[h] = rest[h]; h++; }
            handles[h] = 0;
        }
    }

    char dir[128], srcp[160];
    cstr_copy(dir, sizeof(dir), "C:/projler/");
    int l = slen(dir);
    cstr_copy(dir + l, sizeof(dir) - l, name);
    cstr_copy(srcp, sizeof(srcp), dir);
    l = slen(srcp);
    cstr_copy(srcp + l, sizeof(srcp) - l, "/main.c");

    uint32_t n = 0;
    if (g_asi && g_asi->fs && g_asi->fs->read_path)
        n = g_asi->fs->read_path(srcp, s_wf_src, sizeof(s_wf_src) - 1);
    if (!n) { term_puts("Kaynak okunamadi: "); term_puts(srcp); term_putc('\n'); return; }
    s_wf_src[n] = 0;

    char err[128];
    term_puts("Derleniyor (Wolf C alt kumesi)...\n");
    int plen = wolfc_compile(s_wf_src, s_wf_prog, sizeof(s_wf_prog), err, sizeof(err));
    if (plen <= 0) {
        term_puts("Derleme hatasi: "); term_puts(err[0] ? err : "bilinmeyen");
        term_putc('\n');
        return;
    }

    char meta[96];
    int ml = 0;
    {
        const char *parts[3];
        char rl[24], hl[56];
        int o = 0;
        const char *rp = "role=";
        for (int i = 0; rp[i]; i++) rl[o++] = rp[i];
        for (int i = 0; role[i] && o < (int)sizeof(rl) - 2; i++) rl[o++] = role[i];
        rl[o++] = '\n'; rl[o] = 0;
        parts[ml++] = rl;
        if (handles[0]) {
            o = 0;
            const char *hp = "handles=";
            for (int i = 0; hp[i]; i++) hl[o++] = hp[i];
            for (int i = 0; handles[i] && o < (int)sizeof(hl) - 2; i++) hl[o++] = handles[i];
            hl[o++] = '\n'; hl[o] = 0;
            parts[ml++] = hl;
        }
        if (role[0] == 'r' && role[1] == 'u') parts[ml++] = "terminal=1\n";
        int k = 0;
        for (int p = 0; p < ml; p++)
            for (int i = 0; parts[p][i] && k < (int)sizeof(meta) - 1; i++) meta[k++] = parts[p][i];
        meta[k] = 0;
    }

    uint32_t total = wlf_pack_bytecode_ex(name, name, "1.0", "arctian",
                                          "Genel", 520, 400,
                                          WLF_FLAG_FS | WLF_FLAG_NET | WLF_FLAG_EXEC,
                                          meta,
                                          s_wf_prog, (uint32_t)plen,
                                          s_wf_pkg, sizeof(s_wf_pkg));
    if (!total) { term_puts("Paket olusturulamadi\n"); return; }

    char fn[64];
    cstr_copy(fn, sizeof(fn), name);
    l = slen(fn);
    cstr_copy(fn + l, sizeof(fn) - l, ".wlf");
    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/apps");
    void *h = g_asi->fs->create_in("C:/apps", fn, 0);
    if (!h) { term_puts("C:/apps'e yazilamadi\n"); return; }
    if (g_asi->fs->write(h, s_wf_pkg, total) != 0) {
        term_puts("Yazma hatasi\n"); return;
    }

    char full[128];
    cstr_copy(full, sizeof(full), "C:/apps/");
    l = slen(full);
    cstr_copy(full + l, sizeof(full) - l, fn);
    wlf_scan_one(full);

    term_puts("Derlendi: "); term_puts(full); term_putc('\n');
    term_puts("  bytecode "); term_put_size((uint32_t)plen);
    term_puts(", paket "); term_put_size(total); term_putc('\n');
    term_puts("Calistir: wf run "); term_puts(name); term_putc('\n');
}

static void wf_sdk(void) {
    static const char *mini =
        "/* libWolf — Arctian uygulama katmani (ozet)\n"
        " * Tam baslik ve ornekler: sdk/windows/wolf.h\n"
        " * Giris: int main(void)  (veya wolf_main(const wolf_api_t*))\n"
        " */\n"
        "#include \"wolf.h\"\n";
    wf_write_file("C:/sdk", "wolf.h", mini, (uint32_t)slen(mini));
    term_puts("SDK ozeti yazildi: C:/sdk/wolf.h\n");
    term_puts("Tam SDK (Windows/masaustu): sdk/ klasoru\n");
}

static void wf_install(const char *what) {
    if (!what[0]) { term_puts("Kullanim: wf install <yol|url>\n"); return; }
    char err[128];
    int r;
    if (starts_with(what, "http://") || starts_with(what, "https://")) {
        term_puts("Indiriliyor: "); term_puts(what); term_putc('\n');
        term_flush();
        r = wlf_install_url(what, err, sizeof(err));
    } else {
        char p[176];
        path_resolve(p, sizeof(p), what);
        r = wlf_install_file(p, err, sizeof(err));
    }
    if (r == 0) { term_puts("Kuruldu\n"); wf_list(); }
    else { term_puts("Kurulum hatasi: "); term_puts(err); term_putc('\n'); }
}

/* wf pull <host> <port> <yol>
 * URL'yi parcalardan kurar: noktalama yazmaya gerek yok (klavye duzeni ne
 * olursa olsun calisir). Ornek: wf pull 10.0.2.2 8099 merhaba.wlf
 * Ayni zamanda "wf install http://..." ile ayni isi yapar. */
static void wf_pull(const char *args) {
    char host[64], port[16], path[160];
    int i = 0, n = 0;
    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ' && n < (int)sizeof(host) - 1) host[n++] = args[i++];
    host[n] = 0;
    while (args[i] == ' ') i++;
    n = 0;
    while (args[i] && args[i] != ' ' && n < (int)sizeof(port) - 1) port[n++] = args[i++];
    port[n] = 0;
    while (args[i] == ' ') i++;
    n = 0;
    while (args[i] && args[i] != ' ' && n < (int)sizeof(path) - 1) path[n++] = args[i++];
    path[n] = 0;
    if (!host[0] || !port[0] || !path[0]) {
        term_puts("Kullanim: wf pull <host> <port> <yol>\n");
        term_puts("  ornek: wf pull 10.0.2.2 8099 merhaba.wlf\n");
        return;
    }
    char url[288];
    int u = 0;
    const char *pre = "http://";
    for (int k = 0; pre[k] && u < (int)sizeof(url) - 1; k++) url[u++] = pre[k];
    for (int k = 0; host[k] && u < (int)sizeof(url) - 1; k++) url[u++] = host[k];
    if (u < (int)sizeof(url) - 1) url[u++] = ':';
    for (int k = 0; port[k] && u < (int)sizeof(url) - 1; k++) url[u++] = port[k];
    if (u < (int)sizeof(url) - 1) url[u++] = '/';
    for (int k = 0; path[k] && u < (int)sizeof(url) - 1; k++) url[u++] = path[k];
    url[u] = 0;

    term_puts("Indiriliyor: "); term_puts(url); term_putc('\n');
    term_flush();
    char err[128];
    int r = wlf_install_url(url, err, sizeof(err));
    if (r == 0) {
        term_puts("Kuruldu\n");
        wf_list();
        term_puts("Calistir: wf run <ad>\n");
    } else {
        term_puts("Kurulum hatasi: "); term_puts(err); term_putc('\n');
        term_puts("Ipucu: QEMU'da host 10.0.2.2'dir; sunucu 0.0.0.0 dinlemeli.\n");
    }
}

static void cmd_wolfdok(const char *arg) {
    (void)arg;
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in || !g_asi->fs->write) {
        term_puts("Dosya sistemi yok\n"); return;
    }
    if (g_asi->fs->mkdir) g_asi->fs->mkdir(g_cwd);
    void *h = g_asi->fs->create_in(g_cwd, "libWolf_dokuman.pdf", 0);
    if (!h) { term_puts("Dosya olusturulamadi\n"); return; }
    if (g_asi->fs->write(h, LIBWOLF_DOC_PDF, LIBWOLF_DOC_PDF_SIZE) != 0) {
        term_puts("Yazma hatasi\n"); return;
    }
    term_puts("Olusturuldu: "); term_puts(g_cwd); term_puts("/libWolf_dokuman.pdf\n");
    term_puts("Boyut: "); term_put_size(LIBWOLF_DOC_PDF_SIZE); term_putc('\n');
}

static void cmd_wf(const char *arg) {
    while (*arg == ' ') arg++;
    if (!arg[0] || str_eq(arg, "help")) { wf_help(); return; }
    if (str_eq(arg, "list")) { wf_list(); return; }
    if (str_eq(arg, "apps")) { cmd_apps(); return; }
    if (str_eq(arg, "sdk")) { wf_sdk(); return; }
    if (starts_with(arg, "run ")) { wf_run(arg + 4); return; }
    if (starts_with(arg, "info ")) { wf_info(arg + 5); return; }
    if (starts_with(arg, "new ")) { wf_new(arg + 4); return; }
    if (starts_with(arg, "build ")) { wf_build(arg + 6); return; }
    if (starts_with(arg, "install ")) { wf_install(arg + 8); return; }
    if (starts_with(arg, "pull ")) { wf_pull(arg + 5); return; }
    if (starts_with(arg, "remove ")) {
        const char *id = arg + 7;
        while (*id == ' ') id++;
        int r = wlf_remove(id);
        if (r == 0) { term_puts("Kaldirildi: "); term_puts(id); term_putc('\n'); }
        else if (r == -2) term_puts("Uygulama acik, once kapat\n");
        else term_puts("Bulunamadi\n");
        return;
    }
    wf_help();
}

static void cmd_editor(const char *arg) {
    if (!arg[0]) { term_puts("Kullanim: editor <dosya>\n"); return; }
    char p[176];
    path_resolve(p, sizeof(p), arg);
    arctian_shell_editor(p);
    term_puts("Duzenleyici acildi: "); term_puts(p); term_putc('\n');
}

static int g_wolf_inited;

static void wolf_print_cb(const char *s, unsigned len) {
    for (unsigned i = 0; i < len; i++) term_putc(s[i]);
}

static void cmd_wolf(const char *arg) {
    if (!g_wolf_inited) { wolf_engine_set_print(wolf_print_cb); g_wolf_inited = 1; }
    while (*arg == ' ') arg++;
    if (!arg[0]) {
        term_puts("Wolf Engine JavaScript motoru\n");
        term_puts("  wolf run <dosya.js>   dosyayi calistir\n");
        term_puts("  wolf -e \"kod\"         kodu calistir\n");
        term_puts("  wolf reset            motoru sifirla\n");
    term_puts("  wolfdok               libWolf_dokuman.pdf olustur\n");
        return;
    }
    if (str_eq(arg, "reset")) { wolf_engine_reset(); term_puts("Motor sifirlandi\n"); return; }
    if (starts_with(arg, "run")) {
        const char *f = arg + 3;
        while (*f == ' ') f++;
        if (!f[0]) { term_puts("Kullanim: wolf run <dosya.js>\n"); return; }
        char p[176];
        path_resolve(p, sizeof(p), f);
        static char src[32768];
        uint32_t n = (g_asi && g_asi->fs && g_asi->fs->read_path) ? g_asi->fs->read_path(p, src, sizeof(src) - 1) : 0;
        if (!n) { term_puts("Dosya okunamadi: "); term_puts(p); term_putc('\n'); return; }
        src[n] = 0;
        char out[512];
        out[0] = 0;
        wolf_engine_reset();
        int r = wolf_engine_eval(src, n, out, sizeof(out));
        if (out[0]) {
            term_puts(out);
            if (out[slen(out) - 1] != '\n') term_putc('\n');
        } else if (r != 0) {
            term_puts("Calistirma hatasi\n");
        }
        return;
    }
    if (starts_with(arg, "-e")) {
        const char *code = arg + 2;
        while (*code == ' ') code++;
        int L = slen(code);
        if (L >= 2 && ((code[0] == '"' && code[L - 1] == '"') || (code[0] == '\'' && code[L - 1] == '\''))) {
            code++;
            L -= 2;
        }
        static char cbuf[8192];
        int k = 0;
        for (; k < L && k < (int)sizeof(cbuf) - 1; k++) cbuf[k] = code[k];
        cbuf[k] = 0;
        char out[512];
        out[0] = 0;
        int r = wolf_engine_eval(cbuf, (uint32_t)k, out, sizeof(out));
        if (out[0]) {
            term_puts(out);
            if (out[slen(out) - 1] != '\n') term_putc('\n');
        } else if (r != 0) {
            term_puts("Calistirma hatasi\n");
        }
        return;
    }
    term_puts("Kullanim: wolf run <dosya.js> | wolf -e \"kod\" | wolf reset\n");
}

static void term_exec(asi_t *asi, const char *cmd);
static void sh_hist_save(void);

static const char *env_get(const char *k) {
    for (int i = 0; i < g_env_n; i++)
        if (str_eq(g_env_k[i], k)) return g_env_v[i];
    return 0;
}

static void env_set(const char *k, const char *v) {
    if (!k || !k[0]) return;
    for (int i = 0; i < g_env_n; i++)
        if (str_eq(g_env_k[i], k)) { cstr_copy(g_env_v[i], sizeof(g_env_v[0]), v); return; }
    if (g_env_n >= SH_ENV_MAX) return;
    cstr_copy(g_env_k[g_env_n], sizeof(g_env_k[0]), k);
    cstr_copy(g_env_v[g_env_n], sizeof(g_env_v[0]), v);
    g_env_n++;
}

static void env_unset(const char *k) {
    for (int i = 0; i < g_env_n; i++) {
        if (!str_eq(g_env_k[i], k)) continue;
        for (int j = i; j < g_env_n - 1; j++) {
            cstr_copy(g_env_k[j], sizeof(g_env_k[0]), g_env_k[j + 1]);
            cstr_copy(g_env_v[j], sizeof(g_env_v[0]), g_env_v[j + 1]);
        }
        g_env_n--;
        return;
    }
}

static void sh_env_init(void) {
    g_env_n = 0;
    const char *u = arctian_auth_current();
    const char *h = (u && u[0]) ? arctian_auth_home(u) : 0;
    env_set("USER", (u && u[0]) ? u : "kullanici");
    env_set("HOME", (h && h[0]) ? h : "C:/");
    env_set("PWD", g_cwd);
    env_set("SHELL", "arctian");
    env_set("PATH", "C:/");
}

static void sh_env_print(void) {
    for (int i = 0; i < g_env_n; i++) {
        term_puts(g_env_k[i]); term_putc('='); term_puts(g_env_v[i]); term_putc('\n');
    }
}

static void sh_set_cmd(const char *arg) {
    if (!arg[0]) { sh_env_print(); return; }
    char kv[160];
    cstr_copy(kv, sizeof(kv), arg);
    char *eq = 0;
    for (int i = 0; kv[i]; i++) if (kv[i] == '=') { eq = &kv[i]; break; }
    if (!eq) { term_puts("Kullanim: set AD=DEGER\n"); return; }
    *eq = 0;
    env_set(kv, eq + 1);
}

static void sh_hist_add(const char *cmd) {
    if (!cmd || !cmd[0]) return;
    if (g_hist_n > 0 && str_eq(g_hist[(g_hist_n - 1) % SH_HIST_MAX], cmd)) {
        g_hist_pos = g_hist_n;
        return;
    }
    cstr_copy(g_hist[g_hist_n % SH_HIST_MAX], 256, cmd);
    g_hist_n++;
    g_hist_pos = g_hist_n;
    sh_hist_save();
}

/* Komut gecmisi kalici: her komuttan sonra dosyaya yazilir, terminal
 * acilisinda geri yuklenir. Onceden yalniz RAM'deydi ve pencere kapaninca
 * kayboluyordu. */
#define SH_HIST_FILE "C:/etc/term_hist.cfg"

static void sh_hist_save(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in || !g_asi->fs->write) return;
    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/etc");
    /* tek tampon, tek write: AFS'de append yok, ust uste write cagrisi
     * dosyada yalniz son satiri birakirdi (gecmis kaybi). */
    static char buf[8192];
    uint32_t n = 0, cap = sizeof(buf) - 1;
    int start = g_hist_n > SH_HIST_MAX ? g_hist_n - SH_HIST_MAX : 0;
    for (int i = start; i < g_hist_n; i++) {
        const char *c = g_hist[i % SH_HIST_MAX];
        uint32_t l = 0;
        while (c[l]) l++;
        if (l + 1 > cap - n) break;
        for (uint32_t k = 0; k < l; k++) buf[n++] = c[k];
        buf[n++] = '\n';
    }
    void *h = g_asi->fs->create_in("C:/etc", "term_hist.cfg", 0);
    if (!h) return;
    g_asi->fs->write(h, buf, n);
}

static void sh_hist_load(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->read_path) return;
    static char buf[8192];
    uint32_t n = g_asi->fs->read_path(SH_HIST_FILE, buf, sizeof(buf) - 1);
    if (!n) return;
    buf[n] = 0;
    char line[256];
    int o = 0;
    for (uint32_t i = 0; i <= n; i++) {
        if (i == n || buf[i] == '\n' || buf[i] == '\r') {
            line[o] = 0;
            if (o > 0) {
                cstr_copy(g_hist[g_hist_n % SH_HIST_MAX], 256, line);
                g_hist_n++;
            }
            o = 0;
            if (buf[i] == '\r' && i + 1 < n && buf[i + 1] == '\n') i++;
            continue;
        }
        if (o < 255) line[o++] = buf[i];
    }
    g_hist_pos = g_hist_n;
}

static const char *sh_hist_at(int i) {
    if (i < 0 || i >= g_hist_n) return 0;
    return g_hist[i % SH_HIST_MAX];
}

static void sh_history(void) {
    for (int i = 0; i < g_hist_n; i++) {
        term_put_uint((uint32_t)(i + 1)); term_puts("  ");
        term_puts(sh_hist_at(i)); term_putc('\n');
    }
}

static void sh_expand(const char *in, char *out, int cap) {
    int o = 0;
    for (int i = 0; in[i] && o < cap - 1; ) {
        if (in[i] != '$') { out[o++] = in[i++]; continue; }
        i++;
        char key[24];
        int k = 0;
        if (in[i] == '{') {
            i++;
            while (in[i] && in[i] != '}' && k < 23) key[k++] = in[i++];
            if (in[i] == '}') i++;
        } else {
            while ((in[i] >= 'A' && in[i] <= 'Z') || (in[i] >= 'a' && in[i] <= 'z') ||
                   (in[i] >= '0' && in[i] <= '9') || in[i] == '_') {
                if (k < 23) key[k++] = in[i];
                i++;
            }
        }
        key[k] = 0;
        const char *v = key[0] ? env_get(key) : 0;
        if (v) for (int j = 0; v[j] && o < cap - 1; j++) out[o++] = v[j];
    }
    out[o] = 0;
}

static int sh_read_file(const char *path, char *buf, int cap) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->read_path) return -1;
    char p[176];
    path_resolve(p, sizeof(p), path);
    uint32_t n = g_asi->fs->read_path(p, buf, (uint32_t)(cap - 1));
    if (n >= (uint32_t)cap) n = (uint32_t)cap - 1;
    buf[n] = 0;
    return (int)n;
}

static int sh_write_file(const char *path, const char *data, int len, int append) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create) return -1;
    char p[176];
    path_resolve(p, sizeof(p), path);

    if (append) {
        int old = sh_read_file(path, g_fbuf, SH_BUF_MAX);
        if (old < 0) old = 0;
        if (old + len >= SH_BUF_MAX) return -1;
        for (int i = 0; i < len; i++) g_fbuf[old + i] = data[i];
        len = old + len;
        data = g_fbuf;
    }

    char dir[176];
    const char *base = p;
    const char *slash = 0;
    for (const char *q = p; *q; q++) if (*q == '/') slash = q;
    if (slash) {
        int dl = (int)(slash - p) + 1;
        if (dl >= (int)sizeof(dir)) return -1;
        for (int i = 0; i < dl; i++) dir[i] = p[i];
        dir[dl] = 0;
        base = slash + 1;
    } else {
        dir[0] = 0;
    }

    void *h = 0;
    if (dir[0] && g_asi->fs->create_in) h = g_asi->fs->create_in(dir, base, 0);
    if (!h) h = g_asi->fs->create(0, p, 0);
    if (!h) return -1;
    int wr = g_asi->fs->write ? g_asi->fs->write(h, data, (uint32_t)len) : -1;
    return wr == 0 ? len : -1;
}

static int sh_ci_has(const char *hay, const char *needle) {
    if (!needle[0]) return 0;
    for (const char *p = hay; *p; p++) {
        const char *h = p, *n = needle;
        while (*h && *n) {
            char a = *h, b = *n;
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            h++; n++;
        }
        if (!*n) return 1;
    }
    return 0;
}

static void sh_cat(const char *arg) {
    if (arg[0]) { cmd_cat(arg); return; }
    if (!g_stdin) { term_puts("Kullanim: cat <dosya>  (veya boru ile)\n"); return; }
    term_puts(g_stdin);
    int n = slen(g_stdin);
    if (n > 0 && g_stdin[n - 1] != '\n') term_putc('\n');
}

static void sh_grep(const char *arg) {
    char pat[96];
    int k = 0;
    while (arg[k] && arg[k] != ' ' && k < 95) { pat[k] = arg[k]; k++; }
    pat[k] = 0;
    const char *rest = arg + k;
    while (*rest == ' ') rest++;
    if (!pat[0]) { term_puts("Kullanim: grep <desen> [dosya]\n"); return; }

    const char *src = g_stdin;
    if (rest[0]) {
        if (sh_read_file(rest, g_fbuf, SH_BUF_MAX) < 0) { term_puts("Okunamadi: "); term_puts(rest); term_putc('\n'); return; }
        src = g_fbuf;
    }
    if (!src) { term_puts("grep: girdi yok (boru veya dosya verin)\n"); return; }

    const char *p = src;
    while (*p) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        int len = (int)(e - p);
        char line[512];
        int m = len < 511 ? len : 511;
        for (int i = 0; i < m; i++) line[i] = p[i];
        line[m] = 0;
        if (sh_ci_has(line, pat)) { term_puts(line); term_putc('\n'); }
        p = (*e == '\n') ? e + 1 : e;
    }
}

static void sh_wc(const char *arg) {
    const char *src = g_stdin;
    char name[176];
    cstr_copy(name, sizeof(name), arg);
    while (name[0] == ' ') { for (int i = 0; name[i]; i++) name[i] = name[i + 1]; }
    if (name[0]) {
        if (sh_read_file(name, g_fbuf, SH_BUF_MAX) < 0) { term_puts("Okunamadi\n"); return; }
        src = g_fbuf;
    }
    if (!src) { term_puts("wc: girdi yok\n"); return; }
    int lines = 0, words = 0, chars = 0, inw = 0;
    for (const char *p = src; *p; p++) {
        chars++;
        if (*p == '\n') lines++;
        if (*p == ' ' || *p == '\t' || *p == '\n') inw = 0;
        else if (!inw) { inw = 1; words++; }
    }
    term_put_uint((uint32_t)lines); term_putc(' ');
    term_put_uint((uint32_t)words); term_putc(' ');
    term_put_uint((uint32_t)chars);
    if (name[0]) { term_puts("  "); term_puts(name); }
    term_putc('\n');
}

static void sh_head(const char *arg) {
    int n = 10;
    if (arg[0] >= '0' && arg[0] <= '9') {
        n = 0;
        for (int i = 0; arg[i] >= '0' && arg[i] <= '9'; i++) n = n * 10 + (arg[i] - '0');
    }
    if (!g_stdin) { term_puts("head: girdi yok\n"); return; }
    const char *p = g_stdin;
    int c = 0;
    while (*p && c < n) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        for (const char *q = p; q < e; q++) term_putc(*q);
        term_putc('\n');
        c++;
        p = (*e == '\n') ? e + 1 : e;
    }
}

static char g_sortbuf[SH_BUF_MAX];

static void sh_sort(const char *arg) {
    (void)arg;
    if (!g_stdin) { term_puts("sort: girdi yok\n"); return; }
    cstr_copy(g_sortbuf, SH_BUF_MAX, g_stdin);
    int start[512], len[512], n = 0, p = 0;
    while (g_sortbuf[p] && n < 512) {
        int s = p;
        while (g_sortbuf[p] && g_sortbuf[p] != '\n') p++;
        start[n] = s;
        len[n] = p - s;
        n++;
        if (g_sortbuf[p] == '\n') p++;
    }
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int m = len[i] < len[j] ? len[i] : len[j];
            int cmp = 0;
            for (int k = 0; k < m; k++) {
                if (g_sortbuf[start[i] + k] != g_sortbuf[start[j] + k]) {
                    cmp = g_sortbuf[start[i] + k] - g_sortbuf[start[j] + k];
                    break;
                }
            }
            if (!cmp) cmp = len[i] - len[j];
            if (cmp > 0) {
                int ts = start[i]; start[i] = start[j]; start[j] = ts;
                int tl = len[i]; len[i] = len[j]; len[j] = tl;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < len[i]; k++) term_putc(g_sortbuf[start[i] + k]);
        term_putc('\n');
    }
}

static void sh_tee(const char *arg) {
    if (!arg[0]) { term_puts("Kullanim: tee <dosya>\n"); return; }
    if (!g_stdin) { term_puts("tee: girdi yok\n"); return; }
    sh_write_file(arg, g_stdin, slen(g_stdin), 0);
    term_puts(g_stdin);
    int n = slen(g_stdin);
    if (n > 0 && g_stdin[n - 1] != '\n') term_putc('\n');
}

static void cmd_print(const char *arg) {
    if (!g_asi || !g_asi->spool || !g_asi->spool->submit) {
        term_puts("Spool servisi yok\n");
        return;
    }
    if (!arg[0]) { term_puts("Kullanim: print <dosya>\n"); return; }
    static char pbuf[4096];
    int n = sh_read_file(arg, pbuf, sizeof(pbuf));
    if (n <= 0) { term_puts("Okunamadi: "); term_puts(arg); term_putc('\n'); return; }
    int id = g_asi->spool->submit(arg, pbuf, (uint32_t)n);
    if (id < 0) { term_puts("Kuyruk dolu\n"); return; }
    term_puts("Kuyruga eklendi: is #"); term_put_uint((uint32_t)id); term_putc('\n');
}

static void cmd_jobs(void) {
    if (!g_asi || !g_asi->spool) { term_puts("Spool servisi yok\n"); return; }
    int n = g_asi->spool->count();
    if (n <= 0) { term_puts("Kuyruk bos\n"); return; }
    term_puts("Yazdirma kuyrugu ("); term_put_uint((uint32_t)n); term_puts(" is):\n");
    for (int i = 0; i < n; i++) {
        int id = 0;
        char nm[48];
        uint32_t len = 0;
        if (g_asi->spool->info(i, &id, nm, sizeof(nm), &len) != 0) continue;
        term_puts("  #"); term_put_uint((uint32_t)id);
        term_puts("  "); term_puts(nm);
        term_puts("  "); term_put_uint(len); term_puts(" bayt\n");
    }
}

static void cmd_lp(void) {
    if (!g_asi || !g_asi->spool || !g_asi->spool->process) {
        term_puts("Spool servisi yok\n");
        return;
    }
    int n = g_asi->spool->process();
    if (n <= 0) { term_puts("Kuyruk bos\n"); return; }
    char p[96];
    if (g_asi->spool->last_path) g_asi->spool->last_path(p, sizeof(p));
    term_puts("Yazdirildi: "); term_put_uint((uint32_t)n);
    term_puts(" is -> "); term_puts(p); term_putc('\n');
}

static void sh_trim(char *s) {
    int n = slen(s);
    int a = 0;
    while (a < n && (s[a] == ' ' || s[a] == '\t')) a++;
    int b = n;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    int k = 0;
    for (int i = a; i < b; i++) s[k++] = s[i];
    s[k] = 0;
}

static int sh_parse_redir(char *s, char *opath, int *append, char *ipath) {
    char out[512];
    int o = 0, found = 0;
    opath[0] = 0;
    ipath[0] = 0;
    *append = 0;

    for (int i = 0; s[i]; ) {
        if (s[i] == '\'' || s[i] == '"') {
            char q = s[i];
            out[o++] = s[i++];
            while (s[i] && s[i] != q && o < 510) out[o++] = s[i++];
            if (s[i] == q) out[o++] = s[i++];
            continue;
        }
        if (s[i] == '>' || s[i] == '<') {
            char op = s[i];
            int app = 0;
            i++;
            if (op == '>' && s[i] == '>') { app = 1; i++; }
            while (s[i] == ' ') i++;
            char path[176];
            int k = 0;
            while (s[i] && s[i] != ' ' && k < 175) path[k++] = s[i++];
            path[k] = 0;
            if (op == '>') {
                cstr_copy(opath, 176, path);
                *append = app;
            } else {
                cstr_copy(ipath, 176, path);
            }
            found = 1;
            continue;
        }
        if (o < 510) out[o++] = s[i];
        i++;
    }
    out[o] = 0;
    cstr_copy(s, 512, out);
    sh_trim(s);
    return found;
}

static void sh_run(asi_t *asi, const char *raw) {
    char line[512];
    sh_expand(raw, line, sizeof(line));
    sh_trim(line);

    term_puts("arctian> ");
    term_puts(raw);
    term_putc('\n');

    if (!line[0]) { term_flush(); return; }
    sh_hist_add(raw);

    static char parts[8][512];
    int np = 1, p = 0;
    parts[0][0] = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '|' && np < 8) { parts[np - 1][p] = 0; np++; p = 0; continue; }
        if (p < 511) parts[np - 1][p++] = line[i];
    }
    parts[np - 1][p] = 0;

    static char inbuf[SH_BUF_MAX];
    const char *in = 0;

    for (int s = 0; s < np; s++) {
        char cmd[512];
        cstr_copy(cmd, sizeof(cmd), parts[s]);
        sh_trim(cmd);

        char opath[176], ipath[176];
        int append = 0;
        sh_parse_redir(cmd, opath, &append, ipath);

        if (ipath[0]) {
            if (sh_read_file(ipath, inbuf, SH_BUF_MAX) < 0) {
                term_puts("Dosya okunamadi: "); term_puts(ipath); term_putc('\n');
                break;
            }
            in = inbuf;
        }
        g_stdin = in;

        int last = (s == np - 1);
        int capture = (!last) || opath[0];

        g_cap_on = capture ? 1 : 0;
        g_cap_len = 0;

        if (cmd[0]) term_exec(asi, cmd);

        if (capture) {
            g_cap_on = 0;
            if (g_cap_len >= TERM_LOG_MAX) g_cap_len = TERM_LOG_MAX - 1;
            g_cap[g_cap_len] = 0;
            if (last && opath[0]) {
                if (sh_write_file(opath, g_cap, g_cap_len, append) < 0) {
                    term_puts("Yazilamadi: "); term_puts(opath); term_putc('\n');
                }
            } else {
                int n = g_cap_len < SH_BUF_MAX - 1 ? g_cap_len : SH_BUF_MAX - 1;
                for (int i = 0; i < n; i++) inbuf[i] = g_cap[i];
                inbuf[n] = 0;
                in = inbuf;
            }
        }
        g_stdin = 0;
        env_set("PWD", g_cwd);
    }
    g_cap_on = 0;
    g_stdin = 0;
    term_flush();
}

static void cmd_shelltest(void) {
    char buf[1024];
    int ok = 1;

    if (g_asi && g_asi->fs && g_asi->fs->remove) {
        g_asi->fs->remove("_sht1.txt");
        g_asi->fs->remove("_sht2.txt");
        g_asi->fs->remove("_sht3.txt");
    }

    term_puts("[shelltest] 0) dogrudan yaz/oku\n");
    int w0 = sh_write_file("_sht0.txt", "XYZ\n", 4, 0);
    int r0 = sh_read_file("_sht0.txt", buf, sizeof(buf));
    term_puts("[shelltest]    w="); term_put_uint((uint32_t)(w0 < 0 ? 9999 : w0));
    term_puts(" r="); term_put_uint((uint32_t)r0);
    term_puts(" ["); term_puts(r0 > 0 ? buf : ""); term_puts("]\n");
    if (w0 != 4 || r0 != 4) ok = 0;

    term_puts("[shelltest] 1) ls | cat > dosya\n");
    sh_run(g_asi, "ls | cat > _sht1.txt");
    int n1 = sh_read_file("_sht1.txt", buf, sizeof(buf));
    term_puts("[shelltest]    r="); term_put_uint((uint32_t)n1);
    term_puts(" -> "); term_puts(n1 > 0 ? "OK" : "HATA"); term_putc('\n');
    if (n1 <= 0) ok = 0;

    term_puts("[shelltest] 2) cat dosya | wc\n");
    sh_run(g_asi, "cat _sht1.txt | wc");

    term_puts("[shelltest] 3) sort < a > b\n");
    sh_write_file("_sht2.txt", "b\na\nc\n", 6, 0);
    sh_run(g_asi, "sort < _sht2.txt > _sht3.txt");
    int n3 = sh_read_file("_sht3.txt", buf, sizeof(buf));
    int sorted = (n3 == 6 && buf[0] == 'a' && buf[1] == '\n' &&
                  buf[2] == 'b' && buf[3] == '\n' && buf[4] == 'c' && buf[5] == '\n');
    term_puts("[shelltest]    -> "); term_puts(sorted ? "OK" : "HATA"); term_putc('\n');
    if (!sorted) ok = 0;

    term_puts("[shelltest] 4) cat $PWD/dosya + env\n");
    sh_run(g_asi, "cat $PWD/_sht1.txt");
    const char *u = env_get("USER");
    term_puts("[shelltest]    USER="); term_puts(u ? u : "(yok)"); term_putc('\n');
    if (!u || !u[0]) ok = 0;

    term_puts("[shelltest] SONUC: "); term_puts(ok ? "OK" : "HATA"); term_putc('\n');

    if (g_asi && g_asi->fs && g_asi->fs->remove) {
        g_asi->fs->remove("_sht1.txt");
        g_asi->fs->remove("_sht2.txt");
        g_asi->fs->remove("_sht3.txt");
    }
}

static void term_exec(asi_t *asi, const char *cmd) {
    while (*cmd == ' ') cmd++;

    if (!cmd[0]) { term_flush(); return; }

    char name[16];
    int i = 0;
    while (cmd[i] && cmd[i] != ' ' && i < 15) { name[i] = cmd[i]; i++; }
    name[i] = 0;
    const char *arg = cmd + i;
    while (*arg == ' ') arg++;

    if (str_eq(name, "help")) {
        term_puts("Arctian Terminal komutlari:\n");
        term_puts("  help                 bu yardim\n");
        term_puts("  clear                ekrani temizle\n");
        term_puts("  apps                 uygulamalari listele\n");
        term_puts("  run <uygulama>       uygulama baslat (open)\n");
        term_puts("  close <uygulama>     pencereyi kapat (kill)\n");
        term_puts("  ps                   acik pencereler\n");
        term_puts("  proc                 surecleri listele (PID/PPID/durum)\n");
        term_puts("  proctest             surec testi (spawn/exit/zombie topla)\n");
        term_puts("  wait <pid>           surec bitisini bekle (zombie topla)\n");
        term_puts("  reap                 bekleyen zombie'leri topla\n");
        term_puts("  labtest              sync/thread/ipc self-test\n");
        term_puts("  capstest             sandbox/izin zorlama testi\n");
    term_puts("  onaytest [klasor]    onay penceresini gercek olarak acar\n");
        term_puts("  permtest             dosya izin/sahiplik testi\n");
        term_puts("  socktest             ham TCP/UDP soket testi\n");
        term_puts("  shelltest            boru/yonlendirme/ortam testi\n");
        term_puts("  <komut> | <komut>    boru (pipe): ciktiyi sonrakine ver\n");
        term_puts("  <komut> > dosya      ciktiyi dosyaya yaz (>> ekle)\n");
        term_puts("  <komut> < dosya      girdiyi dosyadan oku\n");
        term_puts("  grep <desen> [dosya] satirlarda ara (boru/dosya)\n");
        term_puts("  wc [dosya]           satir/kelime/bayt say\n");
        term_puts("  head [n]             ilk n satir (boru)\n");
        term_puts("  sort                 satirlari sirala (boru)\n");
        term_puts("  tee <dosya>          girdiyi dosyaya yaz + ekrana bas\n");
        term_puts("  env                  ortam degiskenleri\n");
        term_puts("  set AD=DEGER          ortam degiskeni ata (export)\n");
        term_puts("  unset AD             degiskeni sil\n");
        term_puts("  history              komut gecmisi (yukari/asagi ok)\n");
        term_puts("  fsck [-f]            dosya sistemi denetimi/onarimi\n");
        term_puts("  threads              aktif thread + shm bolge sayisi\n");
        term_puts("  wallpaper [list]     arka planlari listele\n");
        term_puts("  wallpaper set <wp>   arka plan sec\n");
        term_puts("  wallpaper reset      varsayilana don\n");
        term_puts("  ls                   dosyalari listele (dir)\n");
        term_puts("  cat <dosya>          dosya icerigini goster\n");
        term_puts("  rm <dosya>           dosya sil (del)\n");
        term_puts("  mv <kaynak> <hedef>  tasi/yeniden adlandir (move)\n");
        term_puts("  cp <kaynak> <hedef>  dosya kopyala\n");
        term_puts("  find <parca>         klasor agacinda ara\n");
        term_puts("  du [klasor]          boyut ozeti\n");
        term_puts("  tree [derinlik]      klasor agaci\n");
        term_puts("  stat <dosya>         boyut/tur/izin bilgisi\n");
        term_puts("  wf ...               Wolf uygulama platformu (.wlf) — wf help\n");
        term_puts("  touch <dosya>        bos dosya olustur\n");
        term_puts("  trash <dosya>        Cop Kutusu'na tasi (geri alinabilir)\n");
        term_puts("  cd <klasor>          klasor degistir\n");
        term_puts("  pwd                  gecerli klasor\n");
        term_puts("  mkdir <klasor>       klasor olustur\n");
        term_puts("  editor <dosya>       metin duzenleyici ac\n");
        term_puts("  wolf run <dosya.js>  JavaScript calistir\n");
        term_puts("  wolf -e \"kod\"         JavaScript kodu calistir\n");
        term_puts("  pkg [list]           kurulu paketler\n");
        term_puts("  pkg install <id>     paket kur (ag)\n");
        term_puts("  pkg remove <id>      paket kaldir\n");
        term_puts("  date                 tarih/saat\n");
        term_puts("  sysinfo              sistem bilgisi (uname)\n");
        term_puts("  mem                  bellek kullanimi (toplam/kullanim/bos)\n");
        term_puts("  whoami               oturumdaki kullanici\n");
        term_puts("  users                kayitli hesaplar (uid/ev)\n");
        term_puts("  adduser <ad> [p]     yeni hesap olustur (+ev dizini)\n");
        term_puts("  setpassword <p> <p>  kendi parolani belirle/degistir\n");
        term_puts("  passwd [ad] <p> <p>  parola belirle (root: baska hesap)\n");
        term_puts("  kilit                kilit ekrani / kullanici degistir\n");
        term_puts("  ip                   ag bilgisi (ifconfig)\n");
        term_puts("  dns <host>           alan adi cozumle\n");
        term_puts("  curl <url>           HTTP GET istegi\n");
        term_puts("  bgfetch <url>        arka plan indirme (UI donmaz)\n");
        term_puts("  update [kontrol|kur] sistem guncellemesi denetle/uygula\n");
        term_puts("  ntp                  saati NTP ile esitle\n");
        term_puts("  print <dosya>        yazdirma kuyruguna ekle\n");
        term_puts("  jobs                 yazdirma kuyrugunu listele\n");
        term_puts("  lp                   kuyrugu yaziciya gonder\n");
        term_puts("  dmesg                yapisal cekirdek logu\n");
        term_puts("  syslog               logu C:/logs/sys.log'a yaz\n");
        term_puts("  copy <metin>         panoya kopyala\n");
        term_puts("  paste                panoyu yaz\n");
        term_puts("  clip                 pano durumu\n");
        term_puts("  notify <b> <m>       bildirim goster\n");
        term_puts("  drivers              cihaz/surucu listesi\n");
        term_puts("  safemode [on|off]    guvenli mod (blur kapali)\n");
        term_puts("  echo <metin>         metni yaz\n");
        term_puts("  reboot               yeniden baslat\n");
        term_puts("  shutdown             kapat (poweroff)\n");
    } else if (str_eq(name, "clear") || str_eq(name, "cls")) {
        g_log_len = 0;
    } else if (str_eq(name, "apps")) {
        cmd_apps();
    } else if (str_eq(name, "run") || str_eq(name, "open") || str_eq(name, "launch")) {
        cmd_run(arg);
    } else if (str_eq(name, "close") || str_eq(name, "kill")) {
        cmd_close(arg);
    } else if (str_eq(name, "ps")) {
        cmd_ps();
    } else if (str_eq(name, "proc") || str_eq(name, "psproc")) {
        cmd_proc(asi);
    } else if (str_eq(name, "proctest")) {
        cmd_proctest(asi);
    } else if (str_eq(name, "wait")) {
        cmd_procwait(asi, arg);
    } else if (str_eq(name, "reap")) {
        cmd_procreap(asi);
    } else if (str_eq(name, "labtest") || str_eq(name, "synctest")) {
        cmd_labtest(asi);
    } else if (str_eq(name, "threads")) {
        cmd_threads(asi);
    } else if (str_eq(name, "capstest")) {
        cmd_capstest();
    } else if (str_eq(name, "onaytest")) {
        cmd_onaytest(arg);
    } else if (str_eq(name, "permtest")) {
        cmd_permtest();
    } else if (str_eq(name, "socktest")) {
        cmd_socktest();
    } else if (str_eq(name, "shelltest")) {
        cmd_shelltest();
    } else if (str_eq(name, "grep")) {
        sh_grep(arg);
    } else if (str_eq(name, "wc")) {
        sh_wc(arg);
    } else if (str_eq(name, "head")) {
        sh_head(arg);
    } else if (str_eq(name, "sort")) {
        sh_sort(arg);
    } else if (str_eq(name, "tee")) {
        sh_tee(arg);
    } else if (str_eq(name, "env")) {
        sh_env_print();
    } else if (str_eq(name, "set") || str_eq(name, "export")) {
        sh_set_cmd(arg);
    } else if (str_eq(name, "unset")) {
        env_unset(arg);
    } else if (str_eq(name, "history")) {
        sh_history();
    } else if (str_eq(name, "fsck")) {
        cmd_fsck(arg);
    } else if (str_eq(name, "wallpaper")) {
        cmd_wallpaper(arg);
    } else if (str_eq(name, "theme") || str_eq(name, "tema")) {
        cmd_theme(arg);
    } else if (str_eq(name, "ls") || str_eq(name, "dir")) {
        cmd_ls(arg);
    } else if (str_eq(name, "cat")) {
        sh_cat(arg);
    } else if (str_eq(name, "rm") || str_eq(name, "del")) {
        cmd_rm(arg);
    } else if (str_eq(name, "mv") || str_eq(name, "move") || str_eq(name, "ren")) {
        cmd_mv(arg);
    } else if (str_eq(name, "cp") || str_eq(name, "copy-file")) {
        cmd_cp(arg);
    } else if (str_eq(name, "find") || str_eq(name, "ara")) {
        cmd_find(arg);
    } else if (str_eq(name, "du")) {
        cmd_du(arg);
    } else if (str_eq(name, "tree") || str_eq(name, "agac")) {
        cmd_tree(arg);
    } else if (str_eq(name, "stat")) {
        cmd_stat(arg);
    } else if (str_eq(name, "touch")) {
        cmd_touch(arg);
    } else if (str_eq(name, "trash") || str_eq(name, "cop")) {
        cmd_trash(arg);
    } else if (str_eq(name, "cd")) {
        cmd_cd(arg);
    } else if (str_eq(name, "pwd")) {
        cmd_pwd();
    } else if (str_eq(name, "mkdir") || str_eq(name, "md")) {
        cmd_mkdir(arg);
    } else if (str_eq(name, "editor") || str_eq(name, "edit") || str_eq(name, "nano")) {
        cmd_editor(arg);
    } else if (str_eq(name, "wolf")) {
        cmd_wolf(arg);
    } else if (str_eq(name, "wolfdok") || str_eq(name, "dok") || str_eq(name, "dokuman")) {
        cmd_wolfdok(arg);
    } else if (str_eq(name, "wf")) {
        cmd_wf(arg);
    } else if (str_eq(name, "pkg")) {
        cmd_pkg(arg);
    } else if (str_eq(name, "date")) {
        cmd_date();
    } else if (str_eq(name, "sysinfo") || str_eq(name, "uname")) {
        cmd_sysinfo();
    } else if (str_eq(name, "mem") || str_eq(name, "free")) {
        cmd_mem();
    } else if (str_eq(name, "whoami")) {
        const char *u = arctian_auth_current();
        term_puts(u && u[0] ? u : "kullanici"); term_putc('\n');
    } else if (str_eq(name, "users") || str_eq(name, "hesaplar")) {
        cmd_users();
    } else if (str_eq(name, "adduser") || str_eq(name, "useradd")) {
        cmd_adduser(arg);
    } else if (str_eq(name, "setpassword") || str_eq(name, "parola")) {
        cmd_setpassword(arg);
    } else if (str_eq(name, "passwd")) {
        cmd_passwd(arg);
    } else if (str_eq(name, "kilit") || str_eq(name, "lock") || str_eq(name, "logout") || str_eq(name, "oturum-kapat")) {
        cmd_lock();
    } else if (str_eq(name, "ip") || str_eq(name, "ifconfig")) {
        cmd_ip(asi);
    } else if (str_eq(name, "dns")) {
        cmd_dns(asi, arg);
    } else if (str_eq(name, "curl")) {
        cmd_curl(asi, arg);
    } else if (str_eq(name, "bgfetch")) {
        cmd_bgfetch(asi, arg);
    } else if (str_eq(name, "update") || str_eq(name, "guncelle")) {
        cmd_update(asi, arg);
    } else if (str_eq(name, "ntp")) {
        cmd_ntp();
    } else if (str_eq(name, "print")) {
        cmd_print(arg);
    } else if (str_eq(name, "jobs")) {
        cmd_jobs();
    } else if (str_eq(name, "lp")) {
        cmd_lp();
    } else if (str_eq(name, "dmesg")) {
        cmd_dmesg();
    } else if (str_eq(name, "syslog")) {
        cmd_syslog();
    } else if (str_eq(name, "copy")) {
        cmd_copy(arg);
    } else if (str_eq(name, "paste")) {
        cmd_paste();
    } else if (str_eq(name, "clip")) {
        cmd_clip();
    } else if (str_eq(name, "notify")) {
        cmd_notify(arg);
    } else if (str_eq(name, "drivers")) {
        cmd_drivers();
    } else if (str_eq(name, "safemode")) {
        cmd_safemode(arg);
    } else if (str_eq(name, "echo")) {
        term_puts(arg); term_putc('\n');
    } else if (str_eq(name, "reboot")) {
        term_puts("Yeniden baslatiliyor...\n"); term_flush();
        if (asi && asi->system && asi->system->reboot) asi->system->reboot();
    } else if (str_eq(name, "shutdown") || str_eq(name, "poweroff")) {
        term_puts("Kapatiliyor...\n"); term_flush();
        if (asi && asi->system && asi->system->shutdown) asi->system->shutdown();
    } else {
        term_puts("Bilinmeyen komut: "); term_puts(name);
        term_puts("  (help yazin)\n");
    }
    term_flush();
}

static void term_clear(void) {
    g_log_len = 0;
    g_log[0] = 0;
    term_flush();
}

/* ---------------- Gercek sekmeler ----------------
 * Her sekme kendi calisma dizinini ve yarim kalmis girdisini tutar; sekme
 * degisince ekran temizlenip o oturumun dizininden devam edilir. Onceden
 * "+" ve "x" yalnizca ekrani siliyordu (sahte sekme). */
#define TERM_TABS 4

typedef struct {
    char cwd[176];
    char draft[256];
    int  hist_pos;
    int  used;
} term_tab_t;

static term_tab_t g_tabs[TERM_TABS];
static int        g_tab_n;
static int        g_tab;
static lv_obj_t  *g_tabbar;
static lv_obj_t  *g_tabbox[TERM_TABS];
static lv_obj_t  *g_tabtitle[TERM_TABS];

static void tabs_redraw(void);
static void tab_activate(int i);
static void tabs_sync(void);
static void tab_click_cb(lv_event_t *e);

static void term_focus_cb(lv_event_t *e) {
    (void)e;
    if (!g_input || !g_group) return;
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, g_group);
    lv_group_focus_obj(g_input);
}

static void menu_hide(void) {
    if (g_menu) lv_obj_add_flag(g_menu, LV_OBJ_FLAG_HIDDEN);
}

static void menu_toggle(void) {
    if (!g_menu) return;
    if (lv_obj_has_flag(g_menu, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(g_menu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_menu);
    } else {
        lv_obj_add_flag(g_menu, LV_OBJ_FLAG_HIDDEN);
    }
}

static void tab_activate(int i);
static void tabs_sync(void);

static void tab_close_cb(lv_event_t *e) {
    (void)e;
    menu_hide();
    /* Son sekme ise pencere davranisi: ekrani temizle. Aksi halde sekmeyi
     * kapat ve komsu sekmeye gec (gercek sekme davranisi). */
    if (g_tab_n <= 1) { term_clear(); return; }
    for (int i = g_tab; i < g_tab_n - 1; i++) g_tabs[i] = g_tabs[i + 1];
    g_tab_n--;
    g_tabs[g_tab_n].used = 0;
    g_tabs[g_tab_n].cwd[0] = 0;
    g_tabs[g_tab_n].draft[0] = 0;
    if (g_tab >= g_tab_n) g_tab = g_tab_n - 1;
    tab_activate(g_tab);
}

static void tab_new_cb(lv_event_t *e) {
    (void)e;
    menu_hide();
    if (g_tab_n >= TERM_TABS) return;   /* en fazla 4 sekme */
    tabs_sync();
    int i = g_tab_n++;
    g_tabs[i].used = 1;
    cstr_copy(g_tabs[i].cwd, sizeof(g_tabs[i].cwd), "C:/");
    g_tabs[i].draft[0] = 0;
    g_tabs[i].hist_pos = g_hist_n;
    tab_activate(i);
}

static void chev_cb(lv_event_t *e) {
    (void)e;
    menu_toggle();
}

static void menu_item_cb(lv_event_t *e) {
    intptr_t id = (intptr_t)lv_event_get_user_data(e);
    menu_hide();
    if (id == 1) term_clear();
    else if (id == 2) sh_run(g_asi, "help");
}

static void input_ready_cb(lv_event_t *e) {
    asi_t *asi = (asi_t *)lv_event_get_user_data(e);
    lv_obj_t *ta = lv_event_get_target(e);
    const char *txt = lv_textarea_get_text(ta);
    char cmd[256];
    int i = 0;
    for (; txt[i] && i < 255; i++) cmd[i] = txt[i];
    cmd[i] = 0;
    lv_textarea_set_text(ta, "");
    menu_hide();
    sh_run(asi, cmd);
}

static void term_complete(lv_obj_t *ta);

/* 0.7: kabuk/uygulama koprusu. Terminal kendi cikisini kaydeder; baska
 * bir pencere "wf run ..." isteginde bulunursa burada calistirilir. */
static lv_timer_t *g_pend_timer;

static void term_pending_cb(lv_timer_t *t) {
    (void)t;
    const char *cmd = arctian_term_pending();
    if (cmd && cmd[0]) sh_run(g_asi, cmd);
}

static void input_key_cb(lv_event_t *e) {
    uint32_t key = lv_event_get_key(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (key == LV_KEY_UP) {
        if (g_hist_pos > 0) g_hist_pos--;
        const char *h = sh_hist_at(g_hist_pos);
        if (h) lv_textarea_set_text(ta, h);
        lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    } else if (key == LV_KEY_DOWN) {
        if (g_hist_pos < g_hist_n) g_hist_pos++;
        const char *h = sh_hist_at(g_hist_pos);
        lv_textarea_set_text(ta, h ? h : "");
        lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    } else if (key == LV_KEY_NEXT || key == 0x09) {
        /* Tab: komut / dosya adi tamamlama      */
        term_complete(ta);
    }
}

/* Bilinen komut adlari (Tab tamamlama + yardim icin tek liste). */
static const char *const k_cmds[] = {
    "help","clear","apps","run","close","ps","proc","proctest","wait","reap",
    "labtest","capstest","permtest","socktest","shelltest","grep","wc","head",
    "sort","tee","env","set","unset","history","fsck","threads","wallpaper",
    "ls","cat","rm","mv","cp","find","du","tree","stat","touch","trash","cd","pwd","mkdir","editor","wolf","wf","wolfdok","theme","pkg",
    "date","sysinfo","mem","whoami","users","adduser","passwd","kilit","ip",
    "dns","curl","bgfetch","update","ntp","print","jobs","lp","dmesg","syslog",
    "copy","paste","clip","notify","safemode","reboot","shutdown", 0
};

static void term_complete(lv_obj_t *ta) {
    const char *txt = lv_textarea_get_text(ta);
    int len = 0;
    while (txt[len]) len++;
    if (len <= 0) return;

    /* ilk kelime mi? */
    int space = -1;
    for (int i = 0; i < len; i++) if (txt[i] == ' ') { space = i; break; }

    if (space < 0) {
        const char *only = 0;
        int hits = 0;
        for (int i = 0; k_cmds[i]; i++) {
            if (!str_prefix(k_cmds[i], txt)) continue;
            hits++;
            only = k_cmds[i];
        }
        if (hits == 1) { lv_textarea_set_text(ta, only); lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST); }
        else if (hits > 1) {
            term_puts("\n");
            for (int i = 0; k_cmds[i]; i++)
                if (str_prefix(k_cmds[i], txt)) { term_puts(k_cmds[i]); term_puts("  "); }
            term_putc('\n');
            term_flush();
        }
        return;
    }

    /* dosya adi tamamlama (ilk komuttan sonraki kelime) */
    const char *pre = txt + space + 1;
    if (!pre[0]) return;
    char cand[128];
    int found = 0;
    const char *one = 0;
    if (g_asi && g_asi->fs && g_asi->fs->count_dir && g_asi->fs->list_dir) {
        int dn = g_asi->fs->count_dir(g_cwd);
        for (int i = 0; i < dn; i++) {
            char nm[128]; int isd = 0;
            if (g_asi->fs->list_dir(g_cwd, i, nm, sizeof(nm), &isd) != 0) continue;
            if (!str_prefix(nm, pre)) continue;
            found++;
            one = nm;
        }
    }
    if (found == 1) {
        char full[256];
        cstr_copy(full, sizeof(full), txt);
        int k = space + 1;
        full[k] = 0;
        cstr_copy(full + k, sizeof(full) - k, one);
        lv_textarea_set_text(ta, full);
        lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    } else if (found > 1) {
        term_puts("\n");
        term_flush();
    }
    (void)cand;
}

static void term_del_cb(lv_event_t *e) {
    (void)e;
    arctian_term_set_puts(0);
    if (g_pend_timer) { lv_timer_delete(g_pend_timer); g_pend_timer = 0; }
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, NULL);
    g_input = 0;
    g_out_label = 0;
    g_scroll = 0;
    g_menu = 0;
    g_tabbar = 0;
    for (int i = 0; i < TERM_TABS; i++) { g_tabbox[i] = 0; g_tabtitle[i] = 0; }
}

static lv_obj_t *mk_btn(lv_obj_t *parent, const char *sym, lv_event_cb_t cb, void *ud) {
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 30, 24);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2A2A2A), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xB4B4B4), 0);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

/* Sekme cubugunu yeniden cizer: aktif sekme parlak, digerleri soluk. */
static void tabs_redraw(void) {
    if (!g_tabbar) return;
    for (int i = 0; i < TERM_TABS; i++) {
        if (g_tabbox[i]) { lv_obj_delete(g_tabbox[i]); g_tabbox[i] = 0; g_tabtitle[i] = 0; }
    }
    int x = 6;
    for (int i = 0; i < g_tab_n; i++) {
        lv_obj_t *tab = lv_obj_create(g_tabbar);
        lv_obj_remove_style_all(tab);
        lv_obj_set_size(tab, 212, TERM_TAB_H - 6);
        lv_obj_set_pos(tab, x, 3);
        lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(tab, lv_color_hex(i == g_tab ? TERM_TAB_BG : 0x161616), 0);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tab, 8, 0);
        lv_obj_add_event_cb(tab, tab_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *icon = lv_obj_create(tab);
        lv_obj_remove_style_all(icon);
        lv_obj_set_size(icon, 18, 18);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(icon, 5, 0);
        lv_obj_set_style_bg_color(icon, lv_color_hex(0x1E4E8C), 0);
        lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t *ig = lv_label_create(icon);
        lv_label_set_text(ig, ">_");
        lv_obj_set_style_text_font(ig, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ig, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(ig);

        lv_obj_t *title = lv_label_create(tab);
        char nm[20];
        nm[0] = 'T'; nm[1] = 'e'; nm[2] = 'r'; nm[3] = 'm';
        nm[4] = 'i'; nm[5] = 'n'; nm[6] = 'a'; nm[7] = 'l';
        nm[8] = ' ';
        nm[9] = (char)('1' + i);
        nm[10] = 0;
        lv_label_set_text(title, nm);
        lv_obj_set_style_text_color(title, lv_color_hex(i == g_tab ? 0xF2F2F2 : 0x9A9A9A), 0);
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 32, 0);
        g_tabtitle[i] = title;

        lv_obj_t *cls = mk_btn(tab, LV_SYMBOL_CLOSE, tab_close_cb, 0);
        lv_obj_align(cls, LV_ALIGN_RIGHT_MID, -6, 0);
        g_tabbox[i] = tab;
        x += 216;
    }
}

static void tabs_sync(void) {
    /* canli durumu aktif sekmeye yaz */
    if (g_tab >= 0 && g_tab < TERM_TABS) {
        cstr_copy(g_tabs[g_tab].cwd, sizeof(g_tabs[g_tab].cwd), g_cwd);
        g_tabs[g_tab].hist_pos = g_hist_pos;
        if (g_input) cstr_copy(g_tabs[g_tab].draft, sizeof(g_tabs[g_tab].draft),
                               lv_textarea_get_text(g_input));
    }
}

static void tab_activate(int i) {
    if (i < 0 || i >= TERM_TABS || !g_tabs[i].used) return;
    tabs_sync();
    g_tab = i;
    cstr_copy(g_cwd, sizeof(g_cwd), g_tabs[i].cwd[0] ? g_tabs[i].cwd : "C:/");
    g_hist_pos = g_hist_n;
    term_clear();
    if (g_input) lv_textarea_set_text(g_input, g_tabs[i].draft);
    term_puts("Arctian Terminal — sekme ");
    term_put_uint((uint32_t)(i + 1));
    term_puts("\n");
    term_flush();
    tabs_redraw();
}

static void tab_click_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i == g_tab) return;
    tab_activate(i);
}

static lv_obj_t *mk_menu_item(lv_obj_t *parent, const char *txt, intptr_t id) {
    lv_obj_t *it = lv_obj_create(parent);
    lv_obj_remove_style_all(it);
    lv_obj_set_size(it, LV_PCT(100), 30);
    lv_obj_remove_flag(it, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(it, 6, 0);
    lv_obj_set_style_bg_color(it, lv_color_hex(0x2A2A2A), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(it, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_flag(it, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = lv_label_create(it);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(0xD8D8D8), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_add_event_cb(it, menu_item_cb, LV_EVENT_CLICKED, (void *)id);
    return it;
}

static void build_tabbar(lv_obj_t *win, int w) {
    lv_obj_t *bar = lv_obj_create(win);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, w, TERM_TAB_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bar, lv_color_hex(TERM_BAR_BG), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x262626), 0);
    g_tabbar = bar;

    lv_obj_t *plus = mk_btn(bar, LV_SYMBOL_PLUS, tab_new_cb, 0);
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, -70, 0);
    lv_obj_t *chev = mk_btn(bar, LV_SYMBOL_DOWN, chev_cb, 0);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -36, 0);

    g_menu = lv_obj_create(win);
    lv_obj_remove_style_all(g_menu);
    lv_obj_set_size(g_menu, 176, 76);
    lv_obj_set_pos(g_menu, w - 190, TERM_TAB_H + 4);
    lv_obj_set_style_bg_color(g_menu, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_bg_opa(g_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_menu, 8, 0);
    lv_obj_set_style_border_width(g_menu, 1, 0);
    lv_obj_set_style_border_color(g_menu, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_all(g_menu, 6, 0);
    lv_obj_set_style_pad_row(g_menu, 4, 0);
    lv_obj_set_flex_flow(g_menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(g_menu, LV_OBJ_FLAG_HIDDEN);
    mk_menu_item(g_menu, "Yeni oturum", 1);
    mk_menu_item(g_menu, "Yardim", 2);
}

static void build_term(lv_obj_t *win, int w, int h) {
    lv_font_t *font = adl_font();
    int lh = font ? lv_font_get_line_height(font) : 18;

    lv_obj_t *term = lv_obj_create(win);
    lv_obj_remove_style_all(term);
    lv_obj_set_size(term, w, h - TERM_TAB_H);
    lv_obj_set_pos(term, 0, TERM_TAB_H);
    lv_obj_add_flag(term, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(term, lv_color_hex(TERM_BG), 0);
    lv_obj_set_style_bg_opa(term, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(term, 10, 0);
    lv_obj_set_style_pad_row(term, 2, 0);
    lv_obj_set_scroll_dir(term, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(term, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(term, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(term, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    g_scroll = term;

    g_out_label = lv_label_create(term);
    lv_label_set_text(g_out_label, "");
    lv_label_set_long_mode(g_out_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_out_label, LV_PCT(100));
    lv_obj_set_style_text_color(g_out_label, lv_color_hex(TERM_FG), 0);
    if (font) lv_obj_set_style_text_font(g_out_label, font, 0);

    lv_obj_t *row = lv_obj_create(term);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), lh + 6);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *pr = lv_label_create(row);
    lv_label_set_text(pr, "arctian>");
    lv_obj_set_style_text_color(pr, lv_color_hex(TERM_PROMPT), 0);
    if (font) lv_obj_set_style_text_font(pr, font, 0);

    g_input = lv_textarea_create(row);
    lv_textarea_set_one_line(g_input, true);
    lv_obj_set_flex_grow(g_input, 1);
    lv_obj_set_height(g_input, lh + 4);
    lv_obj_remove_flag(g_input, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(g_input, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_input, 0, 0);
    lv_obj_set_style_pad_all(g_input, 0, 0);
    lv_obj_set_style_text_color(g_input, lv_color_hex(TERM_PROMPT), 0);
    if (font) lv_obj_set_style_text_font(g_input, font, 0);
    lv_obj_set_style_text_color(g_input, lv_color_hex(0x5A5A5A), LV_PART_TEXTAREA_PLACEHOLDER);
    if (font) lv_obj_set_style_text_font(g_input, font, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(g_input, lv_color_hex(TERM_PROMPT), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(g_input, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_border_width(g_input, 0, LV_PART_CURSOR);
    lv_obj_set_style_text_color(g_input, lv_color_hex(TERM_BG), LV_PART_CURSOR);
    lv_textarea_set_placeholder_text(g_input, "komut yaz (help)");
    lv_obj_add_event_cb(g_input, input_ready_cb, LV_EVENT_READY, g_asi);
    lv_obj_add_event_cb(g_input, input_key_cb, LV_EVENT_KEY, 0);
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    g_asi = asi;
    g_log_len = 0;
    g_log[0] = 0;
    g_menu = 0;
    g_hist_n = 0;
    g_hist_pos = 0;
    g_cap_on = 0;
    g_cap_len = 0;
    g_stdin = 0;

    const char *cu = arctian_auth_current();
    if (cu && cu[0]) {
        const char *hm = arctian_auth_home(cu);
        char hp[96];
        int o = 0;
        if (hm) for (int i = 0; hm[i] && o < (int)sizeof(hp) - 2; i++) hp[o++] = hm[i];
        if (o > 0 && hp[o - 1] != '/') hp[o++] = '/';
        hp[o] = 0;
        if (o > 0 && fs_dir_exists(hp)) cstr_copy(g_cwd, sizeof(g_cwd), hp);
    }

    sh_env_init();
    sh_hist_load();   /* kalici komut gecmisi */

    lv_obj_update_layout(win);
    int w = lv_obj_get_width(win);
    int h = lv_obj_get_height(win);
    if (w < 260) w = 556;
    if (h < 160) h = 338;

    build_tabbar(win, w);
    build_term(win, w, h);

    /* Ilk sekme: calisma dizini C:/.      */
    for (int i = 0; i < TERM_TABS; i++) {
        g_tabs[i].used = 0;
        g_tabs[i].cwd[0] = 0;
        g_tabs[i].draft[0] = 0;
    }
    g_tab_n = 1;
    g_tab = 0;
    g_tabs[0].used = 1;
    cstr_copy(g_tabs[0].cwd, sizeof(g_tabs[0].cwd), "C:/");
    cstr_copy(g_cwd, sizeof(g_cwd), "C:/");
    tabs_redraw();

    lv_obj_add_event_cb(win, term_del_cb, LV_EVENT_DELETE, 0);
    lv_obj_add_event_cb(win, term_focus_cb, LV_EVENT_CLICKED, 0);

    if (!g_group) g_group = lv_group_create();
    lv_group_remove_all_objs(g_group);
    lv_group_add_obj(g_group, g_input);
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, g_group);
    lv_group_focus_obj(g_input);

    term_puts("Arctian Terminal\n");
    term_puts("Tum sistem terminalden yonetilir. Komutlar icin: help\n\n");

    /* 0.7: terminal merkezli calisma. Cikti koprusu + bekleyen istek. */
    arctian_term_set_puts(term_bridge_puts);
    if (!g_pend_timer) g_pend_timer = lv_timer_create(term_pending_cb, 300, 0);

    term_flush();

    const char *pend = arctian_term_pending();
    if (pend && pend[0]) sh_run(asi, pend);
}

