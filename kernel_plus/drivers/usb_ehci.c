#include "kernel.h"
#include "usb.h"
#include "pci.h"

#define EHCI_USBCMD          0x00
#define EHCI_USBSTS          0x04
#define EHCI_USBINTR         0x08
#define EHCI_FRINDEX         0x0C
#define EHCI_PERIODICLISTBASE 0x14
#define EHCI_ASYNCLISTADDR   0x18
#define EHCI_CONFIGFLAG      0x40
#define EHCI_PORTSC(n)       (0x44 + ((n) * 4))

#define EHCI_MAX_PORTS       8

#define EHCI_QH_NEXT_PTR_MASK   0xFFFFFFE0
#define EHCI_QH_TYPE_QH         0x02
#define EHCI_QH_TYPE_ITD        0x00
#define EHCI_QH_TYPE_SITD       0x04
#define EHCI_QH_TYPE_FSTN       0x06

#define EHCI_TD_ACTIVE          (1 << 7)
#define EHCI_TD_HALTED          (1 << 6)
#define EHCI_TD_DATABUFFER_ERR  (1 << 5)
#define EHCI_TD_BABBLE          (1 << 4)
#define EHCI_TD_NAK             (1 << 3)
#define EHCI_TD_PID_OUT         0x00
#define EHCI_TD_PID_IN          0x01
#define EHCI_TD_PID_SETUP       0x02
#define EHCI_TD_ERROR_COUNT(x)  (((x) & 3) << 8)
#define EHCI_TD_CERR(x)         (((x) & 3) << 10)
#define EHCI_TD_IOC             (1 << 15)

#define EHCI_PID_OUT            0
#define EHCI_PID_IN             1
#define EHCI_PID_SETUP          2

#pragma pack(push, 1)
typedef struct {
    uint32_t next_link;
    uint32_t next_link_hi;
} ehci_link_t;

typedef struct {
    uint32_t horiz_link;
    uint32_t horiz_link_hi;
    uint32_t ep_char[5];
    uint32_t ep_char_hi[4];
} ehci_qh_t;

typedef struct {
    uint32_t next_link;
    uint32_t next_link_hi;
    uint32_t alt_next;
    uint32_t alt_next_hi;
    uint32_t token;
    uint32_t token_hi;
    uint32_t buffer[5];
    uint32_t buffer_hi[5];
} ehci_qtd_t;
#pragma pack(pop)

typedef struct {
    volatile uint32_t *mmio;
    uint64_t mmio_base;
    int n_ports;
    bool has_64bit;
    ehci_qh_t *async_qh;
    ehci_qtd_t *async_qtd;
    uint64_t async_qh_phys;
    uint64_t async_qtd_phys;
    uint8_t pci_irq;
} ehci_controller_t;

static ehci_controller_t ehci_ctrl;
static bool ehci_ok = false;
static usb_device_t ehci_devices[USB_MAX_DEVICES];
static int ehci_dev_count = 0;

static ehci_qh_t ehci_qh_storage[4];
static ehci_qtd_t ehci_qtd_storage[8];

static uint32_t ehci_read32(ehci_controller_t *ctrl, uint32_t reg) {
    return ctrl->mmio[reg / 4];
}

static void ehci_write32(ehci_controller_t *ctrl, uint32_t reg, uint32_t val) {
    ctrl->mmio[reg / 4] = val;
}

static void ehci_set_bits32(ehci_controller_t *ctrl, uint32_t reg, uint32_t bits) {
    ehci_write32(ctrl, reg, ehci_read32(ctrl, reg) | bits);
}

static void ehci_clear_bits32(ehci_controller_t *ctrl, uint32_t reg, uint32_t bits) {
    ehci_write32(ctrl, reg, ehci_read32(ctrl, reg) & ~bits);
}

static void ehci_wait_sts_bit(ehci_controller_t *ctrl, uint32_t mask, bool set, int timeout) {
    while (timeout--) {
        if ((ehci_read32(ctrl, EHCI_USBSTS) & mask) ? set : !set)
            return;
    }
}

static uint64_t ehci_virt_to_phys(void *virt) {
    return (uint64_t)(uintptr_t)virt;
}

