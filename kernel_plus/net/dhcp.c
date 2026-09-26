#include "dhcp.h"
#include "udp.h"
#include "ipv4.h"
#include "net.h"
#include "kernel.h"

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5

#define DHCP_MAGIC0 0x63
#define DHCP_MAGIC1 0x82
#define DHCP_MAGIC2 0x53
#define DHCP_MAGIC3 0x63

static uint32_t dhcp_xid;
static bool     dhcp_bound;
static int      dhcp_state;
static uint8_t  dhcp_offer_ip[4];
static uint8_t  dhcp_offer_mask[4];
static uint8_t  dhcp_offer_gw[4];
static uint8_t  dhcp_offer_dns[4];
static uint8_t  dhcp_server_id[4];
static bool     dhcp_have_mask;
static bool     dhcp_have_gw;
static bool     dhcp_have_dns;

static void cpy4(uint8_t dst[4], const uint8_t src[4]) {
    for (int i = 0; i < 4; i++) dst[i] = src[i];
}

static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint8_t dhcp_parse_options(const uint8_t *opt, uint16_t len) {
    uint8_t type = 0;
    uint16_t i = 0;
    while (i < len) {
        uint8_t code = opt[i++];
        if (code == 0) continue;
        if (code == 255) break;
        if (i >= len) break;
        uint8_t olen = opt[i++];
        if (i + olen > len) break;
        const uint8_t *d = opt + i;
        switch (code) {
            case 53: if (olen >= 1) type = d[0]; break;
            case 1:  if (olen >= 4) { cpy4(dhcp_offer_mask, d); dhcp_have_mask = true; } break;
            case 3:  if (olen >= 4) { cpy4(dhcp_offer_gw, d);   dhcp_have_gw = true; }   break;
            case 6:  if (olen >= 4) { cpy4(dhcp_offer_dns, d);  dhcp_have_dns = true; }  break;
            case 54: if (olen >= 4) { cpy4(dhcp_server_id, d); } break;
        }
        i += olen;
    }
    return type;
}

static void dhcp_udp_handler(const uint8_t src_ip[4], uint16_t src_port,
                             uint16_t dst_port, const uint8_t *data,
                             uint16_t len) {
    (void)src_ip; (void)src_port; (void)dst_port;
    if (len < 240) return;
    if (data[0] != 2) return;
    if (rd32be(data + 4) != dhcp_xid) return;
    if (data[236] != DHCP_MAGIC0 || data[237] != DHCP_MAGIC1 ||
        data[238] != DHCP_MAGIC2 || data[239] != DHCP_MAGIC3) return;

    uint8_t type = dhcp_parse_options(data + 240, (uint16_t)(len - 240));
    cpy4(dhcp_offer_ip, data + 16);

    if (type == DHCPOFFER && dhcp_state == 0)
        dhcp_state = 1;
    else if (type == DHCPACK)
        dhcp_state = 2;
}

static bool dhcp_send(uint8_t msg_type, const uint8_t req_ip[4],
                      const uint8_t server_id[4]) {
    const uint8_t *mac = net_get_mac();
    if (!mac) return false;

    uint8_t frame[600];
    for (int i = 0; i < 600; i++) frame[i] = 0;

    for (int i = 0; i < 6; i++) frame[i] = 0xFF;
    for (int i = 0; i < 6; i++) frame[6 + i] = mac[i];
    frame[12] = 0x08; frame[13] = 0x00;

    uint8_t *ip = frame + 14;
    ip[0] = 0x45; ip[1] = 0x00;
    ip[6] = 0x00; ip[7] = 0x00;
    ip[8] = 64; ip[9] = 17;
    for (int i = 0; i < 4; i++) { ip[12 + i] = 0x00; ip[16 + i] = 0xFF; }

    uint8_t *udp = frame + 34;
    udp[0] = (DHCP_CLIENT_PORT >> 8) & 0xFF; udp[1] = DHCP_CLIENT_PORT & 0xFF;
    udp[2] = (DHCP_SERVER_PORT >> 8) & 0xFF; udp[3] = DHCP_SERVER_PORT & 0xFF;

    uint8_t *b = frame + 42;
    b[0] = 1; b[1] = 1; b[2] = 6; b[3] = 0;
    b[4] = (dhcp_xid >> 24) & 0xFF;
    b[5] = (dhcp_xid >> 16) & 0xFF;
    b[6] = (dhcp_xid >> 8) & 0xFF;
    b[7] = dhcp_xid & 0xFF;
    b[10] = 0x80; b[11] = 0x00;
    for (int i = 0; i < 6; i++) b[28 + i] = mac[i];
    b[236] = DHCP_MAGIC0; b[237] = DHCP_MAGIC1;
    b[238] = DHCP_MAGIC2; b[239] = DHCP_MAGIC3;

    int o = 240;
    b[o++] = 53; b[o++] = 1; b[o++] = msg_type;
    if (msg_type == DHCPREQUEST) {
        b[o++] = 50; b[o++] = 4;
        for (int i = 0; i < 4; i++) b[o++] = req_ip[i];
        b[o++] = 54; b[o++] = 4;
        for (int i = 0; i < 4; i++) b[o++] = server_id[i];
    }
    b[o++] = 55; b[o++] = 4; b[o++] = 1; b[o++] = 3; b[o++] = 6; b[o++] = 54;
    b[o++] = 255;

    int bootp_len = o;
    int udp_total = 8 + bootp_len;
    int ip_total = 20 + udp_total;
    int frame_len = 14 + ip_total;

    ip[2] = (ip_total >> 8) & 0xFF; ip[3] = ip_total & 0xFF;
    ip[4] = (dhcp_xid >> 8) & 0xFF; ip[5] = dhcp_xid & 0xFF;
    uint16_t ipc = ipv4_checksum(ip, 20);
    ip[10] = (ipc >> 8) & 0xFF; ip[11] = ipc & 0xFF;

    udp[4] = (udp_total >> 8) & 0xFF; udp[5] = udp_total & 0xFF;
    udp[6] = 0; udp[7] = 0;

    return net_send_packet(frame, (uint16_t)frame_len);
}

