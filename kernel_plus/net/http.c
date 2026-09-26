#include "http.h"
#include "dns.h"
#include "tcp.h"
#include "kernel.h"
#ifdef ARCTIAN_NET_SELFTEST
#include "serial.h"
#endif

static char http_response_buf[HTTP_MAX_RESPONSE];

void http_init(void) {}

bool http_get(const char *url, http_response_t *response) {
    for (uint32_t i = 0; i < sizeof(http_response_buf); i++) http_response_buf[i] = 0;
    for (int i = 0; i < HTTP_MAX_HEADERS; i++) {
        response->headers[i].name[0] = '\0';
        response->headers[i].value[0] = '\0';
    }
    response->header_count = 0;
    response->body = 0;
    response->body_length = 0;
    response->status_code = 0;
    response->status_text[0] = '\0';

    const char *p = url;
    bool is_https = false;
    if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p') {
        p += 4;
        if (*p == 's') { is_https = true; p++; }
        if (p[0] == ':' && p[1] == '/' && p[2] == '/') p += 3;
    }
    if (is_https) return false;

    char host[256];
    int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < 255) host[hi++] = *p++;
    host[hi] = '\0';

    uint16_t port = HTTP_PORT;
    if (*p == ':') {
        p++;
        uint32_t pv = 0;
        while (*p >= '0' && *p <= '9') { pv = pv * 10u + (uint32_t)(*p - '0'); p++; }
        if (pv > 0 && pv < 65536u) port = (uint16_t)pv;
    }

    char path[512];
    int pi = 0;
    if (*p) {
        while (*p && pi < 511) path[pi++] = *p++;
    } else {
        path[pi++] = '/';
    }
    path[pi] = '\0';

    dns_result_t dns_results[DNS_RESULT_MAX];
    int dns_count = 0;
    if (!dns_resolve(host, dns_results, &dns_count)) return false;

    int conn_id = tcp_connect(dns_results[0].ip, port);
#ifdef ARCTIAN_NET_SELFTEST
    serial_printf("[http] %s -> %d.%d.%d.%d:%d conn=%d\n", host,
                  dns_results[0].ip[0], dns_results[0].ip[1],
                  dns_results[0].ip[2], dns_results[0].ip[3],
                  (int)port, conn_id);
#endif
    if (conn_id < 0) return false;

    char request[2048];
    int ri = 0;
    const char *method = "GET ";
    while (*method) request[ri++] = *method++;
    for (int i = 0; path[i]; i++) request[ri++] = path[i];
    const char *http_ver = " HTTP/1.1\r\n";
    const char *host_line = "Host: ";
    while (*http_ver) request[ri++] = *http_ver++;
    while (*host_line) request[ri++] = *host_line++;
    for (int i = 0; host[i]; i++) request[ri++] = host[i];
    const char *crlf = "\r\n";
    const char *conn_line = "Connection: close\r\n";
    const char *ua_line = "User-Agent: Arctian/0.1\r\n";
    const char *accept_line = "Accept: text/html,text/plain\r\n\r\n";
    while (*crlf) request[ri++] = *crlf++;
    while (*conn_line) request[ri++] = *conn_line++;
    while (*ua_line) request[ri++] = *ua_line++;
    while (*accept_line) request[ri++] = *accept_line++;

    if (!tcp_send(conn_id, (uint8_t *)request, ri)) {
#ifdef ARCTIAN_NET_SELFTEST
        serial_printf("[http] tcp_send FAILED (state=%d)\n", tcp_state(conn_id));
#endif
        tcp_close(conn_id);
        return false;
    }

    int total = 0;
    for (int wait = 0; wait < 20000; wait++) {
        for (int w = 0; w < 20000; w++) { __asm__ volatile("pause"); }

        uint8_t buf[4096];
        int n = tcp_receive(conn_id, buf, sizeof(buf) - 1);
        if (n < 0) break;
        if (n > 0) {
            if (total + n >= HTTP_MAX_RESPONSE) n = HTTP_MAX_RESPONSE - total - 1;
            for (int i = 0; i < n; i++) http_response_buf[total + i] = (char)buf[i];
            total += n;
            wait = 0;
        }
        if (tcp_state(conn_id) == TCP_STATE_TIME_WAIT ||
            tcp_state(conn_id) == TCP_STATE_CLOSED) break;
    }
