#include "kernel.h"
#include "rtl8139.h"
#include "pci.h"
#include "net.h"
#include "isr.h"
#include "pic.h"

#define RTL8139_IDR0         0x00
#define RTL8139_IDR1         0x01
#define RTL8139_IDR2         0x02
#define RTL8139_IDR3         0x03
#define RTL8139_IDR4         0x04
#define RTL8139_IDR5         0x05

#define RTL8139_MAR0         0x08

#define RTL8139_TX_STATUS0   0x10
#define RTL8139_TX_ADDR0     0x20
#define RTL8139_TX_BUFFER    0x20

#define RTL8139_COMMAND      0x37
#define CR_RST               (1 << 4)
#define CR_RE                (1 << 3)
#define CR_TE                (1 << 2)

#define RTL8139_IMR          0x3C
#define RTL8139_ISR          0x3E
#define INT_ROK              (1 << 0)
#define INT_TOK              (1 << 2)

#define RTL8139_RCR          0x44
#define RCR_AAP              (1 << 0)
#define RCR_APM              (1 << 1)
#define RCR_AM               (1 << 2)
#define RCR_AB               (1 << 3)
#define RCR_WRAP             (1 << 7)
#define RCR_BUFE             (0x0000)

#define RTL8139_TCR          0x40
#define TCR_IFG_MASK         0x03

#define RTL8139_CONFIG1      0x52

#define RTL8139_CAPR         0x38
#define RTL8139_RBSTART      0x30
#define RTL8139_MPC          0x4C

#define RX_BUF_SIZE          8192
#define RX_BUF_PTR_MASK      (RX_BUF_SIZE - 1)

#define TX_BUF_SIZE          1536
#define TX_STATUS_OWN        (1 << 13)
#define TX_STATUS_TUN        (1 << 14)
#define TX_STATUS_OWC        (1 << 23)
#define TX_STATUS_TOK        (1 << 15)

typedef struct {
    uint16_t io_base;
    uint8_t mac[6];
    uint8_t irq;
    uint16_t rx_offset;
    int tx_current;
    bool active;
} rtl8139_dev_t;

static rtl8139_dev_t rtl;

__attribute__((aligned(4)))
static uint8_t rx_buf_mem[RX_BUF_SIZE + 16];

__attribute__((aligned(4)))
static uint8_t tx_buf_mem[4][TX_BUF_SIZE];

static void rtl8139_irq_handler(void) {
    uint16_t isr = inw(rtl.io_base + RTL8139_ISR);
    outw(rtl.io_base + RTL8139_ISR, isr);

    if (isr & INT_ROK) {
        while (1) {
            uint8_t *rx = &rx_buf_mem[rtl.rx_offset];
            uint16_t status = *((volatile uint16_t *)(rx));
            uint16_t length = *((volatile uint16_t *)(rx + 2));

            if (status & 0x0001) break;

            if (!(status & (1 << 13)) && !(status & (1 << 14)) &&
                length > 14 && length <= NET_PKT_MAX) {
                if (net_if.queue_count < NET_RING_SIZE) {
                    uint16_t q_idx = net_if.rx_head;
                    memcpy(net_if.pkts[q_idx].data, rx + 4, length - 4);
                    net_if.pkts[q_idx].length = length - 4;
                    net_if.pkts[q_idx].in_use = true;
                    net_if.rx_head = (net_if.rx_head + 1) % NET_RING_SIZE;
                    net_if.queue_count++;
                }
            }

            uint16_t new_offset = (rtl.rx_offset + length + 4 + 3) & ~3;
            if (new_offset >= RX_BUF_SIZE) new_offset -= RX_BUF_SIZE;
            rtl.rx_offset = new_offset;
            outw(rtl.io_base + RTL8139_CAPR, (uint16_t)(rtl.rx_offset - 16));
        }
    }

    if (isr & INT_TOK) {
        if (tx_buf_mem[rtl.tx_current][0] != 0) {
            uint32_t tx_status = inl(rtl.io_base + RTL8139_TX_STATUS0 + rtl.tx_current * 4);
            outl(rtl.io_base + RTL8139_TX_STATUS0 + rtl.tx_current * 4, tx_status);
        }
    }
}

