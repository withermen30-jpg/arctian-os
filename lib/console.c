#include "kernel.h"
#include <stdint.h>
#include <stddef.h>

char system_username[32] = "Kullanici";
char system_password[32] = "1234";

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t *terminal_buffer;

uint8_t terminal_makecolor(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)(fg | (bg << 4));
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

void terminal_initialize(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = terminal_makecolor(ARCTIAN_FG, ARCTIAN_BG);
    terminal_buffer = (uint16_t *)VGA_BUFFER;
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            terminal_buffer[y * VGA_WIDTH + x] = make_vgaentry(' ', terminal_color);
        }
    }
}

void terminal_putchar(char c) {
    if (c == '\n') {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) terminal_row = 0;
        return;
    }
    terminal_buffer[terminal_row * VGA_WIDTH + terminal_column] =
        make_vgaentry(c, terminal_color);
    if (++terminal_column == VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) terminal_row = 0;
    }
}

void terminal_writestring(const char *data) {
    for (size_t i = 0; data[i] != '\0'; i++) terminal_putchar(data[i]);
}

void show_arctian_header(void) {
    kernel_print_banner();
    terminal_color = terminal_makecolor(ARCTIAN_ACCENT, ARCTIAN_BG);
    terminal_writestring("================================================\n");
    terminal_color = terminal_makecolor(ARCTIAN_FG, ARCTIAN_BG);
    terminal_writestring("            ARCTIAN KERNEL\n");
    terminal_writestring("      64-bit Yerli Isletim Sistemi\n");
    terminal_color = terminal_makecolor(ARCTIAN_ACCENT, ARCTIAN_BG);
    terminal_writestring("================================================\n\n");
    terminal_color = terminal_makecolor(ARCTIAN_FG, ARCTIAN_BG);
}

void show_system_info(void) {
    terminal_writestring("Sistem Bilgileri:\n");
    terminal_writestring("-----------------\n");
    terminal_writestring("Mimari: AMD64 (x86_64)\n");
    terminal_writestring("Kernel: Arctian v0.1.0\n");
    terminal_writestring("Maskot: Beyaz Kurt\n\n");
}
