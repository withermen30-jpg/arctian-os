#include "kernel.h"
#include "usb.h"
#include "pci.h"

#define XHCI_CAPLENGTH       0x00
#define XHCI_HCIVERSION      0x02
#define XHCI_HCSPARAMS1      0x04
#define XHCI_HCSPARAMS2      0x08
#define XHCI_HCSPARAMS3      0x0C
#define XHCI_HCCPARAMS1      0x10
#define XHCI_DBOFF           0x14
#define XHCI_RTSOFF          0x18
#define XHCI_HCCPARAMS2      0x1C

#define XHCI_USBCMD          0x00
#define XHCI_USBSTS          0x04
#define XHCI_PAGESIZE        0x08
#define XHCI_DNCTRL          0x10
#define XHCI_CRCR            0x18
#define XHCI_DCBAAP          0x30
#define XHCI_CONFIG          0x38
#define XHCI_PORTREG_SET(n)  (0x400 + ((n) * 0x10))

#define XHCI_PORTSC          0x00
#define XHCI_PORTPMSC        0x04
#define XHCI_PORTLI          0x08

#define XHCI_PORTSC_CCS      (1 << 0)
#define XHCI_PORTSC_PED      (1 << 1)
#define XHCI_PORTSC_RESET    (1 << 4)
#define XHCI_PORTSC_PLS_MASK (0x0F << 5)
#define XHCI_PORTSC_PP       (1 << 9)
#define XHCI_PORTSC_CSC      (1 << 17)
#define XHCI_PORTSC_PRC      (1 << 21)
#define XHCI_PORTSC_WPR      (1 << 31)

#define XHCI_TRB_TYPE_NORMAL         1
#define XHCI_TRB_TYPE_SETUP          2
#define XHCI_TRB_TYPE_DATA           3
#define XHCI_TRB_TYPE_STATUS         4
#define XHCI_TRB_TYPE_ISOCH          5
#define XHCI_TRB_TYPE_LINK           6
#define XHCI_TRB_TYPE_EVENT_DATA     7
#define XHCI_TRB_TYPE_NOOP           8
#define XHCI_TRB_TYPE_ENABLE_SLOT    9
#define XHCI_TRB_TYPE_DISABLE_SLOT   10
#define XHCI_TRB_TYPE_ADDRESS_DEV    11
#define XHCI_TRB_TYPE_CONFIGURE_EP   12
#define XHCI_TRB_TYPE_EVAL_CONTEXT   13
#define XHCI_TRB_TYPE_RESET_EP       14
#define XHCI_TRB_TYPE_STOP_EP        15
#define XHCI_TRB_TYPE_SET_TR_DEQUEUE 16
#define XHCI_TRB_TYPE_RESET_DEV      17
#define XHCI_TRB_TYPE_GET_STATE      250

#define XHCI_TRB_CYCLE     (1 << 0)
#define XHCI_TRB_TC        (1 << 1)
#define XHCI_TRB_CSI       (1 << 2)
#define XHCI_TRB_ENT       (1 << 3)
#define XHCI_TRB_ISP       (1 << 4)
#define XHCI_TRB_NS        (1 << 5)
#define XHCI_TRB_CH        (1 << 6)
#define XHCI_TRB_IOC       (1 << 7)
#define XHCI_TRB_IDT       (1 << 8)
#define XHCI_TRB_TBC       (0x03 << 16)
#define XHCI_TRB_TRT_SHIFT 16
#define XHCI_TRB_TRT_MASK  (0x03 << 16)

#define XHCI_TRB_ERROR_SUCCESS      0
#define XHCI_TRB_ERROR_INVALID      1
#define XHCI_TRB_ERROR_TRB          5
#define XHCI_TRB_ERROR_RESOURCE     6
#define XHCI_TRB_ERROR_RING_FULL    7
#define XHCI_TRB_ERROR_SLOT_NOT_ON  9
#define XHCI_TRB_ERROR_SLOT_PENDING 11
#define XHCI_TRB_ERROR_PARAM        13
#define XHCI_TRB_ERROR_BANDWIDTH    15
#define XHCI_TRB_ERROR_CAPACITY     20

#define XHCI_SLOT_STATE_DISABLED    0
#define XHCI_SLOT_STATE_ENABLED     1
#define XHCI_SLOT_STATE_DEFAULT     2
#define XHCI_SLOT_STATE_ADDRESSED   3
#define XHCI_SLOT_STATE_CONFIGURED  4

#define XHCI_EP_STATE_DISABLED      0
#define XHCI_EP_STATE_RUNNING       1
#define XHCI_EP_STATE_HALTED        2
#define XHCI_EP_STATE_STOPPED       3
#define XHCI_EP_STATE_ERROR         4

