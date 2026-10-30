#include "sig.h"
#include <stdint.h>
#include <stddef.h>
#include "sig_pub.h"

/* ==================================================================== */
/* SHA-256                                                              */
/* ==================================================================== */

static uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static const uint32_t K256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
    0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
    0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
    0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
    0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
    0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static void sha256_block(uint32_t h[8], const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
               ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i-15], 7) ^ ror32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ror32(w[i-2], 17) ^ ror32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = k + S1 + ch + K256[i] + w[i];
        uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        k = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

void sig_sha256(const void *data, uint32_t len, uint8_t out[32]) {
    uint32_t h[8] = { 0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                      0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u };
    const uint8_t *p = (const uint8_t *)data;
    uint32_t n = len;
    while (n >= 64) { sha256_block(h, p); p += 64; n -= 64; }
    uint8_t tail[128];
    for (uint32_t i = 0; i < n; i++) tail[i] = p[i];
    tail[n] = 0x80;
    uint32_t tl = (n < 56) ? 64 : 128;
    for (uint32_t i = n + 1; i < tl - 8; i++) tail[i] = 0;
    uint64_t bits = (uint64_t)len * 8u;
    for (int i = 0; i < 8; i++) tail[tl - 1 - i] = (uint8_t)(bits >> (8 * i));
    sha256_block(h, tail);
    if (tl == 128) sha256_block(h, tail + 64);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)(h[i]);
    }
}

/* ==================================================================== */
/* 2048-bit bignum: Montgomery carpma (yalniz dogrulama + test imza)     */
/* ==================================================================== */

#define NL 64   /* 2048 / 32 */