static int ehci_reset_port(ehci_controller_t *ctrl, int port) {
    uint32_t reg = EHCI_PORTSC(port);
    uint32_t portsc = ehci_read32(ctrl, reg);
    portsc |= EHCI_PORTSC_PP;
    ehci_write32(ctrl, reg, portsc);

    portsc |= EHCI_PORTSC_RESET;
    ehci_write32(ctrl, reg, portsc);

    for (volatile int i = 0; i < 50000; i++) asm volatile ("pause");

    portsc &= ~EHCI_PORTSC_RESET;
    ehci_write32(ctrl, reg, portsc);

    for (volatile int i = 0; i < 100000; i++) asm volatile ("pause");

    portsc = ehci_read32(ctrl, reg);
    if (!(portsc & EHCI_PORTSC_PE)) {
        kernel_debug("EHCI: Port %d baglanti yok", port);
        return -1;
    }

    kernel_debug("EHCI: Port %d cihaz baglandi (portsc=0x%08X)", port, portsc);
    return 0;
}

static void ehci_setup_qh_async(ehci_controller_t *ctrl) {
    ctrl->async_qh = &ehci_qh_storage[0];
    memset(ctrl->async_qh, 0, sizeof(ehci_qh_t));
    ctrl->async_qh_phys = ehci_virt_to_phys(ctrl->async_qh);

    ctrl->async_qh->horiz_link = (uint32_t)(ctrl->async_qh_phys) | EHCI_QH_TYPE_QH;
    if (ctrl->has_64bit)
        ctrl->async_qh->horiz_link_hi = (uint32_t)(ctrl->async_qh_phys >> 32);

    ctrl->async_qh->ep_char[0] = (1 << 14) | (1 << 15) | (1 << 12);
    ctrl->async_qh->ep_char[1] = (1 << 0) | (1 << 7);
    ctrl->async_qh->ep_char[2] = (64 << 16);

    ctrl->async_qtd = &ehci_qtd_storage[0];
    memset(ctrl->async_qtd, 0, sizeof(ehci_qtd_t));
    ctrl->async_qtd_phys = ehci_virt_to_phys(ctrl->async_qtd);
}

static ehci_qtd_t *ehci_create_qtd(usb_device_t *dev, uint8_t pid, uint8_t *data, int len) {
    (void)dev;
    ehci_qtd_t *qtd = &ehci_qtd_storage[1];
    memset(qtd, 0, sizeof(ehci_qtd_t));

    qtd->next_link = 0x01;
    qtd->alt_next = 0x01;

    uint32_t token = (len << 16) | (pid << 8) | EHCI_TD_ACTIVE | EHCI_TD_CERR(3) | EHCI_TD_ERROR_COUNT(0);
    if (pid == EHCI_PID_IN && len > 0)
        token |= EHCI_TD_IOC;

    qtd->token = token;
    qtd->buffer[0] = (uint32_t)(uintptr_t)data;

    return qtd;
}

static int ehci_execute_async(ehci_controller_t *ctrl, ehci_qh_t *qh, ehci_qtd_t *qtd, int timeout) {
    qh->horiz_link = (uint32_t)(ehci_virt_to_phys(qtd) | 0x02);
    if (ctrl->has_64bit)
        qh->horiz_link_hi = (uint32_t)(ehci_virt_to_phys(qtd) >> 32);

    while (timeout--) {
        if (!(qtd->token & EHCI_TD_ACTIVE))
            return 0;
        for (volatile int i = 0; i < 100; i++) asm volatile ("pause");
    }

    if (qtd->token & EHCI_TD_HALTED)
        return -1;

    return 0;
}

