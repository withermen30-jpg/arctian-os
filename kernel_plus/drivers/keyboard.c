
#include "kernel.h"
#include "isr.h"
#include "pic.h"
#include "keyboard.h"

#define USB_SCANCODE_QUEUE_SIZE 16
static uint8_t usb_scancode_queue[USB_SCANCODE_QUEUE_SIZE];
static int usb_scancode_queue_head = 0;
static int usb_scancode_queue_tail = 0;

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool caps_lock = false;
static bool gui_pressed = false;
static bool extended_byte = false;

extern size_t terminal_row;
extern size_t terminal_column;
extern uint8_t terminal_color;
extern uint16_t* terminal_buffer;

__attribute__((weak)) void gui_toggle_start_menu(void) { }

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_COMMAND_PORT 0x64

#define KEYBOARD_CMD_ENABLE 0xAE
#define KEYBOARD_CMD_DISABLE 0xAD
#define KEYBOARD_CMD_READ_CONFIG 0x20
#define KEYBOARD_CMD_WRITE_CONFIG 0x60

#define KEY_RELEASED 0x80

#define KEYBOARD_BUFFER_SIZE 128
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static size_t keyboard_buffer_pos = 0;

#define KEYBOARD_EVENT_QUEUE_SIZE 64
static keyboard_event_t keyboard_event_queue[KEYBOARD_EVENT_QUEUE_SIZE];
static int keyboard_event_head = 0;
static int keyboard_event_tail = 0;

static void keyboard_push_event(char ch, uint8_t scancode, bool pressed) {
    int next = (keyboard_event_head + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    if (next == keyboard_event_tail) return;
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
    [0x27] = 's', [0x28] = 'i', [0x29] = '"',
    [0x2B] = ',',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
    [0x33] = 'o', [0x34] = 'c', [0x35] = '.',
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
    [0x27] = 'S', [0x28] = 'I', [0x29] = '"',
    [0x2B] = ';',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M',
    [0x33] = 'O', [0x34] = 'C', [0x35] = ':',
    [0x39] = ' ', [0x56] = '>'
};

void keyboard_usb_push_scancode(uint8_t scancode) {
    int next = (usb_scancode_queue_head + 1) % USB_SCANCODE_QUEUE_SIZE;
    if (next != usb_scancode_queue_tail) {
        usb_scancode_queue[usb_scancode_queue_head] = scancode;
        usb_scancode_queue_head = next;
    }
}

void keyboard_process_usb_queue(void) {
    while (usb_scancode_queue_tail != usb_scancode_queue_head) {
        uint8_t scancode = usb_scancode_queue[usb_scancode_queue_tail];
        usb_scancode_queue_tail = (usb_scancode_queue_tail + 1) % USB_SCANCODE_QUEUE_SIZE;

        if (scancode & KEY_RELEASED) {
            scancode &= ~KEY_RELEASED;
            switch (scancode) {
                case 0x2A: case 0x36: shift_pressed = false; break;
                case 0x1D: ctrl_pressed = false; break;
                case 0x38: alt_pressed = false; break;
            }
            return;
        }

        switch (scancode) {
            case 0x2A: case 0x36: shift_pressed = true; return;
            case 0x1D: ctrl_pressed = true; return;
            case 0x38: alt_pressed = true; return;
            case 0x3A: caps_lock = !caps_lock; return;
        }

        if (scancode < 128) {
            const char* map = shift_pressed ? keyboard_map_shift : keyboard_map_normal;
            char c = map[scancode];
            if (c) {
                if (caps_lock && c >= 'a' && c <= 'z') c -= 32;
                else if (caps_lock && c >= 'A' && c <= 'Z') c += 32;
                keyboard_push_event(c, scancode, true);
            }
        }
    }
}

void keyboard_interrupt_handler(void) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    keyboard_process_usb_queue();

    if (scancode == 0xE0) {
        extended_byte = true;
        return;
    }

    if (scancode & KEY_RELEASED) {
        scancode &= ~KEY_RELEASED;

        if (extended_byte) {
            extended_byte = false;
            if (scancode == 0x5B || scancode == 0x5C) {
                gui_pressed = false;
            }
            return;
        }

        switch (scancode) {
            case 0x2A:
            case 0x36:
                shift_pressed = false;
                break;
            case 0x1D:
                ctrl_pressed = false;
                break;
            case 0x38:
                alt_pressed = false;
                break;
        }
        return;
    }

    if (extended_byte) {
        extended_byte = false;
        switch (scancode) {
            case 0x5B:
            case 0x5C:
                if (!gui_pressed) {
                    gui_pressed = true;
                    gui_toggle_start_menu();
                }
                return;
            default:
                return;
        }
    }

    switch (scancode) {
        case 0x2A:
        case 0x36:
            shift_pressed = true;
            return;
        case 0x1D:
            ctrl_pressed = true;
            return;
        case 0x38:
            alt_pressed = true;
            return;
        case 0x3A:
            caps_lock = !caps_lock;
            return;
    }

    const char* map = shift_pressed ? keyboard_map_shift : keyboard_map_normal;

    if (scancode < 128) {
        char c = map[scancode];

        if (c) {
            if (caps_lock && c >= 'a' && c <= 'z') {
                c -= 32;
            } else if (caps_lock && c >= 'A' && c <= 'Z') {
                c += 32;
            }

            keyboard_push_event(c, scancode, true);
        }
    }
}

void keyboard_push_char(char c) {
    if (keyboard_buffer_pos < KEYBOARD_BUFFER_SIZE - 1) {
        keyboard_buffer[keyboard_buffer_pos++] = c;
        keyboard_buffer[keyboard_buffer_pos] = '\0';
    }
}

void keyboard_clear_buffer(void) {
    keyboard_buffer_pos = 0;
    keyboard_buffer[0] = '\0';
}

const char* keyboard_get_buffer(void) {
    return keyboard_buffer;
}

void keyboard_init(void) {
    kernel_debug("Klavye driver baslatiliyor (TR-Q layout)...");

    outb(KEYBOARD_COMMAND_PORT, KEYBOARD_CMD_DISABLE);

    outb(KEYBOARD_COMMAND_PORT, KEYBOARD_CMD_READ_CONFIG);
    uint8_t config = inb(KEYBOARD_DATA_PORT);

    config |= 0x01;

    outb(KEYBOARD_COMMAND_PORT, KEYBOARD_CMD_WRITE_CONFIG);
    outb(KEYBOARD_DATA_PORT, config);

    outb(KEYBOARD_COMMAND_PORT, KEYBOARD_CMD_ENABLE);

    keyboard_clear_buffer();

    isr_install_handler(1, keyboard_interrupt_handler);

    pic_set_mask(1, false);

    kernel_debug("Klavye driver basariyla baslatildi (TR-Q, IRQ1)");
}

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

    while (1) {
        if (keyboard_buffer_pos > 0 && keyboard_buffer[keyboard_buffer_pos - 1] == '\n') {
            keyboard_buffer[keyboard_buffer_pos - 1] = '\0';
            break;
        }
    }

    terminal_writestring("\nYazdiklariniz: ");
    terminal_writestring(keyboard_buffer);
    terminal_writestring("\n\n");

    for (i = 0; old_buffer[i] != '\0' && i < KEYBOARD_BUFFER_SIZE - 1; i++) {
        keyboard_buffer[i] = old_buffer[i];
    }
    keyboard_buffer[i] = '\0';
    keyboard_buffer_pos = 0;
    while (keyboard_buffer[keyboard_buffer_pos]) keyboard_buffer_pos++;
}
