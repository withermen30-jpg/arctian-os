/* mbedTLS tabanli HTTPS (TLS 1.3) istemcisi - Arctian. */
#include "https.h"
#include "roots.h"
#include "kernel.h"
#include "net.h"
#include "tcp.h"
#include "dns.h"
#include "ipv4.h"
#include "serial.h"
#include "http.h"
#include "clock.h"

#include "mbedtls/ssl.h"
#include "mbedtls/ssl_ciphersuites.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/error.h"
#include "psa/crypto.h"

#define TLS_HEAP_SIZE   (1024 * 1024)
/* Govde tamponu: 192 KB buyuk sayfalari/indirmeleri kesiyordu.
 * 320 KB pratik ust sinir; asan tampon cekirdek BSS'inde duruyor. */
#define HTTPS_BUF_MAX   (320 * 1024)

static unsigned char tls_heap[TLS_HEAP_SIZE];
static char https_buf[HTTPS_BUF_MAX];

static mbedtls_entropy_context  s_entropy;
static mbedtls_ctr_drbg_context s_drbg;
static mbedtls_x509_crt         s_ca;
static mbedtls_ssl_config       s_conf;

static bool s_inited = false;
static int  s_last_err = 0;
static char s_last_url[512];

static void cdbg(const char *tag, const char *s, int n) {
    char buf[4200];
    int o = 0;
    while (tag && *tag && o < 4000) buf[o++] = *tag++;
    for (int i = 0; s && i < n && o < 4180; i++) {
        char ch = s[i];
        buf[o++] = (ch >= 32 && ch < 127) ? ch : '.';
    }
    buf[o++] = '\n';
    buf[o] = 0;
    serial_puts(buf);
}

void https_init(void) {
    if (s_inited) return;

    mbedtls_memory_buffer_alloc_init(tls_heap, sizeof(tls_heap));

    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_drbg);
    psa_crypto_init();

    static const char pers[] = "arctian-tls";
    int ret = mbedtls_ctr_drbg_seed(&s_drbg, mbedtls_entropy_func, &s_entropy,
                                    (const unsigned char *)pers, sizeof(pers) - 1);
    if (ret != 0) { s_last_err = ret; return; }

    mbedtls_x509_crt_init(&s_ca);
    /* Mozilla kok CA bundle'i (PEM) tek cagrida yukle. Donus > 0 ise bazi
     * sertifikalar ayristirilamadi ama zincir yine de kullanilabilir. */
    int ca_ret = mbedtls_x509_crt_parse(&s_ca, ca_bundle_pem, ca_bundle_pem_len);
#ifdef ARCTIAN_NET_SELFTEST
    serial_printf("[tls] CA parse ret=%d\n", ca_ret);