static int ehci_setup_packet(usb_device_t *dev, ehci_controller_t *ctrl,
                             usb_setup_packet_t *setup, uint8_t *data, int dir) {
    uint8_t setup_buf[8];
    memcpy(setup_buf, setup, 8);

    int maxpkt = dev->max_packet_size ? dev->max_packet_size : 64;
    uint8_t data_buf[512];
    int data_len = setup->wLength;
    if (data_len > 512) data_len = 512;
    if (data && dir == USB_DIR_IN && data_len > 0)
        memset(data_buf, 0, data_len);

    ehci_qh_t *qh = &ehci_qh_storage[1];
    memset(qh, 0, sizeof(ehci_qh_t));

    uint32_t eps = 0;
    eps |= (dev->address & 0x7F) << 0;
    eps |= (0 & 0x0F) << 8;
    eps |= (maxpkt & 0x7FF) << 16;

    qh->ep_char[0] = eps;
    qh->ep_char[1] = (0 << 0) | (0 << 7) | (maxpkt << 16);
    qh->ep_char[2] = (maxpkt << 16);

    ehci_qtd_t *setup_qtd = &ehci_qtd_storage[2];
    ehci_qtd_t *data_qtd = &ehci_qtd_storage[3];
    ehci_qtd_t *status_qtd = &ehci_qtd_storage[4];

    memset(setup_qtd, 0, sizeof(ehci_qtd_t));
    memset(data_qtd, 0, sizeof(ehci_qtd_t));
    memset(status_qtd, 0, sizeof(ehci_qtd_t));

    setup_qtd->next_link = 0x01;
    setup_qtd->alt_next = 0x01;
    setup_qtd->token = (8 << 16) | (EHCI_PID_SETUP << 8) | EHCI_TD_ACTIVE | EHCI_TD_CERR(3);
    setup_qtd->buffer[0] = (uint32_t)(uintptr_t)setup_buf;

    if (data_len > 0) {
        data_qtd->next_link = 0x01;
        data_qtd->alt_next = 0x01;
        uint8_t pid = (dir == USB_DIR_IN) ? EHCI_PID_IN : EHCI_PID_OUT;
        data_qtd->token = (data_len << 16) | (pid << 8) | EHCI_TD_ACTIVE | EHCI_TD_CERR(3);
        data_qtd->buffer[0] = (uint32_t)(uintptr_t)((dir == USB_DIR_IN) ? data_buf : data);

        setup_qtd->next_link = (uint32_t)(uintptr_t)data_qtd;
    }

    uint8_t status_pid = (dir == USB_DIR_IN) ? EHCI_PID_OUT : EHCI_PID_IN;
    status_qtd->next_link = 0x01;
    status_qtd->alt_next = 0x01;
    status_qtd->token = (0 << 16) | (status_pid << 8) | EHCI_TD_ACTIVE | EHCI_TD_CERR(3);

    if (data_len > 0)
        data_qtd->next_link = (uint32_t)(uintptr_t)status_qtd;
    else
        setup_qtd->next_link = (uint32_t)(uintptr_t)status_qtd;

    qh->horiz_link = (uint32_t)(uintptr_t)setup_qtd | 0x02;

    int ret = ehci_execute_async(ctrl, qh, setup_qtd, 100000);

    if (ret == 0 && dir == USB_DIR_IN && data && data_len > 0)
        memcpy(data, data_buf, data_len);

    return ret;
}

static int ehci_get_device_descriptor(usb_device_t *dev, usb_controller_t *ctrl) {
    usb_setup_packet_t setup;
    uint8_t buf[18];

    setup.bmRequestType = USB_DIR_IN | (USB_REQTYPE_STANDARD << 5) | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_DEVICE << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 18;

    dev->max_packet_size = 64;

    if (ehci_setup_packet(dev, &ehci_ctrl, &setup, buf, USB_DIR_IN) != 0)
        return -1;

    usb_device_descriptor_t *desc = (usb_device_descriptor_t *)buf;
    dev->max_packet_size = desc->bMaxPacketSize0;
    dev->class_code = desc->bDeviceClass;
    dev->subclass = desc->bDeviceSubClass;
    dev->protocol = desc->bDeviceProtocol;
    dev->vendor_id = desc->idVendor;
    dev->product_id = desc->idProduct;

    return 0;
}

static int ehci_set_address(usb_device_t *dev, uint8_t address) {
    usb_setup_packet_t setup;
    setup.bmRequestType = USB_DIR_OUT | (USB_REQTYPE_STANDARD << 5) | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_SET_ADDRESS;
    setup.wValue = address;
    setup.wIndex = 0;
    setup.wLength = 0;

    dev->address = address;
    if (ehci_setup_packet(dev, &ehci_ctrl, &setup, 0, USB_DIR_OUT) != 0) {
        dev->address = 0;
        return -1;
    }

    for (volatile int i = 0; i < 10000; i++) asm volatile ("pause");
    return 0;
}

