#include "kernel.h"
#include "usb_hid.h"
#include "usb.h"

static bool hid_initialized = false;

int usb_hid_init(void) {
    int found = 0;

    kernel_debug("USB HID: Cihazlar taranıyor...");

    for (int i = 0; i < usb_device_count; i++) {
        usb_device_t *dev = &usb_devices[i];
        if (dev->class_code != USB_CLASS_HID) continue;
        if (dev->state != USB_DEVSTATE_CONFIGURED) continue;

        if (dev->protocol == USB_PROTOCOL_KEYBOARD) {
            dev->is_keyboard = true;
            usb_hid_set_boot_protocol(dev);

            usb_setup_packet_t setup;
            setup.bmRequestType = USB_DIR_OUT | (USB_REQTYPE_CLASS << 5) | USB_RECIP_INTERFACE;
            setup.bRequest = HID_REQ_SET_PROTOCOL;
            setup.wValue = HID_PROTOCOL_BOOT;
            setup.wIndex = 0;
            setup.wLength = 0;
            usb_control_transfer(dev, &setup, 0, USB_DIR_OUT);

            setup.bmRequestType = USB_DIR_OUT | (USB_REQTYPE_CLASS << 5) | USB_RECIP_INTERFACE;
            setup.bRequest = HID_REQ_SET_IDLE;
            setup.wValue = 0;
            setup.wIndex = 0;
            setup.wLength = 0;
            usb_control_transfer(dev, &setup, 0, USB_DIR_OUT);

            kernel_debug("USB HID: Klavye #%d başlatıldı", i);
            found++;
        } else if (dev->protocol == USB_PROTOCOL_MOUSE) {
            dev->is_mouse = true;
            usb_hid_set_boot_protocol(dev);

            usb_setup_packet_t setup;
            setup.bmRequestType = USB_DIR_OUT | (USB_REQTYPE_CLASS << 5) | USB_RECIP_INTERFACE;
            setup.bRequest = HID_REQ_SET_PROTOCOL;
            setup.wValue = HID_PROTOCOL_BOOT;
            setup.wIndex = 0;
            setup.wLength = 0;
            usb_control_transfer(dev, &setup, 0, USB_DIR_OUT);

            setup.bmRequestType = USB_DIR_OUT | (USB_REQTYPE_CLASS << 5) | USB_RECIP_INTERFACE;
            setup.bRequest = HID_REQ_SET_IDLE;
            setup.wValue = 0;
            setup.wIndex = 0;
            setup.wLength = 0;
            usb_control_transfer(dev, &setup, 0, USB_DIR_OUT);

            kernel_debug("USB HID: Fare #%d başlatıldı", i);
            found++;
        }
    }

    hid_initialized = (found > 0);
    kernel_debug("USB HID: %d cihaz başlatıldı", found);
    return (found > 0) ? 0 : -1;
}

void usb_hid_poll(void) {
    if (!hid_initialized) return;
    usb_poll_all();
}

int usb_hid_keyboard_count(void) {
    int count = 0;
    for (int i = 0; i < usb_device_count; i++) {
        if (usb_devices[i].class_code == USB_CLASS_HID &&
            usb_devices[i].protocol == USB_PROTOCOL_KEYBOARD &&
            usb_devices[i].state == USB_DEVSTATE_CONFIGURED)
            count++;
    }
    return count;
}

int usb_hid_mouse_count(void) {
    int count = 0;
    for (int i = 0; i < usb_device_count; i++) {
        if (usb_devices[i].class_code == USB_CLASS_HID &&
            usb_devices[i].protocol == USB_PROTOCOL_MOUSE &&
            usb_devices[i].state == USB_DEVSTATE_CONFIGURED)
            count++;
    }
    return count;
}

bool usb_hid_has_keyboard(void) {
    return usb_hid_keyboard_count() > 0;
}

bool usb_hid_has_mouse(void) {
    return usb_hid_mouse_count() > 0;
}

void usb_hid_set_boot_protocol(usb_device_t *dev) {
    if (!dev || dev->class_code != USB_CLASS_HID) return;

    usb_setup_packet_t setup;
    setup.bmRequestType = USB_DIR_OUT | (USB_REQTYPE_CLASS << 5) | USB_RECIP_INTERFACE;
    setup.bRequest = HID_REQ_SET_PROTOCOL;
    setup.wValue = HID_PROTOCOL_BOOT;
    setup.wIndex = 0;
    setup.wLength = 0;
    usb_control_transfer(dev, &setup, 0, USB_DIR_OUT);
}
