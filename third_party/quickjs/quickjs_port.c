#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include "asi.h"

extern void *arctian_js_arena(unsigned long *out_size);

typedef struct blk {
    size_t size;
    size_t prev_size;
    struct blk *fprev;
    struct blk *fnext;
} blk_t;

#define BLK_HDR ((size_t)sizeof(blk_t))
#define BLK_FREE_BIT ((size_t)1)

static blk_t *g_free_head = 0;
static unsigned char *g_pool = 0;
static size_t g_pool_size = 0;
static int g_pool_tried = 0;

static size_t align16(size_t n) { return (n + 15u) & ~(size_t)15u; }

static void pool_init(void) {
    g_pool_tried = 1;
    unsigned long sz = 0;
    void *p = arctian_js_arena(&sz);
    if (!p || sz < 65536) return;
    g_pool = (unsigned char *)p;
    g_pool_size = (size_t)sz & ~(size_t)15u;
    blk_t *b = (blk_t *)g_pool;
    b->size = g_pool_size;
    b->prev_size = 0;
    b->fprev = 0;
    b->fnext = 0;
    g_free_head = b;
}

static int blk_is_free(blk_t *b) { return (b->size & BLK_FREE_BIT) != 0; }
static size_t blk_size(blk_t *b) { return b->size & ~BLK_FREE_BIT; }
static void blk_set_size(blk_t *b, size_t s) { b->size = s; }
static void blk_set_free(blk_t *b, int f) {
    if (f) b->size |= BLK_FREE_BIT; else b->size &= ~BLK_FREE_BIT;
}
static blk_t *blk_next(blk_t *b) {
    return (blk_t *)((unsigned char *)b + blk_size(b));
}

static void freelist_remove(blk_t *b) {
    if (b->fprev) b->fprev->fnext = b->fnext;
    else g_free_head = b->fnext;
    if (b->fnext) b->fnext->fprev = b->fprev;
    b->fprev = 0;
    b->fnext = 0;
}

static void freelist_insert(blk_t *b) {
    b->fprev = 0;
    b->fnext = g_free_head;
    if (g_free_head) g_free_head->fprev = b;
    g_free_head = b;
}

static size_t arena_total_used(void);

void *malloc(size_t size) {
    if (!g_pool_tried) pool_init();
    if (!g_pool) return 0;
    size_t need = align16(size < 16 ? 16 : size) + BLK_HDR;
    blk_t *b = g_free_head;
    while (b) {
        size_t bs = blk_size(b);
        if (bs >= need) {
            size_t remain = bs - need;
            if (remain >= BLK_HDR + 32) {
                blk_t *r = (blk_t *)((unsigned char *)b + need);
                r->size = remain;
                r->prev_size = need;
                r->fprev = 0;
                r->fnext = 0;
                freelist_insert(r);
                blk_t *nx = (blk_t *)((unsigned char *)r + remain);
                if ((unsigned char *)nx < g_pool + g_pool_size) nx->prev_size = remain;
                blk_set_size(b, need);
            }
            freelist_remove(b);
            blk_set_free(b, 0);
            return (unsigned char *)b + BLK_HDR;
        }
        b = b->fnext;
    }
    return 0;
}

void free(void *ptr) {
    if (!ptr) return;
    blk_t *b = (blk_t *)((unsigned char *)ptr - BLK_HDR);
    blk_set_free(b, 1);
    blk_t *nx = blk_next(b);
    if ((unsigned char *)nx < g_pool + g_pool_size && blk_is_free(nx)) {
        freelist_remove(nx);
        blk_set_size(b, blk_size(b) + blk_size(nx));
    }
    if (b->prev_size) {
        blk_t *pv = (blk_t *)((unsigned char *)b - b->prev_size);
        if (blk_is_free(pv)) {
            freelist_remove(pv);
            blk_set_size(pv, blk_size(pv) + blk_size(b));
            b = pv;
        }
    }
    blk_t *nn = blk_next(b);
    if ((unsigned char *)nn < g_pool + g_pool_size) nn->prev_size = blk_size(b);
    freelist_insert(b);
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }
    blk_t *b = (blk_t *)((unsigned char *)ptr - BLK_HDR);
    size_t cur = blk_size(b) - BLK_HDR;
    if (size <= cur) return ptr;
    void *np = malloc(size);
    if (!np) return 0;
    memcpy(np, ptr, cur);
    free(ptr);
    return np;
}

