/**
 * Arctian System Services
 * Kapatma, yeniden başlatma ve sistem servisleri
 */

#include "kernel.h"
#include "acpi.h"
#include "keyboard.h"
#include "mouse.h"

// ACPI portları
#define ACPI_SHUTDOWN_PORT 0x604
#define ACPI_REBOOT_PORT 0x64

// Sistem durumu
typedef enum {
    SYSTEM_RUNNING,
    SYSTEM_SHUTTING_DOWN,
    SYSTEM_REBOOTING,
    SYSTEM_SLEEPING,
    SYSTEM_HIBERNATING
} system_state_t;

static system_state_t current_state = SYSTEM_RUNNING;

// Sistem kapatma (gercek)
void system_shutdown(void) {
    current_state = SYSTEM_SHUTTING_DOWN;

    // ACPI varsa FADT uzerinden gercek S5; yoksa QEMU/Bochs portlari.
    acpi_shutdown();

    // ACPI calismazsa
    for (;;) {
        asm volatile ("cli");
        asm volatile ("hlt");
    }
}

// Sistem yeniden baslatma (gercek)
void system_reboot(void) {
    current_state = SYSTEM_REBOOTING;

    // Klavye denetleyicisi uzerinden reset
    for (int i = 0; i < 1000; i++) {
        if (!(inb(0x64) & 0x02)) {
            outb(0x64, 0xFE);
            break;
        }
    }

    // Triple fault (fallback): sifir IDT yukleyip kesme tetikle
    struct __attribute__((packed)) { uint16_t limit; uint64_t base; } idtr = { 0, 0 };
    asm volatile ("lidt %0" :: "m"(idtr));
    asm volatile ("int3");

    for (;;) {
        asm volatile ("cli");
        asm volatile ("hlt");
    }
}

// Sistem uyku modu: ekran masaustu tarafindan karartilir; burada CPU
// `hlt` ile bekletilir ve herhangi bir tus/fare hareketi ile uyanilir.
void system_sleep(void) {
    current_state = SYSTEM_SLEEPING;

    keyboard_event_t ev;
    while (keyboard_pop_event(&ev)) { /* uyanma oncesi kuyrugu bosalt */ }

    mouse_state_t *m = mouse_get_state();
    int lx = m ? m->x : 0, ly = m ? m->y : 0;
    bool lb = m ? m->buttons[MOUSE_BUTTON_LEFT] : false;
    bool rb = m ? m->buttons[MOUSE_BUTTON_RIGHT] : false;

    for (;;) {
        asm volatile ("sti; hlt");
        if (keyboard_pop_event(&ev)) break;
        m = mouse_get_state();
        if (m && (m->x != lx || m->y != ly ||
                  m->buttons[MOUSE_BUTTON_LEFT] != lb ||
                  m->buttons[MOUSE_BUTTON_RIGHT] != rb)) break;
    }

    current_state = SYSTEM_RUNNING;
}

// Sistem durumunu al
system_state_t system_get_state(void) {
    return current_state;
}

// Sistem bilgilerini göster
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

// Sistem servislerini başlat
void system_services_init(void) {
    kernel_debug("Sistem servisleri baslatiliyor...");

    // Servis başlatma mesajları
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