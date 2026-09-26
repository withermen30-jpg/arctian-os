#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
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
