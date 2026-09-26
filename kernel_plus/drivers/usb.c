#include "kernel.h"
#include "usb.h"
#include "pci.h"
#include "usb_hid.h"

usb_device_t usb_devices[USB_MAX_DEVICES];
int usb_device_count = 0;

static uint8_t usb_keyboard_buf[USB_MAX_DEVICES][8];
static int8_t usb_keyboard_state[USB_MAX_DEVICES];
static uint8_t usb_mouse_buf[USB_MAX_DEVICES][4];

static const uint8_t usb_hid_to_scancode[256] = {
    0,0,0,0, 0x1E,0x1F,0x20,0x21, 0x22,0x23,0x24,0x25, 0x26,0x27,0x28,0x29,
    0x2A,0x2B,0x2C,0x2D, 0x2E,0x2F,0x30,0x31, 0x32,0x33,0x34,0x35, 0x36,0x37,0x38,0x39,
    0x3A,0x3B,0x3C,0x3D, 0x3E,0x3F,0x40,0x41, 0x42,0x43,0x44,0x45, 0x46,0x47,0x48,0x49,
    0x4A,0x4B,0x4C,0x4D, 0x4E,0x4F,0x50,0x51, 0x52,0x53,0x54,0x55, 0x56,0x57,0x58,0x59,
    0x5A,0x5B,0x5C,0x5D, 0x5E,0x5F,0x60,0x61, 0x62,0x63,0x64,0x65, 0x66,0x67,0x68,0x69,
    0x6A,0x6B,0x6C,0x6D, 0x6E,0x6F,0x70,0x71, 0x72,0x73,0x74,0x75, 0x76,0x77,0x78,0x79,
    0x7A,0x7B,0x7C,0x7D, 0x7E,0x7F,0x80,0x81, 0x82,0x83,0x84,0x85, 0x86,0x87,0x88,0x89,
    0x8A,0x8B,0x8C,0x8D, 0x8E,0x8F,0x90,0x91, 0x92,0x93,0x94,0x95, 0x96,0x97,0x98,0x99,
    0x9A,0x9B,0x9C,0x9D, 0x9E,0x9F,0xA0,0xA1, 0xA2,0xA3,0xA4,0xA5, 0xA6,0xA7,0xA8,0xA9,
    0xAA,0xAB,0xAC,0xAD, 0xAE,0xAF,0xB0,0xB1, 0xB2,0xB3,0xB4,0xB5, 0xB6,0xB7,0xB8,0xB9,
    0xBA,0xBB,0xBC,0xBD, 0xBE,0xBF,0xC0,0xC1, 0xC2,0xC3,0xC4,0xC5, 0xC6,0xC7,0xC8,0xC9,
    0xCA,0xCB,0xCC,0xCD, 0xCE,0xCF,0xD0,0xD1, 0xD2,0xD3,0xD4,0xD5, 0xD6,0xD7,0xD8,0xD9,
    0xDA,0xDB,0xDC,0xDD, 0xDE,0xDF,0xE0,0xE1, 0xE2,0xE3,0xE4,0xE5, 0xE6,0xE7,0xE8,0xE9,
    0xEA,0xEB,0xEC,0xED, 0xEE,0xEF,0xF0,0xF1, 0xF2,0xF3,0xF4,0xF5, 0xF6,0xF7,0xF8,0xF9,
    0xFA,0xFB,0xFC,0xFD, 0xFE,0xFF,0x00,0x01, 0x02,0x03,0x04,0x05, 0x06,0x07,0x08,0x09,
    0x0A,0x0B,0x0C,0x0D, 0x0E,0x0F,0x10,0x11, 0x12,0x13,0x14,0x15, 0x16,0x17,0x18,0x19,
};

void keyboard_usb_push_scancode(uint8_t scancode);
void mouse_usb_move(int dx, int dy, uint8_t buttons);

static uint8_t usb_get_endpoint_type(usb_controller_t *ctrl) {
    if (!ctrl) return 0xFF;
    switch (ctrl->usb_type) {
        case 0: return 1;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        default: return 0xFF;
    }
}

static void usb_process_keyboard(usb_device_t *dev, uint8_t *buf) {
    int idx = dev - usb_devices;
    if (idx < 0 || idx >= USB_MAX_DEVICES) return;

    uint8_t modifier = buf[0];
    uint8_t sc_ps2 = 0;

    (void)modifier;

    for (int i = 2; i < 8; i++) {
        uint8_t key = buf[i];
        if (key == 0) continue;
        if (key < 256) {
            sc_ps2 = usb_hid_to_scancode[key];
            if (sc_ps2) break;
        }
    }

    if (sc_ps2 && sc_ps2 != usb_keyboard_state[idx]) {
        usb_keyboard_state[idx] = (int8_t)sc_ps2;
        keyboard_usb_push_scancode(sc_ps2);
    }
}