#define XHCI_MAX_SLOTS              32
#define XHCI_MAX_PORTS              32
#define XHCI_TRB_RING_SIZE          256

#pragma pack(push, 1)
typedef struct {
    uint64_t ptr;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

typedef struct {
    uint32_t dw[4];
} xhci_input_ctrl_t;

typedef struct {
    uint32_t dw[4];
} xhci_slot_ctx_t;

typedef struct {
    uint32_t dw[8];
} xhci_ep_ctx_t;

typedef struct {
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t eps[31];
} xhci_dev_ctx_t;

typedef struct {
    xhci_input_ctrl_t input_ctrl;
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t eps[31];
} xhci_input_ctx_t;

typedef struct {
    uint64_t ptrs[4];
    uint32_t res[8];
    uint16_t evt_seg_size;
    uint16_t res2;
    uint32_t res3;
} xhci_erst_seg_t;

typedef struct {
    xhci_trb_t ring[XHCI_TRB_RING_SIZE];
} xhci_trb_ring_t;
#pragma pack(pop)

typedef struct {
    volatile uint32_t *op_regs;
    volatile uint32_t *port_regs;
    volatile uint32_t *doorbell;
    volatile uint32_t *rts_regs;
    uint64_t mmio_base;
    uint64_t rts_offset;
    uint64_t db_offset;

    int n_slots;
    int n_ports;
    int max_ports;
    int page_size;

    xhci_dev_ctx_t **dev_ctx;
    uint64_t *dev_ctx_phys;
    xhci_input_ctx_t **input_ctx;
    uint64_t *input_ctx_phys;

    xhci_trb_ring_t *cmd_ring;
    uint64_t cmd_ring_phys;
    int cmd_cycle;

    xhci_trb_ring_t *evt_ring;
    uint64_t evt_ring_phys;
    xhci_erst_seg_t *erst;
    uint64_t erst_phys;
    int evt_cycle;
    int evt_idx;

    uint8_t pci_irq;
} xhci_controller_t;

static xhci_controller_t xhci;
static bool xhci_ok = false;
static usb_device_t xhci_devices[XHCI_MAX_SLOTS];
static int xhci_dev_count = 0;
static int xhci_slot_map[XHCI_MAX_SLOTS];

static uint32_t xhci_read32(uint64_t base, uint32_t reg) {
    return *(volatile uint32_t*)(uintptr_t)(base + reg);
}

static void xhci_write32(uint64_t base, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(uintptr_t)(base + reg) = val;
}

static uint32_t xhci_read_op32(uint32_t reg) {
    return xhci_read32(xhci.mmio_base, reg);
}

static void xhci_write_op32(uint32_t reg, uint32_t val) {
    xhci_write32(xhci.mmio_base, reg, val);
}

static uint32_t xhci_read_port32(int port, uint32_t reg) {
    return xhci_read32(xhci.mmio_base, XHCI_PORTREG_SET(port) + reg);
}

static void xhci_write_port32(int port, uint32_t reg, uint32_t val) {
    xhci_write32(xhci.mmio_base, XHCI_PORTREG_SET(port) + reg, val);
}

static void xhci_doorbell(int slot, int doorbell) {
    xhci_write32(xhci.mmio_base, xhci.db_offset, (uint32_t)((doorbell << 0) | ((slot & 0xFF) << 8)));
}

static uint64_t xhci_virt_to_phys(void *p) {
    return (uint64_t)(uintptr_t)p;
}

static void xhci_wait_op_bit(uint32_t reg, uint32_t mask, bool set, int timeout) {
    while (timeout--) {
        if ((xhci_read_op32(reg) & mask) ? set : !set)
            return;
    }
}

static void xhci_command_trb(xhci_trb_t *trb) {
    int idx = 0;
    xhci_trb_t *slot = &xhci.cmd_ring->ring[idx];

    slot->ptr = trb->ptr;
    slot->status = trb->status;
    slot->control = trb->control | (xhci.cmd_cycle ? 1 : 0);

    xhci_doorbell(0, 0);

    for (volatile int t = 0; t < 1000000; t++) {
        volatile xhci_trb_t *evt = &xhci.evt_ring->ring[xhci.evt_idx];
        if ((evt->control & 1) == (xhci.evt_cycle ? 1 : 0)) {
            xhci.evt_idx = (xhci.evt_idx + 1) % XHCI_TRB_RING_SIZE;
            if (xhci.evt_idx == 0)
                xhci.evt_cycle = !xhci.evt_cycle;
            break;
        }
        asm volatile ("pause");
    }
}

#define XHCI_MAX_EP_RINGS  16

typedef struct {
    xhci_trb_ring_t *ring;
    uint64_t         ring_phys;
    int              cycle;
    int              enqueue_idx;
    int              dq_idx;
} xhci_transfer_ring_t;

static xhci_transfer_ring_t xhci_ep_rings[XHCI_MAX_SLOTS][XHCI_MAX_EP_RINGS];
static bool xhci_ep_rings_init[XHCI_MAX_SLOTS][XHCI_MAX_EP_RINGS];
static xhci_trb_ring_t ep_ring_storage[XHCI_MAX_SLOTS][XHCI_MAX_EP_RINGS];

static int xhci_ensure_ep_ring(int slot_id, int ep_idx) {
    if (slot_id < 0 || slot_id >= XHCI_MAX_SLOTS) return -1;
    if (ep_idx < 0 || ep_idx >= XHCI_MAX_EP_RINGS) return -1;

    if (xhci_ep_rings_init[slot_id][ep_idx])
        return 0;

    xhci_trb_ring_t *ring = &ep_ring_storage[slot_id][ep_idx];
    memset(ring, 0, sizeof(xhci_trb_ring_t));

    xhci_ep_rings[slot_id][ep_idx].ring = ring;
    xhci_ep_rings[slot_id][ep_idx].ring_phys = xhci_virt_to_phys(ring);
    xhci_ep_rings[slot_id][ep_idx].cycle = 1;
    xhci_ep_rings[slot_id][ep_idx].enqueue_idx = 0;
    xhci_ep_rings[slot_id][ep_idx].dq_idx = 0;

    ring->ring[XHCI_TRB_RING_SIZE - 1].ptr = xhci_ep_rings[slot_id][ep_idx].ring_phys;
    ring->ring[XHCI_TRB_RING_SIZE - 1].control = (6 << 10) | 1;

    xhci_ep_rings_init[slot_id][ep_idx] = true;
    kernel_debug("xHCI: EP ring initialized slot=%d ep=%d phys=0x%llX",
                 slot_id, ep_idx, xhci_ep_rings[slot_id][ep_idx].ring_phys);
    return 0;
}

static void xhci_ring_enqueue_trb(int slot_id, int ep_idx, xhci_trb_t *trb) {
    if (slot_id < 0 || slot_id >= XHCI_MAX_SLOTS) return;
    if (ep_idx < 0 || ep_idx >= XHCI_MAX_EP_RINGS) return;

    xhci_transfer_ring_t *tring = &xhci_ep_rings[slot_id][ep_idx];
    int idx = tring->enqueue_idx;

    trb->control |= (tring->cycle ? XHCI_TRB_CYCLE : 0);

    xhci_trb_t *slot = &tring->ring->ring[idx];
    slot->ptr = trb->ptr;
    slot->status = trb->status;
    slot->control = trb->control;

    tring->enqueue_idx = (idx + 1) % XHCI_TRB_RING_SIZE;
    if (tring->enqueue_idx == 0)
        tring->cycle = !tring->cycle;
}

__attribute__((unused)) static void xhci_set_ep_ring_dequeue_ptr(int slot_id, int ep_idx, uint64_t ptr, int cycle) {
    if (slot_id < 0 || slot_id >= XHCI_MAX_SLOTS) return;
    if (ep_idx < 0 || ep_idx >= XHCI_MAX_EP_RINGS) return;

    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.ptr = ptr;
    trb.status = 0;
    trb.control = (slot_id << 24) | (16 << 10) | (xhci.cmd_cycle ? 1 : 0)
                  | (cycle ? 1 : 0);
    xhci_command_trb(&trb);
}

static int xhci_submit_transfer_trb_chain(int slot_id, int ep_idx,
                                           xhci_trb_t *trbs, int n_trbs,
                                           int timeout) {
    if (slot_id < 0 || slot_id >= XHCI_MAX_SLOTS) return -1;
    if (ep_idx < 0 || ep_idx >= XHCI_MAX_EP_RINGS) return -1;
    if (n_trbs <= 0) return -1;

    xhci_transfer_ring_t *tring = &xhci_ep_rings[slot_id][ep_idx];

    for (int i = 0; i < n_trbs; i++) {
        if (i == n_trbs - 1) {
            trbs[i].control |= XHCI_TRB_IOC;
            trbs[i].control &= ~XHCI_TRB_CH;
        } else {
            trbs[i].control |= XHCI_TRB_CH;
            trbs[i].control &= ~XHCI_TRB_IOC;
        }
        xhci_ring_enqueue_trb(slot_id, ep_idx, &trbs[i]);
    }

    xhci_doorbell(slot_id, ep_idx);

    int found = 0;
    for (volatile int t = 0; t < timeout; t++) {
        volatile xhci_trb_t *evt = &xhci.evt_ring->ring[xhci.evt_idx];
        if ((evt->control & 1) == (xhci.evt_cycle ? 1 : 0)) {
            if (evt->status != 0) {
                found = -1;
                break;
            }
            xhci.evt_idx = (xhci.evt_idx + 1) % XHCI_TRB_RING_SIZE;
            if (xhci.evt_idx == 0)
                xhci.evt_cycle = !xhci.evt_cycle;
            found = 1;
            break;
        }
        asm volatile ("pause");
    }

    if (!found) {
        kernel_debug("xHCI: TRB chain timeout slot=%d ep=%d", slot_id, ep_idx);
        return -1;
    }

    return 0;
}

static int xhci_get_slot_id(void) {
    for (int i = 1; i < xhci.n_slots; i++) {
        if (xhci_slot_map[i] == -1) return i;
    }
    return -1;
}

__attribute__((unused)) static void xhci_enable_slot(void) {
    xhci_trb_t trb;
    trb.ptr = 0;
    trb.status = 0;
    trb.control = (8 << 10) | (xhci.cmd_cycle ? 1 : 0);
    xhci_command_trb(&trb);
}

static int xhci_address_device(int slot_id, int port) {
    xhci_input_ctx_t *ictx = xhci.input_ctx[slot_id];
    memset(ictx, 0, sizeof(xhci_input_ctx_t));

    ictx->input_ctrl.dw[0] = 0x03;
    ictx->input_ctrl.dw[1] = 0x00;

    ictx->slot.dw[0] = (3 << 20) | (port + 1);
    ictx->slot.dw[1] = 0;
    ictx->slot.dw[2] = 0;
    ictx->slot.dw[3] = 0;

    ictx->eps[0].dw[0] = (1 << 7);
    ictx->eps[0].dw[1] = (0 << 3) | (1 << 5) | (0 << 6);
    ictx->eps[0].dw[2] = 0;
    ictx->eps[0].dw[3] = 0;
    ictx->eps[0].dw[4] = 0;
    ictx->eps[0].dw[5] = 0;
    ictx->eps[0].dw[6] = 0;
    ictx->eps[0].dw[7] = 0;

    xhci_dev_ctx_t *dctx = (xhci_dev_ctx_t *)((uintptr_t)xhci.dev_ctx_phys[slot_id]);
    memset(dctx, 0, sizeof(xhci_dev_ctx_t));

    xhci_trb_t trb;
    trb.ptr = xhci_virt_to_phys(ictx);
    trb.status = 0;
    trb.control = (slot_id << 24) | (11 << 10) | (xhci.cmd_cycle ? 1 : 0);
    xhci_command_trb(&trb);

    for (volatile int t = 0; t < 100000; t++) asm volatile ("pause");
    return 0;
}

__attribute__((unused)) static int xhci_configure_endpoint(int slot_id, int ep_idx,
                                   uint8_t ep_type, uint16_t max_pkt,
                                   uint8_t ep_addr) {
    xhci_input_ctx_t *ictx = xhci.input_ctx[slot_id];
    memset(ictx, 0, sizeof(xhci_input_ctx_t));

    ictx->input_ctrl.dw[0] = 0x00;
    ictx->input_ctrl.dw[1] = 0x00;
    ictx->input_ctrl.dw[2] = (1 << (ep_idx + 2));

    int ep_ctx_id = ep_idx + 1;
    ictx->eps[ep_ctx_id].dw[0] = (1 << 7) | ((ep_type & 3) << 3) | (max_pkt << 16);
    ictx->eps[ep_ctx_id].dw[1] = (ep_addr & 0x0F) | ((ep_addr & 0x80) >> 7);
    ictx->eps[ep_ctx_id].dw[2] = (max_pkt << 16);
    ictx->eps[ep_ctx_id].dw[3] = 0;
    ictx->eps[ep_ctx_id].dw[4] = 0;
    ictx->eps[ep_ctx_id].dw[5] = 0;
    ictx->eps[ep_ctx_id].dw[6] = 0;
    ictx->eps[ep_ctx_id].dw[7] = 0;

    xhci_trb_t trb;
    trb.ptr = xhci_virt_to_phys(ictx);
    trb.status = 0;
    trb.control = (slot_id << 24) | (12 << 10) | (xhci.cmd_cycle ? 1 : 0);
    xhci_command_trb(&trb);

    return 0;
}

static int xhci_evaluate_context(int slot_id, uint16_t max_pkt) {
    xhci_input_ctx_t *ictx = xhci.input_ctx[slot_id];
    memset(ictx, 0, sizeof(xhci_input_ctx_t));

    ictx->input_ctrl.dw[0] = 0x03;
    ictx->input_ctrl.dw[1] = 0x00;

    ictx->slot.dw[0]  = xhci_slot_map[slot_id];
    ictx->slot.dw[1]  = 0;
    ictx->slot.dw[2]  = 0;
    ictx->slot.dw[3]  = 0;

    ictx->eps[0].dw[0] = (1 << 7) | (0 << 3) | (1 << 5) | (0 << 6) | (max_pkt << 16);
    ictx->eps[0].dw[1] = (0 << 0) | (0 << 2);
    ictx->eps[0].dw[2] = 0;
    ictx->eps[0].dw[3] = 0;
    ictx->eps[0].dw[4] = 0;
    ictx->eps[0].dw[5] = 0;
    ictx->eps[0].dw[6] = 0;
    ictx->eps[0].dw[7] = 0;

    xhci_trb_t trb;
    trb.ptr = xhci_virt_to_phys(ictx);
    trb.status = 0;
    trb.control = (slot_id << 24) | (13 << 10) | (xhci.cmd_cycle ? 1 : 0);
    xhci_command_trb(&trb);

    return 0;
}

__attribute__((unused)) static void xhci_set_max_packet(xhci_input_ctx_t *ictx, uint16_t max_pkt) {
    ictx->eps[0].dw[0] &= ~0xFFFF0000;
    ictx->eps[0].dw[0] |= ((uint32_t)max_pkt << 16);
}

__attribute__((unused)) static int xhci_get_descriptor(int slot_id, uint8_t type, uint8_t *buf, int len) {
    (void)slot_id;
    (void)type;
    (void)buf;
    (void)len;
    return -1;
}

static int xhci_enum_port(xhci_controller_t *ctrl, int port, usb_controller_t *pcictrl) {
    uint32_t portsc = xhci_read_port32(port, XHCI_PORTSC);
    if (!(portsc & XHCI_PORTSC_CCS)) return -1;

    kernel_debug("xHCI: Port %d cihaz var, resetleniyor...", port);

    portsc |= XHCI_PORTSC_RESET;
    xhci_write_port32(port, XHCI_PORTSC, portsc);
    for (volatile int i = 0; i < 50000; i++) asm volatile ("pause");

    portsc &= ~XHCI_PORTSC_RESET;
    xhci_write_port32(port, XHCI_PORTSC, portsc);
    for (volatile int i = 0; i < 100000; i++) asm volatile ("pause");

    int slot_id = xhci_get_slot_id();
    if (slot_id < 0) return -1;

    xhci_slot_map[slot_id] = port;

    xhci_address_device(slot_id, port);

    usb_device_t *dev = &xhci_devices[xhci_dev_count];
    memset(dev, 0, sizeof(usb_device_t));
    dev->xhci_slot_id = slot_id;
    dev->controller_port = port;
    dev->address = (uint8_t)(xhci_dev_count + 1);
    dev->speed = USB_SPEED_SUPER;
    dev->max_packet_size = 64;
    dev->state = USB_DEVSTATE_ADDRESSED;

    usb_setup_packet_t setup;
    uint8_t buf[18];

    setup.bmRequestType = USB_DIR_IN | (USB_REQTYPE_STANDARD << 5) | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_DEVICE << 8);
    setup.wIndex = 0;
    setup.wLength = 18;

    dev->max_packet_size = 512;

    int ret = xhci_control_transfer(dev, &setup, buf, USB_DIR_IN);
    if (ret != 0) {
        kernel_debug("xHCI: Cihaz descriptor okunamadi slot=%d", slot_id);
        xhci_slot_map[slot_id] = -1;
        return -1;
    }

    usb_device_descriptor_t *dd = (usb_device_descriptor_t *)buf;
    dev->max_packet_size = dd->bMaxPacketSize0;
    dev->class_code = dd->bDeviceClass;
    dev->subclass = dd->bDeviceSubClass;
    dev->protocol = dd->bDeviceProtocol;
    dev->vendor_id = dd->idVendor;
    dev->product_id = dd->idProduct;

    xhci_evaluate_context(slot_id, dev->max_packet_size);

    uint8_t cfg_buf[9];
    setup.bmRequestType = USB_DIR_IN | (USB_REQTYPE_STANDARD << 5) | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_CONFIG << 8);
    setup.wIndex = 0;
    setup.wLength = 9;

    if (xhci_control_transfer(dev, &setup, cfg_buf, USB_DIR_IN) != 0) {
        kernel_debug("xHCI: Config descriptor okunamadi");
        return -1;
    }

    usb_config_descriptor_t *cfg = (usb_config_descriptor_t *)cfg_buf;
    uint16_t total_len = cfg->wTotalLength;
    if (total_len > 512) total_len = 512;

    uint8_t full_cfg[512];
    setup.wLength = total_len;

    if (xhci_control_transfer(dev, &setup, full_cfg, USB_DIR_IN) != 0)
        return -1;

    uint8_t *p = full_cfg + 9;
    uint8_t *end = full_cfg + total_len;

    while (p < end) {
        uint8_t dlen = p[0];
        if (dlen == 0) break;

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
        p += dlen;
    }

    setup.bmRequestType = USB_DIR_OUT | (USB_REQTYPE_STANDARD << 5) | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_SET_CONFIGURATION;
    setup.wValue = cfg->bConfigurationValue;
    setup.wIndex = 0;
    setup.wLength = 0;
    xhci_control_transfer(dev, &setup, 0, USB_DIR_OUT);

    dev->state = USB_DEVSTATE_CONFIGURED;

    if (dev->class_code == USB_CLASS_HID &&
        (dev->protocol == USB_PROTOCOL_KEYBOARD || dev->protocol == USB_PROTOCOL_MOUSE)) {
        for (int e = 0; e < dev->endpoint_count; e++) {
            usb_endpoint_descriptor_t *ep = &dev->endpoints[e];
            if ((ep->bEndpointAddress & 0x80) &&
                (ep->bmAttributes & 0x03) == USB_EP_TYPE_INTERRUPT) {
                int ep_idx = ep->bEndpointAddress & 0x0F;
                kernel_debug("xHCI: Config interrupt EP%d slot=%d maxpkt=%d",
                              ep_idx, slot_id, ep->wMaxPacketSize);

                xhci_ensure_ep_ring(slot_id, ep_idx);

                uint64_t ring_phys = xhci_virt_to_phys(&ep_ring_storage[slot_id][ep_idx]);
                int dcs = 1;
                xhci_input_ctx_t *ictx = xhci.input_ctx[slot_id];
                memset(ictx, 0, sizeof(xhci_input_ctx_t));

                ictx->input_ctrl.dw[0] = 0x00;
                ictx->input_ctrl.dw[1] = 0x00;
                ictx->input_ctrl.dw[2] = (1 << (ep_idx + 2));

                ictx->eps[ep_idx].dw[0] = (1 << 7)
                                         | ((ep->bmAttributes & 0x03) << 3)
                                         | (ep->wMaxPacketSize << 16);
                ictx->eps[ep_idx].dw[1] = (ep->bEndpointAddress & 0x0F)
                                         | ((ep->bEndpointAddress & 0x80) >> 7);
                ictx->eps[ep_idx].dw[2] = (uint32_t)(ring_phys & 0xFFFFFFFF);
                ictx->eps[ep_idx].dw[3] = (uint32_t)(ring_phys >> 32);
                ictx->eps[ep_idx].dw[4] = 0;
                ictx->eps[ep_idx].dw[5] = 0;
                ictx->eps[ep_idx].dw[6] = 0;
                ictx->eps[ep_idx].dw[7] = 0;

                xhci_trb_t trb;
                memset(&trb, 0, sizeof(trb));
                trb.ptr = xhci_virt_to_phys(ictx);
                trb.status = 0;
                trb.control = (slot_id << 24) | (12 << 10) | (xhci.cmd_cycle ? 1 : 0);
                xhci_command_trb(&trb);

                break;
            }
        }
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

    xhci_dev_count++;
    return 0;
}