#endif
    if (ca_ret < 0) {
        /* Kok CA yuklenemezse dogrulama yapilamaz. */
        s_last_err = ca_ret;
        return;
    }

    mbedtls_ssl_config_init(&s_conf);
    mbedtls_ssl_config_defaults(&s_conf, MBEDTLS_SSL_IS_CLIENT,
                                MBEDTLS_SSL_TRANSPORT_STREAM,
                                MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_rng(&s_conf, mbedtls_ctr_drbg_random, &s_drbg);
    mbedtls_ssl_conf_ca_chain(&s_conf, &s_ca, NULL);
    mbedtls_ssl_conf_authmode(&s_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_min_tls_version(&s_conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&s_conf, MBEDTLS_SSL_VERSION_TLS1_3);

    /* Gercek Chrome ClientHello'suna yaklastir: ALPN, cipher ve egriler. */
    static const char *s_alpn[] = { "http/1.1", NULL };
    mbedtls_ssl_conf_alpn_protocols(&s_conf, s_alpn);

    static const int s_ciphersuites[] = {
        MBEDTLS_TLS1_3_AES_128_GCM_SHA256,
        MBEDTLS_TLS1_3_AES_256_GCM_SHA384,
        MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256,
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
        MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_RSA_WITH_AES_256_GCM_SHA384,
        MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA,
        MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA,
        0
    };
    mbedtls_ssl_conf_ciphersuites(&s_conf, s_ciphersuites);

    static const uint16_t s_groups[] = {
        MBEDTLS_SSL_IANA_TLS_GROUP_X25519,
        MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1,
        MBEDTLS_SSL_IANA_TLS_GROUP_SECP384R1,
        0
    };
    mbedtls_ssl_conf_groups(&s_conf, s_groups);

    static const uint16_t s_sig_algs[] = {
        MBEDTLS_TLS1_3_SIG_ECDSA_SECP256R1_SHA256,
        MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA256,
        MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA256,
        MBEDTLS_TLS1_3_SIG_ECDSA_SECP384R1_SHA384,
        MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA384,
        MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA384,
        MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA512,
        MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA512,
        MBEDTLS_TLS1_3_SIG_ECDSA_SECP521R1_SHA512,
        MBEDTLS_TLS1_3_SIG_NONE
    };
    mbedtls_ssl_conf_sig_algs(&s_conf, s_sig_algs);

    mbedtls_ssl_conf_tls13_key_exchange_modes(
        &s_conf, MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL |
                 MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL);

    s_inited = true;
    s_last_err = 0;
}

/* ---- BIO: mbedTLS'i mevcut TCP yigini uzerinden tasir ---- */
static int tls_bio_send(void *ctx, const unsigned char *buf, size_t len) {
    int conn = *(int *)ctx;
    if (len == 0) return 0;
    if (len > 16384) len = 16384;
    if (!tcp_send(conn, (const uint8_t *)buf, (uint16_t)len))
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return (int)len;
}

static int tls_bio_recv(void *ctx, unsigned char *buf, size_t len) {
    int conn = *(int *)ctx;
    if (len == 0) return 0;
    if (len > 16384) len = 16384;

    /* Okuma zaman asimi: TCG/emulasyonda 3 sn yetmiyordu; yavas ama
     * calisan baglantilar "TLS hatasi" gibi gorunuyordu. */
    uint64_t deadline = clock_ms() + 6000;
    for (;;) {
        int st = tcp_state(conn);
        int n = tcp_receive(conn, (uint8_t *)buf, (uint16_t)len);
        if (n > 0) return n;
        if (n < 0 || st == TCP_STATE_CLOSED || st == TCP_STATE_TIME_WAIT ||
            st == TCP_STATE_CLOSE_WAIT)
            return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
        if ((int64_t)(clock_ms() - deadline) >= 0) return MBEDTLS_ERR_SSL_TIMEOUT;
        for (volatile int w = 0; w < 3000; w++) __asm__ volatile("pause");
    }
}

static void append(char *dst, uint32_t cap, uint32_t *pos, const char *s, uint32_t n) {
    for (uint32_t i = 0; i < n && *pos < cap - 1; i++) dst[(*pos)++] = s[i];
}

/* ---- Cerez deposu: Set-Cookie sakla, Cookie gonder ---- */
#define CK_MAX      128
#define CK_HOSTMAX  128
#define CK_NAMEMAX  64
#define CK_VALMAX   4096

typedef struct {
    char host[CK_HOSTMAX];
    char name[CK_NAMEMAX];
    char val[CK_VALMAX];
} ck_ent_t;

static ck_ent_t s_ck[CK_MAX];
static int s_ck_count = 0;

static int ck_len(const char *s) {
    int n = 0;
    if (s) while (s[n]) n++;
    return n;
}

static void ck_copy(char *dst, int cap, const char *src, int n) {
    if (cap <= 0) return;
    if (n > cap - 1) n = cap - 1;
    for (int i = 0; i < n; i++) dst[i] = src[i];
    dst[n] = 0;
}

static char ck_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int ck_eqn(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] || !b[i] || ck_lower(a[i]) != ck_lower(b[i])) return 0;
    }
    return 1;
}

static int ck_host_match(const char *req, const char *dom) {
    int rl = ck_len(req), dl = ck_len(dom);
    if (dl == 0 || rl < dl) return 0;
    if (rl == dl) return ck_eqn(req, dom, dl);
    int off = rl - dl;
    return (req[off - 1] == '.') && ck_eqn(req + off, dom, dl);
}

