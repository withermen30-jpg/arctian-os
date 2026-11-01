/**
 * Arctian mini printf (VGA konsoluna yazar)
 * %s %c %d %u %x %% destekler; l/ll/z uzunluk goreleri taninir.
 */
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

static void put_ulong(unsigned long v, int base, int upper) {
    char buf[24];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (v == 0) { terminal_putchar('0'); return; }
    while (v && i < 24) { buf[i++] = digits[v % (unsigned long)base]; v /= (unsigned long)base; }
    while (i--) terminal_putchar(buf[i]);
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { terminal_putchar(*p); continue; }
        p++;
        if (!*p) { terminal_putchar('%'); break; }   /* '%' ile biten format */
        /* l / ll / z uzunluk gorevleri: okunmadan gecilirse sonraki
         * argumanlar kayiyordu (%ld basilan bir satirda bute sunlar). */
        int lng = 0;
        while (*p == 'l' || *p == 'z') { if (*p == 'l') lng++; p++; }
        switch (*p) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                terminal_writestring(s ? s : "(null)");
                break;
            }
            case 'c': terminal_putchar((char)va_arg(ap, int)); break;
            case 'd': {
                if (lng) {
                    long v = va_arg(ap, long);
                    if (v < 0) { terminal_putchar('-'); v = -v; }
                    put_ulong((unsigned long)v, 10, 0);
                } else {
                    int v = va_arg(ap, int);
                    if (v < 0) { terminal_putchar('-'); v = -v; }
                    put_uint((unsigned)v, 10, 0);
                }
                break;
            }
            case 'u':
                if (lng) put_ulong(va_arg(ap, unsigned long), 10, 0);
                else     put_uint(va_arg(ap, unsigned), 10, 0);
                break;
            case 'x':
                if (lng) put_ulong(va_arg(ap, unsigned long), 16, 0);
                else     put_uint(va_arg(ap, unsigned), 16, 0);
                break;
            case 'X':
                if (lng) put_ulong(va_arg(ap, unsigned long), 16, 1);
                else     put_uint(va_arg(ap, unsigned), 16, 1);
                break;
            case '%': terminal_putchar('%'); break;
            default: terminal_putchar('%'); terminal_putchar(*p); break;
        }
    }
    va_end(ap);
}