static int ehci_get_config_descriptor(usb_device_t *dev, usb_controller_t *ctrl) {
    uint8_t buf[9];
    usb_setup_packet_t setup;

    setup.bmRequestType = USB_DIR_IN | (USB_REQTYPE_STANDARD << 5) | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_CONFIG << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 9;

    if (ehci_setup_packet(dev, &ehci_ctrl, &setup, buf, USB_DIR_IN) != 0)
        return -1;

    usb_config_descriptor_t *cfg = (usb_config_descriptor_t *)buf;
    uint16_t total_len = cfg->wTotalLength;

    if (total_len > 512) total_len = 512;

    uint8_t full_cfg[512];
    setup.wLength = total_len;

    if (ehci_setup_packet(dev, &ehci_ctrl, &setup, full_cfg, USB_DIR_IN) != 0)
        return -1;

    uint8_t *p = full_cfg + 9;
    uint8_t *end = full_cfg + total_len;

    while (p < end) {
        uint8_t len = p[0];
        if (len == 0) break;

        switch (p[1]) {
            case USB_DESC_INTERFACE: {
                usb_interface_descriptor_t *iface = (usb_interface_descriptor_t *)p;
                dev->class_code = iface->bInterfaceClass;
                dev->subclass = iface->bInterfaceSubClass;
                dev->protocol = iface->bInterfaceProtocol;
                break;
            }
            case USB_DESC_ENDPOINT: {
                if (dev->endpoint_count < USB_MAX_ENDPOINTS) {
                    usb_endpoint_descriptor_t *ep = (usb_endpoint_descriptor_t *)p;
                    memcpy(&dev->endpoints[dev->endpoint_count], ep, sizeof(usb_endpoint_descriptor_t));
                    dev->endpoint_count++;
                }
                break;
            }
        }
        p += len;
    }

    setup.bmRequestType = USB_DIR_OUT | (USB_REQTYPE_STANDARD << 5) | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_SET_CONFIGURATION;
    setup.wValue = cfg->bConfigurationValue;
    setup.wIndex = 0;
    setup.wLength = 0;
    ehci_setup_packet(dev, &ehci_ctrl, &setup, 0, USB_DIR_OUT);

    dev->state = USB_DEVSTATE_CONFIGURED;
    return 0;
}

static int ehci_enumerate_device(usb_controller_t *ctrl, int port) {
    usb_device_t *dev = &ehci_devices[ehci_dev_count];
    memset(dev, 0, sizeof(usb_device_t));
    dev->speed = USB_SPEED_HIGH;

    if (ehci_get_device_descriptor(dev, ctrl) != 0)
        return -1;

    uint8_t new_addr = (uint8_t)(ehci_dev_count + 1);
    if (ehci_set_address(dev, new_addr) != 0)
        return -1;

    if (ehci_get_config_descriptor(dev, ctrl) != 0) {
        dev->state = USB_DEVSTATE_ADDRESSED;
        return -1;
    }

    if (dev->class_code == USB_CLASS_HID &&
        (dev->protocol == USB_PROTOCOL_KEYBOARD || dev->protocol == USB_PROTOCOL_MOUSE)) {
        dev->is_keyboard = (dev->protocol == USB_PROTOCOL_KEYBOARD);
        dev->is_mouse = (dev->protocol == USB_PROTOCOL_MOUSE);

        if (usb_device_count < USB_MAX_DEVICES) {
            memcpy(&usb_devices[usb_device_count], dev, sizeof(usb_device_t));
            usb_device_count++;
        }
    }

    ehci_dev_count++;
    return 0;
}

int ehci_init(usb_controller_t *ctrl) {
    memset(&ehci_ctrl, 0, sizeof(ehci_controller_t));

    ehci_ctrl.pci_irq = ctrl->dev.irq;

    int bar_idx = 0;
    if (ctrl->bar[0].is_io && ctrl->bar[1].address && !ctrl->bar[1].is_io)
        bar_idx = 1;
    else if (!ctrl->bar[0].is_io)
        bar_idx = 0;
    else if (!ctrl->bar[1].is_io)
        bar_idx = 1;
    else if (!ctrl->bar[2].is_io)
        bar_idx = 2;

    ehci_ctrl.mmio_base = ctrl->bar[bar_idx].address;
    ehci_ctrl.mmio = (volatile uint32_t *)(uintptr_t)ehci_ctrl.mmio_base;

    if (ctrl->bar[bar_idx].is_64bit)
        ehci_ctrl.has_64bit = true;

    pci_enable_bus_mastering(ctrl->dev.bus, ctrl->dev.slot, ctrl->dev.func);

    ehci_write32(&ehci_ctrl, EHCI_USBCMD, EHCI_CMD_HC_RESET);
    ehci_wait_sts_bit(&ehci_ctrl, EHCI_CMD_HC_RESET, false, 100000);

    if (ehci_read32(&ehci_ctrl, EHCI_USBSTS) & EHCI_STS_HCHALTED) {
        ehci_write32(&ehci_ctrl, EHCI_USBCMD, EHCI_CMD_RUN_STOP);
        ehci_wait_sts_bit(&ehci_ctrl, EHCI_STS_HCHALTED, false, 100000);
    }

    uint32_t hcsparams = ehci_read32(&ehci_ctrl, 0x08);
    ehci_ctrl.n_ports = hcsparams & 0x0F;
    if (ehci_ctrl.n_ports == 0 || ehci_ctrl.n_ports > EHCI_MAX_PORTS)
        ehci_ctrl.n_ports = EHCI_MAX_PORTS;

    kernel_debug("EHCI: mmio=0x%llX port=%d irq=%d", ehci_ctrl.mmio_base, ehci_ctrl.n_ports, ehci_ctrl.pci_irq);

    ehci_setup_qh_async(&ehci_ctrl);

    ehci_write32(&ehci_ctrl, EHCI_ASYNCLISTADDR, (uint32_t)ehci_ctrl.async_qh_phys);
    ehci_write32(&ehci_ctrl, EHCI_CONFIGFLAG, 1);

    ehci_set_bits32(&ehci_ctrl, EHCI_USBCMD, EHCI_CMD_ASYNC_ENABLE | EHCI_CMD_PERIODIC_ENABLE);

    ehci_ok = true;

    for (int i = 0; i < ehci_ctrl.n_ports; i++) {
        uint32_t portsc = ehci_read32(&ehci_ctrl, EHCI_PORTSC(i));
        if (portsc & EHCI_PORTSC_CCS) {
            kernel_debug("EHCI: Port %d cihaz var, enumerasyon basliyor...", i);
            if (ehci_reset_port(&ehci_ctrl, i) == 0) {
                ehci_enumerate_device(ctrl, i);
            }
        }
    }

    return 0;
}

