/**
 * Arctian Kernel Header
 * Arctian
 * 64-bit AMD64 mimarisi
 */

#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Surum — TEK KAYNAK: version.json (Makefile bunu -DARCTIAN_VERSION_STR olarak tum
// cevrime verir; burada sadece onun alias'i turetilir, kati deger YAZILMAZ).
#include "version.h"
#define KERNEL_VERSION_STRING ARCTIAN_VERSION

// Kernel build bilgisi
#define KERNEL_BUILD_DATE __DATE__
#define KERNEL_BUILD_TIME __TIME__

// Kernel özellikleri
#define ARCTIAN_NAME "Arctian"
#define ARCTIAN_FULL_NAME "Arctian"
#define ARCTIAN_VENDOR "Arctian"
#define ARCTIAN_DESCRIPTION "Yerli Isletim Sistemi"

// Sistem yapılandırması (tek kaynak)
#define SYSTEM_RAM_MB 512

// Kullanıcı bilgileri (kurulumda doldurulur, settings/fs tarafından okunur)
extern char system_username[32];
extern char system_password[32];

// Donanım sabitleri
#define VGA_BUFFER 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

// Renk tanımlamaları
typedef enum {
    COLOR_BLACK = 0,
    COLOR_BLUE = 1,
    COLOR_GREEN = 2,
    COLOR_CYAN = 3,
    COLOR_RED = 4,
    COLOR_MAGENTA = 5,
    COLOR_BROWN = 6,
    COLOR_LIGHT_GREY = 7,
    COLOR_DARK_GREY = 8,
    COLOR_LIGHT_BLUE = 9,
    COLOR_LIGHT_GREEN = 10,
    COLOR_LIGHT_CYAN = 11,
    COLOR_LIGHT_RED = 12,
    COLOR_LIGHT_MAGENTA = 13,
    COLOR_YELLOW = 14,
    COLOR_WHITE = 15
} vga_color_t;

// Arctian teması renkleri
#define ARCTIAN_BG COLOR_BROWN
#define ARCTIAN_FG COLOR_YELLOW
#define ARCTIAN_ACCENT COLOR_LIGHT_RED
#define ARCTIAN_HIGHLIGHT COLOR_WHITE

// Terminal fonksiyonları
void terminal_initialize(void);
void terminal_putchar(char c);
void terminal_writestring(const char* data);
void terminal_setcolor(uint8_t color);
uint8_t terminal_makecolor(vga_color_t fg, vga_color_t bg);

// Kernel ana fonksiyonu
void kernel_main(void);

// Konsol ekranlari (lib/console.c)
void show_arctian_header(void);
void show_system_info(void);

// Sistem bilgisi fonksiyonları
void kernel_print_banner(void);
void kernel_print_info(void);
void kernel_print_features(void);

// Hata işleme
void kernel_panic(const char* message);
void kernel_warning(const char* message);
void kernel_debug(const char* message, ...);

// Yardımcı fonksiyonlar
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
size_t strlen(const char* s);
char* strcpy(char* dest, const char* src);
char* strcat(char* dest, const char* src);
int strcmp(const char* s1, const char* s2);
char* itoa(int value, char* str, int base);

// VGA yardımcı fonksiyonu
static inline uint16_t make_vgaentry(char c, uint8_t color) {
    uint16_t c16 = (uint16_t)(unsigned char)c;
    uint16_t color16 = color;
    return c16 | color16 << 8;
}

// Port IO
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif // KERNEL_H