int xhci_init(usb_controller_t *ctrl) {
    memset(&xhci, 0, sizeof(xhci_controller_t));

    xhci.pci_irq = ctrl->dev.irq;

    int bar_idx = 0;
    for (int i = 0; i < 6; i++) {
        if (!ctrl->bar[i].is_io && ctrl->bar[i].address != 0) {
            bar_idx = i;
            break;
        }
    }

    xhci.mmio_base = ctrl->bar[bar_idx].address;

    uint8_t cap_length = *(volatile uint8_t *)(uintptr_t)xhci.mmio_base;

    xhci.db_offset = xhci_read32(xhci.mmio_base, 0x14);
    xhci.rts_offset = xhci_read32(xhci.mmio_base, 0x18);

    xhci.op_regs = (volatile uint32_t *)(uintptr_t)(xhci.mmio_base + cap_length);

    uint32_t hcsp1 = xhci_read32(xhci.mmio_base, XHCI_HCSPARAMS1);
    uint32_t hcsp2 = xhci_read32(xhci.mmio_base, XHCI_HCSPARAMS2);
    xhci.n_slots = hcsp1 & 0xFF;
    xhci.n_ports = (hcsp1 >> 24) & 0xFF;
    xhci.max_ports = (hcsp2 >> 24) & 0xFF;
    if (xhci.max_ports == 0) xhci.max_ports = XHCI_MAX_PORTS;
    if (xhci.n_ports > xhci.max_ports) xhci.n_ports = xhci.max_ports;

    uint32_t pagesize = xhci_read_op32(XHCI_PAGESIZE);
    xhci.page_size = 4096;
    if (pagesize & 0x01) xhci.page_size = 4096;

    kernel_debug("xHCI: mmio=0x%llX cap_len=%d slots=%d ports=%d irq=%d",
                 xhci.mmio_base, cap_length, xhci.n_slots, xhci.n_ports, xhci.pci_irq);

    xhci_write_op32(XHCI_USBCMD, XHCI_CMD_HC_RESET);
    xhci_wait_op_bit(XHCI_USBCMD, XHCI_CMD_HC_RESET, false, 100000);

    xhci_write_op32(XHCI_USBCMD, XHCI_CMD_RUN_STOP);
    xhci_wait_op_bit(XHCI_USBSTS, XHCI_STS_HCHALTED, false, 100000);

    xhci_write_op32(XHCI_CONFIG, xhci.n_slots);

    static xhci_dev_ctx_t dev_ctx_storage[XHCI_MAX_SLOTS];
    static xhci_input_ctx_t input_ctx_storage[XHCI_MAX_SLOTS];
    static uint64_t dcbaap_storage[XHCI_MAX_SLOTS + 1];
    static xhci_trb_ring_t cmd_ring_storage;
    static xhci_trb_ring_t evt_ring_storage;
    static xhci_erst_seg_t erst_storage;

    xhci.dev_ctx = (xhci_dev_ctx_t **)dev_ctx_storage;
    xhci.dev_ctx_phys = dcbaap_storage;
    xhci.input_ctx = (xhci_input_ctx_t **)input_ctx_storage;
    xhci.input_ctx_phys = (uint64_t *)&input_ctx_storage;

    for (int i = 0; i < XHCI_MAX_SLOTS; i++) {
        xhci.dev_ctx_phys[i] = xhci_virt_to_phys(&dev_ctx_storage[i]);
        xhci.dev_ctx[i] = &dev_ctx_storage[i];
        xhci.input_ctx[i] = &input_ctx_storage[i];
    }

    xhci_slot_map[0] = -1;
    for (int i = 1; i < XHCI_MAX_SLOTS; i++) xhci_slot_map[i] = -1;

    xhci.cmd_ring = &cmd_ring_storage;
    memset(xhci.cmd_ring, 0, sizeof(xhci_trb_ring_t));
    xhci.cmd_ring_phys = xhci_virt_to_phys(xhci.cmd_ring);
    xhci.cmd_ring->ring[255].ptr = xhci.cmd_ring_phys;
    xhci.cmd_ring->ring[255].control = (6 << 10) | 1;
    xhci.cmd_cycle = 1;

    xhci.evt_ring = &evt_ring_storage;
    memset(xhci.evt_ring, 0, sizeof(xhci_trb_ring_t));
    xhci.evt_ring_phys = xhci_virt_to_phys(xhci.evt_ring);
    xhci.evt_cycle = 1;
    xhci.evt_idx = 0;

    xhci.erst = &erst_storage;
    memset(xhci.erst, 0, sizeof(xhci_erst_seg_t));
    xhci.erst_phys = xhci_virt_to_phys(xhci.erst);
    xhci.erst->ptrs[0] = xhci.evt_ring_phys;
    xhci.erst->evt_seg_size = XHCI_TRB_RING_SIZE;

    xhci_write_op32(XHCI_CRCR, (uint32_t)(xhci.cmd_ring_phys | (xhci.cmd_cycle ? 1 : 0)));
    xhci_write_op32(XHCI_CRCR + 4, (uint32_t)(xhci.cmd_ring_phys >> 32));
    xhci_write_op32(XHCI_DCBAAP, (uint32_t)(xhci_virt_to_phys(dcbaap_storage)));
    xhci_write_op32(XHCI_DCBAAP + 4, (uint32_t)(xhci_virt_to_phys(dcbaap_storage) >> 32));

    uint64_t rts_base = xhci.mmio_base + xhci.rts_offset;
    xhci.rts_regs = (volatile uint32_t *)(uintptr_t)rts_base;

    uint64_t erst_addr = xhci.erst_phys;
    volatile uint32_t *erstsz = (volatile uint32_t *)(uintptr_t)(rts_base + 0x18);
    volatile uint32_t *erstba_lo = (volatile uint32_t *)(uintptr_t)(rts_base + 0x28);
    volatile uint32_t *erstba_hi = (volatile uint32_t *)(uintptr_t)(rts_base + 0x2C);
    volatile uint32_t *erdp_lo = (volatile uint32_t *)(uintptr_t)(rts_base + 0x38);
    volatile uint32_t *erdp_hi = (volatile uint32_t *)(uintptr_t)(rts_base + 0x3C);

    *erstsz = 1;
    *erstba_lo = (uint32_t)(erst_addr & 0xFFFFFFFF);
    *erstba_hi = (uint32_t)(erst_addr >> 32);
    *erdp_lo = (uint32_t)(xhci.evt_ring_phys & 0xFFFFFFFF);
    *erdp_hi = (uint32_t)(xhci.evt_ring_phys >> 32);

    xhci.db_offset = xhci.db_offset;
    xhci.doorbell = (volatile uint32_t *)(uintptr_t)(xhci.mmio_base + xhci.db_offset);

    xhci.port_regs = (volatile uint32_t *)(uintptr_t)(xhci.op_regs + (XHCI_PORTREG_SET(0) / 4));

    xhci_ok = true;

    for (int i = 0; i < xhci.n_ports; i++) {
        xhci_enum_port(&xhci, i, ctrl);
    }

    return 0;
}