bool dhcp_configure(void) {
    if (!net_get_mac()) return false;

    if (!dhcp_bound) {
        udp_bind(DHCP_CLIENT_PORT, dhcp_udp_handler);
        dhcp_bound = true;
    }

    uint8_t saved_ip[4], saved_gw[4], saved_mask[4], saved_dns[4];
    cpy4(saved_ip, ipv4_config.src_ip);
    cpy4(saved_gw, ipv4_config.gateway);
    cpy4(saved_mask, ipv4_config.subnet_mask);
    cpy4(saved_dns, ipv4_config.dns_server);
    bool saved_cfg = ipv4_config.configured;

    for (int i = 0; i < 4; i++) ipv4_config.src_ip[i] = 0;
    ipv4_config.configured = false;

    const uint8_t *mac = net_get_mac();
    dhcp_xid = 0x3903F326u;
    if (mac)
        dhcp_xid = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
                   ((uint32_t)mac[4] << 8) | (uint32_t)mac[5];
    dhcp_xid ^= 0xA5A50000u;
    if (dhcp_xid == 0) dhcp_xid = 0x12345678u;

    dhcp_have_mask = dhcp_have_gw = dhcp_have_dns = false;
    for (int i = 0; i < 4; i++)
        dhcp_offer_ip[i] = dhcp_offer_mask[i] = dhcp_offer_gw[i] =
        dhcp_offer_dns[i] = dhcp_server_id[i] = 0;

    bool got = false;
    for (int disc = 0; disc < 3 && !got; disc++) {
        dhcp_state = 0;
        dhcp_send(DHCPDISCOVER, 0, 0);
        for (int i = 0; i < 1000 && dhcp_state == 0; i++) {
            for (int w = 0; w < 20000; w++) { __asm__ volatile("pause"); }
            ipv4_poll();
        }
        if (dhcp_state != 1) continue;

        dhcp_send(DHCPREQUEST, dhcp_offer_ip, dhcp_server_id);
        for (int i = 0; i < 1000 && dhcp_state != 2; i++) {
            for (int w = 0; w < 20000; w++) { __asm__ volatile("pause"); }
            ipv4_poll();
        }
        if (dhcp_state == 2) got = true;
    }

    if (got) {
        uint8_t mask[4] = {255, 255, 255, 0};
        uint8_t gw[4]   = {0, 0, 0, 0};
        uint8_t dns[4]  = {8, 8, 8, 8};
        if (dhcp_have_mask) cpy4(mask, dhcp_offer_mask);
        if (dhcp_have_gw)   cpy4(gw, dhcp_offer_gw);
        if (dhcp_have_dns)  cpy4(dns, dhcp_offer_dns);
        ipv4_configure(dhcp_offer_ip, gw, mask, dns);
        kernel_debug("DHCP: IP=%d.%d.%d.%d GW=%d.%d.%d.%d",
                     dhcp_offer_ip[0], dhcp_offer_ip[1],
                     dhcp_offer_ip[2], dhcp_offer_ip[3],
                     gw[0], gw[1], gw[2], gw[3]);
        return true;
    }

    cpy4(ipv4_config.src_ip, saved_ip);
    cpy4(ipv4_config.gateway, saved_gw);
    cpy4(ipv4_config.subnet_mask, saved_mask);
    cpy4(ipv4_config.dns_server, saved_dns);
    ipv4_config.configured = saved_cfg;
    kernel_debug("DHCP: yanit yok, mevcut yapilandirma korunuyor");
    return false;
}
