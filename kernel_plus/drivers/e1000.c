#include "kernel.h"
#include "e1000.h"
#include "pci.h"
#include "net.h"
#include "isr.h"
#include "pic.h"

#define E1000_REG_CTRL      0x0000
#define E1000_REG_STATUS    0x0008
#define E1000_REG_EECD      0x0010
#define E1000_REG_EERD      0x0014
#define E1000_REG_ICR       0x00C0
#define E1000_REG_ITR       0x00C4
#define E1000_REG_ICS       0x00C8
#define E1000_REG_IMS       0x00D0
#define E1000_REG_IMC       0x00D8
#define E1000_REG_RCTL      0x0100
#define E1000_REG_RDBAL     0x2800
#define E1000_REG_RDBAH     0x2804
#define E1000_REG_RDLEN     0x2808
#define E1000_REG_RDH       0x2810
#define E1000_REG_RDT       0x2818
#define E1000_REG_RXDCTL    0x2828
#define E1000_REG_TCTL      0x0400
#define E1000_REG_TIPG      0x0410
#define E1000_REG_TDBAL     0x3800
#define E1000_REG_TDBAH     0x3804
#define E1000_REG_TDLEN     0x3808
#define E1000_REG_TDH       0x3810
#define E1000_REG_TDT       0x3818
#define E1000_REG_TXDCTL    0x3828

#define E1000_RA_BASE       0x5400
#define E1000_MTA_BASE      0x5200

#define RCTL_EN             (1 << 1)
#define RCTL_SBP            (1 << 2)
#define RCTL_UPE            (1 << 3)
#define RCTL_MPE            (1 << 4)
#define RCTL_LPE            (1 << 5)
#define RCTL_LBM_MASK       0xC0
#define RCTL_BAM            (1 << 15)
#define RCTL_BSIZE_MASK     (3 << 16)
#define RCTL_BSIZE_2048     (0 << 16)
#define RCTL_SECRC          (1 << 26)

#define TCTL_EN             (1 << 1)
#define TCTL_PSP            (1 << 3)
#define TCTL_CT_MASK        0x00000FF0
#define TCTL_CT_SHIFT       4
#define TCTL_COLD_FULL      0x0003F000
#define TCTL_COLD_SHIFT     12

#define TX_CMD_EOP          (1 << 0)
#define TX_CMD_IFCS         (1 << 1)
#define TX_CMD_RS           (1 << 3)
#define TX_STAT_DD          (1 << 0)

#define RX_STAT_DD          (1 << 0)
#define RX_STAT_EOP         (1 << 1)

#define ICR_TXDW            (1 << 0)
#define ICR_TXQE            (1 << 1)
#define ICR_LSC             (1 << 2)
#define ICR_RXDMT0          (1 << 4)
#define ICR_RXT0            (1 << 7)
#define ICR_RXO             (1 << 6)

#define EECD_SK             (1 << 0)
#define EECD_CS             (1 << 1)
#define EECD_DI             (1 << 2)
#define EECD_DO             (1 << 3)

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

typedef struct {
    volatile uint32_t *mmio;
    uint64_t mmio_base;
    uint8_t mac[6];
    uint8_t irq;
    uint16_t rx_idx;
    uint16_t tx_idx;
    bool active;
} e1000_dev_t;

static e1000_dev_t e1000;

__attribute__((aligned(16)))
static e1000_rx_desc_t rx_descs[E1000_NUM_RX_DESC];

__attribute__((aligned(16)))
static e1000_tx_desc_t tx_descs[E1000_NUM_TX_DESC];

__attribute__((aligned(16)))
static uint8_t rx_buffers[E1000_NUM_RX_DESC][NET_PKT_MAX];

__attribute__((aligned(16)))
static uint8_t tx_buffers[E1000_NUM_TX_DESC][NET_PKT_MAX];

static inline uint32_t e1000_read_reg(uint32_t reg) {
    return e1000.mmio[reg / 4];
}

static inline void e1000_write_reg(uint32_t reg, uint32_t val) {
    e1000.mmio[reg / 4] = val;
}

