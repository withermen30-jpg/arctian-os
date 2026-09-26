
#include "kernel.h"
#include <stdarg.h>

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }

    return dest;
}

void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }

    return 0;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* itoa(int value, char* str, int base) {
    char* rc;
    char* ptr;
    char* low;

    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }

    rc = ptr = str;

    if (value < 0 && base == 10) {
        *ptr++ = '-';
        value = -value;
    }

    low = ptr;

    do {
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
        value /= base;
    } while (value);

    *ptr-- = '\0';

    while (low < ptr) {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }

    return rc;
}

void kernel_print_banner(void) {
    terminal_writestring("\n");
    terminal_writestring("  +------------------------------------------+\n");
    terminal_writestring("  |              A R C T I A N               |\n");
    terminal_writestring("  |        64-bit AMD64 Isletim Sistemi      |\n");
    terminal_writestring("  +------------------------------------------+\n");
    terminal_writestring("\n");
}

void kernel_print_info(void) {
    terminal_writestring("Kernel Bilgileri:\n");
    terminal_writestring("-----------------\n");

    terminal_writestring("Isim: ");
    terminal_writestring(ARCTIAN_NAME);
    terminal_writestring("\n");

    terminal_writestring("Versiyon: ");
    terminal_writestring(KERNEL_VERSION_STRING);
    terminal_writestring("\n");

    terminal_writestring("Derleme: ");
    terminal_writestring(KERNEL_BUILD_DATE);
    terminal_writestring(" ");
    terminal_writestring(KERNEL_BUILD_TIME);
    terminal_writestring("\n");

    terminal_writestring("Uretici: ");
    terminal_writestring(ARCTIAN_VENDOR);
    terminal_writestring("\n");

    terminal_writestring("Aciklama: ");
    terminal_writestring(ARCTIAN_DESCRIPTION);
    terminal_writestring("\n\n");
}

void kernel_print_features(void) {
    terminal_writestring("Ozellikler:\n");
    terminal_writestring("-----------\n");
    terminal_writestring("- AMD64 (x86_64) mimarisi\n");
    terminal_writestring("- Yerli bootloader (GRUB bagimliligi yok)\n");
    terminal_writestring("- VGA text modu destegi\n");
    terminal_writestring("- Arctian temasi\n");
    terminal_writestring("- TR-Q klavye destegi\n");
    terminal_writestring("- HDMI/VGA ekran destegi\n");
    terminal_writestring("- Windows 12 benzeri GUI\n");
    terminal_writestring("- Buzlu cam efekti\n");
    terminal_writestring("- Taskbar ve masaustu\n");
    terminal_writestring("- Kapatma/yeniden baslatma\n");
    terminal_writestring("\n");
}

void kernel_panic(const char* message) {
    terminal_setcolor(terminal_makecolor(COLOR_WHITE, COLOR_RED));
    terminal_writestring("\n\nKERNEL PANIC: ");
    terminal_writestring(message);
    terminal_writestring("\nSistem durduruluyor...\n");

    asm volatile ("cli");
    while (1) {
        asm volatile ("hlt");
    }
}

void kernel_warning(const char* message) {
    terminal_setcolor(terminal_makecolor(COLOR_YELLOW, ARCTIAN_BG));
    terminal_writestring("UYARI: ");
    terminal_writestring(message);
    terminal_writestring("\n");
    terminal_setcolor(terminal_makecolor(ARCTIAN_FG, ARCTIAN_BG));
}

static void append_char(char *buf, size_t size, size_t *pos, char c) {
    if (*pos + 1 < size)
        buf[*pos] = c;
    (*pos)++;
}

static void append_string(char *buf, size_t size, size_t *pos, const char *s) {
    if (!s)
        s = "(null)";
    while (*s)
        append_char(buf, size, pos, *s++);
}

static void append_unsigned(char *buf, size_t size, size_t *pos, unsigned long long value, int base, int width, bool upper) {
    char tmp[32];
    int i = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    do {
        tmp[i++] = digits[value % (unsigned)base];
        value /= (unsigned)base;
    } while (value && i < (int)sizeof(tmp));

    while (i < width)
        tmp[i++] = '0';

    while (i > 0)
        append_char(buf, size, pos, tmp[--i]);
}

static void format_string(char *buf, size_t size, const char *fmt, va_list args) {
    size_t pos = 0;

    while (*fmt) {
        if (*fmt != '%') {
            append_char(buf, size, &pos, *fmt++);
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            append_char(buf, size, &pos, *fmt++);
            continue;
        }

        int width = 0;
        if (*fmt == '0') {
            fmt++;
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
        }

        bool long_long = false;
        if (*fmt == 'l' && *(fmt + 1) == 'l') {
            long_long = true;
            fmt += 2;
        }

        char spec = *fmt++;
        switch (spec) {
            case 'd': {
                long long v = long_long ? va_arg(args, long long) : va_arg(args, int);
                if (v < 0) {
                    append_char(buf, size, &pos, '-');
                    v = -v;
                }
                append_unsigned(buf, size, &pos, (unsigned long long)v, 10, width, false);
                break;
            }
            case 'u': {
                unsigned long long v = long_long ? va_arg(args, unsigned long long) : va_arg(args, unsigned int);
                append_unsigned(buf, size, &pos, v, 10, width, false);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long long v = long_long ? va_arg(args, unsigned long long) : va_arg(args, unsigned int);
                append_unsigned(buf, size, &pos, v, 16, width, spec == 'X');
                break;
            }
            case 's':
                append_string(buf, size, &pos, va_arg(args, const char *));
                break;
            case 'c':
                append_char(buf, size, &pos, (char)va_arg(args, int));
                break;
            default:
                append_char(buf, size, &pos, '%');
                append_char(buf, size, &pos, spec);
                break;
        }
    }

    if (size > 0) {
        if (pos >= size)
            pos = size - 1;
        buf[pos] = '\0';
    }
}

void kernel_debug(const char* message, ...) {
    char formatted[256];
    va_list args;
    va_start(args, message);
    format_string(formatted, sizeof(formatted), message, args);
    va_end(args);

    terminal_setcolor(terminal_makecolor(COLOR_CYAN, ARCTIAN_BG));
    terminal_writestring("DEBUG: ");
    terminal_writestring(formatted);
    terminal_writestring("\n");
    terminal_setcolor(terminal_makecolor(ARCTIAN_FG, ARCTIAN_BG));
}
