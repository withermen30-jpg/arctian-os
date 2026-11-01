/**
 * Arctian kernel_plus
 * Surucu + servis katmani. main_kernel tarafindan yuklenir.
 * Gorev: suruculeri baslat, ASI'yi (gfx/input) tamamla, desktop'u RAM'e yukle
 * (Loader with Arc ilerleme ekrani ile) ve desktop'a atla.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "kernel.h"
#include "memory_map.h"
#include "asi.h"
#include "media.h"
#include "loader.h"
#include "idt.h"
#include "pic.h"
#include "isr.h"
#include "syscall.h"
#include "vesa.h"
#include "backbuffer.h"
#include "mouse.h"
#include "keyboard.h"
#include "usb.h"
#include "usb_hid.h"
#include "audio.h"
#include "pci.h"
#include "serial.h"
#include "rtc.h"
#include "disk.h"
#include "net.h"
#include "socket.h"
#include "ntp.h"
#include "spool.h"
#include "blockdev.h"
#include "net_task.h"
#include "ipv4.h"
#include "arp.h"
#include "tcp.h"
#include "dns.h"
#include "http.h"
#include "https.h"
#include "wifi.h"
#include "firewall.h"
#include "acpi.h"
#include "blockdev.h"
#include "fat32.h"
#include "volfs.h"
#include "printf.h"
#include "serial.h"
#include "clock.h"
#include "sched.h"
#include "proc.h"
#include "thread.h"
#include "ipc.h"
#include "caps.h"
#include "afs.h"
#include "diskcrypt.h"
#include "storage.h"

asi_t *g_asi = 0;

extern void keyboard_init(void);
extern void system_services_init(void);
extern void system_shutdown(void);
extern void system_reboot(void);
extern void system_sleep(void);
extern void io_bench(void);

static uint64_t s_last_wb = 0;

static void sys_shutdown(void) { storage_flush(); system_shutdown(); }
static void sys_reboot(void)   { storage_flush(); system_reboot(); }

static void sys_writeback(void) {
    uint64_t now = clock_ms();
    if (now - s_last_wb < 1000u) return;
    s_last_wb = now;
    storage_flush();
}

/* ---- Cokme koruma ----
 * Masaustu bir uygulama baslatirken kurtarma noktasini kaydeder. PIT IRQ'si
 * icinden zaman asimi denetlenir; istisna yakalama main_kernel/isr.c'de. */
static void rc_set_reason(arctian_recover_t *c, const char *s) {
    int i = 0;
    for (; s[i] && i < (int)sizeof(c->reason) - 1; i++) c->reason[i] = s[i];
    c->reason[i] = 0;
}

static void sys_recover_begin(arctian_recover_t *ctx, uint32_t timeout_ms) {
    if (!ctx) return;
    ctx->active = 1;
    ctx->code = 0;
    ctx->reason[0] = 0;
    ctx->deadline_ms = (uint32_t)clock_ms() + timeout_ms;
    g_arctian_recover = ctx;
}

static void sys_recover_end(void) {
    if (g_arctian_recover) g_arctian_recover->active = 0;
    g_arctian_recover = NULL;
}

static void recover_tick(void) {
    arctian_recover_t *c = g_arctian_recover;
    if (!c || !c->active) return;
    if ((uint32_t)clock_ms() < c->deadline_ms) return;
    rc_set_reason(c, "Uygulama yanit vermedi (5 saniye zaman asimi)");
    c->code = 2;
    arctian_longjmp(c, 2);
}

static uint64_t time_ticks_ms(void) { return clock_ms(); }
static void     time_sleep_ms(uint32_t ms) { clock_sleep_ms(ms); }
static int      time_now(asi_datetime_t *out) { return clock_now(out) ? 1 : 0; }
static int      time_ntp_sync(uint32_t ms) { return ntp_sync(ms); }
static uint64_t time_unix_now(void) { return (uint64_t)clock_unix(); }
static void     time_set_unix(uint32_t u) { clock_set_unix(u); }
static void     time_set_tz(int minutes) { clock_set_tz_offset(minutes); }
static int  net_present(void) { return net_get_mac() ? 1 : 0; }
static int  net_send(const void *d, uint32_t n) { return net_send_packet((const uint8_t *)d, (uint16_t)n) ? 0 : -1; }
static int  net_recv(void *b, uint32_t n) {
    uint16_t len = (uint16_t)n;
    if (net_receive_packet((uint8_t *)b, &len)) return (int)len;
    return -1;
}
static int  net_has(void) { return 0; }
static void net_mac(uint8_t out[6]) {
    const uint8_t *m = net_get_mac();
    if (m) for (int i = 0; i < 6; i++) out[i] = m[i];
}

/* ---- netlink (baglanti + wifi + uygulama ag API) ---- */

static void ip_copy(uint8_t out[4], const uint8_t in[4]) {
    for (int i = 0; i < 4; i++) out[i] = in[i];
}