static void ck_set(const char *host, const char *name, int nlen,
                   const char *val, int vlen) {
    if (!host || !name || nlen <= 0) return;
    int idx = -1;
    int hl = ck_len(host);
    for (int i = 0; i < s_ck_count; i++) {
        if (ck_eqn(s_ck[i].name, name, nlen) &&
            ck_eqn(s_ck[i].host, host, hl) &&
            ck_len(s_ck[i].name) == nlen && ck_len(s_ck[i].host) == hl) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (s_ck_count < CK_MAX) idx = s_ck_count++;
        else idx = 0;
    }
    ck_copy(s_ck[idx].host, CK_HOSTMAX, host, hl);
    ck_copy(s_ck[idx].name, CK_NAMEMAX, name, nlen);
    ck_copy(s_ck[idx].val, CK_VALMAX, val, vlen);
}

static int ck_build(const char *host, char *out, int cap) {
    int o = 0;
    for (int i = 0; i < s_ck_count; i++) {
        if (!ck_host_match(host, s_ck[i].host)) continue;
        int need = ck_len(s_ck[i].name) + 1 + ck_len(s_ck[i].val) + (o ? 2 : 0);
        if (o + need >= cap - 1) break;
        if (o) { out[o++] = ';'; out[o++] = ' '; }
        for (const char *n = s_ck[i].name; *n && o < cap - 1; n++) out[o++] = *n;
        if (o < cap - 1) out[o++] = '=';
        for (const char *v = s_ck[i].val; *v && o < cap - 1; v++) out[o++] = *v;
    }
    out[o] = 0;
    return o;
}

static void ck_absorb(const char *hdr, uint32_t hlen, const char *host) {
    uint32_t i = 0;
    while (i < hlen) {
        uint32_t ls = i;
        while (i < hlen && hdr[i] != '\n') i++;
        uint32_t le = i;
        if (i < hlen) i++;
        if (le > ls && hdr[le - 1] == '\r') le--;
        if (le - ls <= 11) continue;
        if (!ck_eqn(hdr + ls, "set-cookie:", 11)) continue;

        uint32_t p = ls + 11;
        while (p < le && (hdr[p] == ' ' || hdr[p] == '\t')) p++;
        uint32_t ns = p;
        while (p < le && hdr[p] != '=' && hdr[p] != ';') p++;
        if (p >= le || hdr[p] != '=') continue;
        uint32_t ne = p;
        p++;
        uint32_t vs = p;
        while (p < le && hdr[p] != ';') p++;
        uint32_t ve = p;
        while (ve > vs && (hdr[ve - 1] == ' ' || hdr[ve - 1] == '\t')) ve--;

        char dom[CK_HOSTMAX];
        ck_copy(dom, CK_HOSTMAX, host, ck_len(host));
        uint32_t q = ve;
        while (q + 7 <= le) {
            if (ck_eqn(hdr + q, "domain=", 7)) {
                uint32_t ds = q + 7;
                uint32_t de = ds;
                while (de < le && hdr[de] != ';' && hdr[de] != ' ' && hdr[de] != '\t') de++;
                if (ds < de && hdr[ds] == '.') ds++;
                if (de > ds) ck_copy(dom, CK_HOSTMAX, hdr + ds, (int)(de - ds));
                break;
            }
            q++;
        }
        ck_set(dom, hdr + ns, (int)(ne - ns), hdr + vs, (int)(ve - vs));
        cdbg("[ck] absorb ", hdr + ns, (int)(ne - ns));
    }
}

void https_cookie_set(const char *host, const char *name, const char *value) {
    if (!host || !name || !name[0]) return;
    cdbg("[ck] jsset name=", name, ck_len(name));
    cdbg("[ck] jsval=", value ? value : "", ck_len(value ? value : ""));
    ck_set(host, name, ck_len(name), value ? value : "", ck_len(value ? value : ""));
}

