#include "kernel.h"
#include "pcnet.h"
#include "pci.h"
#include "net.h"


#define PCNET_RDP     0x10
#define PCNET_RAP     0x12
#define PCNET_RESET   0x14
#define PCNET_BDP     0x16

#define PCNET_RX_COUNT   16
#define PCNET_TX_COUNT   8
#define PCNET_BUF_SIZE   2048

#define CSR0_INIT   0x0001
#define CSR0_STRT   0x0002
#define CSR0_STOP   0x0004
#define CSR0_TDMD   0x0008
#define CSR0_IDON   0x0100
#define CSR0_ERR    0x8000

#define BCR_SWSTYLE  20

#define DESC_OWN    0x8000
#define RX_ERR      0x4000
#define RX_STP      0x0200
#define RX_ENP      0x0100
#define TX_STP      0x0200
#define TX_ENP      0x0100

typedef struct {
    uint32_t rbadr;
    int16_t  buf_length;
    int16_t  status;
    uint32_t msg_length;
    uint32_t res;
} __attribute__((packed)) pcnet_rx_desc_t;

typedef struct {
    uint32_t tbadr;
    int16_t  length;
    int16_t  status;
    uint32_t misc;
    uint32_t res;
} __attribute__((packed)) pcnet_tx_desc_t;

typedef struct {
    uint16_t mode;
    uint8_t  rlen;
    uint8_t  tlen;
    uint16_t padr[3];
    uint16_t _res;
    uint16_t ladrf[4];
    uint32_t rdra;
    uint32_t tdra;
} __attribute__((packed)) pcnet_init_block_t;

typedef struct {
    uint16_t io_base;
    uint8_t  mac[6];
    uint8_t  irq;
    int      rx_idx;
    int      tx_idx;
    bool     active;
} pcnet_dev_t;

static pcnet_dev_t pcnet;

__attribute__((aligned(16))) static pcnet_rx_desc_t rx_descs[PCNET_RX_COUNT];
__attribute__((aligned(16))) static pcnet_tx_desc_t tx_descs[PCNET_TX_COUNT];
__attribute__((aligned(16))) static uint8_t rx_buffers[PCNET_RX_COUNT][PCNET_BUF_SIZE];
__attribute__((aligned(16))) static uint8_t tx_buffers[PCNET_TX_COUNT][PCNET_BUF_SIZE];
__attribute__((aligned(16))) static pcnet_init_block_t init_block;

static void pcnet_write_csr(uint8_t csr, uint16_t val) {
    outw(pcnet.io_base + PCNET_RAP, (uint16_t)(csr & 0x7F));
    outw(pcnet.io_base + PCNET_RDP, val);
}

static uint16_t pcnet_read_csr(uint8_t csr) {
    outw(pcnet.io_base + PCNET_RAP, (uint16_t)(csr & 0x7F));
    return inw(pcnet.io_base + PCNET_RDP);
}

static void pcnet_write_bcr(uint8_t bcr, uint16_t val) {
    outw(pcnet.io_base + PCNET_RAP, (uint16_t)(bcr & 0x7F));
    outw(pcnet.io_base + PCNET_BDP, val);
}

static uint16_t pcnet_read_bcr(uint8_t bcr) {
    outw(pcnet.io_base + PCNET_RAP, (uint16_t)(bcr & 0x7F));
    return inw(pcnet.io_base + PCNET_BDP);
}