static int net_link_type(void) {
    if (wifi_up()) return ASI_LINK_WIFI;
    if (net_get_mac() && net_link_up()) return ASI_LINK_ETHERNET;
    return ASI_LINK_NONE;
}
static int net_link_up_fn(void) {
    if (wifi_up()) return 1;
    return (net_get_mac() && net_link_up()) ? 1 : 0;
}
static void net_ip(uint8_t out[4])      { ip_copy(out, ipv4_config.src_ip); }
static void net_netmask(uint8_t out[4]) { ip_copy(out, ipv4_config.subnet_mask); }
static void net_gateway(uint8_t out[4]) { ip_copy(out, ipv4_config.gateway); }
static void net_dns(uint8_t out[4])     { ip_copy(out, ipv4_config.dns_server); }
static int  net_configured(void) { return ipv4_config.configured ? 1 : 0; }
static int  net_autoconfig(void) { ipv4_config_dhcp(); return ipv4_config.configured ? 0 : -1; }

static int net_wifi_hw(void) { return wifi_hw_detected() ? 1 : 0; }
static int net_wifi_scan(asi_wifi_net_t *out, int max) { return wifi_scan(out, max); }
static int net_wifi_connect(const char *ssid, const char *pass) {
    int r = wifi_connect(ssid, pass);
    if (r == 0) ipv4_config_dhcp();
    return r;
}
static int net_wifi_disconnect(void) { return wifi_disconnect(); }
static int net_wifi_state(void) { return wifi_state(); }
static const char *net_wifi_ssid(void) { return wifi_ssid(); }

static int net_perm(void) { return caps_check(CAP_NET) != 0; }
static void net_poll(void) { ipv4_poll(); tcp_poll(); }
static int net_dns_resolve_fn(const char *host, uint8_t out_ip[4]) {
    if (net_perm()) return -1;
    dns_result_t r[DNS_RESULT_MAX]; int n = 0;
    if (!dns_resolve(host, r, &n) || n <= 0) return -1;
    ip_copy(out_ip, r[0].ip);
    return 0;
}
static int net_tcp_connect_fn(const uint8_t ip[4], uint16_t port) {
    if (net_perm()) return -1;
    return tcp_connect(ip, port);
}
static int net_tcp_send_fn(int c, const void *d, uint32_t n) {
    if (net_perm()) return -1;
    return tcp_send(c, (const uint8_t *)d, (uint16_t)n) ? 0 : -1;
}
static int net_tcp_recv_fn(int c, void *b, uint32_t n) {
    if (net_perm()) return -1;
    return tcp_receive(c, (uint8_t *)b, (uint16_t)n);
}
static int net_tcp_close_fn(int c) { if (net_perm()) return -1; return tcp_close(c) ? 0 : -1; }
static int net_tcp_state_fn(int c) { return tcp_state(c); }
static int net_sock_socket_fn(int type) {
    if (net_perm()) return -1;
    int t = (type == ASI_SOCK_DGRAM) ? KSOCK_DGRAM : KSOCK_STREAM;
    return ksock_socket(KSOCK_AF_INET, t, 0);
}
static int net_sock_bind_fn(int fd, const uint8_t ip[4], uint16_t port) {
    if (net_perm()) return -1;
    return ksock_bind(fd, ip, port);
}
static int net_sock_listen_fn(int fd) { if (net_perm()) return -1; return ksock_listen(fd); }
static int net_sock_accept_fn(int fd) { if (net_perm()) return -1; return ksock_accept(fd); }
static int net_sock_connect_fn(int fd, const uint8_t ip[4], uint16_t port) {
    if (net_perm()) return -1;
    return ksock_connect(fd, ip, port);
}
static int net_sock_send_fn(int fd, const void *d, uint32_t n) {
    if (net_perm()) return -1;
    return ksock_send(fd, d, n);
}
static int net_sock_recv_fn(int fd, void *b, uint32_t n) {
    if (net_perm()) return -1;
    return ksock_recv(fd, b, n);
}
static int net_sock_close_fn(int fd) { if (net_perm()) return -1; return ksock_close(fd); }
static int net_sock_state_fn(int fd) { return ksock_state(fd); }
static int net_sock_port_fn(int fd) { return ksock_port(fd); }

static int proc_info_ex_fn(int i, asi_proc_info_t *o) { return proc_info_ex(i, o); }
static uint32_t proc_sys_ticks_fn(void) { return proc_sys_ticks(); }

static void klog_file_sink(const klog_entry_t *e, void *ud) {
    (void)ud;
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in) return;
    static char buf[8192];
    int n = 0;
    if (g_asi->fs->read_path) n = (int)g_asi->fs->read_path("C:/logs/sys.log", buf, sizeof(buf) - 200);
    if (n < 0) n = 0;
    if (n > (int)sizeof(buf) - 200) n = (int)sizeof(buf) - 200;

    int o = n;
    if (o == 0) {
        const char *hd = "# Arctian syslog\n";
        for (int i = 0; hd[i]; i++) buf[o++] = hd[i];
    }
    const char *lv = klog_level_str(e->level);
    buf[o++] = '[';
    buf[o++] = lv[0]; buf[o++] = lv[1]; buf[o++] = lv[2];
    buf[o++] = ']'; buf[o++] = ' ';
    for (int i = 0; e->tag[i] && o < (int)sizeof(buf) - 120; i++) buf[o++] = e->tag[i];
    buf[o++] = ':'; buf[o++] = ' ';
    for (int i = 0; e->msg[i] && o < (int)sizeof(buf) - 100; i++) buf[o++] = e->msg[i];
    buf[o++] = '\n';

    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/logs/");
    void *h = g_asi->fs->create_in("C:/logs/", "sys.log", 0);
    if (!h && g_asi->fs->create) h = g_asi->fs->create(0, "sys.log", 0);
    if (h && g_asi->fs->write) g_asi->fs->write(h, buf, (uint32_t)o);
}