#ifdef ARCTIAN_NET_SELFTEST
    serial_printf("[http] total=%d state=%d\n", total, tcp_state(conn_id));
#endif
    tcp_close(conn_id);

    if (total == 0) return false;
    http_response_buf[total] = '\0';

#ifdef ARCTIAN_NET_SELFTEST
    {
        char dl[161];
        int base;
        for (base = 0; base < 800 && base < total; base += 160) {
            int n = 0;
            for (; n < 160 && (base + n) < total; n++) {
                char c = http_response_buf[base + n];
                dl[n] = (c >= 32 && c < 127) ? c : '.';
            }
            dl[n] = 0;
            serial_printf("[http] %d: %s\n", base, dl);
        }
    }
#endif

    char *rp = http_response_buf;
    char *line_start = rp;
    int line_num = 0;
    bool headers_done = false;

    while (*rp) {
        char *end = rp;
        while (*end && *end != '\r' && *end != '\n') end++;
        char saved = *end;
        *end = '\0';
        int line_len = (int)(end - line_start);

        if (line_num == 0 && line_len >= 12) {
            response->status_code = ((line_start[9] - '0') * 100) +
                                    ((line_start[10] - '0') * 10) +
                                    (line_start[11] - '0');
            int si = 0;
            for (int i = 13; line_start[i] && si < 63; i++, si++)
                response->status_text[si] = line_start[i];
            response->status_text[si] = '\0';
        } else if (!headers_done && line_len > 0) {
            char *colon = line_start;
            while (*colon && *colon != ':') colon++;
            if (*colon == ':' && response->header_count < HTTP_MAX_HEADERS) {
                *colon = '\0';
                char *val = colon + 1;
                while (*val == ' ') val++;
                int ni = 0;
                for (int i = 0; line_start[i] && ni < 63; i++, ni++)
                    response->headers[response->header_count].name[ni] = line_start[i];
                response->headers[response->header_count].name[ni] = '\0';
                int vi = 0;
                for (int i = 0; val[i] && vi < 255; i++, vi++)
                    response->headers[response->header_count].value[vi] = val[i];
                response->headers[response->header_count].value[vi] = '\0';
                response->header_count++;
            }
        } else if (!headers_done && line_len == 0) {
            headers_done = true;
        }

        *end = saved;
        line_start = end;
        if (*line_start == '\r') line_start++;
        if (*line_start == '\n') line_start++;
        rp = line_start;
        line_num++;

        if (headers_done && line_len == 0) {
            response->body = line_start;
            response->body_length = total - (int)(line_start - http_response_buf);
            break;
        }
    }

#ifdef ARCTIAN_NET_SELFTEST
    serial_printf("[http] parse hdrs=%d body_off=%d total=%d\n",
                  response->header_count,
                  response->body ? (int)(response->body - http_response_buf) : -1,
                  total);
#endif

    return true;
}

void http_free_response(http_response_t *response) {
    response->body = 0;
    response->body_length = 0;
    response->status_code = 0;
}

