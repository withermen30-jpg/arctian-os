#include "app.h"
#include "lv_port.h"
#include <stdint.h>

#define TERM_LOG_MAX 20000

static char  g_log[TERM_LOG_MAX];
static int   g_log_len;

static lv_obj_t *g_out_box;
static lv_obj_t *g_out_label;
static lv_obj_t *g_input;
static lv_group_t *g_group;

static void term_putc(char c) {
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
    if (g_out_box) lv_obj_scroll_to_y(g_out_box, LV_COORD_MAX, LV_ANIM_OFF);
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static int starts_with(const char *s, const char *p) {
    while (*p) { if (*s != *p) return 0; s++; p++; }
    return 1;
}

static void req_append(char *buf, int *n, const char *s) {
    while (*s && *n < 580) buf[(*n)++] = *s++;
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
    req_append(req, &r, "User-Agent: Arctian/0.1\r\n");
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
        if (st == 8  || st == 0 ) break;
    }
    if (asi->net->tcp_close) asi->net->tcp_close(c);

    if (total == 0) term_puts("\n(Yanit alinamadi)\n");
    else term_putc('\n');
    term_flush();
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

static void term_exec(asi_t *asi, const char *cmd) {
    while (*cmd == ' ') cmd++;

    term_puts("arctian> "); term_puts(cmd); term_putc('\n');

    if (!cmd[0]) { term_flush(); return; }

    char name[16];
    int i = 0;
    while (cmd[i] && cmd[i] != ' ' && i < 15) { name[i] = cmd[i]; i++; }
    name[i] = 0;
    const char *arg = cmd + i;
    while (*arg == ' ') arg++;

    if (str_eq(name, "help")) {
        term_puts("Komutlar:\n");
        term_puts("  help            bu yardim\n");
        term_puts("  ip              ag baglanti bilgisi\n");
        term_puts("  dns <host>      alan adi cozumle\n");
        term_puts("  curl <url>      HTTP GET istegi at\n");
        term_puts("  clear           ekrani temizle\n");
    } else if (str_eq(name, "clear")) {
        g_log_len = 0;
    } else if (str_eq(name, "ip") || str_eq(name, "ifconfig")) {
        cmd_ip(asi);
    } else if (str_eq(name, "dns")) {
        cmd_dns(asi, arg);
    } else if (str_eq(name, "curl")) {
        cmd_curl(asi, arg);
    } else {
        term_puts("Bilinmeyen komut: "); term_puts(name);
        term_puts("  (help yazin)\n");
    }
    term_flush();
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
    term_exec(asi, cmd);
}

static void term_del_cb(lv_event_t *e) {
    (void)e;
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, NULL);
    g_input = 0;
    g_out_label = 0;
    g_out_box = 0;
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    g_log_len = 0;
    g_log[0] = 0;

    int h = lv_obj_get_height(win);
    if (h < 120) h = 300;

    g_out_box = lv_obj_create(win);
    lv_obj_remove_style_all(g_out_box);
    lv_obj_set_size(g_out_box, LV_PCT(100), h - 52);
    lv_obj_set_pos(g_out_box, 0, 0);
    lv_obj_set_style_bg_color(g_out_box, lv_color_hex(0x0A0A0E), 0);
    lv_obj_set_style_bg_opa(g_out_box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_out_box, 8, 0);
    lv_obj_set_scroll_dir(g_out_box, LV_DIR_VER);

    g_out_label = lv_label_create(g_out_box);
    lv_label_set_text(g_out_label, "");
    lv_label_set_long_mode(g_out_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_out_label, LV_PCT(100));
    lv_obj_set_style_text_color(g_out_label, lv_color_hex(0x7CFC9A), 0);

    g_input = lv_textarea_create(win);
    lv_textarea_set_one_line(g_input, true);
    lv_textarea_set_placeholder_text(g_input, "komut yaz (help)");
    lv_obj_set_width(g_input, LV_PCT(100));
    lv_obj_set_height(g_input, 44);
    lv_obj_align(g_input, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(g_input, input_ready_cb, LV_EVENT_READY, asi);
    lv_obj_add_event_cb(win, term_del_cb, LV_EVENT_DELETE, 0);

    if (!g_group) g_group = lv_group_create();
    lv_group_remove_all_objs(g_group);
    lv_group_add_obj(g_group, g_input);
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, g_group);
    lv_group_focus_obj(g_input);

    term_puts("Arctian Terminal\n");
    term_puts("Komutlar: help, ip, dns <host>, curl <url>, clear\n\n");
    term_flush();
}