int ehci_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup_buf,
                          uint8_t *data, int dir) {
    if (!ehci_ok) return -1;
    if (!dev) return -1;
    return ehci_setup_packet(dev, &ehci_ctrl, setup_buf, data, dir);
}

int ehci_interrupt_transfer(usb_device_t *dev, uint8_t *buf, int len, int ep_idx) {
    if (!ehci_ok || !dev) return -1;

    uint8_t ep_num = 0;
    uint8_t ep_dir = USB_DIR_IN;

    if (ep_idx >= 0 && ep_idx < dev->endpoint_count) {
        ep_num = dev->endpoints[ep_idx].bEndpointAddress & 0x0F;
        ep_dir = (dev->endpoints[ep_idx].bEndpointAddress & 0x80) ? USB_DIR_IN : USB_DIR_OUT;
    }

    ehci_qh_t *qh = &ehci_qh_storage[2];
    memset(qh, 0, sizeof(ehci_qh_t));

    int maxpkt = dev->max_packet_size ? dev->max_packet_size : 8;
    if (ep_idx >= 0 && ep_idx < dev->endpoint_count)
        maxpkt = dev->endpoints[ep_idx].wMaxPacketSize;

    uint32_t eps = 0;
    eps |= (dev->address & 0x7F) << 0;
    eps |= (ep_num & 0x0F) << 8;
    eps |= (maxpkt & 0x7FF) << 16;

    uint8_t ep_type = USB_EP_TYPE_INTERRUPT;

    qh->ep_char[0] = eps | (ep_type << 12);
    qh->ep_char[1] = (ep_num << 0) | (ep_dir == USB_DIR_IN ? (1 << 7) : 0) | (maxpkt << 16);
    qh->ep_char[2] = (maxpkt << 16);

    ehci_qtd_t *qtd = &ehci_qtd_storage[5];
    memset(qtd, 0, sizeof(ehci_qtd_t));
    qtd->next_link = 0x01;
    qtd->alt_next = 0x01;
    uint8_t pid = (ep_dir == USB_DIR_IN) ? EHCI_PID_IN : EHCI_PID_OUT;
    qtd->token = (len << 16) | (pid << 8) | EHCI_TD_ACTIVE | EHCI_TD_CERR(3) | EHCI_TD_IOC;
    qtd->buffer[0] = (uint32_t)(uintptr_t)buf;

    qh->horiz_link = (uint32_t)(uintptr_t)qtd | 0x02;

    return ehci_execute_async(&ehci_ctrl, qh, qtd, 100000);
}

int ehci_poll_keyboard(usb_device_t *dev, uint8_t *buf) {
    if (!ehci_ok || !dev || !dev->is_keyboard) return -1;
    memset(buf, 0, 8);
    return ehci_interrupt_transfer(dev, buf, 8, 0);
}

int ehci_poll_mouse(usb_device_t *dev, uint8_t *buf) {
    if (!ehci_ok || !dev || !dev->is_mouse) return -1;
    memset(buf, 0, 4);
    return ehci_interrupt_transfer(dev, buf, 4, 0);
}
