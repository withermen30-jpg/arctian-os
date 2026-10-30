#include "kernel.h"
#include "usb.h"
#include "pci.h"
#include "serial.h"

#define UHCI_USBCMD      0x00
#define UHCI_USBSTS      0x02
#define UHCI_USBINTR     0x04
#define UHCI_FRNUM       0x06
#define UHCI_FLBASEADD   0x08
#define UHCI_SOFMOD      0x0C
#define UHCI_PORTSC(n)   (0x10 + 2 * (n))

#define UHCI_CMD_RS      0x0001
#define UHCI_CMD_HCRESET 0x0002
#define UHCI_CMD_CF      0x0040
#define UHCI_CMD_MAXP    0x0080

#define UHCI_STS_USBINT  0x0001
#define UHCI_STS_ERRINT  0x0002
#define UHCI_STS_HSE     0x0008
#define UHCI_STS_HCPE    0x0010
#define UHCI_STS_HCH     0x0020

#define UHCI_PORT_CCS    0x0001
#define UHCI_PORT_CSC    0x0002
#define UHCI_PORT_EN     0x0004
#define UHCI_PORT_ENC    0x0008
#define UHCI_PORT_LSC    0x0010
#define UHCI_PORT_RD     0x0040
#define UHCI_PORT_LSDA   0x0100
#define UHCI_PORT_RESET  0x0200

#define UHCI_PID_IN      0x69
#define UHCI_PID_OUT     0xE1
#define UHCI_PID_SETUP   0x2D

#define UHCI_LINK_T      (1u << 0)
#define UHCI_LINK_QH     (1u << 1)

#define TD_ACTIVE        (1u << 23)
#define TD_IOC           (1u << 24)
#define TD_LS            (1u << 26)
#define TD_CERR3         (3u << 27)
#define TD_SPD           (1u << 29)
#define TD_STALLED       (1u << 22)
#define TD_CRC           (1u << 18)
#define TD_BABBLE        (1u << 20)
#define TD_DBUF          (1u << 21)

typedef struct {
    uint32_t head;
    uint32_t elem;
    uint32_t r1;
    uint32_t r2;
} uhci_qh_t;

typedef struct {
    uint32_t link;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
    uint32_t sw[4];
} uhci_td_t;

static uint16_t s_iobase;
static int s_nports;
static bool s_ready;
static int s_next_addr;

static uint32_t s_flist[1024] __attribute__((aligned(4096)));
static uhci_qh_t s_qh __attribute__((aligned(16)));
static uhci_td_t s_tds[512] __attribute__((aligned(16)));
static uint8_t s_dma[16384] __attribute__((aligned(4096)));
static uint8_t s_toggle[128][16];

static inline uint16_t uhci_inw(uint16_t r) { return inw(s_iobase + r); }
static inline void uhci_outw(uint16_t r, uint16_t v) { outw(s_iobase + r, v); }
static inline uint32_t uhci_inl(uint16_t r) { return inl(s_iobase + r); }
static inline void uhci_outl(uint16_t r, uint32_t v) { outl(s_iobase + r, v); }

#define UHCI_PIT_CH0 0x40
#define UHCI_PIT_CMD 0x43

