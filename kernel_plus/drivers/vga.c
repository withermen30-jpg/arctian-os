
#include "kernel.h"

extern size_t terminal_row;
extern size_t terminal_column;
extern uint8_t terminal_color;
extern uint16_t* terminal_buffer;


#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5

#define VGA_CURSOR_HIGH 0x0E
#define VGA_CURSOR_LOW 0x0F

void vga_update_cursor(void) {
    uint16_t pos = terminal_row * VGA_WIDTH + terminal_column;

    outb(VGA_CTRL_REGISTER, VGA_CURSOR_HIGH);
    outb(VGA_DATA_REGISTER, (pos >> 8) & 0xFF);
    outb(VGA_CTRL_REGISTER, VGA_CURSOR_LOW);
    outb(VGA_DATA_REGISTER, pos & 0xFF);
}

void vga_disable_cursor(void) {
    outb(VGA_CTRL_REGISTER, 0x0A);
    outb(VGA_DATA_REGISTER, 0x20);
}

void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    outb(VGA_CTRL_REGISTER, 0x0A);
    outb(VGA_DATA_REGISTER, (inb(VGA_DATA_REGISTER) & 0xC0) | cursor_start);

    outb(VGA_CTRL_REGISTER, 0x0B);
    outb(VGA_DATA_REGISTER, (inb(VGA_DATA_REGISTER) & 0xE0) | cursor_end);
}

void vga_scroll(void) {
    if (terminal_row >= VGA_HEIGHT) {
        for (size_t y = 1; y < VGA_HEIGHT; y++) {
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                const size_t dest_index = (y - 1) * VGA_WIDTH + x;
                const size_t src_index = y * VGA_WIDTH + x;
                terminal_buffer[dest_index] = terminal_buffer[src_index];
            }
        }

        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
            terminal_buffer[index] = make_vgaentry(' ', terminal_color);
        }

        terminal_row = VGA_HEIGHT - 1;
    }
}

void vga_init(void) {
    kernel_debug("VGA driver baslatiliyor...");

    vga_enable_cursor(14, 15);

    vga_update_cursor();

    kernel_debug("VGA driver basariyla baslatildi");
}

void vga_test(void) {
    terminal_writestring("VGA Testi:\n");
    terminal_writestring("----------\n");

    uint8_t original_color = terminal_color;

    terminal_setcolor(terminal_makecolor(COLOR_RED, COLOR_BLACK));
    terminal_writestring("Kirmizi yazi testi\n");

    terminal_setcolor(terminal_makecolor(COLOR_GREEN, COLOR_BLACK));
    terminal_writestring("Yesil yazi testi\n");

    terminal_setcolor(terminal_makecolor(COLOR_BLUE, COLOR_BLACK));
    terminal_writestring("Mavi yazi testi\n");

    terminal_setcolor(terminal_makecolor(COLOR_YELLOW, COLOR_BROWN));
    terminal_writestring("Arctian teması testi\n");

    terminal_setcolor(original_color);
    terminal_writestring("\nVGA testi tamamlandi.\n\n");
}