const char *http_header_get(const http_response_t *resp, const char *name) {
    for (int i = 0; i < resp->header_count; i++) {
        int j;
        for (j = 0; name[j] && resp->headers[i].name[j]; j++) {
            char a = name[j];
            char b = resp->headers[i].name[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (name[j] == '\0' && resp->headers[i].name[j] == '\0')
            return resp->headers[i].value;
    }
    return 0;
}

static int hdr_find_clen(const char *h, uint32_t n, uint32_t *out) {
    static const char key[] = "content-length:";
    for (uint32_t i = 0; i + sizeof(key) - 1 <= n; i++) {
        int m = 1;
        for (int j = 0; key[j]; j++) {
            char a = h[i + j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (a != key[j]) { m = 0; break; }
        }
        if (!m) continue;
        uint32_t v = 0, j = i + sizeof(key) - 1;
        while (j < n && (h[j] == ' ' || h[j] == '\t')) j++;
        while (j < n && h[j] >= '0' && h[j] <= '9') { v = v * 10u + (uint32_t)(h[j] - '0'); j++; }
        *out = v;
        return 1;
    }
    return 0;
}

int http_download(const char *url, void *dst, uint32_t cap, uint32_t *len_out) {
    if (!url || !dst || cap == 0) return -1;

    const char *p = url;
    if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p') {
        p += 4;
        if (*p == 's') return -1;
        if (p[0] == ':' && p[1] == '/' && p[2] == '/') p += 3;
    }

    char host[256];
    int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < 255) host[hi++] = *p++;
    host[hi] = '\0';
    if (hi == 0) return -1;

    uint16_t port = HTTP_PORT;
    if (*p == ':') {
        p++;
        uint32_t pv = 0;
        while (*p >= '0' && *p <= '9') { pv = pv * 10u + (uint32_t)(*p - '0'); p++; }
        if (pv > 0 && pv < 65536u) port = (uint16_t)pv;
    }

    char path[512];
    int pi = 0;
    if (*p) { while (*p && pi < 511) path[pi++] = *p++; }
    else { path[pi++] = '/'; }
    path[pi] = '\0';

    dns_result_t res[DNS_RESULT_MAX];
    int dn = 0;
    if (!dns_resolve(host, res, &dn) || dn <= 0) return -2;

    int conn = tcp_connect(res[0].ip, port);
    if (conn < 0) return -3;

    char req[1024];
    int ri = 0;
    const char *m = "GET ";
    while (*m && ri < 1020) req[ri++] = *m++;
    for (int i = 0; path[i] && ri < 1000; i++) req[ri++] = path[i];
    const char *v = " HTTP/1.1\r\nHost: ";
    while (*v && ri < 1020) req[ri++] = *v++;
    for (int i = 0; host[i] && ri < 1020; i++) req[ri++] = host[i];
    const char *t = "\r\nUser-Agent: Arctian/0.1\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    while (*t && ri < 1023) req[ri++] = *t++;

    if (!tcp_send(conn, (const uint8_t *)req, (uint16_t)ri)) {
        tcp_close(conn);
        return -4;
    }

    uint8_t *out = (uint8_t *)dst;
    uint32_t total = 0;
    uint32_t need = 0;
    uint32_t boff = 0;
    int have_hdr = 0;
    static uint8_t s_dl_tmp[32768];
    long idle = 0;

    for (;;) {
        int n = tcp_receive(conn, s_dl_tmp, sizeof(s_dl_tmp));
        if (n < 0) break;
        if (n > 0) {
            uint32_t c = (uint32_t)n;
            if (total + c > cap) c = cap - total;
            for (uint32_t i = 0; i < c; i++) out[total + i] = s_dl_tmp[i];
            total += c;
            idle = 0;

            if (!have_hdr) {
                for (uint32_t i = 0; i + 3 < total; i++) {
                    if (out[i] == '\r' && out[i + 1] == '\n' &&
                        out[i + 2] == '\r' && out[i + 3] == '\n') {
                        boff = i + 4;
                        have_hdr = 1;
                        break;
                    }
                }
                if (have_hdr) hdr_find_clen((const char *)out, boff, &need);
            }
            if (have_hdr && need && (total - boff) >= need) break;
            if (total >= cap) break;
        } else {
            int st = tcp_state(conn);
            if (st == TCP_STATE_CLOSED || st == TCP_STATE_TIME_WAIT) break;
            if (st == TCP_STATE_CLOSE_WAIT) {
                if (!have_hdr) break;
                if (need && (total - boff) >= need) break;
                if (++idle > 30000L) break;
            } else {
                if (++idle > 100000L) break;
            }
            for (volatile int w = 0; w < 20000; w++) __asm__ volatile("pause");
        }
    }
    tcp_close(conn);

    if (total < 16 || !have_hdr) return -5;
    int status = (out[9] - '0') * 100 + (out[10] - '0') * 10 + (out[11] - '0');
    if (status < 200 || status >= 300) return -6;

    uint32_t blen = total > boff ? total - boff : 0;
    for (uint32_t i = 0; i < blen; i++) out[i] = out[boff + i];
    if (len_out) *len_out = blen;
    return 0;
}
