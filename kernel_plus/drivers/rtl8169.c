/* ===================================================================== *
 * rtl8169.c — Realtek RTL8169/8168/8125 Gigabit Ethernet sürücüsü
 *
 * Arctian 0.5. RTL8139 sürücüsünden ayrı olarak hızlı ethernet (Gigabit)
 * sunan Realtek kartları için ayrı bir sürücü.
 *
 * DÜRÜST DURUM: Kayıtli DMA halka yapısı port edilmiştir ancak canlı
 * donanımda doğrulanmamıştır (QEMU'da RTL8169 emülasyonu yok).
 * ===================================================================== */

#include "kernel.h"
#include "pci.h"
#include "net.h"
#include "isr.h"
#include "pic.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Register tanımları (RTL8169 datasheet) */
#define RTL8169_IDR0        0x00
#define RTL8169_IDR6        0x06
#define RTL8169_CMD         0x37
#define RTL8169_CMD_TX_EN   0x08
#define RTL8169_CMD_RX_EN   0x04
#define RTL8169_IER0        0x38
#define RTL8169_9103        0x40
#define RTL8169_TSR         0x04
#define RTL8169_TCR         0x04
#define RTL8169_RCR         0x08
#define RTL8169_CCR         0x58
#define RTL8169_CCR_LED     0x10
#define RTL8169_CCR_TEST    0x20
#define RTL8169_CCR_PHY     0x01
#define RTL8169_CCR_PHYADD  0x1F

/* Descriptor tabanı (PCI BAR değil, IO alanı) */
#define RTL8169_TX_DESC_BASE 0x10000
#define RTL8169_RX_DESC_BASE 0x11000

#define RTL8169_RXDESC_NUM  4
#define RTL8169_TXDESC_NUM  4
#define RTL8169_DESC_SIZE   256
#define RTL8169_RX_BUF_SIZE 2048

typedef struct {
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t len;
    uint32_t flags;
} rtl_desc_t;

typedef struct {
    rtl_desc_t rx[RTL8169_RXDESC_NUM];
    rtl_desc_t tx[RTL8169_TXDESC_NUM];
    uint8_t    rx_buf[RTL8169_RXDESC_NUM][RTL8169_RX_BUF_SIZE] __attribute__((aligned(4096)));
    uint8_t    tx_buf[RTL8169_TXDESC_NUM][2048] __attribute__((aligned(512)));
    uint16_t   rx_cur;
    uint16_t   tx_cur;
    uint16_t   io_base;
    uint8_t    irq;
    uint8_t    mac[6];
    bool       active;
} rtl8169_t;

static rtl8169_t r8169 = {0};

static inline uint8_t  r_rd8(uint16_t off)   { return inb((uint16_t)(r8169.io_base + off)); }
static inline uint16_t r_rd16(uint16_t off)  { return inw((uint16_t)(r8169.io_base + off)); }
static inline uint32_t r_rd32(uint16_t off)  { return inl((uint16_t)(r8169.io_base + off)); }
static inline void r_wr8(uint16_t off, uint8_t v)   { outb((uint16_t)(r8169.io_base + off), v); }
static inline void r_wr16(uint16_t off, uint16_t v) { outw((uint16_t)(r8169.io_base + off), v); }
static inline void r_wr32(uint16_t off, uint32_t v) { outl((uint16_t)(r8169.io_base + off), v); }

/* ------------------------------------------------------------------ */
/* IRQ isleyici                                                        */
/* ------------------------------------------------------------------ */
static void rtl8169_irq_handler(void) {
    if (!r8169.active) return;
    /* RX halkasını oku, net_if kuyruğuna aktar */
    for (;;) {
        uint16_t head = r_rd16(RTL8169_RX_DESC_BASE + 0x0C);
        if (head == r8169.rx_cur) break;
        uint16_t idx = r8169.rx_cur;
        uint32_t len = r8169.rx[idx].len & 0x1FFF;
        if (len > RTL8169_RX_BUF_SIZE) len = RTL8169_RX_BUF_SIZE;
        if (len >= 4 && len <= RTL8169_RX_BUF_SIZE) {
            if (net_if.queue_count < NET_RING_SIZE) {
                uint16_t q = net_if.rx_head;
                memcpy(net_if.pkts[q].data, r8169.rx_buf[idx] + 4, len - 4);
                net_if.pkts[q].length = (uint16_t)(len - 4);
                net_if.pkts[q].in_use = true;
                net_if.rx_head = (net_if.rx_head + 1) % NET_RING_SIZE;
                net_if.queue_count++;
            }
        }
        /* Bufferi geri ver */
        r8169.rx[idx].addr_lo = (uint32_t)(uintptr_t)r8169.rx_buf[idx];
        r8169.rx[idx].addr_hi = (uint32_t)((uintptr_t)r8169.rx_buf[idx] >> 32);
        r8169.rx[idx].len = RTL8169_RX_BUF_SIZE;
        r8169.rx[idx].flags = 0x20000000 | RTL8169_RX_BUF_SIZE;
        r8169.rx_cur = (idx + 1) % RTL8169_RXDESC_NUM;
    }
}

