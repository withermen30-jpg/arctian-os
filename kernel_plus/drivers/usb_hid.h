#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>
#include <stdbool.h>
#include "usb.h"

#define USB_HID_KEYBOARD_POLL_MS   50
#define USB_HID_MOUSE_POLL_MS      20

typedef struct {
    uint8_t scancode;
    bool pressed;
} hid_key_event_t;

typedef struct {
    int dx;
    int dy;
    uint8_t buttons;
    int scroll;
} hid_mouse_event_t;

int usb_hid_init(void);
void usb_hid_poll(void);

int usb_hid_keyboard_count(void);
int usb_hid_mouse_count(void);

bool usb_hid_has_keyboard(void);
bool usb_hid_has_mouse(void);

void usb_hid_set_boot_protocol(usb_device_t *dev);

#endif