size_t malloc_usable_size(void *ptr) {
    if (!ptr) return 0;
    blk_t *b = (blk_t *)((unsigned char *)ptr - BLK_HDR);
    return blk_size(b) - BLK_HDR;
}

static size_t arena_total_used(void) { return 0; }

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    for (size_t i = 0; i < n; i++) if (p[i] == (unsigned char)c) return (void *)(p + i);
    return 0;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return 0;
    }
}
char *strrchr(const char *s, int c) {
    const char *last = 0;
    for (;; s++) {
        if (*s == (char)c) last = s;
        if (!*s) return (char *)last;
    }
}
char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
char *strncat(char *d, const char *s, size_t n) {
    char *p = d + strlen(d);
    size_t i = 0;
    for (; i < n && s[i]; i++) p[i] = s[i];
    p[i] = 0;
    return d;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x != y) return (int)x - (int)y;
        if (!x) return 0;
    }
    return 0;
}
char *strstr(const char *h, const char *n) {
    if (!*n) return (char *)h;
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char *)h;
    }
    return 0;
}

int abs(int x) { return x < 0 ? -x : x; }
void abort(void) { for (;;) { } }

double fmax(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    return x > y ? x : y;
}
double fmin(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    return x < y ? x : y;
}
double trunc(double x) { return x < 0 ? ceil(x) : floor(x); }
double round(double x) {
    if (isnan(x) || isinf(x) || x == 0.0) return x;
    return x > 0 ? floor(x + 0.5) : ceil(x - 0.5);
}
long lrint(double x) { return (long)(x > 0 ? floor(x + 0.5) : ceil(x - 0.5)); }
double fmod(double x, double y) {
    if (y == 0.0 || isnan(x) || isnan(y) || isinf(x)) return NAN;
    if (isinf(y)) return x;
    double q = trunc(x / y);
    return x - q * y;
}
static double scal2(double x, int n) {
    while (n > 0) { x *= 2.0; n--; }
    while (n < 0) { x *= 0.5; n++; }
    return x;
}
double exp(double x) {
    if (isnan(x)) return x;
    if (x > 709.78) return INFINITY;
    if (x < -745.14) return 0.0;
    double k = floor(x / 0.6931471805599453 + 0.5);
    double r = x - k * 0.6931471805599453;
    double r2 = r * r;
    double p = 1.0 + r + r2 / 2.0 + r2 * r / 6.0 + r2 * r2 / 24.0
             + r2 * r2 * r / 120.0 + r2 * r2 * r2 / 720.0
             + r2 * r2 * r2 * r / 5040.0 + r2 * r2 * r2 * r2 / 40320.0
             + r2 * r2 * r2 * r2 * r / 362880.0;
    return scal2(p, (int)k);
}
double expm1(double x) {
    if (fabs(x) < 1e-5) return x + x * x / 2.0 + x * x * x / 6.0;
    return exp(x) - 1.0;
}
double log(double x) {
    if (isnan(x)) return x;
    if (x < 0.0) return NAN;
    if (x == 0.0) return -INFINITY;
    if (isinf(x)) return x;
    int e = 0;
    while (x >= 2.0) { x *= 0.5; e++; }
    while (x < 0.5) { x *= 2.0; e--; }
    double z = (x - 1.0) / (x + 1.0);
    double z2 = z * z;
    double s = z, t = z;
    for (int i = 1; i < 24; i++) { t *= z2; s += t / (double)(2 * i + 1); }
    return 2.0 * s + (double)e * 0.6931471805599453;
}
double log1p(double x) {
    if (fabs(x) < 1e-5) return x - x * x / 2.0 + x * x * x / 3.0;
    return log(1.0 + x);
}
double log2(double x) { return log(x) / 0.6931471805599453; }
double log10(double x) { return log(x) / 2.302585092994046; }
double pow(double a, double b) {
    if (b == 0.0) return 1.0;
    if (a == 1.0) return 1.0;
    if (isnan(a) || isnan(b)) return NAN;
    if (a == 0.0) return b > 0.0 ? 0.0 : INFINITY;
    if (a < 0.0) {
        double bi = round(b);
        if (fabs(b - bi) > 1e-9) return NAN;
        double r = exp(b * log(-a));
        return fmod(bi, 2.0) != 0.0 ? -r : r;
    }
    return exp(b * log(a));
}
double cbrt(double x) {
    if (x == 0.0 || isnan(x) || isinf(x)) return x;
    int neg = x < 0;
    if (neg) x = -x;
    double r = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 60; i++) r = (2.0 * r + x / (r * r)) / 3.0;
    return neg ? -r : r;
}
double hypot(double x, double y) {
    x = fabs(x); y = fabs(y);
    if (isinf(x) || isinf(y)) return INFINITY;
    if (isnan(x) || isnan(y)) return NAN;
    double m = x > y ? x : y;
    if (m == 0.0) return 0.0;
    double a = x / m, b = y / m;
    return m * sqrt(a * a + b * b);
}
double tan(double x) { return sin(x) / cos(x); }
double atan(double x) {
    int neg = x < 0;
    if (neg) x = -x;
    double r;
    if (x > 1.0) r = 1.5707963267948966 - atan(1.0 / x);
    else {
        double x2 = x * x, t = x, s = x;
        for (int i = 1; i < 64; i++) { t *= -x2; s += t / (double)(2 * i + 1); }
        r = s;
    }
    return neg ? -r : r;
}
double atan2(double y, double x) {
    if (x > 0.0) return atan(y / x);
    if (x < 0.0) return y >= 0.0 ? atan(y / x) + 3.141592653589793
                                 : atan(y / x) - 3.141592653589793;
    if (y > 0.0) return 1.5707963267948966;
    if (y < 0.0) return -1.5707963267948966;
    return 0.0;
}
double asin(double x) {
    if (x > 1.0 || x < -1.0) return NAN;
    if (x == 1.0) return 1.5707963267948966;
    if (x == -1.0) return -1.5707963267948966;
    return atan2(x, sqrt(1.0 - x * x));
}
double acos(double x) {
    if (x > 1.0 || x < -1.0) return NAN;
    return 1.5707963267948966 - asin(x);
}
double sinh(double x) { double e = exp(x); return (e - 1.0 / e) * 0.5; }
double cosh(double x) { double e = exp(x); return (e + 1.0 / e) * 0.5; }
double tanh(double x) {
    if (x > 20.0) return 1.0;
    if (x < -20.0) return -1.0;
    double e = exp(2.0 * x);
    return (e - 1.0) / (e + 1.0);
}
double asinh(double x) { return log(x + sqrt(x * x + 1.0)); }
double acosh(double x) { return x < 1.0 ? NAN : log(x + sqrt(x * x - 1.0)); }
double atanh(double x) {
    if (x >= 1.0 || x <= -1.0) return x == 1.0 ? INFINITY : (x == -1.0 ? -INFINITY : NAN);
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

typedef struct {
    char *buf;
    size_t cap;
    size_t pos;
} fmtbuf_t;

static void fb_putc(fmtbuf_t *f, char c) {
    if (f->pos + 1 < f->cap) f->buf[f->pos] = c;
    f->pos++;
}
static void fb_puts(fmtbuf_t *f, const char *s, int len) {
    for (int i = 0; i < len; i++) fb_putc(f, s[i]);
}
static void fb_pad(fmtbuf_t *f, char c, int n) { while (n-- > 0) fb_putc(f, c); }

static void fb_num(fmtbuf_t *f, unsigned long long v, int base, int upper, int width, int zero, int left) {
    char tmp[32];
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = dig[v % (unsigned)base]; v /= (unsigned)base; }
    int pad = width - i;
    if (!left && pad > 0 && zero) fb_pad(f, '0', pad);
    else if (!left && pad > 0) fb_pad(f, ' ', pad);
    while (i > 0) fb_putc(f, tmp[--i]);
    if (left && pad > 0) fb_pad(f, ' ', pad);
}
static void fb_signed(fmtbuf_t *f, long long v, int base, int width, int zero, int left, int plus, int space) {
    int neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)(-(v + 1)) + 1ULL : (unsigned long long)v;
    if (neg || plus || space) {
        int pad = width - 1;
        if (!left && pad > 0 && zero) { fb_putc(f, neg ? '-' : (plus ? '+' : ' ')); fb_pad(f, '0', pad); }
        else if (!left && pad > 0) { fb_pad(f, ' ', pad); fb_putc(f, neg ? '-' : (plus ? '+' : ' ')); }
        else fb_putc(f, neg ? '-' : (plus ? '+' : ' '));
        fb_num(f, u, base, 0, 0, 0, 0);
        return;
    }
    fb_num(f, u, base, 0, width, zero, left);
}
static void fb_float(fmtbuf_t *f, double v, int prec, int width, int zero, int left, int plus, int space) {
    char tmp[64];
    int n = 0;
    int neg = 0;
    if (isnan(v)) { fb_num(f, 0, 10, 0, width, 0, left); return; }
    if (v < 0) { neg = 1; v = -v; }
    if (prec < 0) prec = 6;
    double scale = 1.0;
    for (int i = 0; i < prec; i++) scale *= 10.0;
    double rv = floor(v * scale + 0.5);
    unsigned long long ip = (unsigned long long)(rv / scale);
    unsigned long long fp = (unsigned long long)(rv - (double)ip * scale);
    char ibuf[24]; int ii = 0;
    if (ip == 0) ibuf[ii++] = '0';
    while (ip) { ibuf[ii++] = (char)('0' + ip % 10); ip /= 10; }
    for (int i = 0; i < ii; i++) tmp[n++] = ibuf[ii - 1 - i];
    if (prec > 0) {
        tmp[n++] = '.';
        char fbuf[24];
        for (int i = 0; i < prec; i++) { fbuf[i] = (char)('0' + fp % 10); fp /= 10; }
        for (int i = prec - 1; i >= 0; i--) tmp[n++] = fbuf[i];
    }
    int signlen = (neg || plus || space) ? 1 : 0;
    int pad = width - n - signlen;
    if (!left && pad > 0 && zero && signlen) { fb_putc(f, neg ? '-' : (plus ? '+' : ' ')); fb_pad(f, '0', pad); }
    else if (!left && pad > 0 && zero) fb_pad(f, '0', pad);
    else if (!left && pad > 0) fb_pad(f, ' ', pad);
    if (signlen && !(zero && !left)) fb_putc(f, neg ? '-' : (plus ? '+' : ' '));
    fb_puts(f, tmp, n);
    if (left && pad > 0) fb_pad(f, ' ', pad);
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    fmtbuf_t f;
    f.buf = buf; f.cap = n; f.pos = 0;
    while (*fmt) {
        if (*fmt != '%') { fb_putc(&f, *fmt++); continue; }
        fmt++;
        if (*fmt == '%') { fb_putc(&f, '%'); fmt++; continue; }
        int left = 0, zero = 0, plus = 0, space = 0;
        for (;;) {
            if (*fmt == '-') { left = 1; fmt++; }
            else if (*fmt == '0') { zero = 1; fmt++; }
            else if (*fmt == '+') { plus = 1; fmt++; }
            else if (*fmt == ' ') { space = 1; fmt++; }
            else break;
        }
        int width = 0;
        if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else { prec = 0; while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; } }
        }
        int lng = 0;
        if (*fmt == 'l') { lng = 1; fmt++; if (*fmt == 'l') { lng = 2; fmt++; } }
        else if (*fmt == 'h') { fmt++; if (*fmt == 'h') fmt++; }
        else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't') { lng = 1; fmt++; }
        char spec = *fmt++;
        switch (spec) {
            case 'd': case 'i': {
                long long v;
                if (lng == 2) v = va_arg(ap, long long);
                else if (lng == 1) v = va_arg(ap, long);
                else v = va_arg(ap, int);
                fb_signed(&f, v, 10, width, zero, left, plus, space);
                break;
            }
            case 'u': case 'x': case 'X': case 'o': {
                unsigned long long v;
                if (lng == 2) v = va_arg(ap, unsigned long long);
                else if (lng == 1) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                int base = (spec == 'o') ? 8 : ((spec == 'u') ? 10 : 16);
                fb_num(&f, v, base, spec == 'X', width, zero, left);
                break;
            }
            case 'p':
                fb_puts(&f, "0x", 2);
                fb_num(&f, (unsigned long long)(uintptr_t)va_arg(ap, void *), 16, 0, width, 0, left);
                break;
            case 'c': {
                char c = (char)va_arg(ap, int);
                fb_putc(&f, c);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                int len = 0;
                while (s[len] && (prec < 0 || len < prec)) len++;
                int pad = width - len;
                if (!left && pad > 0) fb_pad(&f, ' ', pad);
                fb_puts(&f, s, len);
                if (left && pad > 0) fb_pad(&f, ' ', pad);
                break;
            }
            case 'f': case 'F': {
                double v = va_arg(ap, double);
                fb_float(&f, v, prec, width, zero, left, plus, space);
                break;
            }
            case 'e': case 'E': case 'g': case 'G': {
                double v = va_arg(ap, double);
                fb_float(&f, v, prec < 0 ? 6 : prec, width, zero, left, plus, space);
                break;
            }
            default:
                fb_putc(&f, '%');
                fb_putc(&f, spec);
                break;
        }
    }
    if (n > 0) {
        size_t end = f.pos < n ? f.pos : n - 1;
        buf[end] = '\0';
    }
    return (int)f.pos;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    (void)stream; (void)fmt;
    return 0;
}