/* ------------------------------------------------------------------ */
/* Kurulum                                                             */
/* ------------------------------------------------------------------ */
void rtl8169_init(net_controller_t *nc) {
    r8169.active = false;
    r8169.io_base = 0;

    if (nc->bar[0].is_io && nc->bar[0].address != 0) {
        r8169.io_base = (uint16_t)(nc->bar[0].address & 0xFFFC);
    } else if (nc->bar[1].is_io && nc->bar[1].address != 0) {
        r8169.io_base = (uint16_t)(nc->bar[1].address & 0xFFFC);
    } else {
        kernel_debug("RTL8169: IO port bulunamadi!");
        return;
    }
    r8169.irq = nc->dev.irq;

    /* Reset */
    r_wr8(RTL8169_CMD, 0x10);
    for (volatile int i = 0; i < 100000; i++) {
        if ((r_rd8(RTL8169_CMD) & 0x10) == 0) break;
        asm volatile ("pause");
    }
    r_wr8(RTL8169_CMD, 0x00);

    /* Interrupt mask */
    r_wr16(RTL8169_IER0, 0x0000);

    for (int i = 0; i < 6; i++) r8169.mac[i] = r_rd8(RTL8169_IDR0 + i);
    memcpy(net_if.mac, r8169.mac, 6);
    net_if.valid = true;

    memset(r8169.rx_buf, 0, sizeof(r8169.rx_buf));
    memset(r8169.tx_buf, 0, sizeof(r8169.tx_buf));
    r8169.rx_cur = 0;
    r8169.tx_cur = 0;

    kernel_debug("RTL8169: IO=0x%X IRQ=%d MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                 r8169.io_base, r8169.irq, r8169.mac[0], r8169.mac[1], r8169.mac[2],
                 r8169.mac[3], r8169.mac[4], r8169.mac[5]);

    /* Interrupt kesme kaydı */
    if (r8169.irq != 0xFF && r8169.irq < 16) {
        r_wr16(RTL8169_IER0, 0x0005);   /* RX | TX */
        isr_install_handler(r8169.irq, rtl8169_irq_handler);
        pic_set_mask(r8169.irq, false);
        kernel_debug("RTL8169: IRQ%d kesme kaydedildi", r8169.irq);
    }

    r8169.active = true;
    nc->initialized = true;
}

/* ------------------------------------------------------------------ */
/* Gonderme / alma (poll tabanli)                                      */
/* ------------------------------------------------------------------ */
bool rtl8169_send_packet(void *ctx, const uint8_t *data, uint16_t length) {
    (void)ctx;
    if (!r8169.active || length > 2048) return false;
    uint16_t cur = r8169.tx_cur;

    memcpy(r8169.tx_buf[cur], data, length);
    r8169.tx[cur].addr_lo = (uint32_t)(uintptr_t)r8169.tx_buf[cur];
    r8169.tx[cur].addr_hi = (uint32_t)((uintptr_t)r8169.tx_buf[cur] >> 32);
    r8169.tx[cur].len = length;
    r8169.tx[cur].flags = 0x20000000 | (length & 0x1FFF);

    r8169.tx_cur = (cur + 1) % RTL8169_TXDESC_NUM;

    /* TX poll */
    r_wr32(RTL8169_TX_DESC_BASE + (RTL8169_TXDESC_NUM - 1) * RTL8169_DESC_SIZE,
            r8169.tx_cur);
    return true;
}

bool rtl8169_has_packet(void *ctx) {
    (void)ctx;
    if (!r8169.active) return false;
    return r8169.rx_cur != r_rd16(RTL8169_RX_DESC_BASE + 0x0C);
}

bool rtl8169_receive_packet(void *ctx, uint8_t *buf, uint16_t *length) {
    (void)ctx;
    if (!r8169.active) return false;
    uint16_t head = r_rd16(RTL8169_RX_DESC_BASE + 0x0C);
    if (head == r8169.rx_cur) return false;

    uint16_t idx = r8169.rx_cur;
    uint32_t len = r8169.rx[idx].len & 0x1FFF;
    if (len > RTL8169_RX_BUF_SIZE) len = RTL8169_RX_BUF_SIZE;
    if (len && buf && length && *length >= len) {
        memcpy(buf, r8169.rx_buf[idx], len);
        *length = (uint16_t)len;
    } else if (len && length) {
        *length = (uint16_t)len;
        return false;
    }
    r8169.rx[idx].addr_lo = (uint32_t)(uintptr_t)r8169.rx_buf[idx];
    r8169.rx[idx].addr_hi = (uint32_t)((uintptr_t)r8169.rx_buf[idx] >> 32);
    r8169.rx[idx].len = RTL8169_RX_BUF_SIZE;
    r8169.rx[idx].flags = 0x20000000 | RTL8169_RX_BUF_SIZE;
    r8169.rx_cur = (idx + 1) % RTL8169_RXDESC_NUM;
    return len > 0;
}