static void e1000_irq_handler(void) {
    (void)e1000_read_reg(E1000_REG_ICR);
}

void e1000_init(net_controller_t *nc) {
    e1000.active = false;
    e1000.mmio = 0;

    if (nc->bar[0].is_io || nc->bar[0].address == 0) {
        kernel_debug("E1000: Gecersiz BAR0!");
        return;
    }

    e1000.mmio_base = nc->bar[0].address;
    e1000.mmio = (volatile uint32_t *)(uintptr_t)e1000.mmio_base;
    e1000.irq = nc->dev.irq;
    e1000.rx_idx = 0;
    e1000.tx_idx = 0;

    uint32_t status = e1000_read_reg(E1000_REG_STATUS);
    kernel_debug("E1000: MMIO=0x%llX Status=0x%X IRQ=%d",
                 (unsigned long long)e1000.mmio_base, status, e1000.irq);

    uint32_t ctrl = e1000_read_reg(E1000_REG_CTRL);
    ctrl |= (1 << 26);
    e1000_write_reg(E1000_REG_CTRL, ctrl);

    for (volatile int i = 0; i < 10000; i++) asm volatile ("pause");

    for (int i = 0; i < 4; i++) {
        uint32_t ral = e1000_read_reg(E1000_RA_BASE + i * 8 + 0);
        uint32_t rah = e1000_read_reg(E1000_RA_BASE + i * 8 + 4);
        if (ral != 0 || (rah & 0xFFFF) != 0) {
            e1000.mac[0] = (uint8_t)(ral);
            e1000.mac[1] = (uint8_t)(ral >> 8);
            e1000.mac[2] = (uint8_t)(ral >> 16);
            e1000.mac[3] = (uint8_t)(ral >> 24);
            e1000.mac[4] = (uint8_t)(rah);
            e1000.mac[5] = (uint8_t)(rah >> 8);
            break;
        }
    }

    if (e1000.mac[0] == 0 && e1000.mac[1] == 0 &&
        e1000.mac[2] == 0 && e1000.mac[3] == 0 &&
        e1000.mac[4] == 0 && e1000.mac[5] == 0) {
        e1000.mac[0] = 0x52; e1000.mac[1] = 0x54;
        e1000.mac[2] = 0x00; e1000.mac[3] = 0x12;
        e1000.mac[4] = 0x34; e1000.mac[5] = 0x56;
        uint32_t ral = (uint32_t)e1000.mac[0] | ((uint32_t)e1000.mac[1] << 8) |
                       ((uint32_t)e1000.mac[2] << 16) | ((uint32_t)e1000.mac[3] << 24);
        uint32_t rah = (uint32_t)e1000.mac[4] | ((uint32_t)e1000.mac[5] << 8);
        rah |= (1 << 31);
        e1000_write_reg(E1000_RA_BASE, ral);
        e1000_write_reg(E1000_RA_BASE + 4, rah);
    }

    memcpy(net_if.mac, e1000.mac, 6);
    net_if.valid = true;

    kernel_debug("E1000: MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                 e1000.mac[0], e1000.mac[1], e1000.mac[2],
                 e1000.mac[3], e1000.mac[4], e1000.mac[5]);

    memset(rx_descs, 0, sizeof(rx_descs));
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_descs[i].addr = (uint64_t)(uintptr_t)rx_buffers[i];
        rx_descs[i].status = 0;
    }

    memset(tx_descs, 0, sizeof(tx_descs));
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_descs[i].addr = (uint64_t)(uintptr_t)tx_buffers[i];
        tx_descs[i].status = TX_STAT_DD;
        tx_descs[i].cmd = 0;
    }

    e1000_write_reg(E1000_REG_RDBAL, (uint32_t)((uintptr_t)rx_descs & 0xFFFFFFFF));
    e1000_write_reg(E1000_REG_RDBAH, 0);
    e1000_write_reg(E1000_REG_RDLEN, E1000_NUM_RX_DESC * 16);
    e1000_write_reg(E1000_REG_RDH, 0);
    e1000_write_reg(E1000_REG_RDT, E1000_NUM_RX_DESC - 1);
    e1000_write_reg(E1000_REG_RXDCTL, 0);

    uint32_t rctl = RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE |
                    RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC;
    e1000_write_reg(E1000_REG_RCTL, rctl);

    e1000_write_reg(E1000_REG_TDBAL, (uint32_t)((uintptr_t)tx_descs & 0xFFFFFFFF));
    e1000_write_reg(E1000_REG_TDBAH, 0);
    e1000_write_reg(E1000_REG_TDLEN, E1000_NUM_TX_DESC * 16);
    e1000_write_reg(E1000_REG_TDH, 0);
    e1000_write_reg(E1000_REG_TDT, 0);
    e1000_write_reg(E1000_REG_TXDCTL, 0);

    uint32_t tctl = TCTL_EN | TCTL_PSP |
                    ((15 & 0xFF) << TCTL_CT_SHIFT) |
                    ((64 & 0x3FF) << TCTL_COLD_SHIFT);
    e1000_write_reg(E1000_REG_TCTL, tctl);

    e1000_write_reg(E1000_REG_TIPG, 0x0060200A);

    for (int i = 0; i < 128; i++) {
        e1000_write_reg(E1000_MTA_BASE + i * 4, 0);
    }

    if (e1000.irq != 0xFF && e1000.irq < 16) {
        e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFF);
        e1000_write_reg(E1000_REG_ICR, 0xFFFFFFFF);
        e1000_write_reg(E1000_REG_IMS, ICR_RXT0 | ICR_RXDMT0 | ICR_LSC);

        isr_install_handler(e1000.irq, e1000_irq_handler);
        pic_set_mask(e1000.irq, false);

        kernel_debug("E1000: IRQ%d kesme kaydedildi", e1000.irq);
    }

    e1000.active = true;
    nc->initialized = true;
}