const char *https_get(const char *host, uint16_t port, const char *path,
                      uint32_t *len_out) {
    if (!s_inited) https_init();
    if (!s_inited) return 0;
    if (!host || !host[0]) return 0;
    if (!path || !path[0]) path = "/";
    cdbg("[ck] GET ", path, (int)ck_len(path));

    uint8_t ip[4];
    dns_result_t res[DNS_RESULT_MAX];
    int dn = 0;
    if (!dns_resolve(host, res, &dn) || dn <= 0) { s_last_err = -2; return 0; }
    for (int i = 0; i < 4; i++) ip[i] = res[0].ip[i];

    int conn = tcp_connect(ip, port);
    if (conn < 0) { s_last_err = -3; return 0; }

    mbedtls_ssl_context ssl;
    mbedtls_ssl_init(&ssl);

    int ret = mbedtls_ssl_setup(&ssl, &s_conf);
    if (ret == 0) {
        mbedtls_ssl_set_hostname(&ssl, host);
        mbedtls_ssl_set_bio(&ssl, &conn, tls_bio_send, tls_bio_recv, NULL);

        for (;;) {
            ret = mbedtls_ssl_handshake(&ssl);
            if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            break;
        }
    }


    if (ret == 0) {
        char req[8192];
        uint32_t r = 0;
        const char *pre = "GET ";
        while (*pre) req[r++] = *pre++;
        for (int i = 0; path[i] && r < 3000; i++) req[r++] = path[i];
        const char *v = " HTTP/1.1\r\nHost: ";
        while (*v) req[r++] = *v++;
        for (int i = 0; host[i] && r < 3100; i++) req[r++] = host[i];
        char ck[6144];
        if (ck_build(host, ck, sizeof(ck)) > 0) {
            cdbg("[ck] cookie=", ck, (int)ck_len(ck));
            int has = 0;
            for (int i = 0; ck[i]; i++) {
                if (ck[i] == 'S' && ck[i+1] == 'G' && ck[i+2] == '_' && ck[i+3] == 'S' && ck[i+4] == 'S') { has = 1; break; }
            }
            { char hb[48]; int ho = 0; const char *p = "[ck] cklen="; while (*p) hb[ho++] = *p++;
              int L = ck_len(ck); char d[8]; int di = 0; if (!L) d[di++] = '0'; while (L) { d[di++] = (char)('0' + L % 10); L /= 10; }
              while (di) hb[ho++] = d[--di]; hb[ho++] = ' '; hb[ho++] = 's'; hb[ho++] = 'g'; hb[ho++] = '='; hb[ho++] = has ? 'Y' : 'N'; hb[ho++] = '\n'; hb[ho] = 0; serial_puts(hb); }
            const char *ch = "\r\nCookie: ";
            while (*ch) req[r++] = *ch++;
            for (int i = 0; ck[i] && r < 7500; i++) req[r++] = ck[i];
        }
        const char *tail =
            "\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
            "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"
            "\r\nsec-ch-ua: \"Chromium\";v=\"122\", \"Not(A:Brand\";v=\"24\", "
            "\"Google Chrome\";v=\"122\""
            "\r\nsec-ch-ua-mobile: ?0"
            "\r\nsec-ch-ua-platform: \"Windows\""
            "\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,"
            "image/avif,image/webp,image/apng,*/*;q=0.8,"
            "application/signed-exchange;v=b3;q=0.7"
            "\r\nAccept-Language: tr-TR,tr;q=0.9,en-US;q=0.8,en;q=0.7"
            "\r\nAccept-Encoding: identity"
            "\r\nSec-Fetch-Dest: document"
            "\r\nSec-Fetch-Mode: navigate"
            "\r\nSec-Fetch-Site: none"
            "\r\nSec-Fetch-User: ?1"
            "\r\nUpgrade-Insecure-Requests: 1"
            "\r\nConnection: close\r\n\r\n";
        while (*tail && r < 8180) req[r++] = *tail++;

        size_t off = 0;
        while (off < r) {
            ret = mbedtls_ssl_write(&ssl, (const unsigned char *)req + off, r - off);
            if (ret > 0) { off += (size_t)ret; continue; }
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            break;
        }
    }

    uint32_t total = 0;
    int hdr_end = -1;
    int has_cl = 0;
    uint32_t content_len = 0;
    if (ret >= 0) {
        for (;;) {
            unsigned char tmp[2048];
            ret = mbedtls_ssl_read(&ssl, tmp, sizeof(tmp));
            if (ret > 0) {
                append(https_buf, HTTPS_BUF_MAX, &total, (const char *)tmp, (uint32_t)ret);

                if (hdr_end < 0) {
                    for (uint32_t i = 0; i + 3 < total; i++) {
                        if (https_buf[i] == '\r' && https_buf[i + 1] == '\n' &&
                            https_buf[i + 2] == '\r' && https_buf[i + 3] == '\n') {
                            hdr_end = (int)(i + 4);
                            break;
                        }
                    }
                    if (hdr_end > 0) {
                        static const char clname[] = "content-length";
                        for (int i = 0; i + 14 < hdr_end; i++) {
                            int m = 1;
                            for (int k = 0; k < 14; k++) {
                                char a = https_buf[i + k];
                                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                                if (a != clname[k]) { m = 0; break; }
                            }
                            if (!m) continue;
                            int j = i + 14;
                            while (j < hdr_end && (https_buf[j] == ' ' || https_buf[j] == '\t' ||
                                   https_buf[j] == ':')) j++;
                            uint32_t v = 0;
                            while (j < hdr_end && https_buf[j] >= '0' && https_buf[j] <= '9') {
                                v = v * 10u + (uint32_t)(https_buf[j] - '0');
                                j++;
                            }
                            has_cl = 1; content_len = v;
                            break;
                        }
                    }
                }

                if (hdr_end > 0 && has_cl &&
                    total >= (uint32_t)hdr_end + content_len) break;
                if (hdr_end > 0 && total >= 5 &&
                    https_buf[total - 5] == '0' && https_buf[total - 4] == '\r' &&
                    https_buf[total - 3] == '\n' && https_buf[total - 2] == '\r' &&
                    https_buf[total - 1] == '\n') break;
                if (total >= HTTPS_BUF_MAX - 1) break;
                continue;
            }
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            break; /* 0, close notify veya hata */
        }
    }

    {
        uint32_t hle = total;
        for (uint32_t i = 0; i + 3 < total; i++) {
            if (https_buf[i] == '\r' && https_buf[i + 1] == '\n' &&
                https_buf[i + 2] == '\r' && https_buf[i + 3] == '\n') {
                hle = i;
                break;
            }
        }
        if (hle > 0) ck_absorb(https_buf, hle, host);
    }

    s_last_err = ret;
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    tcp_close(conn);

    if (total == 0) return 0;
    https_buf[total] = 0;
    if (len_out) *len_out = total;
    return https_buf;
}

