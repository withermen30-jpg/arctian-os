/**
 * Arctian Keyboard Driver
 * TR-Q klavye destegi - 64-bit AMD64
 * Bagimsiz: <string.h> kullanilmadi
 * USB HID bridge: keyboard_usb_push_scancode()
 */

#include "kernel.h"
#include "isr.h"
#include "pic.h"
#include "keyboard.h"
#include "clock.h"
#include "mouse.h"

/* USB HID'den gelen scancode'ları PS/2 handler'a iletmek için */
#define USB_SCANCODE_QUEUE_SIZE 16
static uint8_t usb_scancode_queue[USB_SCANCODE_QUEUE_SIZE];
static int usb_scancode_queue_head = 0;
static int usb_scancode_queue_tail = 0;

// Klavye durumu (static globals — MUST be before event queue code)
static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool caps_lock = false;
static bool gui_pressed = false;
static bool altgr_pressed = false;  // right Alt (AltGr)
static bool extended_byte = false;  // 0xE0 prefix handler

// Terminal degiskenleri (main.c'de tanimli)
extern size_t terminal_row;
extern size_t terminal_column;
extern uint8_t terminal_color;
extern uint16_t* terminal_buffer;

// GUI baslat menusu (desktop.c'de tanimli)
/* Desktop ayri bir modul oldugundan bu kanca zayif birakilir. */
__attribute__((weak)) void gui_toggle_start_menu(void) { }

// Klavye portlari
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_COMMAND_PORT 0x64

// Klavye komutlari
#define KEYBOARD_CMD_ENABLE 0xAE
#define KEYBOARD_CMD_DISABLE 0xAD
#define KEYBOARD_CMD_READ_CONFIG 0x20
#define KEYBOARD_CMD_WRITE_CONFIG 0x60

// Controller hazir olana kadar bekle; hazirsa 1 doner
static int ps2_wait_buffer(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(KEYBOARD_STATUS_PORT) & 0x01) return 1;
    return 0;
}

// Bufferde bekleyen tum byte'lari okuyup at
static void ps2_flush(void) {
    for (int i = 0; i < 32; i++) {
        if (!(inb(KEYBOARD_STATUS_PORT) & 0x01)) break;
        inb(KEYBOARD_DATA_PORT);
    }
}

// Klavye durum bitleri
#define KEY_RELEASED 0x80

// Klavye bufferi
#define KEYBOARD_BUFFER_SIZE 128
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static size_t keyboard_buffer_pos = 0;

// Event queue for GUI main loop (circular buffer)
#define KEYBOARD_EVENT_QUEUE_SIZE 64
static keyboard_event_t keyboard_event_queue[KEYBOARD_EVENT_QUEUE_SIZE];
static int keyboard_event_head = 0;
static int keyboard_event_tail = 0;

static int s_hotkey_q[8];
static int s_hotkey_h = 0;
static int s_hotkey_t = 0;
static int s_hot_guard = 0;

void keyboard_push_hotkey(int code) {
    static uint64_t last_ms;
    uint64_t now = clock_ms();
    if (now - last_ms < 400) return;
    last_ms = now;
    int next = (s_hotkey_h + 1) % 8;
    if (next == s_hotkey_t) return;
    s_hotkey_q[s_hotkey_h] = code;
    s_hotkey_h = next;
}

int keyboard_pop_hotkey(void) {
    if (s_hotkey_t == s_hotkey_h) return 0;
    int c = s_hotkey_q[s_hotkey_t];
    s_hotkey_t = (s_hotkey_t + 1) % 8;
    return c;
}

/* Tiklama aninda Ctrl/Shift durumu: coklu secim (Ctrl+tik), aralik secim
 * (Shift+tik) gibi fare ile klavye kombinasyonlari icin. */
int keyboard_ctrl_down(void)  { return ctrl_pressed ? 1 : 0; }
int keyboard_shift_down(void) { return shift_pressed ? 1 : 0; }