static void log_write_fn(int lv, const char *tag, const char *msg) { klog_write(lv, tag, msg); }static int  log_count_fn(void) { return klog_count(); }
static int  log_get_fn(int i, klog_entry_t *o) { return klog_get(i, o); }
static void log_clear_fn(void) { klog_clear(); }
static int  log_flush_fn(void) { return klog_flush(); }

/* Cihaz listesi — 0.5'te surucu sayisi genisletildi:
 * ag icin birden fazla surucu ailesi (E1000 / RTL8139 / RTL8169 / PCnet /
 * Wi-Fi iwl / Wi-Fi generic) listelenir. */
#define DEV_ROWS 14

static int dev_count_fn(void) { return DEV_ROWS; }

static const char *dev_name_fn(int i) {
    static const char *n[DEV_ROWS] = {
        "Ekran (Bochs VBE)",
        "Disk",
        "Ag - Intel E1000 surucusu",
        "Ag - Realtek RTL8139 surucusu",
        "Ag - Realtek RTL8169 surucusu",
        "Ag - AMD PCnet surucusu",
        "Wi-Fi - Intel iwlwifi (AX201) surucusu",
        "Wi-Fi - Genel 802.11 surucusu",
        "Ses (AC97)",
        "USB",
        "Klavye (PS/2)",
        "Fare (PS/2)",
        "RTC",
        "MbedTLS / HTTPS"
    };
    return (i >= 0 && i < DEV_ROWS) ? n[i] : 0;
}

static const char *dev_status_fn(int i) {
    switch (i) {
        case 0: return "aktif";
        case 1: return blockdev_count() > 0 ? "aktif" : "yok";
        case 2: return net_driver_type() == 0 ? "aktif" : "beklemede";
        case 3: return net_driver_type() == 1 ? "aktif" : "beklemede";
        case 4: return net_driver_type() == 4 ? "aktif" : "beklemede";
        case 5: return net_driver_type() == 3 ? "aktif" : "beklemede";
        case 6: return wifi_driver_name()[0] ? "aktif" : "sürücü yok";
        case 7: return wifi_hw_detected() ? "kart var" : "yok";
        case 8: return audio_present() ? "aktif" : "yok";
        case 9: return usb_device_count > 0 ? "aktif" : "cihaz yok";
        case 10: case 11: case 12: return "aktif";
        case 13: return "aktif";
    }
    return "?";
}
static const char *net_http_get(const char *url, uint32_t *len_out) {
    if (net_perm()) { if (len_out) *len_out = 0; return 0; }
    return https_fetch_body(url, len_out);
}
static const char *net_https_get(const char *url, uint32_t *len_out) {
    if (net_perm()) { if (len_out) *len_out = 0; return 0; }
    return https_fetch_body(url, len_out);
}
static const char *net_last_url(void) {
    return https_fetch_last_url();
}
static void net_cookie_set(const char *host, const char *name, const char *value) {
    if (net_perm()) return;
    https_cookie_set(host, name, value);
}
/* Akisli indirme: https icin mevcut tamponlu yol, http icin streaming. */
static int net_download(const char *url, void *dst, uint32_t cap, uint32_t *len_out) {
    if (net_perm()) return -1;
    if (url && url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' &&
        url[4] == 's') {
        uint32_t n = 0;
        const char *body = https_fetch_body(url, &n);
        if (!body || n == 0) return -1;
        if (n > cap) return -2;
        memcpy(dst, body, n);
        if (len_out) *len_out = n;
        return 0;
    }
    return http_download(url, dst, cap, len_out);
}

/* Arka plan ag gorevleri (fiber) icin ASI sarmallari. */
static int  net_submit(void (*fn)(void *), asi_net_done_t done, void *user) {
    if (net_perm()) return -1;
    return net_task_submit(fn, done, user);
}
static int  net_busy(void) { return net_task_busy(); }
static void net_service(void) { net_task_service(); }

/* ---- Guvenlik duvari ASI sarmallari ---- */
static int fw_perm(void) { return caps_check(CAP_NET) != 0; }
static int fw_enabled_fn(void) { return firewall_enabled(); }
static int fw_set_enabled_fn(int on) { if (fw_perm()) return -1; return firewall_set_enabled(on); }
static int fw_count_fn(void) { return firewall_count(); }
static int fw_rule_fn(int i, asi_fw_rule_t *o) { return firewall_get_rule(i, o); }
static int fw_add_fn(const asi_fw_rule_t *r) { if (fw_perm()) return -1; return firewall_add_rule(r); }
static int fw_remove_fn(int i) { if (fw_perm()) return -1; return firewall_remove_rule(i); }
static int fw_clear_fn(void) { if (fw_perm()) return -1; return firewall_clear(); }
static int fw_save_fn(void) { if (fw_perm()) return -1; return firewall_save(); }
static int fw_load_fn(void) { if (fw_perm()) return -1; return firewall_load(); }
static int fw_selftest_fn(void) { return firewall_selftest(); }