static int xhci_find_slot_for_device(usb_device_t *dev) {
    if (!dev) return -1;
    return dev->xhci_slot_id;
}

int xhci_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup_buf,
                          uint8_t *data, int dir) {
    if (!xhci_ok || !dev) return -1;

    int slot_id = xhci_find_slot_for_device(dev);
    if (slot_id < 1) {
        kernel_debug("xHCI ctrl: slot bulunamadi");
        return -1;
    }

    if (xhci_ensure_ep_ring(slot_id, 0) != 0)
        return -1;

    int maxpkt = dev->max_packet_size ? dev->max_packet_size : 64;
    int data_len = setup_buf->wLength;
    if (data_len > 1024) data_len = 1024;

    uint8_t setup_raw[8];
    memcpy(setup_raw, setup_buf, 8);

    int trb_count = (data_len > 0 && data) ? 3 : 2;
    xhci_trb_t trbs[3];
    memset(trbs, 0, sizeof(trbs));

    trbs[0].ptr = xhci_virt_to_phys(setup_raw);
    trbs[0].status = 0;
    trbs[0].control = (8 << 17)
                    | (2 << 16)
                    | (XHCI_TRB_TYPE_SETUP << 10)
                    | XHCI_TRB_IOC;

    int idx = 1;

    if (data_len > 0 && data) {
        trbs[idx].ptr = xhci_virt_to_phys(data);
        trbs[idx].status = (data_len << 17);
        trbs[idx].control = ((dir == USB_DIR_IN ? 1 : 0) << 16)
                          | (XHCI_TRB_TYPE_DATA << 10)
                          | XHCI_TRB_IOC;
        if (dir == USB_DIR_IN)
            trbs[idx].control |= XHCI_TRB_ISP;
        idx++;
    }

    trbs[idx].ptr = 0;
    trbs[idx].status = 0;
    trbs[idx].control = ((dir == USB_DIR_IN ? 0 : 1) << 16)
                      | (XHCI_TRB_TYPE_STATUS << 10)
                      | XHCI_TRB_IOC;
    if (data_len == 0 || !data) {
        trbs[idx].control |= (1 << 16);
    }
    idx++;

    return xhci_submit_transfer_trb_chain(slot_id, 0, trbs, idx, 5000000);
}