static uint16_t uhci_pit_count(void) {
    outb(UHCI_PIT_CMD, 0x00);
    uint8_t lo = inb(UHCI_PIT_CH0);
    uint8_t hi = inb(UHCI_PIT_CH0);
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

static void uhci_wait_us(uint32_t us) {
    uint32_t total = (uint32_t)((uint64_t)us * 1193182ULL / 1000000ULL);
    if (total == 0) total = 1;
    uint16_t prev = uhci_pit_count();
    uint32_t elapsed = 0;
    while (elapsed < total) {
        uint16_t cur = uhci_pit_count();
        if (cur <= prev) elapsed += (uint32_t)(prev - cur);
        else elapsed += (uint32_t)prev + 65536u - cur;
        prev = cur;
    }
}

static void uhci_hc_reset(void) {
    uhci_outw(UHCI_USBCMD, UHCI_CMD_HCRESET);
    uhci_wait_us(10000);
    for (int t = 0; t < 1000; t++) {
        if (!(uhci_inw(UHCI_USBCMD) & UHCI_CMD_HCRESET)) break;
        uhci_wait_us(200);
    }
    uhci_outw(UHCI_USBSTS, 0xFFFF);
    uhci_outw(UHCI_USBINTR, 0);
    uhci_outw(UHCI_FRNUM, 0);
    uhci_outw(UHCI_SOFMOD, 64);
    for (int i = 0; i < 1024; i++) s_flist[i] = UHCI_LINK_T;
    uhci_outl(UHCI_FLBASEADD, (uint32_t)(uintptr_t)s_flist);
}

static int uhci_port_reset(int port) {
    uint16_t sc = uhci_inw(UHCI_PORTSC(port));
    if (!(sc & UHCI_PORT_CCS)) return -1;

    uhci_outw(UHCI_PORTSC(port), UHCI_PORT_RESET);
    uhci_wait_us(50000);
    uhci_outw(UHCI_PORTSC(port), 0);
    uhci_wait_us(20000);
    uhci_outw(UHCI_PORTSC(port), UHCI_PORT_EN | UHCI_PORT_CSC | UHCI_PORT_ENC);
    uhci_wait_us(5000);

    sc = uhci_inw(UHCI_PORTSC(port));
    if (!(sc & UHCI_PORT_CCS)) return -1;
    return 0;
}

static void td_setup_td(uhci_td_t *td, uint32_t link, int pid, int addr, int ep,
                        int toggle, const void *buf, int len, int ls, int ioc, int spd) {
    td->link = link;
    td->status = TD_CERR3 | TD_ACTIVE | (ls ? TD_LS : 0) |
                 (ioc ? TD_IOC : 0) | (spd ? TD_SPD : 0);
    td->token = (uint32_t)(pid & 0xFF) |
                ((uint32_t)(addr & 0x7F) << 8) |
                ((uint32_t)(ep & 0x0F) << 15) |
                ((uint32_t)(toggle & 0x01) << 19) |
                ((uint32_t)((len - 1) & 0x7FF) << 21);
    td->buffer = (uint32_t)(uintptr_t)buf;
    td->sw[0] = td->sw[1] = td->sw[2] = td->sw[3] = 0;
}

static int uhci_run_td_chain(int count) {
    if (!s_ready) return -1;

    s_qh.head = UHCI_LINK_T;
    s_qh.elem = (uint32_t)(uintptr_t)&s_tds[0];
    s_qh.r1 = 0;
    s_qh.r2 = 0;

    uhci_outw(UHCI_USBSTS, 0xFFFF);

    uint32_t qhp = (uint32_t)(uintptr_t)&s_qh | UHCI_LINK_QH;
    for (int i = 0; i < 1024; i++) s_flist[i] = qhp;

    uhci_outw(UHCI_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);

    int done = 0;
    for (int t = 0; t < 6000; t++) {
        if (s_qh.elem & UHCI_LINK_T) { done = 1; break; }
        uint16_t sts = uhci_inw(UHCI_USBSTS);
        if (sts & (UHCI_STS_HSE | UHCI_STS_HCPE)) break;
        if (t == 0) { for (volatile int s = 0; s < 4000; s++) asm volatile("pause"); }
        else uhci_wait_us(500);
    }

    for (int i = 0; i < 1024; i++) s_flist[i] = UHCI_LINK_T;

    if (!done) return -1;

    for (int i = 0; i < count; i++) {
        uint32_t st = s_tds[i].status;
        if (st & TD_ACTIVE) return -1;
        if (st & TD_STALLED) return -2;
        if (st & (TD_CRC | TD_BABBLE | TD_DBUF)) return -3;
    }
    return 0;
}

static int uhci_control_raw(usb_device_t *dev, usb_setup_packet_t *setup,
                            uint8_t *data, int dir) {
    int addr = dev->address;
    int ls = (dev->speed == USB_SPEED_LOW) ? 1 : 0;
    int maxpkt = dev->max_packet_size ? dev->max_packet_size : 8;
    if (ls) maxpkt = 8;
    int data_len = setup->wLength;
    int n = 0;

    memcpy(s_dma, setup, 8);
    uint8_t *stage = s_dma + 16;
    if (data_len > 0 && data && dir == USB_DIR_OUT)
        memcpy(stage, data, data_len);

    td_setup_td(&s_tds[n], UHCI_LINK_T, UHCI_PID_SETUP, addr, 0, 0, s_dma, 8, ls, 0, 0);
    n++;

    int toggle = 1;
    if (data_len > 0 && data) {
        int off = 0;
        while (off < data_len && n < 500) {
            int chunk = data_len - off;
            if (chunk > maxpkt) chunk = maxpkt;
            int pid = (dir == USB_DIR_IN) ? UHCI_PID_IN : UHCI_PID_OUT;
            td_setup_td(&s_tds[n], UHCI_LINK_T, pid, addr, 0, toggle,
                        stage + off, chunk, ls, 0, (dir == USB_DIR_IN) ? 1 : 0);
            toggle ^= 1;
            off += chunk;
            n++;
        }
    }

    int status_pid = (dir == USB_DIR_IN) ? UHCI_PID_OUT : UHCI_PID_IN;
    td_setup_td(&s_tds[n], UHCI_LINK_T, status_pid, addr, 0, 1, 0, 0, ls, 0, 0);
    n++;

    for (int i = 0; i + 1 < n; i++)
        s_tds[i].link = (uint32_t)(uintptr_t)&s_tds[i + 1];
    s_tds[n - 1].link = UHCI_LINK_T;

    int rc = uhci_run_td_chain(n);
    if (rc == 0 && dir == USB_DIR_IN && data && data_len > 0)
        memcpy(data, stage, data_len);
    return rc;
}

static int uhci_bulk_raw(int addr, int ep, int ls, int dir, void *data, int len) {
    int maxpkt = 64;
    int toggle = s_toggle[addr][ep] & 1;
    int n = 0;

    if (len == 0) {
        int pid = (dir == USB_DIR_IN) ? UHCI_PID_IN : UHCI_PID_OUT;
        td_setup_td(&s_tds[n], UHCI_LINK_T, pid, addr, ep, toggle, 0, 0, ls, 1, 0);
        n++;
    } else {
        int off = 0;
        while (off < len && n < 500) {
            int chunk = len - off;
            if (chunk > maxpkt) chunk = maxpkt;
            int pid = (dir == USB_DIR_IN) ? UHCI_PID_IN : UHCI_PID_OUT;
            int last = (off + chunk >= len);
            td_setup_td(&s_tds[n], UHCI_LINK_T, pid, addr, ep, toggle,
                        (uint8_t *)data + off, chunk, ls, last,
                        (dir == USB_DIR_IN) ? 1 : 0);
            toggle ^= 1;
            off += chunk;
            n++;
        }
        s_toggle[addr][ep] = (uint8_t)toggle;
    }

    for (int i = 0; i + 1 < n; i++)
        s_tds[i].link = (uint32_t)(uintptr_t)&s_tds[i + 1];
    s_tds[n - 1].link = UHCI_LINK_T;

    return uhci_run_td_chain(n);
}

static int uhci_enum_port(int port) {
    if (!(uhci_inw(UHCI_PORTSC(port)) & UHCI_PORT_CCS)) return -1;
    if (uhci_port_reset(port) != 0) return -1;
    uint16_t sc = uhci_inw(UHCI_PORTSC(port));
    int ls = (sc & UHCI_PORT_LSDA) ? 1 : 0;

    usb_device_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.address = 0;
    dev.speed = ls ? USB_SPEED_LOW : USB_SPEED_FULL;
    dev.max_packet_size = 8;
    dev.state = USB_DEVSTATE_ADDRESSED;
    dev.xhci_slot_id = -1;
    dev.controller_port = port;

    usb_setup_packet_t s;
    uint8_t buf[8];
    s.bmRequestType = 0x80;
    s.bRequest = USB_REQ_GET_DESCRIPTOR;
    s.wValue = (uint16_t)(USB_DESC_DEVICE << 8);
    s.wIndex = 0;
    s.wLength = 8;
    if (uhci_control_raw(&dev, &s, buf, USB_DIR_IN) != 0) return -1;
    dev.max_packet_size = buf[7] ? buf[7] : 8;

    int addr = s_next_addr++;
    dev.address = 0;
    usb_setup_packet_t sa;
    sa.bmRequestType = 0x00;
    sa.bRequest = USB_REQ_SET_ADDRESS;
    sa.wValue = (uint16_t)addr;
    sa.wIndex = 0;
    sa.wLength = 0;
    if (uhci_control_raw(&dev, &sa, 0, USB_DIR_OUT) != 0) return -1;
    dev.address = (uint8_t)addr;
    uhci_wait_us(3000);

    uint8_t dbuf[18];
    s.wLength = 18;
    s.wValue = (uint16_t)(USB_DESC_DEVICE << 8);
    if (uhci_control_raw(&dev, &s, dbuf, USB_DIR_IN) != 0) return -1;
    dev.vendor_id = (uint16_t)(dbuf[8] | (dbuf[9] << 8));
    dev.product_id = (uint16_t)(dbuf[10] | (dbuf[11] << 8));
    dev.class_code = dbuf[4];
    dev.subclass = dbuf[5];
    dev.protocol = dbuf[6];

    uint8_t cbuf[72];
    usb_setup_packet_t cs;
    cs.bmRequestType = 0x80;
    cs.bRequest = USB_REQ_GET_DESCRIPTOR;
    cs.wValue = (uint16_t)(USB_DESC_CONFIG << 8);
    cs.wIndex = 0;
    cs.wLength = 9;
    if (uhci_control_raw(&dev, &cs, cbuf, USB_DIR_IN) != 0) return -1;
    int total = cbuf[2] | (cbuf[3] << 8);
    if (total < 9) total = 9;
    if (total > 72) total = 72;
    cs.wLength = (uint16_t)total;
    if (uhci_control_raw(&dev, &cs, cbuf, USB_DIR_IN) != 0) return -1;
    uint8_t cfg_value = cbuf[5];

    int off = 9;
    int cur_ep = 0;
    while (off + 2 <= total) {
        uint8_t blen = cbuf[off];
        uint8_t btype = cbuf[off + 1];
        if (blen < 2) break;
        if (btype == USB_DESC_INTERFACE && off + 9 <= total) {
            dev.class_code = cbuf[off + 5];
            dev.subclass = cbuf[off + 6];
            dev.protocol = cbuf[off + 7];
        } else if (btype == USB_DESC_ENDPOINT && off + 7 <= total &&
                   cur_ep < USB_MAX_ENDPOINTS) {
            usb_endpoint_descriptor_t *ep = &dev.endpoints[cur_ep];
            ep->bLength = cbuf[off];
            ep->bDescriptorType = cbuf[off + 1];
            ep->bEndpointAddress = cbuf[off + 2];
            ep->bmAttributes = cbuf[off + 3];
            ep->wMaxPacketSize = (uint16_t)((cbuf[off + 4] | (cbuf[off + 5] << 8)) & 0x7FF);
            ep->bInterval = cbuf[off + 6];
            cur_ep++;
        }
        off += blen;
    }
    dev.endpoint_count = cur_ep;

    usb_setup_packet_t scfg;
    scfg.bmRequestType = 0x00;
    scfg.bRequest = USB_REQ_SET_CONFIGURATION;
    scfg.wValue = (uint16_t)cfg_value;
    scfg.wIndex = 0;
    scfg.wLength = 0;
    if (uhci_control_raw(&dev, &scfg, 0, USB_DIR_OUT) != 0) return -1;
    dev.state = USB_DEVSTATE_CONFIGURED;

    for (int i = 0; i < 16; i++) s_toggle[addr][i] = 0;

    if (usb_device_count < USB_MAX_DEVICES) {
        usb_devices[usb_device_count++] = dev;
    }
    return 0;
}

int uhci_init(usb_controller_t *ctrl) {
    s_iobase = 0;
    for (int b = 0; b < 6; b++) {
        if (ctrl->bar[b].is_io && ctrl->bar[b].address) {
            s_iobase = (uint16_t)ctrl->bar[b].address;
            break;
        }
    }
    if (!s_iobase) {
        kernel_debug("USB: UHCI I/O BAR bulunamadi");
        return -1;
    }

    s_ready = false;
    s_next_addr = 1;
    memset(s_toggle, 0, sizeof(s_toggle));

    uhci_hc_reset();

    s_nports = 0;
    for (int p = 0; p < 4; p++) {
        uint16_t sc = uhci_inw(UHCI_PORTSC(p));
        if (sc == 0xFFFF || sc == 0xFF7F) break;
        s_nports++;
    }
    if (s_nports <= 0) s_nports = 2;

    s_ready = true;

    for (int p = 0; p < s_nports; p++)
        uhci_enum_port(p);

    return 0;
}

int uhci_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup,
                          uint8_t *data, int dir) {
    if (!s_ready || !dev) return -1;
    return uhci_control_raw(dev, setup, data, dir);
}