static int bn_cmp(const uint32_t *a, const uint32_t *b, int n) {
    for (int i = n - 1; i >= 0; i--) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

static void bn_sub(uint32_t *a, const uint32_t *b, int n) {
    uint64_t br = 0;
    for (int i = 0; i < n; i++) {
        uint64_t d = (uint64_t)a[i] - (uint64_t)b[i] - br;
        a[i] = (uint32_t)d;
        br = (d >> 63) & 1u;
    }
}

/* t = 2*t mod m (t < m varsayimi) */
static void bn_dblmod(uint32_t *t, const uint32_t *m, int n) {
    uint32_t carry = 0;
    for (int i = 0; i < n; i++) {
        uint32_t hi = t[i] >> 31;
        t[i] = (t[i] << 1) | carry;
        carry = hi;
    }
    if (carry || bn_cmp(t, m, n) >= 0) bn_sub(t, m, n);
}

/* n0inv = -m^-1 mod 2^32 (m tek) */
static uint32_t bn_n0inv(uint32_t m0) {
    uint32_t inv = 1;
    for (int i = 0; i < 5; i++) inv *= 2u - m0 * inv;   /* Newton */
    return (uint32_t)(0u - inv);
}

/* Montgomery carpma: r = a*b*R^-1 mod m (CIOS) */
static void mmul(uint32_t *r, const uint32_t *a, const uint32_t *b,
                 const uint32_t *m, uint32_t n0inv) {
    uint32_t t[NL + 2];
    for (int i = 0; i < NL + 2; i++) t[i] = 0;
    for (int i = 0; i < NL; i++) {
        uint64_t c = 0;
        for (int j = 0; j < NL; j++) {
            uint64_t s = (uint64_t)a[j] * (uint64_t)b[i] + (uint64_t)t[j] + c;
            t[j] = (uint32_t)s;
            c = s >> 32;
        }
        uint64_t s = (uint64_t)t[NL] + c;
        t[NL] = (uint32_t)s;
        t[NL + 1] = (uint32_t)(s >> 32);

        uint32_t u = (uint32_t)(t[0] * n0inv);
        c = 0;
        for (int j = 0; j < NL; j++) {
            uint64_t s2 = (uint64_t)u * (uint64_t)m[j] + (uint64_t)t[j] + c;
            if (j > 0) t[j - 1] = (uint32_t)s2;
            c = s2 >> 32;
        }
        s = (uint64_t)t[NL] + c;
        t[NL - 1] = (uint32_t)s;
        t[NL] = t[NL + 1] + (uint32_t)(s >> 32);
    }
    if (t[NL] || bn_cmp(t, m, NL) >= 0) bn_sub(t, m, NL);
    for (int i = 0; i < NL; i++) r[i] = t[i];
}

/* R^2 mod m; R = 2^2048 */
static void bn_r2(uint32_t *r2, const uint32_t *m) {
    uint32_t x[NL];
    for (int i = 0; i < NL; i++) x[i] = 0;
    x[0] = 1;
    for (int i = 0; i < 2048; i++) bn_dblmod(x, m, NL);   /* R mod m */
    for (int i = 0; i < 2048; i++) bn_dblmod(x, m, NL);   /* R^2 mod m */
    for (int i = 0; i < NL; i++) r2[i] = x[i];
}

/* out = base^exp mod m (base normal domainde) */
static void bn_powmod(const uint32_t *base, const uint32_t *exp, int exp_bits,
                      const uint32_t *m, uint32_t *out) {
    uint32_t n0 = bn_n0inv(m[0]);
    uint32_t r2[NL], b[NL], acc[NL], one[NL];
    bn_r2(r2, m);
    mmul(b, base, r2, m, n0);           /* base -> Montgomery */
    for (int i = 0; i < NL; i++) one[i] = 0;
    one[0] = 1;
    mmul(acc, one, r2, m, n0);          /* 1 -> Montgomery */
    for (int bit = exp_bits - 1; bit >= 0; bit--) {
        mmul(acc, acc, acc, m, n0);
        if ((exp[bit / 32] >> (bit % 32)) & 1u) mmul(acc, acc, b, m, n0);
    }
    mmul(out, acc, one, m, n0);         /* Montgomery -> normal */
}

/* PKCS#1 v1.5 SHA-256 DigestInfo */
static int emsa_check(const uint8_t *em) {
    static const uint8_t di[19] = {
        0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,
        0x04,0x02,0x01,0x05,0x00,0x04,0x20
    };
    if (em[0] != 0x00 || em[1] != 0x01) return -1;
    int i = 2;
    while (i < 256 - 32 - 19 && em[i] == 0xFF) i++;
    if (em[i] != 0x00) return -2;
    i++;
    for (int k = 0; k < 19; k++, i++) if (em[i] != di[k]) return -3;
    return 0;
}

int sig_verify(const uint8_t *body, uint32_t len, const uint8_t sig[SIG_RSA_LEN]) {
    if (!body || !sig) return -1;
    uint32_t s[NL];
    for (int i = 0; i < NL; i++) {
        int o = (NL - 1 - i) * 4;
        s[i] = ((uint32_t)sig[o] << 24) | ((uint32_t)sig[o+1] << 16) |
               ((uint32_t)sig[o+2] << 8) | (uint32_t)sig[o+3];
    }
    if (bn_cmp(s, SIG_MOD, NL) >= 0) return -2;

    uint32_t e[1] = { 65537u };
    uint32_t m[NL];
    bn_powmod(s, e, 17, SIG_MOD, m);

    uint8_t em[256];
    for (int i = 0; i < NL; i++) {
        int o = (NL - 1 - i) * 4;
        em[o]   = (uint8_t)(m[i] >> 24);
        em[o+1] = (uint8_t)(m[i] >> 16);
        em[o+2] = (uint8_t)(m[i] >> 8);
        em[o+3] = (uint8_t)(m[i]);
    }
    if (emsa_check(em) != 0) return -3;

    uint8_t h[32];
    sig_sha256(body, len, h);
    for (int i = 0; i < 32; i++)
        if (em[256 - 32 + i] != h[i]) return -4;
    return 0;
}

uint32_t sig_body_len(const uint8_t *buf, uint32_t size) {
    if (!buf || size <= SIG_TRAILER_LEN) return size;
    const uint8_t *t = buf + size - SIG_TRAILER_LEN;
    if (t[0] == 'A' && t[1] == 'S' && t[2] == 'G' && t[3] == '1')
        return size - SIG_TRAILER_LEN;
    return size;
}

int sig_verify_pkg(const uint8_t *buf, uint32_t size) {
    if (!buf || size <= SIG_TRAILER_LEN) return -1;
    uint32_t bl = sig_body_len(buf, size);
    if (bl == size) return -1;                       /* imza yok */
    return sig_verify(buf, bl, buf + size - SIG_RSA_LEN);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int sig_verify_hex(const char *body, uint32_t len, const char *sig_hex) {
    if (!sig_hex) return -1;
    uint8_t sig[SIG_RSA_LEN];
    for (int i = 0; i < SIG_RSA_LEN; i++) {
        int hi = hexval(sig_hex[i * 2]);
        int lo = hexval(sig_hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        sig[i] = (uint8_t)((hi << 4) | lo);
    }
    return sig_verify((const uint8_t *)body, len, sig);
}

#ifdef ARCTIAN_07_TEST
#include "sig_priv.h"

int sig_test_seal(uint8_t *buf, uint32_t cap, uint32_t len, uint32_t *out_len) {
    if (!buf || cap < len + SIG_TRAILER_LEN) return -1;

    /* EM = 00 01 FF..FF 00 DigestInfo SHA256(body) */
    uint8_t h[32];
    sig_sha256(buf, len, h);
    static const uint8_t di[19] = {
        0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,
        0x04,0x02,0x01,0x05,0x00,0x04,0x20
    };
    uint8_t em[256];
    em[0] = 0x00; em[1] = 0x01;
    for (int i = 2; i < 256 - 32 - 19 - 1; i++) em[i] = 0xFF;
    em[256 - 32 - 19 - 1] = 0x00;
    for (int i = 0; i < 19; i++) em[256 - 32 - 19 + i] = di[i];
    for (int i = 0; i < 32; i++) em[256 - 32 + i] = h[i];

    uint32_t m[NL];
    for (int i = 0; i < NL; i++) {
        int o = (NL - 1 - i) * 4;
        m[i] = ((uint32_t)em[o] << 24) | ((uint32_t)em[o+1] << 16) |
               ((uint32_t)em[o+2] << 8) | (uint32_t)em[o+3];
    }
    uint32_t s[NL];
    bn_powmod(m, SIG_D, 2048, SIG_MOD, s);

    buf[len + 0] = 'A'; buf[len + 1] = 'S';
    buf[len + 2] = 'G'; buf[len + 3] = '1';
    for (int i = 0; i < NL; i++) {
        int o = (NL - 1 - i) * 4;
        buf[len + 4 + o]     = (uint8_t)(s[i] >> 24);
        buf[len + 4 + o + 1] = (uint8_t)(s[i] >> 16);
        buf[len + 4 + o + 2] = (uint8_t)(s[i] >> 8);
        buf[len + 4 + o + 3] = (uint8_t)(s[i]);
    }
    if (out_len) *out_len = len + SIG_TRAILER_LEN;
    return 0;
}
#else
int sig_test_seal(uint8_t *buf, uint32_t cap, uint32_t len, uint32_t *out_len) {
    (void)buf; (void)cap; (void)len; (void)out_len;
    return -1;
}
#endif
