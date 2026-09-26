#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "kernel.h"
#include "alloc.h"


void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) break;
    }
    return 0;
}

char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strstr(const char *h, const char *needle) {
    if (!needle || !needle[0]) return (char *)h;
    for (; *h; h++) {
        const char *a = h, *b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char *)h;
    }
    return 0;
}

char *strchr(const char *s, int c) {
    for (; *s; s++) if ((int)(unsigned char)*s == c) return (char *)s;
    return (c == 0) ? (char *)s : 0;
}

char *strrchr(const char *s, int c) {
    const char *last = 0;
    for (;; s++) {
        if ((int)(unsigned char)*s == c) last = s;
        if (!*s) break;
    }
    return (char *)last;
}

void explicit_bzero(void *s, size_t n) {
    volatile unsigned char *p = (volatile unsigned char *)s;
    while (n--) *p++ = 0;
}

int inet_pton(int af, const char *src, void *dst) {
    if (af != 2) return 0;
    unsigned char *out = (unsigned char *)dst;
    int i = 0;
    const char *p = src;
    for (int part = 0; part < 4; part++) {
        if (*p < '0' || *p > '9') return 0;
        int v = 0, digits = 0;
        while (*p >= '0' && *p <= '9' && digits < 3) { v = v * 10 + (*p - '0'); p++; digits++; }
        if (v > 255) return 0;
        out[i++] = (unsigned char)v;
        if (part < 3) { if (*p != '.') return 0; p++; }
    }
    return (*p == 0) ? 1 : 0;
}


static int fmt_uint(char *out, size_t cap, size_t *pos, unsigned long long v,
                    int base, int upper, int width, int left) {
    char tmp[32];
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = dig[v % (unsigned)base]; v /= (unsigned)base; }
    int written = 0;
    if (!left) for (int i = n; i < width; i++) { if (*pos < cap) out[(*pos)] = ' '; (*pos)++; written++; }
    for (int i = n - 1; i >= 0; i--) { char c = tmp[i]; if (*pos < cap) out[(*pos)] = c; (*pos)++; written++; }
    if (left) for (int i = n; i < width; i++) { if (*pos < cap) out[(*pos)] = ' '; (*pos)++; written++; }
    return written;
}

int vsnprintf(char *out, size_t cap, const char *fmt, va_list ap) {
    size_t pos = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { if (pos + 1 < cap) out[pos] = *p; pos++; continue; }
        p++;
        int left = 0, zero = 0, width = 0; int longcount = 0;
        if (*p == '-') { left = 1; p++; }
        if (*p == '0') { zero = 1; p++; }
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        if (*p == 'l') { longcount++; p++; if (*p == 'l') { longcount++; p++; } }
        else if (*p == 'z' || *p == 'h') { longcount = 1; p++; }
        switch (*p) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                for (; *s; s++) { if (pos + 1 < cap) out[pos] = *s; pos++; }
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                if (pos + 1 < cap) out[pos] = c; pos++;
                break;
            }
            case 'd': case 'i': {
                long long v = (longcount >= 2) ? va_arg(ap, long long)
                            : (longcount == 1) ? (long long)va_arg(ap, long)
                            : (long long)va_arg(ap, int);
                if (v < 0) { if (pos + 1 < cap) out[pos] = '-'; pos++; v = -v; }
                fmt_uint(out, cap, &pos, (unsigned long long)v, 10, 0, width, left);
                break;
            }
            case 'u': {
                unsigned long long v = (longcount >= 2) ? va_arg(ap, unsigned long long)
                            : (longcount == 1) ? (unsigned long long)va_arg(ap, unsigned long)
                            : (unsigned long long)va_arg(ap, unsigned int);
                fmt_uint(out, cap, &pos, v, 10, 0, width, left);
                break;
            }
            case 'x': case 'X': {
                unsigned long long v = (longcount >= 2) ? va_arg(ap, unsigned long long)
                            : (longcount == 1) ? (unsigned long long)va_arg(ap, unsigned long)
                            : (unsigned long long)va_arg(ap, unsigned int);
                fmt_uint(out, cap, &pos, v, 16, (*p == 'X'), width, left);
                break;
            }
            case 'p': {
                unsigned long long v = (unsigned long long)(uintptr_t)va_arg(ap, void *);
                if (pos + 2 < cap) { out[pos] = '0'; out[pos+1] = 'x'; }
                pos += 2;
                fmt_uint(out, cap, &pos, v, 16, 0, (int)(sizeof(void*) * 2), 0);
                break;
            }
            case '%': if (pos + 1 < cap) out[pos] = '%'; pos++; break;
            default:  if (pos + 1 < cap) out[pos] = *p; pos++; break;
        }
    }
    if (cap) out[pos < cap ? pos : cap - 1] = 0;
    return (int)pos;
}

int snprintf(char *out, size_t cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return r;
}

int fprintf(void *stream, const char *fmt, ...) {
    (void)stream; (void)fmt;
    return 0;
}

int printf(const char *fmt, ...) { (void)fmt; return 0; }


void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void free(void *p) { (void)p; }

void *malloc(size_t size) { return kmalloc(size); }
void *realloc(void *p, size_t size) { (void)p; return kmalloc(size); }

void exit(int code) { (void)code; for (;;) __asm__ volatile("hlt"); }
void abort(void) { for (;;) __asm__ volatile("hlt"); }


static int g_rdrand_supported = -1;

static int cpu_has_rdrand(void) {
    uint32_t eax, ebx, ecx, edx;
    eax = 1;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));
    (void)ebx; (void)edx;
    return (ecx >> 30) & 1;
}

static int rdrand64(uint64_t *out) {
    unsigned char ok;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(*out), "=qm"(ok));
    return ok;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    if (g_rdrand_supported < 0) g_rdrand_supported = cpu_has_rdrand();

    size_t i = 0;
    uint64_t tsc = 0;
    while (i < len) {
        uint64_t v;
        if (g_rdrand_supported && rdrand64(&v)) {
        } else {
            __asm__ volatile("rdtsc" : "=A"(tsc));
            v = tsc ^ ((uint64_t)(uintptr_t)output << 17) ^ ((uint64_t)i * 0x9E3779B97F4A7C15ull);
            v ^= v << 13; v ^= v >> 7; v ^= v << 17;
        }
        for (int b = 0; b < 8 && i < len; b++, i++) output[i] = (unsigned char)(v >> (b * 8));
    }
    if (olen) *olen = len;
    return 0;
}
