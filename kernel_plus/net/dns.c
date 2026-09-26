#include "dns.h"
#include "udp.h"
#include "ipv4.h"
#include "net.h"
#include "kernel.h"
#ifdef ARCTIAN_NET_SELFTEST
#include "serial.h"
#endif

#define DNS_SRC_PORT 40000

static uint16_t dns_xid = 1;
static uint16_t dns_query_xid = 0;
static int dns_name_len = 0;
static dns_result_t *dns_results_ptr = 0;
static int *dns_count_ptr = 0;
static bool dns_done = false;
static bool dns_bound = false;

static int dns_encode_name(uint8_t *buf, const char *hostname) {
    int pos = 0;
    const char *p = hostname;
    while (*p) {
        const char *start = p;
        while (*p && *p != '.') p++;
        int len = (int)(p - start);
        if (len > 63) len = 63;
        buf[pos++] = (uint8_t)len;
        for (int i = 0; i < len; i++) buf[pos++] = (uint8_t)start[i];
        if (*p == '.') p++;
    }
    buf[pos++] = 0;
    return pos;
}

static int dns_decode_name(const uint8_t *buf, int offset, int max_len,
                           char *out, int out_max) {
    int opos = 0;
    int pos = offset;
    bool jumped = false;
    int jump_pos = 0;

    while (pos < max_len && opos < out_max) {
        uint8_t len = buf[pos];
        if (len == 0) {
            if (!jumped) return pos + 1 - offset;
            out[opos] = '\0';
            return jump_pos - offset;
        }
        if ((len & 0xC0) == 0xC0) {
            if (!jumped) jump_pos = pos + 2;
            jumped = true;
            pos = ((len & 0x3F) << 8) | buf[pos + 1];
            continue;
        }
        pos++;
        if (opos > 0) out[opos++] = '.';
        for (uint8_t i = 0; i < len && pos < max_len && opos < out_max; i++)
            out[opos++] = (char)buf[pos++];
    }
    out[opos] = '\0';
    return 0;
}

static void dns_udp_handler(const uint8_t src_ip[4], uint16_t src_port,
                            uint16_t dst_port, const uint8_t *udp_data,
                            uint16_t udp_len) {
    (void)src_ip; (void)src_port; (void)dst_port;
    if (dns_done || !dns_results_ptr || !dns_count_ptr || udp_len < 12) return;
#ifdef ARCTIAN_NET_SELFTEST
    serial_printf("[dns] rx len=%d xid=%d\n", (int)udp_len,
                  (int)(((uint16_t)udp_data[0] << 8) | udp_data[1]));
#endif
    if (udp_data[0] != ((dns_query_xid >> 8) & 0xFF) ||
        udp_data[1] != (dns_query_xid & 0xFF)) return;

    uint16_t ancount = ((uint16_t)udp_data[6] << 8) | udp_data[7];
    if (ancount == 0) {
        *dns_count_ptr = 0;
        dns_done = true;
        return;
    }

    int pos = 12 + dns_name_len + 4;
    int cnt = 0;
    for (uint16_t a = 0; a < ancount && cnt < DNS_RESULT_MAX; a++) {
        char name[256];
        int name_adv = dns_decode_name(udp_data, pos, udp_len, name, sizeof(name));
        if (name_adv <= 0) break;
        pos += name_adv;
        if (pos + 10 > udp_len) break;

        uint16_t rtype = ((uint16_t)udp_data[pos] << 8) | udp_data[pos + 1];
        pos += 2;
        pos += 2;
        pos += 4;
        uint16_t rdlen = ((uint16_t)udp_data[pos] << 8) | udp_data[pos + 1];
        pos += 2;

        if (rtype == 1 && rdlen == 4 && pos + 4 <= udp_len) {
            dns_results_ptr[cnt].ip[0] = udp_data[pos];
            dns_results_ptr[cnt].ip[1] = udp_data[pos + 1];
            dns_results_ptr[cnt].ip[2] = udp_data[pos + 2];
            dns_results_ptr[cnt].ip[3] = udp_data[pos + 3];
            cnt++;
        }
        pos += rdlen;
    }

    *dns_count_ptr = cnt;
    dns_done = true;
}

void dns_init(void) {
    if (!dns_bound) {
        udp_bind(DNS_SRC_PORT, dns_udp_handler);
        dns_bound = true;
    }
}

void dns_set_server(const uint8_t server[4]) {
    for (int i = 0; i < 4; i++) ipv4_config.dns_server[i] = server[i];
}

static bool dns_parse_ipv4_literal(const char *s, uint8_t out[4]) {
    int part = 0;
    uint32_t val = 0;
    int digits = 0;
    for (;; s++) {
        char c = *s;
        if (c >= '0' && c <= '9') {
            val = val * 10u + (uint32_t)(c - '0');
            if (val > 255u) return false;
            digits++;
        } else if (c == '.' || c == '\0') {
            if (digits == 0 || part > 3) return false;
            out[part++] = (uint8_t)val;
            val = 0;
            digits = 0;
            if (c == '\0') break;
        } else {
            return false;
        }
    }
    return part == 4;
}

bool dns_resolve(const char *hostname, dns_result_t results[DNS_RESULT_MAX], int *count) {
    if (!ipv4_config.configured) return false;

    uint8_t lit[4];
    if (dns_parse_ipv4_literal(hostname, lit)) {
        for (int i = 0; i < 4; i++) results[0].ip[i] = lit[i];
        if (count) *count = 1;
        return true;
    }

    if (!dns_bound) dns_init();

    uint8_t query[512];
    for (int i = 0; i < 512; i++) query[i] = 0;

    uint16_t xid = dns_xid++;
    query[0] = (xid >> 8) & 0xFF;
    query[1] = xid & 0xFF;
    query[2] = 0x01; query[3] = 0x00;
    query[4] = 0x00; query[5] = 0x01;
    query[6] = 0x00; query[7] = 0x00;
    query[8] = 0x00; query[9] = 0x00;
    query[10] = 0x00; query[11] = 0x00;

    int name_len = dns_encode_name(query + 12, hostname);
    int qpos = 12 + name_len;
    query[qpos++] = 0x00; query[qpos++] = 0x01;
    query[qpos++] = 0x00; query[qpos++] = 0x01;

    dns_query_xid = xid;
    dns_name_len = name_len;
    dns_results_ptr = results;
    dns_count_ptr = count;
    dns_done = false;
    if (count) *count = 0;

    if (!udp_send(ipv4_config.dns_server, DNS_SRC_PORT, DNS_PORT, query, qpos)) {
#ifdef ARCTIAN_NET_SELFTEST
        serial_printf("[dns] udp_send FAILED\n");
#endif
        return false;
    }
#ifdef ARCTIAN_NET_SELFTEST
    serial_printf("[dns] query sent xid=%d name=%s\n", (int)xid, hostname);
#endif

    for (int attempt = 0; attempt < 3 && !dns_done; attempt++) {
        for (int i = 0; i < 20000 && !dns_done; i++) {
            for (int w = 0; w < 5000; w++) { __asm__ volatile("pause"); }
            ipv4_poll();
        }
        if (!dns_done && attempt < 2) {
            if (!udp_send(ipv4_config.dns_server, DNS_SRC_PORT, DNS_PORT, query, qpos))
                break;
#ifdef ARCTIAN_NET_SELFTEST
            serial_printf("[dns] retry xid=%d name=%s\n", (int)xid, hostname);
#endif
        }
    }

    dns_results_ptr = 0;
    dns_count_ptr = 0;
    if (!dns_done) return false;
    return (*count > 0);
}