const char *https_get_url(const char *url, uint32_t *len_out) {
    if (!url) return 0;
    const char *p = url;
    if (!(p[0]=='h'&&p[1]=='t'&&p[2]=='t'&&p[3]=='p'&&p[4]=='s')) return 0;
    p += 5;
    if (p[0]==':'&&p[1]=='/'&&p[2]=='/') p += 3;

    char host[128];
    int hl = 0;
    while (*p && *p != '/' && *p != ':' && hl < 127) host[hl++] = *p++;
    host[hl] = 0;
    if (hl == 0) return 0;

    uint16_t port = 443;
    if (*p == ':') {
        p++;
        uint32_t pv = 0;
        while (*p >= '0' && *p <= '9') { pv = pv*10u + (uint32_t)(*p-'0'); p++; }
        if (pv > 0 && pv < 65536u) port = (uint16_t)pv;
    }
    const char *path = (*p == '/') ? p : "/";
    return https_get(host, port, path, len_out);
}

/* ================================================================== */
/* Yonlendirme takibi + chunked cozumu + yalnizca govde getirisi       */
/* ================================================================== */

static int ci_starts_with(const char *s, const char *p) {
    for (; *p; s++, p++) {
        char ca = *s, cb = *p;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb || ca == 0) return 0;
    }
    return 1;
}