bool e1000_send_packet(void *ctx, const uint8_t *data, uint16_t length) {
    (void)ctx;
    if (!e1000.active || length > NET_PKT_MAX) return false;

    if (!(tx_descs[e1000.tx_idx].status & TX_STAT_DD)) {
        for (volatile int w = 0; w < 100000; w++) {
            if (tx_descs[e1000.tx_idx].status & TX_STAT_DD) break;
            asm volatile ("pause");
        }
        if (!(tx_descs[e1000.tx_idx].status & TX_STAT_DD)) return false;
    }

    memcpy(tx_buffers[e1000.tx_idx], data, length);
    tx_descs[e1000.tx_idx].length = length;
    tx_descs[e1000.tx_idx].cmd = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    tx_descs[e1000.tx_idx].status = 0;

    e1000.tx_idx = (e1000.tx_idx + 1) % E1000_NUM_TX_DESC;
    e1000_write_reg(E1000_REG_TDT, e1000.tx_idx);

    return true;
}

bool e1000_receive_packet(void *ctx, uint8_t *buf, uint16_t *length) {
    (void)ctx;
    if (!e1000.active) return false;

    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        if (rx_descs[e1000.rx_idx].status & RX_STAT_DD) {
            uint16_t pkt_len = rx_descs[e1000.rx_idx].length;
            if (pkt_len > 0 && buf && length && *length >= pkt_len) {
                memcpy(buf, rx_buffers[e1000.rx_idx], pkt_len);
                *length = pkt_len;
            } else if (length) {
                *length = pkt_len;
            }
            rx_descs[e1000.rx_idx].status = 0;
            e1000.rx_idx = (e1000.rx_idx + 1) % E1000_NUM_RX_DESC;
            e1000_write_reg(E1000_REG_RDT,
                            (e1000.rx_idx + E1000_NUM_RX_DESC - 1) % E1000_NUM_RX_DESC);
            return true;
        }
    }
    return false;
}

bool e1000_has_packet(void *ctx) {
    (void)ctx;
    if (!e1000.active) return false;
    return (rx_descs[e1000.rx_idx].status & RX_STAT_DD) != 0;
}

bool e1000_link_up(void) {
    if (!e1000.active || !e1000.mmio) return false;
    return (e1000_read_reg(E1000_REG_STATUS) & 0x02) != 0;
}