/* ---- Guc yonetimi ASI sarmallari ---- */
static acpi_power_t s_pwr_cache;
static uint64_t     s_pwr_last;
static void power_refresh(void) {
    uint64_t now = clock_ms();
    if (s_pwr_last && now - s_pwr_last < 1500u) return;
    s_pwr_last = now;
    acpi_power_poll(&s_pwr_cache);
}
static int power_source_fn(void) { power_refresh(); return s_pwr_cache.source; }
static int power_percent_fn(void) { power_refresh(); return s_pwr_cache.percent; }
static int power_present_fn(void) { power_refresh(); return s_pwr_cache.present; }
static int power_charging_fn(void) { power_refresh(); return s_pwr_cache.charging; }
static int power_minutes_fn(void) { power_refresh(); return s_pwr_cache.minutes; }
static int power_acpi_fn(void) { return acpi_present(); }
static void power_sleep_fn(void) { system_sleep(); }

/* main_kernel zaten hazir bir ASI verir; biz gfx + input ekleriz. */
static asi_gfx_t   s_gfx;
static asi_input_t s_input;
static asi_system_t s_system;
static asi_net_t   s_net;
static asi_time_t  s_time;
static asi_proc_t  s_proc;
static asi_lab_t   s_lab;
static asi_audio_t s_audio;
static asi_crypt_t s_crypt;
static asi_firewall_t s_firewall;
static asi_power_t s_power;
static asi_spool_t s_spool;
static asi_log_t s_log;
static asi_dev_t s_dev;

static int      gfx_w(void)   { return backbuffer_width(); }
static int      gfx_h(void)   { return backbuffer_height(); }
static uint32_t *gfx_fb(void) { return (uint32_t *)BACKBUFFER_BASE; }
static uint32_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
static void     gfx_present(void) { backbuffer_blit(); }
static void     gfx_present_rect(int x, int y, int w, int h) {
    backbuffer_blit_rect(x, y, w, h);
}

static int gfx_set_mode(int width, int height) {
    if (width < 320 || height < 200) return -1;
    if (vesa_init(width, height, VESA_DEFAULT_BPP) != 0) return -2;

    framebuffer_t *fb = vesa_get_framebuffer();
    int aw = (fb && fb->width)  ? fb->width  : width;
    int ah = (fb && fb->height) ? fb->height : height;
    if (aw < 1 || ah < 1) return -3;

    backbuffer_resize(aw, ah);
    mouse_set_screen_size(aw, ah);
    backbuffer_clear(0x0E0B12);
    return 0;
}

static int in_mx(void) { mouse_poll(); mouse_state_t *m = mouse_get_state(); return m ? m->x : 0; }
static int in_my(void) { mouse_state_t *m = mouse_get_state(); return m ? m->y : 0; }
static int in_btns(void) {
    mouse_state_t *m = mouse_get_state();
    if (!m) return 0;
    return (m->buttons[MOUSE_BUTTON_LEFT] ? 1 : 0) |
           (m->buttons[MOUSE_BUTTON_RIGHT] ? 2 : 0) |
           (m->buttons[MOUSE_BUTTON_MIDDLE] ? 4 : 0);
}
static int in_scroll(void) {
    mouse_state_t *m = mouse_get_state();
    if (!m) return 0;
    int s = m->scroll;
    m->scroll = 0;
    return s;
}
static int in_key_avail(void) { return keyboard_event_avail() ? 1 : 0; }
static int in_hotkey_pop(void) { return keyboard_pop_hotkey(); }
static void in_kbd_layout(int id) { keyboard_set_layout(id); }
static int in_ctrl_down(void) { return keyboard_ctrl_down(); }
static int in_shift_down(void) { return keyboard_shift_down(); }
/* Sol tus basma kenari sayaci: yavas kare hizinda kaybolan tiklamalar
 * masaustu kabugunda bu sayac uzerinden telafi edilir. */
static uint32_t in_press_seq(void) { return mouse_left_press_seq(); }
static int in_key_pop(void) {
    keyboard_event_t ev;
    if (keyboard_pop_event(&ev) && ev.pressed) return (unsigned char)ev.ch;
    return -1;
}

/* ---- Yükleme ekrani: Loader with Arc ---- */
static const int s_dx[8] = { 26, 18, 0, -18, -26, -18, 0, 18 };
static const int s_dy[8] = { 0, 18, 26, 18, 0, -18, -26, -18 };

static void draw_arc_loader(int cx, int cy, int frame) {
    /* Arka plan */
    backbuffer_clear(0x0E0B12);

    /* Baslik */
    backbuffer_draw_string_smooth(cx - 40, 110, "Arctian", 0xF5F5F7);
    backbuffer_draw_string_smooth(cx - 92, 132, "Beyaz Kurt yukleniyor...", 0x9AA0A6);

    /* Dots ring */
    for (int i = 0; i < 8; i++) {
        int active = ((frame + i) % 8) < 8;
        int lead = (frame / 3) % 8;
        uint32_t col;
        int d = (i - lead + 8) % 8;
        if (d == 0)      col = 0xFFFFFF;
        else if (d == 1) col = 0xC9CCD6;
        else if (d == 2) col = 0x8A8F98;
        else             col = 0x2A2D34;
        int px = cx + s_dx[i];
        int py = cy + s_dy[i];
        backbuffer_fill_rect(px - 3, py - 3, 6, 6, col);
        (void)active;
    }

    /* Arc halkasi (basit daire) */
    for (int a = 0; a < 360; a += 4) {
        int idx = (a / 45) % 8;
        int px = cx + (s_dx[idx] * 62) / 26;
        int py = cy + (s_dy[idx] * 62) / 26;
        backbuffer_put_pixel(px, py, 0x2A2D34);
    }

    backbuffer_draw_string_smooth(cx - 70, cy + 90, "Suruculer yukleniyor...", 0x9AA0A6);
    backbuffer_blit();
}