static void keyboard_push_event(char ch, uint8_t scancode, bool pressed) {
    int next = (keyboard_event_head + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    if (next == keyboard_event_tail) return; /* queue full — drop */
    keyboard_event_t *ev = &keyboard_event_queue[keyboard_event_head];
    ev->ch = ch;
    ev->scancode = scancode;
    ev->pressed = pressed;
    ev->alt = alt_pressed;
    ev->ctrl = ctrl_pressed;
    ev->shift = shift_pressed;
    keyboard_event_head = next;
}

bool keyboard_pop_event(keyboard_event_t *ev) {
    if (keyboard_event_tail == keyboard_event_head) return false;
    *ev = keyboard_event_queue[keyboard_event_tail];
    keyboard_event_tail = (keyboard_event_tail + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    return true;
}

/* Kuyrukta bekleyen olay var mi? Eskiden bunu sabit 1 donduren bir stub
 * uyuyordu; olay olmayan yerde okuyan tarafta bos olay sayardi. */
bool keyboard_event_avail(void) {
    return keyboard_event_tail != keyboard_event_head;
}

// TR-Q klavye layout
/* Turkce-Q (Türkçe Q) duzeni - kbd "trq.map" temel alindi.
 * ONEMLI: Türkçeye ozgu harfler (ı ğ ü ş ö ç) simdilik ASCII karsiliklariyla
 * (i g u s o c) eslenir; boylece Latin-5/UTF-8 sorunu olmadan kullanilabilir. */
const char keyboard_map_normal[128] = {
    [0x01] = '\x1b',
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '*', [0x0D] = '-', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = 'g', [0x1B] = 'u', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = 's', [0x28] = 'i', [0x29] = ',',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ', [0x56] = '<'
};

const char keyboard_map_shift[128] = {
    [0x01] = '\x1b',
    [0x02] = '!', [0x03] = '\'', [0x04] = '^', [0x05] = '+', [0x06] = '%',
    [0x07] = '&', [0x08] = '/', [0x09] = '(', [0x0A] = ')', [0x0B] = '=',
    [0x0C] = '?', [0x0D] = '_', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = 'G', [0x1B] = 'U', [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = 'S', [0x28] = 'I', [0x29] = ';',
    [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M',
    [0x33] = ';', [0x34] = ':', [0x35] = '?',
    [0x39] = ' ', [0x56] = '>'
};

/* AltGr (sag Alt) katmani - TR-Q. Programlama sembolleri icin sart. */
const char keyboard_map_altgr[128] = {
    [0x02] = '>', [0x04] = '#', [0x05] = '$',
    [0x08] = '{', [0x09] = '[', [0x0A] = ']', [0x0B] = '}',
    [0x0C] = '\\', [0x0D] = '|',
    [0x10] = '@',
    [0x1B] = '~',
    [0x27] = '`', [0x29] = '<', [0x56] = '|'
};

static const char keyboard_map_f_normal[128] = {
    [0x01] = '\x1b',
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '/', [0x0D] = '-', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'f', [0x11] = 'g', [0x12] = 'g', [0x13] = 'i', [0x14] = 'o',
    [0x15] = 'd', [0x16] = 'r', [0x17] = 'n', [0x18] = 'h', [0x19] = 'p',
    [0x1A] = 'q', [0x1B] = 'w', [0x1C] = '\n',
    [0x1E] = 'u', [0x1F] = 'i', [0x20] = 'e', [0x21] = 'a', [0x22] = 'u',
    [0x23] = 't', [0x24] = 'k', [0x25] = 'm', [0x26] = 'l', [0x27] = 'y',
    [0x28] = 's', [0x29] = '+',
    [0x2B] = 'x', [0x2C] = 'j', [0x2D] = 'o', [0x2E] = 'v', [0x2F] = 'c',
    [0x30] = 'c', [0x31] = 'z', [0x32] = 's', [0x33] = 'b', [0x34] = '.',
    [0x35] = ',',
    [0x39] = ' ', [0x56] = '<'
};

static const char keyboard_map_f_shift[128] = {
    [0x01] = '\x1b',
    [0x02] = '!', [0x03] = '"', [0x04] = '^', [0x05] = '$', [0x06] = '%',
    [0x07] = '&', [0x08] = '\'', [0x09] = '(', [0x0A] = ')', [0x0B] = '=',
    [0x0C] = '?', [0x0D] = '_', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'F', [0x11] = 'G', [0x12] = 'G', [0x13] = 'I', [0x14] = 'O',
    [0x15] = 'D', [0x16] = 'R', [0x17] = 'N', [0x18] = 'H', [0x19] = 'P',
    [0x1A] = 'Q', [0x1B] = 'W', [0x1C] = '\n',
    [0x1E] = 'U', [0x1F] = 'I', [0x20] = 'E', [0x21] = 'A', [0x22] = 'U',
    [0x23] = 'T', [0x24] = 'K', [0x25] = 'M', [0x26] = 'L', [0x27] = 'Y',
    [0x28] = 'S', [0x29] = '*',
    [0x2B] = 'X', [0x2C] = 'J', [0x2D] = 'O', [0x2E] = 'V', [0x2F] = 'C',
    [0x30] = 'C', [0x31] = 'Z', [0x32] = 'S', [0x33] = 'B', [0x34] = ':',
    [0x35] = ';',
    [0x39] = ' ', [0x56] = '>'
};

static const char keyboard_map_f_altgr[128] = {
    [0x04] = '#', [0x08] = '{', [0x09] = '[', [0x0A] = ']', [0x0B] = '}',
    [0x0C] = '\\', [0x0D] = '|',
    [0x10] = '@',
    [0x1B] = '~',
    [0x2B] = '`'
};

static const char keyboard_map_en_normal[128] = {
    [0x01] = '\x1b',
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '=', [0x0D] = '-', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ', [0x56] = '<'
};

static const char keyboard_map_en_shift[128] = {
    [0x01] = '\x1b',
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '+', [0x0D] = '_', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}', [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x39] = ' ', [0x56] = '>'
};

static const char keyboard_map_en_altgr[128] = {0};

static const char *s_map_normal = keyboard_map_normal;
static const char *s_map_shift = keyboard_map_shift;
static const char *s_map_altgr = keyboard_map_altgr;
static int s_layout = 0;

void keyboard_set_layout(int id) {
    if (id == 1) {
        s_map_normal = keyboard_map_f_normal;
        s_map_shift = keyboard_map_f_shift;
        s_map_altgr = keyboard_map_f_altgr;
    } else if (id == 2) {
        s_map_normal = keyboard_map_en_normal;
        s_map_shift = keyboard_map_en_shift;
        s_map_altgr = keyboard_map_en_altgr;
    } else {
        s_map_normal = keyboard_map_normal;
        s_map_shift = keyboard_map_shift;
        s_map_altgr = keyboard_map_altgr;
    }
    s_layout = (id >= 0 && id <= 2) ? id : 0;
}

int keyboard_get_layout(void) { return s_layout; }

// USB HID'den scancode enjekte et
void keyboard_usb_push_scancode(uint8_t scancode) {
    int next = (usb_scancode_queue_head + 1) % USB_SCANCODE_QUEUE_SIZE;
    if (next != usb_scancode_queue_tail) {
        usb_scancode_queue[usb_scancode_queue_head] = scancode;
        usb_scancode_queue_head = next;
    }
}

// USB kuyruğundan scancode işle
void keyboard_process_usb_queue(void) {
    while (usb_scancode_queue_tail != usb_scancode_queue_head) {
        uint8_t scancode = usb_scancode_queue[usb_scancode_queue_tail];
        usb_scancode_queue_tail = (usb_scancode_queue_tail + 1) % USB_SCANCODE_QUEUE_SIZE;

        // Kuyruktan gelen scancode'u normal PS/2 akışına enjekte et
        // released flag eklemeden (PS/2 donanımı aksine USB HID anlık bildirir)
        if (scancode & KEY_RELEASED) {
            scancode &= ~KEY_RELEASED;
            switch (scancode) {
                case 0x2A: case 0x36: shift_pressed = false; break;
                case 0x1D: ctrl_pressed = false; break;
                case 0x38: alt_pressed = false; break;
            }
            continue;   /* eskiden return'du; kuyrukta kalan tusler dusuyordu */
        }

        switch (scancode) {
            case 0x2A: case 0x36: shift_pressed = true;  continue;
            case 0x1D: ctrl_pressed = true;  continue;
            case 0x38: alt_pressed = true;   continue;
            case 0x3A: caps_lock = !caps_lock; continue;
        }

        if (scancode < 128) {
            const char* map = altgr_pressed ? s_map_altgr
                                            : (shift_pressed ? s_map_shift : s_map_normal);
            char c = map[scancode];
            if (c) {
                if (caps_lock && c >= 'a' && c <= 'z') c -= 32;
                else if (caps_lock && c >= 'A' && c <= 'Z') c += 32;
                keyboard_push_event(c, scancode, true);
            }
        }
    }
}

// Klavye interrupt handler (PS/2 + USB kuyruğu)
void keyboard_interrupt_handler(void) {
    uint8_t status = inb(KEYBOARD_STATUS_PORT);

    // IRQ1 kuyruk onceligi yuzunden fare byte'i buraya dusmis olabilir:
    // AUX verisini fare isleyicisine yonlendir (kayip/gesiz paket olmasin)
    if (status & 0x20) {
        mouse_irq_handler();
        return;
    }
    if (!(status & 0x01)) return;

    // PS/2 donanımından oku
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    // USB kuyruğunu işle (her interrupt'ta düzenli boşalt)
    keyboard_process_usb_queue();

    // 0xE0 extended byte prefix
    if (scancode == 0xE0) {
        extended_byte = true;
        return;
    }

    // Key release kontrolu
    if (scancode & KEY_RELEASED) {
        scancode &= ~KEY_RELEASED;

        // Extended tus release (Win key)
        if (extended_byte) {
            extended_byte = false;
            if (scancode == 0x5B || scancode == 0x5C) { // Left/Right GUI
                gui_pressed = false;
            } else if (scancode == 0x38) { // Right Alt (AltGr)
                altgr_pressed = false;
            }
            return;
        }

        // Modifier tuslari kontrol et
        if (scancode == 0x0F || scancode == 0x14) s_hot_guard = 0;
        switch (scancode) {
            case 0x2A: // Left Shift
            case 0x36: // Right Shift
                shift_pressed = false;
                break;
            case 0x1D: // Ctrl
                ctrl_pressed = false;
                break;
            case 0x38: // Alt
                alt_pressed = false;
                break;
        }
        return;
    }

    // Extended tus (0xE0 prefix) isle
    if (extended_byte) {
        extended_byte = false;
        switch (scancode) {
            case 0x5B: // Left GUI (Win)
            case 0x5C: // Right GUI
                if (!gui_pressed) {
                    gui_pressed = true;
                    gui_toggle_start_menu();
                }
                return;
            case 0x38: // Right Alt (AltGr)
                altgr_pressed = true;
                return;
            default:
                return;
        }
    }

    // Modifier tuslari kontrol et
    switch (scancode) {
        case 0x2A: // Left Shift
        case 0x36: // Right Shift
            shift_pressed = true;
            return;
        case 0x1D: // Ctrl
            ctrl_pressed = true;
            return;
        case 0x38: // Alt
            alt_pressed = true;
            return;
        case 0x3A: // Caps Lock
            caps_lock = !caps_lock;
            return;
    }

    // Karakteri al
    if (scancode == 0x0F && alt_pressed && !s_hot_guard) {
        s_hot_guard = 1;
        keyboard_push_hotkey(1);
        return;
    }
    if (scancode == 0x14 && ctrl_pressed && alt_pressed && !s_hot_guard) {
        s_hot_guard = 1;
        keyboard_push_hotkey(2);
        return;
    }
    if (scancode == 0x23 && ctrl_pressed && alt_pressed && !s_hot_guard) {
        s_hot_guard = 1;
        keyboard_push_hotkey(9);        /* Ctrl+Alt+H: yardim paneli */
        return;
    }
    if (ctrl_pressed && !alt_pressed) {
        if (scancode == 0x2E) { keyboard_push_hotkey(3); return; }  /* Ctrl+C */
        if (scancode == 0x2D) { keyboard_push_hotkey(4); return; }  /* Ctrl+X */
        if (scancode == 0x2F) { keyboard_push_hotkey(5); return; }  /* Ctrl+V */
        if (scancode == 0x1E) { keyboard_push_hotkey(6); return; }  /* Ctrl+A */
        if (scancode == 0x2C) { keyboard_push_hotkey(7); return; }  /* Ctrl+Z */
        if (scancode == 0x15) { keyboard_push_hotkey(8); return; }  /* Ctrl+Y */
    }
    /* Sanal masaustu gecisi: Ctrl+Alt+, ve Ctrl+Alt+.
     * Ctrl+Alt+Shift+,/. ayni pencereleri tasir (0.6). */
    if (ctrl_pressed && alt_pressed && !s_hot_guard) {
        if (scancode == 0x33) {
            s_hot_guard = 1;
            keyboard_push_hotkey(shift_pressed ? 15 : 10);
            return;
        }
        if (scancode == 0x34) {
            s_hot_guard = 1;
            keyboard_push_hotkey(shift_pressed ? 16 : 11);
            return;
        }
        /* Ctrl+Alt+A: Arctian Asistani; Ctrl+Alt+F: fps HUD       */
        if (scancode == 0x1E) { s_hot_guard = 1; keyboard_push_hotkey(12); return; }
        if (scancode == 0x21) { s_hot_guard = 1; keyboard_push_hotkey(13); return; }
    }

    const char* map = altgr_pressed ? s_map_altgr
                                     : (shift_pressed ? s_map_shift : s_map_normal);

    if (scancode < 128) {
        char c = map[scancode];

        if (c) {
            // Caps Lock kontrolu
            if (caps_lock && c >= 'a' && c <= 'z') {
                c -= 32;
            } else if (caps_lock && c >= 'A' && c <= 'Z') {
                c += 32;
            }

            // Push event only — GUI main loop feeds legacy buffer
            keyboard_push_event(c, scancode, true);
        }
    }
}

// Event kuyrugundan karakteri legacy buffer'a ekle (GUI main loop cagirir)
void keyboard_push_char(char c) {
    if (keyboard_buffer_pos < KEYBOARD_BUFFER_SIZE - 1) {
        keyboard_buffer[keyboard_buffer_pos++] = c;
        keyboard_buffer[keyboard_buffer_pos] = '\0';
    }
}

// Klavye bufferini temizle
void keyboard_clear_buffer(void) {
    keyboard_buffer_pos = 0;
    keyboard_buffer[0] = '\0';
}

// Klavye bufferini al
const char* keyboard_get_buffer(void) {
    return keyboard_buffer;
}

// Klavye baslatma
void keyboard_init(void) {
    kernel_debug("Klavye driver baslatiliyor (TR-Q layout)...");

    // Klavye controlleri disable et
    outb(KEYBOARD_COMMAND_PORT, KEYBOARD_CMD_DISABLE);

    // Bufferde bekleyen eski veriyi at (aksi halde config byte yerine bir
    // scancode okunup geri yazilir ve controller yanlis modda kalir)
    ps2_flush();

    // Config bytei oku (controller'in veriyi hazirlamasini bekle)
    outb(KEYBOARD_COMMAND_PORT, KEYBOARD_CMD_READ_CONFIG);
    uint8_t config = 0;
    int config_ok = ps2_wait_buffer();
    if (config_ok) config = inb(KEYBOARD_DATA_PORT);

    /* Okuma zaman asimina ugradiginda config 0 kalir; 0|0x01 yazmak
     * controller'in gercek ayarlarini (cevirme, IRQ12) silerdi. Okunamadiysa
     * hic yazmiyoruz; mevcut ayar zaten calisir haldedir. */
    if (config_ok) {
        // Sadece klavye kesmesi (bit0) acik; bit1 (IRQ12/fare) aynen korunur
        config |= 0x01;

        // Config bytei yaz
        outb(KEYBOARD_COMMAND_PORT, KEYBOARD_CMD_WRITE_CONFIG);
        outb(KEYBOARD_DATA_PORT, config);
    } else {
        kernel_debug("Klavye config bytei okunamadi; mevcut ayar korundu");
    }

    // Klavye controlleri enable et
    outb(KEYBOARD_COMMAND_PORT, KEYBOARD_CMD_ENABLE);

    // Bufferi temizle
    keyboard_clear_buffer();

    // IRQ1 handler'ini ISR sistemine kaydet
    isr_install_handler(1, keyboard_interrupt_handler);

    // IRQ1 maskesini PIC'te kaldir
    pic_set_mask(1, false);

    kernel_debug("Klavye driver basariyla baslatildi (TR-Q, IRQ1)");
}

// Klavye test fonksiyonu
void keyboard_test(void) {
    terminal_writestring("Klavye Testi (TR-Q):\n");
    terminal_writestring("--------------------\n");
    terminal_writestring("Lutfen bir seyler yazin (Enter ile bitirin):\n");

    char old_buffer[KEYBOARD_BUFFER_SIZE];
    size_t i;
    for (i = 0; keyboard_buffer[i] != '\0' && i < KEYBOARD_BUFFER_SIZE - 1; i++) {
        old_buffer[i] = keyboard_buffer[i];
    }
    old_buffer[i] = '\0';

    keyboard_clear_buffer();

    // Kullanici girisini bekle
    while (1) {
        // Enter tusunu kontrol et
        if (keyboard_buffer_pos > 0 && keyboard_buffer[keyboard_buffer_pos - 1] == '\n') {
            keyboard_buffer[keyboard_buffer_pos - 1] = '\0'; // Newlinei kaldir
            break;
        }
    }

    terminal_writestring("\nYazdiklariniz: ");
    terminal_writestring(keyboard_buffer);
    terminal_writestring("\n\n");

    // Eski bufferi geri yukle
    for (i = 0; old_buffer[i] != '\0' && i < KEYBOARD_BUFFER_SIZE - 1; i++) {
        keyboard_buffer[i] = old_buffer[i];
    }
    keyboard_buffer[i] = '\0';
    keyboard_buffer_pos = 0;
    while (keyboard_buffer[keyboard_buffer_pos]) keyboard_buffer_pos++;
}
