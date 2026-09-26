#include "serial.h"
#include "kernel.h"
#include <stdarg.h>

void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static int tx_empty(void) { return inb(0x3F8 + 5) & 0x20; }

void serial_putc(char c) {
    while (!tx_empty()) { }
    outb(0x3F8, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}

static void sput_uint(unsigned int v, int base, int upper, int width) {
    char buf[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (v == 0) buf[i++] = '0';
    while (v) { buf[i++] = digits[v % (unsigned)base]; v /= (unsigned)base; }
    while (i < width) buf[i++] = '0';
    while (i--) serial_putc(buf[i]);
}

void serial_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { serial_putc(*p); continue; }
        p++;
        int width = 0;
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        if (*p == 'l') { p++; if (*p == 'l') p++; }
        switch (*p) {
            case 's': { const char *s = va_arg(ap, const char *); serial_puts(s ? s : "(null)"); break; }
            case 'c': serial_putc((char)va_arg(ap, int)); break;
            case 'd': { int v = va_arg(ap, int); if (v < 0) { serial_putc('-'); v = -v; } sput_uint((unsigned)v, 10, 0, width); break; }
            case 'u': sput_uint(va_arg(ap, unsigned), 10, 0, width); break;
            case 'x': sput_uint(va_arg(ap, unsigned), 16, 0, width); break;
            case 'X': sput_uint(va_arg(ap, unsigned), 16, 1, width); break;
            case '%': serial_putc('%'); break;
            default: serial_putc('%'); if (*p) serial_putc(*p); break;
        }
    }
    va_end(ap);
}