/* Ekran logu (takilma yerini gormek icin) */
static char g_log[14][40];
static int  g_logn = 0;

static void draw_log(void) {
    backbuffer_clear(0x0E0B12);
    backbuffer_draw_string_smooth(60, 40, "Arctian", 0xF5F5F7);
    backbuffer_draw_string_smooth(60, 60, "64-bit Isletim Sistemi", 0x9AA0A6);
    for (int i = 0; i < g_logn; i++)
        backbuffer_draw_string_smooth(60, 100 + i * 16, g_log[i], 0x7DD3FC);
    backbuffer_blit();
}
static void LOG(const char *s) {
    if (g_logn < 14) {
        int i = 0;
        for (; s[i] && i < 39; i++) g_log[g_logn][i] = s[i];
        g_log[g_logn][i] = 0;
        g_logn++;
    }
    serial_puts(s);
    draw_log();
}

/* LOG ile sayisal deger gosterir (teshis icin). */
static void LOGN(const char *label, uint32_t v) {
    char b[40];
    int i = 0;
    while (label[i] && i < 24) { b[i] = label[i]; i++; }
    char d[11];
    int n = 0;
    if (v == 0) d[n++] = '0';
    while (v && n < 10) { d[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (n && i < 39) b[i++] = d[--n];
    b[i] = 0;
    LOG(b);
}

#ifdef ARCTIAN_NET_SELFTEST
static void net_selftest(void) {
    serial_puts("\n[nettest] ==== AG SELF-TEST ====\n");

    uint8_t ip[4] = {0,0,0,0};
    net_ip(ip);
    serial_printf("[nettest] IP=%d.%d.%d.%d configured=%d\n",
                  ip[0], ip[1], ip[2], ip[3], ipv4_config.configured ? 1 : 0);

    const uint8_t *mac = net_get_mac();
    if (mac)
        serial_printf("[nettest] MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Tarayicinin birincil yolu: dogrudan google HTTPS. */
    {
        uint32_t gl = 0;
        const char *gb = net_https_get("https://www.google.com/", &gl);
        if (gb) {
            serial_printf("[nettest] GOOGLE HTTPS OK len=%d\n", (int)gl);
            char line[201];
            int n = 0;
            for (; n < 200 && gb[n]; n++) {
                char c = gb[n];
                line[n] = (c >= 32 && c < 127) ? c : '.';
            }
            line[n] = 0;
            serial_puts("[nettest] GOOGLE BODY: ");
            serial_puts(line);
            serial_puts("\n");
        } else {
            char eb[160];
            https_last_error(eb, sizeof(eb));
            serial_printf("[nettest] GOOGLE HTTPS FAIL: %s\n", eb);
        }
    }

    const char *urls[3] = { "http://google.com/", "http://www.google.com/",
                            "http://github.com/" };
    for (int u = 0; u < 3; u++) {
        uint8_t dip[4] = {0,0,0,0};
        int hostlen = 0;
        const char *q = urls[u];
        while (*q && *q != '/' ) q++;            /* semadan sonra */
        q += 2;                                   /* "//" sonrasi */
        char host[128];
        while (*q && *q != '/' && hostlen < 127) host[hostlen++] = *q++;
        host[hostlen] = 0;

        if (net_dns_resolve_fn(host, dip) == 0) {
            serial_printf("[nettest] DNS %s -> %d.%d.%d.%d\n", host,
                          dip[0], dip[1], dip[2], dip[3]);
            int c = net_tcp_connect_fn(dip, 80);
            serial_printf("[nettest] TCP %s:80 -> %d\n", host, c);
            if (c >= 0) net_tcp_close_fn(c);
        } else {
            serial_printf("[nettest] DNS %s -> FAIL\n", host);
        }

        uint32_t len = 0;
        const char *body = net_http_get(urls[u], &len);
        if (body) {
            serial_printf("[nettest] HTTP %s OK len=%d\n", urls[u], (int)len);
            char line[121];
            int n = 0;
            for (; n < 120 && body[n]; n++) {
                char c = body[n];
                line[n] = (c >= 32 && c < 127) ? c : '.';
            }
            line[n] = 0;
            serial_puts("[nettest] BODY: ");
            serial_puts(line);
            serial_puts("\n");
        } else {
            serial_printf("[nettest] HTTP %s -> FAIL\n", urls[u]);
        }
    }
    const char *https_urls[3] = { "https://www.google.com/", "https://google.com/",
                                  "https://github.com/" };
    for (int u = 0; u < 3; u++) {
        serial_printf("[nettest] HTTPS %s ...\n", https_urls[u]);
        uint32_t hlen = 0;
        const char *hb = net_https_get(https_urls[u], &hlen);
        if (hb) {
            serial_printf("[nettest] HTTPS OK url=%s len=%d\n", https_urls[u], (int)hlen);
            char line[121];
            int n = 0;
            for (; n < 120 && hb[n]; n++) {
                char c = hb[n];
                line[n] = (c >= 32 && c < 127) ? c : '.';
            }
            line[n] = 0;
            serial_puts("[nettest] HTTPS BODY: ");
            serial_puts(line);
            serial_puts("\n");
        } else {
            char eb[160];
            https_last_error(eb, sizeof(eb));
            serial_printf("[nettest] HTTPS FAIL url=%s: %s\n", https_urls[u], eb);
        }
    }

    serial_puts("[nettest] ==== SON ====\n");
}
#endif

void kernel_plus_main(void *arg) __attribute__((section(".text.entry")));
void kernel_plus_main(void *arg) {
    g_asi = (asi_t *)arg;
    serial_puts("[kp] start\n");

    kernel_debug("kernel_plus baslatiliyor");
    klog_init();
    klog_write(KLOG_INFO, "boot", "kernel_plus basladi");
    pic_init();
    idt_init();
    syscall_init();
    clock_init();
    sched_init();
    proc_init();
    arctian_set_recover_tick(recover_tick);
    asm volatile("sti");

    /* Grafik */
    terminal_writestring("kernel_plus: VESA baslatiliyor...\n");
    int vr = vesa_init(VESA_DEFAULT_WIDTH, VESA_DEFAULT_HEIGHT, VESA_DEFAULT_BPP);
    bool gui = (vr == 0);
    serial_puts(gui ? "[kp] vesa ok\n" : "[kp] vesa FAIL\n");
    if (gui) {
        vesa_clear(0x0E0B12);
        if (backbuffer_init() != 0) gui = false;
    }
    if (gui) backbuffer_clear(0x0E0B12);
    serial_puts(gui ? "[kp] backbuffer ok\n" : "[kp] no gui\n");

    /* Suruculer */
    LOG("[kp] pci...");
    pci_init();
    LOG("[kp] acpi...");
    acpi_init();
    LOG("[kp] mouse...");
    mouse_init();
    LOG("[kp] keyboard...");
    keyboard_init();
    LOG("[kp] usb...");
    usb_init();
    blockdev_set_scan_hook(usb_msd_scan);
    LOG("[kp] ses...");
    if (audio_init() != 0) serial_puts("[kp] AC97 yok (ses kapali)\n");
    LOG("[kp] net...");
    extern void iwl_ax201_register(void);
    extern void wifi_generic_register(void);
    extern void ath9k_register(void);
    iwl_ax201_register();
    ath9k_register();          /* AR928x: wifi_generic'den ONCE kayitli olmali */
    wifi_generic_register();
    net_init();
    ipv4_init();
    arp_init();
    tcp_init();
    ksock_init();
    spool_init();
    firewall_init();
    dns_init();
    net_task_init();
    ipv4_config_dhcp();   /* IP yapilandirmasi (gercek DHCP, olmazsa slirp) */
#ifdef ARCTIAN_NET_SELFTEST
    net_selftest();
#endif
#ifdef ARCTIAN_SOCK_SELFTEST
    ksock_selftest();
#endif
#ifdef ARCTIAN_NTP_SELFTEST
    {
        int rc = ntp_sync(6000);
        if (rc == 0)
            serial_printf("[ntp] senkron OK unix=%u ofset=%lld\n",
                          (unsigned)clock_unix(), (long long)clock_ntp_offset());
        else
            serial_printf("[ntp] senkron HATA kod=%d\n", -rc);
    }
#endif
#ifdef ARCTIAN_FW_SELFTEST
    firewall_selftest();
#endif
#ifdef ARCTIAN_ACPI_SELFTEST
    acpi_selftest();
#endif
#ifdef ARCTIAN_BLK_SELFTEST
    {
        int bn = blockdev_init();
        serial_printf("[kpblk] %d cihaz\n", bn);
        for (int i = 0; i < bn; i++) {
            blockdev_info_t in;
            blockdev_info(i, &in);
            serial_printf("[kpblk] #%d %s kind=%d secs=%d\n",
                          i, in.name, in.kind, (int)in.sectors);
        }
    }
#endif
    LOG("[kp] disk...");
    disk_init();
    LOG("[kp] services...");
    system_services_init();
    LOG("[kp] media...");
    if (media_init() != 0) kernel_panic("kernel_plus: boot medyasi yok");
    LOG("[kp] media ok");
    blockdev_cache_enable(1);
    LOG("[kp] disk sifreleme...");
    diskcrypt_init();
#ifdef ARCTIAN_BLK_SELFTEST
    io_bench();
#endif

    if (gui) mouse_set_screen_size(backbuffer_width(), backbuffer_height());

    /* ASI gfx + input */
    s_gfx.width = gfx_w;
    s_gfx.height = gfx_h;
    s_gfx.framebuffer = gfx_fb;
    s_gfx.rgb = gfx_rgb;
    s_gfx.present = gfx_present;
    s_gfx.present_rect = gfx_present_rect;
    s_gfx.set_mode = gfx_set_mode;
    s_input.mouse_x = in_mx;
    s_input.mouse_y = in_my;
    s_input.mouse_buttons = in_btns;
    s_input.mouse_scroll = in_scroll;
    s_input.key_avail = in_key_avail;
    s_input.key_pop = in_key_pop;
    s_input.hotkey_pop = in_hotkey_pop;
    s_input.kbd_layout = in_kbd_layout;
    s_input.ctrl_down = in_ctrl_down;
    s_input.shift_down = in_shift_down;
    s_input.mouse_press_seq = in_press_seq;
    s_system.shutdown = sys_shutdown;
    s_system.reboot = sys_reboot;
    s_system.writeback = sys_writeback;
    s_system.recover_begin = sys_recover_begin;
    s_system.recover_end = sys_recover_end;
    s_net.present = net_present;
    s_net.send = net_send;
    s_net.recv = net_recv;
    s_net.has = net_has;
    s_net.mac = net_mac;
    s_net.link_type = net_link_type;
    s_net.link_up = net_link_up_fn;
    s_net.ip = net_ip;
    s_net.netmask = net_netmask;
    s_net.gateway = net_gateway;
    s_net.dns = net_dns;
    s_net.configured = net_configured;
    s_net.autoconfig = net_autoconfig;
    s_net.wifi_hw = net_wifi_hw;
    s_net.wifi_scan = net_wifi_scan;
    s_net.wifi_connect = net_wifi_connect;
    s_net.wifi_disconnect = net_wifi_disconnect;
    s_net.wifi_state = net_wifi_state;
    s_net.wifi_ssid = net_wifi_ssid;
    s_net.poll = net_poll;
    s_net.dns_resolve = net_dns_resolve_fn;
    s_net.tcp_connect = net_tcp_connect_fn;
    s_net.tcp_send = net_tcp_send_fn;
    s_net.tcp_recv = net_tcp_recv_fn;
    s_net.tcp_close = net_tcp_close_fn;
    s_net.tcp_state = net_tcp_state_fn;
    s_net.sock_socket = net_sock_socket_fn;
    s_net.sock_bind = net_sock_bind_fn;
    s_net.sock_listen = net_sock_listen_fn;
    s_net.sock_accept = net_sock_accept_fn;
    s_net.sock_connect = net_sock_connect_fn;
    s_net.sock_send = net_sock_send_fn;
    s_net.sock_recv = net_sock_recv_fn;
    s_net.sock_close = net_sock_close_fn;
    s_net.sock_state = net_sock_state_fn;
    s_net.sock_port = net_sock_port_fn;
    s_net.http_get = net_http_get;
    s_net.https_get = net_https_get;
    s_net.last_url = net_last_url;
    s_net.cookie_set = net_cookie_set;
    s_net.download = net_download;
    s_net.submit = net_submit;
    s_net.busy = net_busy;
    s_net.service = net_service;
    s_time.ticks_ms = time_ticks_ms;
    s_time.sleep_ms = time_sleep_ms;
    s_time.now = time_now;
    s_time.ntp_sync = time_ntp_sync;
    s_time.unix_now = time_unix_now;
    s_time.set_unix = time_set_unix;
    s_time.set_tz_offset = time_set_tz;
    s_proc.spawn = proc_spawn;
    s_proc.fork = proc_fork;
    s_proc.exec = proc_exec;
    s_proc.exit = proc_exit;
    s_proc.wait = proc_wait;
    s_proc.kill = proc_kill;
    s_proc.getpid = proc_getpid;
    s_proc.getppid = proc_getppid;
    s_proc.count = proc_count;
    s_proc.info = proc_info;
    s_proc.app_open = proc_app_open;
    s_proc.app_open_caps = proc_app_open_caps;
    s_proc.caps_of = proc_caps_of;
    s_proc.app_close = proc_app_close;
    s_proc.reap = proc_reap;
    s_proc.info_ex = proc_info_ex_fn;
    s_proc.sys_ticks = proc_sys_ticks_fn;
    s_lab.selftest = lab_selftest;
    s_lab.thread_count = kthread_count;
    s_lab.shm_count = shm_count;
    s_lab.pipe_test = ipc_selftest;
    s_audio.present = audio_present;
    s_audio.play_wav = audio_play_wav;
    s_audio.stop = audio_stop;
    s_audio.is_playing = audio_is_playing;
    s_audio.set_volume = audio_set_volume;
    s_audio.volume = audio_volume;
    s_audio.service = audio_service;
    s_audio.last_error = audio_last_error;
    s_audio.sample_rate = audio_sample_rate;
    s_crypt.present = diskcrypt_present;
    s_crypt.unlocked = diskcrypt_unlocked;
    s_crypt.unlock = diskcrypt_unlock;
    s_crypt.setup = diskcrypt_setup;
    s_crypt.disable = diskcrypt_disable;
    s_crypt.status = diskcrypt_status;
    s_firewall.enabled = fw_enabled_fn;
    s_firewall.set_enabled = fw_set_enabled_fn;
    s_firewall.count = fw_count_fn;
    s_firewall.rule = fw_rule_fn;
    s_firewall.add = fw_add_fn;
    s_firewall.remove = fw_remove_fn;
    s_firewall.clear = fw_clear_fn;
    s_firewall.save = fw_save_fn;
    s_firewall.load = fw_load_fn;
    s_firewall.selftest = fw_selftest_fn;
    s_power.source = power_source_fn;
    s_power.percent = power_percent_fn;
    s_power.present = power_present_fn;
    s_power.charging = power_charging_fn;
    s_power.minutes = power_minutes_fn;
    s_power.acpi = power_acpi_fn;
    s_power.sleep = power_sleep_fn;
    s_spool.submit = spool_submit;
    s_spool.count = spool_count;
    s_spool.info = spool_info;
    s_spool.cancel = spool_cancel;
    s_spool.process = spool_process;
    s_spool.last_path = spool_last_path;
    s_log.write = log_write_fn;
    s_log.count = log_count_fn;
    s_log.get = log_get_fn;
    s_log.clear = log_clear_fn;
    s_log.flush = log_flush_fn;
    s_dev.count = dev_count_fn;
    s_dev.name = dev_name_fn;
    s_dev.status = dev_status_fn;
    g_asi->crypt = &s_crypt;
    g_asi->firewall = &s_firewall;
    g_asi->power = &s_power;
    g_asi->spool = &s_spool;
    g_asi->log = &s_log;
    g_asi->dev = &s_dev;
    g_asi->gfx = &s_gfx;
    g_asi->input = &s_input;
    g_asi->system = &s_system;
    g_asi->net = &s_net;
    g_asi->time = &s_time;
    g_asi->proc = &s_proc;
    g_asi->lab = &s_lab;
    g_asi->audio = &s_audio;

    caps_init(g_asi);
    g_asi->fs = volfs_install(afs_asi());
    caps_fs_guard_install(&g_asi->fs);
    klog_set_sink(klog_file_sink, 0);
    klog_write(KLOG_INFO, "fs", "AFS hazir");
#ifdef ARCTIAN_SPOOL_SELFTEST
    {
        const char *doc = "Arctian yazici testi\n";
        int id = spool_submit("belge", doc, 21);
        int n = spool_process();
        char path[96];
        char rb[128];
        spool_last_path(path, sizeof(path));
        uint32_t rn = (g_asi->fs && g_asi->fs->read_path)
                    ? g_asi->fs->read_path(path, rb, sizeof(rb) - 1) : 0;
        serial_printf("[spool] submit=%d islenen=%d yol=%s okunan=%u\n",
                      id, n, path, (unsigned)rn);
    }
#endif
#ifdef ARCTIAN_KLOG_SELFTEST
    {
        klog_write(KLOG_WARN, "test", "yapisal log testi");
        int n = klog_flush();
        char rb[256];
        uint32_t rn = (g_asi->fs && g_asi->fs->read_path)
                    ? g_asi->fs->read_path("C:/logs/sys.log", rb, sizeof(rb) - 1) : 0;
        serial_printf("[klog] kayit=%d yazilan=%d dosya=%u bayt\n",
                      klog_count(), n, (unsigned)rn);
    }
#endif

    sched_selftest();
    proc_selftest();
#ifdef ARCTIAN_SELFTESTS
    proc_fault_selftest();
#endif
    caps_selftest();
    if (afs_is_locked()) {
        serial_printf("[afs] disk sifreli, AFS kilitli (acilis ekrani bekleniyor)\n");
    } else {
        int pr = afs_perm_selftest();
        /* -2 = yazma basarisiz: canli ISO (salt okunur onyukleme ortami).
         * Bu bir ariza degil; izin testi yazma gerektirdigi icin atlanir. */
        if (pr == -2)
            serial_printf("[afs] dosya izin selftest: yazilamaz ortam (canli ISO) -> atlandi\n");
        else
            serial_printf("[afs] dosya izin selftest: kod=%d -> %s\n",
                          pr, pr == 0 ? "OK" : "HATA");
#ifdef ARCTIAN_AFS_SELFTEST
        {
            int bp = 0, bf = 0;
            int br = afs_fsck(0, &bp, &bf);
            int jr = afs_journal_selftest();
            int fp = 0, ff = 0;
            int fr = afs_fsck(0, &fp, &ff);
            serial_printf("[afs] fsck(once): durum=%d sorun=%d\n", br, bp);
            serial_printf("[afs] journal selftest: kod=%d -> %s\n",
                          jr, jr == 0 ? "OK" : "HATA");
            serial_printf("[afs] fsck: durum=%d sorun=%d -> %s\n",
                          fr, fp, (fr == 0 && fp == 0) ? "TEMIZ" : "SORUN");
        }
#endif
    }
    lab_selftest();

    /* Desktop'u yukle */
    LOG("[kp] desktop find");
    module_entry_t d;
    if (module_find_by_name("desktop", &d) != 0)
        kernel_panic("kernel_plus: desktop modulu yok");
    LOG("[kp] desktop load");
    {
        int rc = module_load(&d);
        if (rc != 0) {
            LOG("[kp] desktop HATA");
            LOGN("rc=", (uint32_t)rc);
            LOGN("lba=", d.image_lba);
            LOGN("sz=", d.image_size);
            LOGN("ld=", d.load_addr);
            kernel_panic("kernel_plus: desktop yuklenemedi");
        }
    }
    LOG("[kp] desktop ok");

    /* Loader with Arc - en az 2 saniye */
    if (gui) {
        int cx = backbuffer_width() / 2;
        int cy = backbuffer_height() / 2 - 40;
        for (int f = 0; f < 240; f++) {
            draw_arc_loader(cx, cy, f);
            for (volatile int w = 0; w < 90000; w++) asm volatile("nop");
        }
    }

    terminal_writestring("kernel_plus: desktop baslatiliyor...\n");
    klog_write(KLOG_INFO, "boot", "desktop baslatiliyor");
    klog_flush();
    serial_puts("[kp] jumping to desktop\n");
    ((void (*)(void *))(uintptr_t)d.load_addr)(g_asi);

    kernel_panic("kernel_plus: desktop geri dondu");
    for (;;) asm volatile("hlt");
}
