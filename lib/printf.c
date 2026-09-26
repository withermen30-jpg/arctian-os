#include "printf.h"
#include "kernel.h"
#include <stdarg.h>

static void put_uint(unsigned int v, int base, int upper) {
    char buf[16];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (v == 0) { terminal_putchar('0'); return; }
    while (v && i < 16) { buf[i++] = digits[v % (unsigned)base]; v /= (unsigned)base; }
    while (i--) terminal_putchar(buf[i]);
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { terminal_putchar(*p); continue; }
        p++;
        switch (*p) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                terminal_writestring(s ? s : "(null)");
                break;
            }
            case 'c': terminal_putchar((char)va_arg(ap, int)); break;
            case 'd': {
                int v = va_arg(ap, int);
                if (v < 0) { terminal_putchar('-'); v = -v; }
                put_uint((unsigned)v, 10, 0);
                break;
            }
            case 'u': put_uint(va_arg(ap, unsigned), 10, 0); break;
            case 'x': put_uint(va_arg(ap, unsigned), 16, 0); break;
            case 'X': put_uint(va_arg(ap, unsigned), 16, 1); break;
            case '%': terminal_putchar('%'); break;
            default: terminal_putchar('%'); terminal_putchar(*p); break;
        }
    }
    va_end(ap);
}