void pcnet_init(net_controller_t *nc) {
    pcnet.active = false;
    pcnet.io_base = 0;

    if (nc->bar[0].is_io && nc->bar[0].address != 0) {
        pcnet.io_base = (uint16_t)(nc->bar[0].address & 0xFFF0);
    } else if (nc->bar[1].is_io && nc->bar[1].address != 0) {
        pcnet.io_base = (uint16_t)(nc->bar[1].address & 0xFFF0);
    } else {
        kernel_debug("PCNET: IO port bulunamadi!");
        return;
    }

    pcnet.irq = nc->dev.irq;
    pcnet.rx_idx = 0;
    pcnet.tx_idx = 0;

    kernel_debug("PCNET: IO=0x%X IRQ=%d", pcnet.io_base, pcnet.irq);

    inw(pcnet.io_base + PCNET_RESET);
    for (volatile int i = 0; i < 100000; i++) asm volatile ("pause");

    for (int i = 0; i < 6; i++) pcnet.mac[i] = inb(pcnet.io_base + i);

    bool mac_zero = true, mac_ff = true;
    for (int i = 0; i < 6; i++) {
        if (pcnet.mac[i] != 0x00) mac_zero = false;
        if (pcnet.mac[i] != 0xFF) mac_ff = false;
    }
    if (mac_zero || mac_ff) {
        kernel_debug("PCNET: APROM gecersiz, yerel yonetimli MAC kullaniliyor");
        pcnet.mac[0] = 0x02; pcnet.mac[1] = 0x00; pcnet.mac[2] = 0x00;
        pcnet.mac[3] = 0x00; pcnet.mac[4] = 0x00; pcnet.mac[5] = 0x01;
    }

    kernel_debug("PCNET: MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                 pcnet.mac[0], pcnet.mac[1], pcnet.mac[2],
                 pcnet.mac[3], pcnet.mac[4], pcnet.mac[5]);

    pcnet_write_csr(0, CSR0_STOP);
    pcnet_write_csr(4, 0x0915);
    pcnet_write_bcr(BCR_SWSTYLE, 0x0002);

    for (int i = 0; i < PCNET_RX_COUNT; i++) {
        rx_descs[i].rbadr = (uint32_t)(uintptr_t)rx_buffers[i];
        rx_descs[i].buf_length =
            (int16_t)(0xF000 | ((4096 - PCNET_BUF_SIZE) & 0x0FFF));
        rx_descs[i].status = (int16_t)DESC_OWN;
        rx_descs[i].msg_length = 0;
        rx_descs[i].res = 0;
    }

    for (int i = 0; i < PCNET_TX_COUNT; i++) {
        tx_descs[i].tbadr = 0;
        tx_descs[i].length = (int16_t)0xF000;
        tx_descs[i].status = 0;
        tx_descs[i].misc = 0;
        tx_descs[i].res = 0;
    }

    init_block.mode = 0x0000;
    init_block.rlen = (uint8_t)(4 << 4);
    init_block.tlen = (uint8_t)(3 << 4);
    init_block.padr[0] = (uint16_t)(pcnet.mac[0] | (pcnet.mac[1] << 8));
    init_block.padr[1] = (uint16_t)(pcnet.mac[2] | (pcnet.mac[3] << 8));
    init_block.padr[2] = (uint16_t)(pcnet.mac[4] | (pcnet.mac[5] << 8));
    init_block._res = 0;
    for (int i = 0; i < 4; i++) init_block.ladrf[i] = 0;
    init_block.rdra = (uint32_t)(uintptr_t)rx_descs;
    init_block.tdra = (uint32_t)(uintptr_t)tx_descs;

    uint32_t ib = (uint32_t)(uintptr_t)&init_block;
    pcnet_write_csr(1, (uint16_t)(ib & 0xFFFF));
    pcnet_write_csr(2, (uint16_t)(ib >> 16));

    pcnet_write_csr(0, CSR0_INIT);

    bool ok = false;
    for (int i = 0; i < 1000000; i++) {
        uint16_t csr0 = pcnet_read_csr(0);
        if (csr0 & CSR0_ERR) {
            kernel_debug("PCNET: INIT hatasi (CSR0=0x%04X)", csr0);
            break;
        }
        if (csr0 & CSR0_IDON) { ok = true; break; }
        asm volatile ("pause");
    }
    if (!ok) {
        kernel_debug("PCNET: INIT zaman asimi");
        return;
    }

    pcnet_write_csr(0, CSR0_STRT);
    for (volatile int i = 0; i < 1000; i++) asm volatile ("pause");

    memcpy(net_if.mac, pcnet.mac, 6);
    net_if.valid = true;

    pcnet.active = true;
    nc->initialized = true;
    kernel_debug("PCNET: hazir");
}

bool pcnet_send_packet(void *ctx, const uint8_t *data, uint16_t length) {
    (void)ctx;
    if (!pcnet.active || length == 0 || length > PCNET_BUF_SIZE) return false;

    int idx = pcnet.tx_idx;
    pcnet_tx_desc_t *d = &tx_descs[idx];

    if (d->status & DESC_OWN) {
        for (volatile int w = 0; w < 100000; w++) {
            if (!(d->status & DESC_OWN)) break;
            asm volatile ("pause");
        }
        if (d->status & DESC_OWN) return false;
    }

    memcpy(tx_buffers[idx], data, length);
    d->tbadr = (uint32_t)(uintptr_t)tx_buffers[idx];
    d->length = (int16_t)(0xF000 | ((4096 - length) & 0x0FFF));
    d->misc = 0;
    d->res = 0;
    d->status = (int16_t)(DESC_OWN | TX_STP | TX_ENP);

    pcnet_write_csr(0, CSR0_TDMD);

    pcnet.tx_idx = (idx + 1) % PCNET_TX_COUNT;
    return true;
}

static uint16_t pcnet_rx_length(const pcnet_rx_desc_t *d) {
    uint16_t len = (uint16_t)(d->msg_length & 0x0FFF);
    if (len >= 4) len -= 4;
    return len;
}

bool pcnet_receive_packet(void *ctx, uint8_t *buf, uint16_t *length) {
    (void)ctx;
    if (!pcnet.active) return false;

    for (int n = 0; n < PCNET_RX_COUNT; n++) {
        pcnet_rx_desc_t *d = &rx_descs[pcnet.rx_idx];
        uint16_t st = (uint16_t)d->status;

        if (st & DESC_OWN) return false;

        if ((st & RX_ERR) || (st & (RX_STP | RX_ENP)) != (RX_STP | RX_ENP)) {
            d->status = (int16_t)DESC_OWN;
            pcnet.rx_idx = (pcnet.rx_idx + 1) % PCNET_RX_COUNT;
            continue;
        }

        uint16_t len = pcnet_rx_length(d);
        if (buf && length && *length >= len) {
            memcpy(buf, rx_buffers[pcnet.rx_idx], len);
            *length = len;
        } else if (length) {
            *length = len;
        }

        d->status = (int16_t)DESC_OWN;
        pcnet.rx_idx = (pcnet.rx_idx + 1) % PCNET_RX_COUNT;
        return true;
    }
    return false;
}

bool pcnet_has_packet(void *ctx) {
    (void)ctx;
    if (!pcnet.active) return false;
    return !(rx_descs[pcnet.rx_idx].status & DESC_OWN);
}

bool pcnet_link_up(void) {
    if (!pcnet.active) return false;
    return (pcnet_read_bcr(4) & 0x8000) != 0;
}