void rtl8139_init(net_controller_t *nc) {
    rtl.active = false;
    rtl.io_base = 0;

    if (nc->bar[0].is_io && nc->bar[0].address != 0) {
        rtl.io_base = (uint16_t)(nc->bar[0].address & 0xFFFC);
    } else if (nc->bar[1].is_io && nc->bar[1].address != 0) {
        rtl.io_base = (uint16_t)(nc->bar[1].address & 0xFFFC);
    } else {
        kernel_debug("RTL8139: IO port bulunamadi!");
        return;
    }

    rtl.irq = nc->dev.irq;
    rtl.rx_offset = 0;
    rtl.tx_current = 0;

    outb(rtl.io_base + RTL8139_COMMAND, CR_RST);
    for (volatile int i = 0; i < 100000; i++) {
        if ((inb(rtl.io_base + RTL8139_COMMAND) & CR_RST) == 0) break;
        asm volatile ("pause");
    }

    kernel_debug("RTL8139: IO=0x%X IRQ=%d", rtl.io_base, rtl.irq);

    for (int i = 0; i < 6; i++) {
        rtl.mac[i] = inb(rtl.io_base + RTL8139_IDR0 + i);
    }

    memcpy(net_if.mac, rtl.mac, 6);
    net_if.valid = true;

    kernel_debug("RTL8139: MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                 rtl.mac[0], rtl.mac[1], rtl.mac[2],
                 rtl.mac[3], rtl.mac[4], rtl.mac[5]);

    memset(rx_buf_mem, 0, sizeof(rx_buf_mem));
    memset(tx_buf_mem, 0, sizeof(tx_buf_mem));

    rtl.rx_offset = 0;
    rtl.tx_current = 0;

    outl(rtl.io_base + RTL8139_RBSTART, (uint32_t)(uintptr_t)rx_buf_mem);

    outb(rtl.io_base + RTL8139_CONFIG1, 0x00);

    uint32_t rcr = RCR_AB | RCR_AM | RCR_APM | RCR_AAP | (2 << 11);
    outl(rtl.io_base + RTL8139_RCR, rcr);

    outl(rtl.io_base + RTL8139_TCR, 0x03000700);

    outw(rtl.io_base + RTL8139_CAPR, 0xFFF0);

    outb(rtl.io_base + RTL8139_COMMAND, CR_RE | CR_TE);

    outw(rtl.io_base + RTL8139_ISR, 0xFFFF);

    if (rtl.irq != 0xFF && rtl.irq < 16) {
        outw(rtl.io_base + RTL8139_IMR, INT_ROK | INT_TOK);
        isr_install_handler(rtl.irq, rtl8139_irq_handler);
        pic_set_mask(rtl.irq, false);
        kernel_debug("RTL8139: IRQ%d kesme kaydedildi", rtl.irq);
    }

    rtl.active = true;
    nc->initialized = true;
}

bool rtl8139_send_packet(void *ctx, const uint8_t *data, uint16_t length) {
    (void)ctx;
    if (!rtl.active || length > TX_BUF_SIZE) return false;

    int tx_idx = rtl.tx_current;
    uint32_t tx_status = inl(rtl.io_base + RTL8139_TX_STATUS0 + tx_idx * 4);

    if (!(tx_status & TX_STATUS_OWC) && !(tx_status & TX_STATUS_TOK)) {
        rtl.tx_current = (rtl.tx_current + 1) % 4;
        tx_idx = rtl.tx_current;
        tx_status = inl(rtl.io_base + RTL8139_TX_STATUS0 + tx_idx * 4);
        if (!(tx_status & TX_STATUS_OWC) && !(tx_status & TX_STATUS_TOK)) return false;
    }

    memcpy(tx_buf_mem[tx_idx], data, length);
    outl(rtl.io_base + RTL8139_TX_ADDR0 + tx_idx * 4, (uint32_t)(uintptr_t)tx_buf_mem[tx_idx]);
    outl(rtl.io_base + RTL8139_TX_STATUS0 + tx_idx * 4, (uint32_t)length);

    rtl.tx_current = (rtl.tx_current + 1) % 4;
    return true;
}

bool rtl8139_receive_packet(void *ctx, uint8_t *buf, uint16_t *length) {
    (void)ctx;
    if (!rtl.active) return false;

    uint8_t *rx = &rx_buf_mem[rtl.rx_offset];
    uint16_t status = *((volatile uint16_t *)(rx));

    if (status & 0x0001) return false;

    uint16_t pkt_len = *((volatile uint16_t *)(rx + 2));
    if (pkt_len > 14 && pkt_len <= NET_PKT_MAX) {
        uint16_t data_len = pkt_len - 4;

        if (buf && length && *length >= data_len) {
            memcpy(buf, rx + 4, data_len);
            *length = data_len;
        } else if (length) {
            *length = data_len;
        }

        uint16_t new_offset = (rtl.rx_offset + pkt_len + 4 + 3) & ~3;
        if (new_offset >= RX_BUF_SIZE) new_offset -= RX_BUF_SIZE;
        rtl.rx_offset = new_offset;
        outw(rtl.io_base + RTL8139_CAPR, (uint16_t)(rtl.rx_offset - 16));
        return true;
    }

    uint16_t new_offset = (rtl.rx_offset + pkt_len + 4 + 3) & ~3;
    if (new_offset >= RX_BUF_SIZE) new_offset -= RX_BUF_SIZE;
    rtl.rx_offset = new_offset;
    outw(rtl.io_base + RTL8139_CAPR, (uint16_t)(rtl.rx_offset - 16));
    return false;
}

bool rtl8139_has_packet(void *ctx) {
    (void)ctx;
    if (!rtl.active) return false;
    uint8_t *rx = &rx_buf_mem[rtl.rx_offset];
    uint16_t status = *((volatile uint16_t *)(rx));
    return !(status & 0x0001);
}