static void usb_process_mouse(usb_device_t *dev, uint8_t *buf) {
    int idx = dev - usb_devices;
    if (idx < 0 || idx >= USB_MAX_DEVICES) return;

    int dx = (int)(int8_t)buf[1];
    int dy = (int)(int8_t)buf[2];
    uint8_t buttons = buf[0] & 0x07;

    if (dx != 0 || dy != 0 || buttons != (usb_mouse_buf[idx][0] & 0x07)) {
        usb_mouse_buf[idx][0] = buf[0];
        usb_mouse_buf[idx][1] = buf[1];
        usb_mouse_buf[idx][2] = buf[2];
        usb_mouse_buf[idx][3] = buf[3];
        mouse_usb_move(dx, dy, buttons);
    }
}

static int usb_init_controller(usb_controller_t *ctrl) {
    if (ctrl->initialized) return 0;

    pci_enable_bus_mastering(ctrl->dev.bus, ctrl->dev.slot, ctrl->dev.func);

    int ret = -1;
    switch (ctrl->usb_type) {
        case 0:
            kernel_debug("USB: UHCI baslatiliyor...");
            ret = uhci_init(ctrl);
            break;
        case 1:
            kernel_debug("USB: OHCI baslatiliyor...");
            ret = -1;
            break;
        case 2:
            kernel_debug("USB: EHCI baslatiliyor...");
            ret = -1;
            break;
        case 3:
            kernel_debug("USB: xHCI baslatiliyor...");
            ret = xhci_init(ctrl);
            break;
        default:
            kernel_debug("USB: Bilinmeyen denetleyici tipi (0x%02X)", ctrl->usb_type);
            return -1;
    }

    if (ret == 0)
        ctrl->initialized = true;

    return ret;
}

void usb_init(void) {
    kernel_debug("USB: Sürücü başlatılıyor...");

    pci_init();

    if (usb_controller_count == 0) {
        kernel_debug("USB: Denetleyici bulunamadı, PS/2 fallback");
        return;
    }

    kernel_debug("USB: %d denetleyici bulundu", usb_controller_count);

    for (int i = 0; i < usb_controller_count; i++) {
        usb_controller_t *ctrl = &usb_controllers[i];
        const char *names[] = {"UHCI", "OHCI", "EHCI", "xHCI"};
        const char *name = (ctrl->usb_type < 4) ? names[ctrl->usb_type] : "?";

        kernel_debug("USB: #%d %s denetleyici başlatılıyor...", i, name);

        if (usb_init_controller(ctrl) == 0) {
            kernel_debug("USB: #%d %s başarıyla başlatıldı", i, name);
        } else {
            kernel_debug("USB: #%d %s başlatılamadı", i, name);
        }
    }

    if (usb_device_count > 0) {
        kernel_debug("USB: %d cihaz bulundu", usb_device_count);
        for (int i = 0; i < usb_device_count; i++) {
            usb_device_t *dev = &usb_devices[i];
            if (dev->is_keyboard)
                kernel_debug("USB: Cihaz #%d = HID Klavye", i);
            else if (dev->is_mouse)
                kernel_debug("USB: Cihaz #%d = HID Fare", i);
        }
        usb_hid_init();
    }
}

void usb_poll_all(void) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        usb_device_t *dev = &usb_devices[i];
        if (dev->state != USB_DEVSTATE_CONFIGURED) continue;

        uint8_t buf[8];
        int ret;

        if (dev->is_keyboard) {
            ret = -1;
            for (int c = 0; c < usb_controller_count; c++) {
                usb_controller_t *ctrl = &usb_controllers[c];
                if (!ctrl->initialized) continue;
                switch (ctrl->usb_type) {
                    case 2:
                        ret = ehci_poll_keyboard(dev, buf);
                        break;
                    case 3:
                        ret = xhci_poll_keyboard(dev, buf);
                        break;
                }
                if (ret == 0) break;
            }
            if (ret == 0)
                usb_process_keyboard(dev, buf);
        } else if (dev->is_mouse) {
            ret = -1;
            for (int c = 0; c < usb_controller_count; c++) {
                usb_controller_t *ctrl = &usb_controllers[c];
                if (!ctrl->initialized) continue;
                switch (ctrl->usb_type) {
                    case 2:
                        ret = ehci_poll_mouse(dev, buf);
                        break;
                    case 3:
                        ret = xhci_poll_mouse(dev, buf);
                        break;
                }
                if (ret == 0) break;
            }
            if (ret == 0)
                usb_process_mouse(dev, buf);
        }
    }
}

int usb_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup,
                         uint8_t *data, int dir) {
    if (!dev) return -1;

    for (int c = 0; c < usb_controller_count; c++) {
        usb_controller_t *ctrl = &usb_controllers[c];
        if (!ctrl->initialized) continue;
        switch (ctrl->usb_type) {
            case 0:
                return uhci_control_transfer(dev, setup, data, dir);
            case 2:
                return ehci_control_transfer(dev, setup, data, dir);
            case 3:
                return xhci_control_transfer(dev, setup, data, dir);
        }
    }
    return -1;
}