int uhci_bulk_transfer(usb_device_t *dev, int ep_addr, void *data, int len, int dir) {
    if (!s_ready || !dev) return -1;
    int ep = ep_addr & 0x0F;
    int ls = (dev->speed == USB_SPEED_LOW) ? 1 : 0;
    return uhci_bulk_raw(dev->address, ep, ls, dir, data, len);
}

/* Kesme (interrupt IN) yoklamasi: UHCI'de TD bicimi bulk ile aynidir
 * (yalnizca zamanlama farki var), bu yuzden ayni TD zinciri kullanilir.
 * Onceden usb_poll_all yalniz EHCI/xHCI'yi yokluyordu; UHCI'deki HID
 * klavye/fare hic okunmuyordu. */
static int uhci_interrupt_in(usb_device_t *dev, int len, uint8_t *buf) {
    if (!s_ready || !dev) return -1;
    int ep = 1;
    for (int i = 0; i < dev->endpoint_count; i++) {
        if (dev->endpoints[i].bEndpointAddress & 0x80) {
            ep = dev->endpoints[i].bEndpointAddress & 0x0F;
            break;
        }
    }
    int ls = (dev->speed == USB_SPEED_LOW) ? 1 : 0;
    return uhci_bulk_raw(dev->address, ep, ls, 1 /* IN */, buf, len);
}

int uhci_poll_keyboard(usb_device_t *dev, uint8_t *buf) {
    if (!dev || !dev->is_keyboard) return -1;
    return uhci_interrupt_in(dev, 8, buf);
}

int uhci_poll_mouse(usb_device_t *dev, uint8_t *buf) {
    if (!dev || !dev->is_mouse) return -1;
    return uhci_interrupt_in(dev, 4, buf);
}
