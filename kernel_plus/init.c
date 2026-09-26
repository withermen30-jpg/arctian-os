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
#include "vesa.h"
#include "backbuffer.h"
#include "mouse.h"
#include "keyboard.h"
#include "usb.h"
#include "usb_hid.h"
#include "pci.h"
#include "rtc.h"
#include "disk.h"
#include "net.h"
#include "ipv4.h"
#include "arp.h"
#include "tcp.h"
#include "dns.h"
#include "http.h"
#include "https.h"
#include "wifi.h"
#include "blockdev.h"
#include "printf.h"
#include "serial.h"

asi_t *g_asi = 0;

extern void keyboard_init(void);
extern void system_services_init(void);
extern void system_shutdown(void);
extern void system_reboot(void);

static void sys_shutdown(void) { system_shutdown(); }
static void sys_reboot(void)   { system_reboot(); }

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

static void net_poll(void) { ipv4_poll(); tcp_poll(); }
static int net_dns_resolve_fn(const char *host, uint8_t out_ip[4]) {
    dns_result_t r[DNS_RESULT_MAX]; int n = 0;
    if (!dns_resolve(host, r, &n) || n <= 0) return -1;
    ip_copy(out_ip, r[0].ip);
    return 0;
}
static int net_tcp_connect_fn(const uint8_t ip[4], uint16_t port) { return tcp_connect(ip, port); }
static int net_tcp_send_fn(int c, const void *d, uint32_t n) { return tcp_send(c, (const uint8_t *)d, (uint16_t)n) ? 0 : -1; }
static int net_tcp_recv_fn(int c, void *b, uint32_t n) { return tcp_receive(c, (uint8_t *)b, (uint16_t)n); }
static int net_tcp_close_fn(int c) { return tcp_close(c) ? 0 : -1; }
static int net_tcp_state_fn(int c) { return tcp_state(c); }
static const char *net_http_get(const char *url, uint32_t *len_out) {
    return https_fetch_body(url, len_out);
}
static const char *net_https_get(const char *url, uint32_t *len_out) {
    return https_fetch_body(url, len_out);
}
static int net_download(const char *url, void *dst, uint32_t cap, uint32_t *len_out) {
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

static asi_gfx_t   s_gfx;
static asi_input_t s_input;
static asi_system_t s_system;
static asi_net_t   s_net;

static int      gfx_w(void)   { return backbuffer_width(); }
static int      gfx_h(void)   { return backbuffer_height(); }
static uint32_t *gfx_fb(void) { return (uint32_t *)BACKBUFFER_BASE; }
static uint32_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
static void     gfx_present(void) { backbuffer_blit(); }

static int in_mx(void) { mouse_state_t *m = mouse_get_state(); return m ? m->x : 0; }
static int in_my(void) { mouse_state_t *m = mouse_get_state(); return m ? m->y : 0; }
static int in_btns(void) {
    mouse_state_t *m = mouse_get_state();
    if (!m) return 0;
    return (m->buttons[MOUSE_BUTTON_LEFT] ? 1 : 0) |
           (m->buttons[MOUSE_BUTTON_RIGHT] ? 2 : 0) |
           (m->buttons[MOUSE_BUTTON_MIDDLE] ? 4 : 0);
}
static int in_key_avail(void) { return 0; }
static int in_key_pop(void) {
    keyboard_event_t ev;
    if (keyboard_pop_event(&ev) && ev.pressed) return (unsigned char)ev.ch;
    return -1;
}

static const int s_dx[8] = { 26, 18, 0, -18, -26, -18, 0, 18 };
static const int s_dy[8] = { 0, 18, 26, 18, 0, -18, -26, -18 };

static void draw_arc_loader(int cx, int cy, int frame) {
    backbuffer_clear(0x0E0B12);

    backbuffer_draw_string_smooth(cx - 40, 110, "Arctian", 0xF5F5F7);
    backbuffer_draw_string_smooth(cx - 92, 132, "Beyaz Kurt yukleniyor...", 0x9AA0A6);

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

    for (int a = 0; a < 360; a += 4) {
        int idx = (a / 45) % 8;
        int px = cx + (s_dx[idx] * 62) / 26;
        int py = cy + (s_dy[idx] * 62) / 26;
        backbuffer_put_pixel(px, py, 0x2A2D34);
    }

    backbuffer_draw_string_smooth(cx - 70, cy + 90, "Suruculer yukleniyor...", 0x9AA0A6);
    backbuffer_blit();
}

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
        while (*q && *q != '/' ) q++;
        q += 2;
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
    pic_init();
    idt_init();
    asm volatile("sti");

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

    LOG("[kp] pci...");
    pci_init();
    LOG("[kp] mouse...");
    mouse_init();
    LOG("[kp] keyboard...");
    keyboard_init();
    LOG("[kp] usb...");
    usb_init();
    LOG("[kp] net...");
    net_init();
    ipv4_init();
    arp_init();
    tcp_init();
    dns_init();
    ipv4_config_dhcp();
#ifdef ARCTIAN_NET_SELFTEST
    net_selftest();
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

    if (gui) mouse_set_screen_size(backbuffer_width(), backbuffer_height());

    s_gfx.width = gfx_w;
    s_gfx.height = gfx_h;
    s_gfx.framebuffer = gfx_fb;
    s_gfx.rgb = gfx_rgb;
    s_gfx.present = gfx_present;
    s_input.mouse_x = in_mx;
    s_input.mouse_y = in_my;
    s_input.mouse_buttons = in_btns;
    s_input.key_avail = in_key_avail;
    s_input.key_pop = in_key_pop;
    s_system.shutdown = sys_shutdown;
    s_system.reboot = sys_reboot;
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
    s_net.http_get = net_http_get;
    s_net.https_get = net_https_get;
    s_net.download = net_download;
    g_asi->gfx = &s_gfx;
    g_asi->input = &s_input;
    g_asi->system = &s_system;
    g_asi->net = &s_net;

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

    if (gui) {
        int cx = backbuffer_width() / 2;
        int cy = backbuffer_height() / 2 - 40;
        for (int f = 0; f < 240; f++) {
            draw_arc_loader(cx, cy, f);
            for (volatile int w = 0; w < 90000; w++) asm volatile("nop");
        }
    }

    terminal_writestring("kernel_plus: desktop baslatiliyor...\n");
    serial_puts("[kp] jumping to desktop\n");
    ((void (*)(void *))(uintptr_t)d.load_addr)(g_asi);

    kernel_panic("kernel_plus: desktop geri dondu");
    for (;;) asm volatile("hlt");
}
