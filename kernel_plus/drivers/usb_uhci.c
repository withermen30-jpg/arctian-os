#include "kernel.h"
#include "usb.h"
#include "pci.h"

int uhci_init(usb_controller_t *ctrl) {
    (void)ctrl;
    kernel_debug("USB: UHCI denetleyici bulundu, surucu henuz yok");
    return -1;
}

int uhci_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup, uint8_t *data, int dir) {
    (void)dev;
    (void)setup;
    (void)data;
    (void)dir;
    return -1;
}