static long long days_from_civil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097LL + (long long)doe - 719468LL;
}
static void civil_from_days(long long z, long long *yy, unsigned *mm, unsigned *dd) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    long long y = (long long)yoe + era * 400;
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    unsigned mp = (5u * doy + 2u) / 153u;
    unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    unsigned m = mp + (mp < 10u ? 3u : (unsigned)-9);
    *yy = y + (m <= 2);
    *mm = m;
    *dd = d;
}

static int current_epoch(long long *sec, long *usec) {
    *sec = 0; *usec = 0;
    if (!g_asi || !g_asi->time) return 0;
    if (g_asi->time->now) {
        asi_datetime_t dt;
        if (g_asi->time->now(&dt)) {
            *sec = days_from_civil(dt.year, (unsigned)dt.month, (unsigned)dt.day) * 86400LL
                 + (long long)dt.hour * 3600 + (long long)dt.minute * 60 + dt.second;
        }
    } else if (g_asi->time->ticks_ms) {
        *sec = (long long)(g_asi->time->ticks_ms() / 1000u);
    }
    if (g_asi->time->ticks_ms) *usec = (long)((g_asi->time->ticks_ms() % 1000u) * 1000u);
    return 1;
}

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    long long s; long us;
    current_epoch(&s, &us);
    if (tv) { tv->tv_sec = (time_t)s; tv->tv_usec = (suseconds_t)us; }
    return 0;
}
int clock_gettime(clockid_t id, struct timespec *ts) {
    (void)id;
    long long s; long us;
    current_epoch(&s, &us);
    if (ts) { ts->tv_sec = (time_t)s; ts->tv_nsec = (long)us * 1000L; }
    return 0;
}
struct tm *localtime_r(const time_t *t, struct tm *out) {
    long long sec = t ? (long long)*t : 0;
    long long z = sec / 86400;
    long long rem = sec % 86400;
    if (rem < 0) { rem += 86400; z -= 1; }
    long long y; unsigned m, d;
    civil_from_days(z, &y, &m, &d);
    out->tm_sec = (int)(rem % 60);
    out->tm_min = (int)((rem / 60) % 60);
    out->tm_hour = (int)(rem / 3600);
    out->tm_mday = (int)d;
    out->tm_mon = (int)m - 1;
    out->tm_year = (int)y - 1900;
    out->tm_wday = (int)((z % 7 + 11) % 7);
    out->tm_yday = 0;
    out->tm_isdst = 0;
    out->tm_gmtoff = 0;
    out->tm_zone = "UTC";
    return out;
}

int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)c; (void)a; return 0; }
int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_signal(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { (void)c; (void)m; return 0; }
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *ts) {
    (void)c; (void)m; (void)ts;
    return 110;
}
