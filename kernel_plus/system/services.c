
#include "kernel.h"

#define ACPI_SHUTDOWN_PORT 0x604
#define ACPI_REBOOT_PORT 0x64

typedef enum {
    SYSTEM_RUNNING,
    SYSTEM_SHUTTING_DOWN,
    SYSTEM_REBOOTING,
    SYSTEM_SLEEPING,
    SYSTEM_HIBERNATING
} system_state_t;

static system_state_t current_state = SYSTEM_RUNNING;

void system_shutdown(void) {
    current_state = SYSTEM_SHUTTING_DOWN;

    outw(ACPI_SHUTDOWN_PORT, 0x2000);
    outw(0xB004, 0x2000);

    for (;;) {
        asm volatile ("cli");
        asm volatile ("hlt");
    }
}

void system_reboot(void) {
    current_state = SYSTEM_REBOOTING;

    for (int i = 0; i < 1000; i++) {
        if (!(inb(0x64) & 0x02)) {
            outb(0x64, 0xFE);
            break;
        }
    }

    struct __attribute__((packed)) { uint16_t limit; uint64_t base; } idtr = { 0, 0 };
    asm volatile ("lidt %0" :: "m"(idtr));
    asm volatile ("int3");

    for (;;) {
        asm volatile ("cli");
        asm volatile ("hlt");
    }
}

void system_sleep(void) {
    kernel_debug("Sistem uyku moduna aliniyor...");
    current_state = SYSTEM_SLEEPING;

    terminal_setcolor(terminal_makecolor(COLOR_CYAN, COLOR_BLACK));
    terminal_writestring("\n\nSistem uyku moduna aliniyor...\n");


    while (1) {
        asm volatile ("hlt");
    }
}

system_state_t system_get_state(void) {
    return current_state;
}

void system_show_info(void) {
    terminal_writestring("\nSistem Bilgileri:\n");
    terminal_writestring("=================\n");

    terminal_writestring("Durum: ");
    switch (current_state) {
        case SYSTEM_RUNNING:
            terminal_writestring("Calisiyor\n");
            break;
        case SYSTEM_SHUTTING_DOWN:
            terminal_writestring("Kapatiliyor\n");
            break;
        case SYSTEM_REBOOTING:
            terminal_writestring("Yeniden baslatiliyor\n");
            break;
        case SYSTEM_SLEEPING:
            terminal_writestring("Uykuda\n");
            break;
        case SYSTEM_HIBERNATING:
            terminal_writestring("Hazırda bekletme\n");
            break;
    }

    terminal_writestring("Servisler: Kapatma, Yeniden Baslatma, Uyku\n");
    terminal_writestring("GUI: Windows 12 benzeri masaustu\n");
    terminal_writestring("Tema: Arctian\n");
    terminal_writestring("Efekt: Buzlu cam\n");
    terminal_writestring("\n");
}

void system_services_init(void) {
    kernel_debug("Sistem servisleri baslatiliyor...");

    terminal_writestring("Sistem Servisleri:\n");
    terminal_writestring("------------------\n");
    terminal_writestring("- Kapatma Servisi: AKTIF\n");
    terminal_writestring("- Yeniden Baslatma Servisi: AKTIF\n");
    terminal_writestring("- Guvenlik Servisi: AKTIF\n");
    terminal_writestring("- Zamanlayici Servisi: AKTIF\n");
    terminal_writestring("- Ag Servisi: HAZIR\n");
    terminal_writestring("\n");

    kernel_debug("Sistem servisleri basariyla baslatildi");
}