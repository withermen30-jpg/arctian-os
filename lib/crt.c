/**
 * Freestanding CRT/mini-libm: tiny_ttf (stb_truetype) icin gerekli
 * memcpy/memset ve basit matematik fonksiyonlari.
 */
#include <stddef.h>
#include <stdint.h>

/* 64 bitlik sozler hizali olmak zorunda degil; x86-64 hizada olmayan
 * erisimi destekler. aligned(1)+may_alias ile hem hizada-olmayan hem de
 * strict-aliasing kurallarini bozmayan okuma/yazma yapilir. */
typedef uint64_t u64p __attribute__((aligned(1), may_alias));

/* Bu fonksiyonlar sistemin en sicak yoludur: LVGL dolumlari, blok onbellek
 * sektor kopyalari, tiny_ttf ve PNG cozucu hepsini cagirir. Bayt bayt
 * dongu yerine once hedefi 8'e hizalar, sonra 8 baytlik bloklarla calisir. */

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n && ((size_t)d & 7u)) { *d++ = *s++; n--; }
    while (n >= 8) { *(u64p *)d = *(const u64p *)s; d += 8; s += 8; n -= 8; }
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    unsigned char b = (unsigned char)c;
    while (n && ((size_t)d & 7u)) { *d++ = b; n--; }
    if (n >= 8) {
        uint64_t pat = (uint64_t)b * 0x0101010101010101ull;
        while (n >= 8) { *(u64p *)d = pat; d += 8; n -= 8; }
    }
    while (n--) *d++ = b;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n && ((size_t)d & 7u)) { *d++ = *s++; n--; }
        while (n >= 8) { *(u64p *)d = *(const u64p *)s; d += 8; s += 8; n -= 8; }
        while (n--) *d++ = *s++;
    } else if (d > s) {
        /* Geriden kopya: once sonu 8'e hizala, sonra bloklarla geri sar. */
        while (n && ((size_t)(d + n) & 7u)) { n--; d[n] = s[n]; }
        while (n >= 8) { n -= 8; *(u64p *)(d + n) = *(const u64p *)(s + n); }
        while (n) { n--; d[n] = s[n]; }
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (n >= 8) {
        uint64_t xa = *(const u64p *)x, ya = *(const u64p *)y;
        if (xa != ya) {
            /* Ilk farkli bayti bul (kucuk uc sonlu: dusuk bayt once). */
            unsigned diff = (unsigned)__builtin_ctzll(xa ^ ya) >> 3;
            return x[diff] < y[diff] ? -1 : 1;
        }
        x += 8; y += 8; n -= 8;
    }
    while (n--) { if (*x != *y) return *x < *y ? -1 : 1; x++; y++; }
    return 0;
}

int bcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

double fabs(double x) { return x < 0 ? -x : x; }

double floor(double x) {
    double f = (double)(long long)x;
    if (x < 0 && f != x) f -= 1.0;
    return f;
}

double ceil(double x) {
    double f = (double)(long long)x;
    if (x > 0 && f != x) f += 1.0;
    return f;
}

double sqrt(double x) {
    if (x <= 0) return 0;
    double r = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 48; i++) r = 0.5 * (r + x / r);
    return r;
}

#define SVG_PI 3.14159265358979

double sin(double x) {
    while (x > SVG_PI) x -= 2.0 * SVG_PI;
    while (x < -SVG_PI) x += 2.0 * SVG_PI;
    double x2 = x * x;
    double term = x, s = x;
    for (int i = 1; i < 9; i++) {
        term *= -x2 / (double)((2 * i) * (2 * i + 1));
        s += term;
    }
    return s;
}

double cos(double x) { return sin(x + SVG_PI / 2.0); }