int xhci_interrupt_transfer(usb_device_t *dev, uint8_t *buf, int len) {
    if (!xhci_ok || !dev) return -1;

    int slot_id = xhci_find_slot_for_device(dev);
    if (slot_id < 1) return -1;

    int ep_idx = 0;
    for (int i = 0; i < dev->endpoint_count; i++) {
        if ((dev->endpoints[i].bEndpointAddress & 0x80) &&
            (dev->endpoints[i].bmAttributes & 0x03) == USB_EP_TYPE_INTERRUPT) {
            ep_idx = dev->endpoints[i].bEndpointAddress & 0x0F;
            break;
        }
    }
    if (ep_idx == 0) {
        ep_idx = 1;
    }

    if (xhci_ensure_ep_ring(slot_id, ep_idx) != 0)
        return -1;

    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.ptr = xhci_virt_to_phys(buf);
    trb.status = (len << 17);
    trb.control = (XHCI_TRB_TYPE_NORMAL << 10)
                | XHCI_TRB_IOC
                | XHCI_TRB_ISP;

    return xhci_submit_transfer_trb_chain(slot_id, ep_idx, &trb, 1, 1000000);
}

int xhci_poll_keyboard(usb_device_t *dev, uint8_t *buf) {
    if (!xhci_ok || !dev || !dev->is_keyboard) return -1;
    memset(buf, 0, 8);
    return xhci_interrupt_transfer(dev, buf, 8);
}

int xhci_poll_mouse(usb_device_t *dev, uint8_t *buf) {
    if (!xhci_ok || !dev || !dev->is_mouse) return -1;
    memset(buf, 0, 4);
    return xhci_interrupt_transfer(dev, buf, 4);
}