static int ci_contains_n(const char *hay, uint32_t n, const char *needle) {
    if (!needle || !needle[0]) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t k = i;
        const char *b = needle;
        while (k < n && *b) {
            char ca = hay[k], cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
            if (ca != cb) break;
            k++; b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

static int ci_contains(const char *hay, const char *needle) {
    if (!hay) return 0;
    uint32_t n = 0; while (hay[n]) n++;
    return ci_contains_n(hay, n, needle);
}

/* Ham yanit baslik blogunda "name: value" arar. */
static int raw_header_value(const char *hdr, uint32_t hlen, const char *name,
                            char *out, int cap) {
    uint32_t i = 0;
    while (i < hlen) {
        uint32_t ls = i;
        while (i < hlen && hdr[i] != '\n') i++;
        uint32_t le = i;
        if (i < hlen) i++;
        if (le > ls && hdr[le - 1] == '\r') le--;

        uint32_t c = ls;
        while (c < le && hdr[c] != ':') c++;
        if (c < le) {
            uint32_t nl = c - ls;
            int match = 1;
            int j = 0;
            for (; name[j]; j++) {
                if (j >= (int)nl) { match = 0; break; }
                char a = hdr[ls + (uint32_t)j], b = name[j];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                if (a != b) { match = 0; break; }
            }
            if (match && j == (int)nl) {
                uint32_t v = c + 1;
                while (v < le && (hdr[v] == ' ' || hdr[v] == '\t')) v++;
                int o = 0;
                while (v < le && o < cap - 1) out[o++] = hdr[v++];
                out[o] = 0;
                return 1;
            }
        }
    }
    return 0;
}

/* "Transfer-Encoding: chunked" govdesini cozer. */
static uint32_t decode_chunked(const char *in, uint32_t in_len,
                               char *out, uint32_t out_cap) {
    uint32_t ip = 0, op = 0;
    while (ip < in_len) {
        uint32_t sz = 0;
        int digits = 0;
        while (ip < in_len) {
            char c = in[ip];
            int v = -1;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            if (v < 0) break;
            sz = (sz << 4) | (uint32_t)v;
            ip++; digits++;
            if (digits > 8) break;
        }
        if (digits == 0) break;
        while (ip < in_len && in[ip] != '\n') ip++;   /* uzanti/CRLF atla */
        if (ip < in_len) ip++;
        if (sz == 0) break;                            /* son chunk */
        if (ip + sz > in_len) sz = in_len - ip;
        for (uint32_t k = 0; k < sz && op < out_cap - 1; k++)
            out[op++] = in[ip + k];
        ip += sz;
        while (ip < in_len && (in[ip] == '\r' || in[ip] == '\n')) ip++;
    }
    out[op] = 0;
    return op;
}

/* URL'den sema + yetki ayirir; path baslangicini dondurur. */
static const char *split_authority(const char *url, char *scheme, int scap,
                                   char *auth, int acap) {
    const char *p = url;
    const char *s = p;
    while (*p && *p != ':') p++;
    int sl = (int)(p - s);
    if (sl >= scap) sl = scap - 1;
    for (int i = 0; i < sl; i++) scheme[i] = s[i];
    scheme[sl] = 0;
    if (p[0] == ':' && p[1] == '/' && p[2] == '/') p += 3;
    else p = s;
    const char *a = p;
    while (*p && *p != '/') p++;
    int al = (int)(p - a);
    if (al >= acap) al = acap - 1;
    for (int i = 0; i < al; i++) auth[i] = a[i];
    auth[al] = 0;
    return p;
}

static void str_copy_n(char *dst, const char *src, int cap) {
    int i = 0;
    for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* Location'i mutlak URL'ye cevirir. */
static void resolve_location(const char *base, const char *loc, char *out, int cap) {
    char scheme[8], auth[160];
    split_authority(base, scheme, sizeof(scheme), auth, sizeof(auth));
    int o = 0;
    if (ci_starts_with(loc, "http://") || ci_starts_with(loc, "https://")) {
        for (int i = 0; loc[i] && o < cap - 1; i++) out[o++] = loc[i];
    } else if (loc[0] == '/' && loc[1] == '/') {
        for (int i = 0; scheme[i] && o < cap - 1; i++) out[o++] = scheme[i];
        if (o < cap - 1) out[o++] = ':';
        for (int i = 0; loc[i] && o < cap - 1; i++) out[o++] = loc[i];
    } else if (loc[0] == '/') {
        for (int i = 0; scheme[i] && o < cap - 1; i++) out[o++] = scheme[i];
        for (int i = 0; "://"[i] && o < cap - 1; i++) out[o++] = "://"[i];
        for (int i = 0; auth[i] && o < cap - 1; i++) out[o++] = auth[i];
        for (int i = 0; loc[i] && o < cap - 1; i++) out[o++] = loc[i];
    } else {
        for (int i = 0; scheme[i] && o < cap - 1; i++) out[o++] = scheme[i];
        for (int i = 0; "://"[i] && o < cap - 1; i++) out[o++] = "://"[i];
        for (int i = 0; auth[i] && o < cap - 1; i++) out[o++] = auth[i];
        if (o < cap - 1) out[o++] = '/';
        for (int i = 0; loc[i] && o < cap - 1; i++) out[o++] = loc[i];
    }
    out[o] = 0;
}

/* http/https ortak getirme: yonlendirmeleri (max 6) takip eder, chunked
 * govdeyi yerinde cozer ve YALNIZCA govdeyi dondurur. */
const char *https_fetch_body(const char *url, uint32_t *len_out) {
    if (!url || !url[0]) return 0;

    char cur[512];
    str_copy_n(cur, url, sizeof(cur));

    for (int hop = 0; hop < 6; hop++) {
        if (ci_starts_with(cur, "https://")) {
            uint32_t rl = 0;
            const char *raw_c = https_get_url(cur, &rl);
            if (!raw_c || rl == 0) return 0;
            char *raw = (char *)raw_c;

            uint32_t hlen = rl, boff = rl;
            for (uint32_t i = 0; i + 3 < rl; i++) {
                if (raw[i] == '\r' && raw[i + 1] == '\n' &&
                    raw[i + 2] == '\r' && raw[i + 3] == '\n') {
                    hlen = i; boff = i + 4; break;
                }
            }
            int status = 0;
            if (rl >= 12) status = (raw[9] - '0') * 100 + (raw[10] - '0') * 10 + (raw[11] - '0');

            char *body = raw + boff;
            uint32_t body_len = rl - boff;
            uint32_t blen = body_len;
            if (body_len > 0 && ci_contains_n(raw, hlen, "Transfer-Encoding") &&
                ci_contains_n(raw, hlen, "chunked")) {
                blen = decode_chunked(body, body_len, body, body_len + 1);
            } else {
                body[body_len] = 0;
            }

            if (status >= 300 && status < 400) {
                char loc[512];
                if (raw_header_value(raw, hlen, "Location", loc, sizeof(loc)) && loc[0]) {
                    char nxt[512];
                    resolve_location(cur, loc, nxt, sizeof(nxt));
                    str_copy_n(cur, nxt, sizeof(cur));
                    continue;
                }
            }
            if (len_out) *len_out = blen;
            str_copy_n(s_last_url, cur, sizeof(s_last_url));
            return body;
        } else {
            http_response_t resp;
            if (!http_get(cur, &resp)) return 0;
            const char *te = http_header_get(&resp, "Transfer-Encoding");
            char *body = resp.body ? resp.body : (char *)"";
            uint32_t body_len = resp.body ? resp.body_length : 0;
            uint32_t blen = body_len;
            if (body_len > 0 && te && ci_contains(te, "chunked")) {
                blen = decode_chunked(body, body_len, body, body_len + 1);
            }
            if (resp.status_code >= 300 && resp.status_code < 400) {
                const char *loc = http_header_get(&resp, "Location");
                if (loc && loc[0]) {
                    char nxt[512];
                    resolve_location(cur, loc, nxt, sizeof(nxt));
                    str_copy_n(cur, nxt, sizeof(cur));
                    continue;
                }
            }
            if (len_out) *len_out = blen;
            str_copy_n(s_last_url, cur, sizeof(s_last_url));
            return body;
        }
    }
    return 0;
}

const char *https_fetch_last_url(void) {
    return s_last_url;
}

void https_last_error(char *buf, int cap) {
    if (!buf || cap <= 0) return;
    mbedtls_strerror(s_last_err, buf, (size_t)cap);
}
