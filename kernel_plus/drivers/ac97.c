#include "kernel.h"
#include "ac97.h"
#include "pci.h"
#include "serial.h"

#define AC97_VENDOR_INTEL 0x8086
#define AC97_DEV_82801AA  0x2415
#define PCI_CLASS_MULTIMEDIA 0x04
#define PCI_SUBCLASS_AUDIO   0x01

#define NAM_RESET           0x00
#define NAM_MASTER_VOL      0x02
#define NAM_PCM_OUT_VOL     0x18
#define NAM_POWERDOWN       0x26
#define NAM_EXT_AUDIO_CTRL  0x2A
#define NAM_PCM_FRONT_RATE  0x2C
#define NAM_VENDOR_ID1      0x7C
#define NAM_VENDOR_ID2      0x7E

#define NABM_PO_BDBAR       0x10
#define NABM_PO_CIV         0x14
#define NABM_PO_LVI         0x15
#define NABM_PO_SR          0x16
#define NABM_PO_CR          0x1B
#define NABM_GLOB_CNT       0x2C
#define NABM_GLOB_STA       0x30

#define PO_CR_RPBM          0x01
#define PO_CR_RR            0x02

typedef struct {
    bool     present;
    uint16_t nam;
    uint16_t nabm;
    uint8_t  bus, slot, func, irq;
    uint32_t rate;
} ac97_dev_t;

static ac97_dev_t s_ac;

static void ac97_delay(void) {
    for (volatile int i = 0; i < 10000; i++) asm volatile("pause");
}

bool ac97_present(void) { return s_ac.present; }
uint32_t ac97_rate(void) { return s_ac.rate; }

static void nam_write(uint16_t reg, uint16_t val) { outw((uint16_t)(s_ac.nam + reg), val); }
static uint16_t nam_read(uint16_t reg) { return inw((uint16_t)(s_ac.nam + reg)); }

static uint8_t  nabm_readb(uint16_t reg) { return inb((uint16_t)(s_ac.nabm + reg)); }
static void     nabm_writeb(uint16_t reg, uint8_t v) { outb((uint16_t)(s_ac.nabm + reg), v); }
static uint16_t nabm_readw(uint16_t reg) { return inw((uint16_t)(s_ac.nabm + reg)); }
static void     nabm_writew(uint16_t reg, uint16_t v) { outw((uint16_t)(s_ac.nabm + reg), v); }
static void     nabm_writel(uint16_t reg, uint32_t v) { outl((uint16_t)(s_ac.nabm + reg), v); }

void ac97_set_rate(uint32_t hz) {
    if (!s_ac.present) return;
    if (hz == 0) hz = 48000;
    nam_write(NAM_EXT_AUDIO_CTRL, 0x0009);
    nam_write(NAM_PCM_FRONT_RATE, (uint16_t)(hz & 0xFFFF));
    s_ac.rate = hz;
}

void ac97_set_volume(int percent) {
    if (!s_ac.present) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint16_t att = (uint16_t)(((100 - percent) * 31) / 100);
    if (att > 31) att = 31;
    uint16_t val = (uint16_t)((att << 8) | att);
    if (percent == 0) val |= 0x8000u;
    nam_write(NAM_MASTER_VOL, 0x0000);
    nam_write(NAM_PCM_OUT_VOL, val);
}

void ac97_reset_dma(void) {
    if (!s_ac.present) return;
    nabm_writeb(NABM_PO_CR, PO_CR_RR | PO_CR_RPBM);
    for (volatile int i = 0; i < 1000; i++) asm volatile("pause");
    nabm_writeb(NABM_PO_CR, 0);
    nabm_writew(NABM_PO_SR, 0x1C);
    (void)nabm_readw(NABM_PO_SR);
}

void ac97_set_bdbar(uint32_t phys) { if (s_ac.present) nabm_writel(NABM_PO_BDBAR, phys); }
void ac97_set_lvi(uint8_t idx)     { if (s_ac.present) nabm_writeb(NABM_PO_LVI, (uint8_t)(idx & 31)); }
void ac97_start_dma(void)          { if (s_ac.present) nabm_writeb(NABM_PO_CR, PO_CR_RPBM); }
void ac97_stop_dma(void)           { if (s_ac.present) nabm_writeb(NABM_PO_CR, 0); }
uint8_t  ac97_civ(void)            { return s_ac.present ? (uint8_t)(nabm_readb(NABM_PO_CIV) & 31) : 0; }
uint16_t ac97_status(void)         { return s_ac.present ? nabm_readw(NABM_PO_SR) : 0; }

static bool ac97_find(pci_device_t *out, pci_bar_t *bar0, pci_bar_t *bar1) {
    for (int b = 0; b < 256; b++) {
        for (int s = 0; s < 32; s++) {
            if (pci_config_readw((uint8_t)b, (uint8_t)s, 0, 0) == 0xFFFF) continue;
            uint8_t hdr = pci_config_readb((uint8_t)b, (uint8_t)s, 0, 0x0E);
            int nfunc = (hdr & 0x80) ? 8 : 1;
            for (int f = 0; f < nfunc; f++) {
                uint16_t vendor = pci_config_readw((uint8_t)b, (uint8_t)s, (uint8_t)f, 0);
                if (vendor == 0xFFFF) continue;
                uint32_t rc = pci_config_readl((uint8_t)b, (uint8_t)s, (uint8_t)f, 0x08);
                uint8_t cls = (rc >> 24) & 0xFF;
                uint8_t sub = (rc >> 16) & 0xFF;
                uint16_t dev = pci_config_readw((uint8_t)b, (uint8_t)s, (uint8_t)f, 0x02);
                if (cls != PCI_CLASS_MULTIMEDIA || sub != PCI_SUBCLASS_AUDIO) continue;
                if (vendor != AC97_VENDOR_INTEL || dev != AC97_DEV_82801AA) continue;

                out->bus = (uint8_t)b;
                out->slot = (uint8_t)s;
                out->func = (uint8_t)f;
                out->vendor = vendor;
                out->device = dev;
                out->class_code = cls;
                out->subclass = sub;
                out->irq = pci_config_readb((uint8_t)b, (uint8_t)s, (uint8_t)f, PCI_CONFIG_INTERRUPT);
                *bar0 = pci_read_bar((uint8_t)b, (uint8_t)s, (uint8_t)f, 0);
                *bar1 = pci_read_bar((uint8_t)b, (uint8_t)s, (uint8_t)f, 1);
                return true;
            }
        }
    }
    return false;
}

int ac97_init(void) {
    s_ac.present = false;
    s_ac.nam = 0;
    s_ac.nabm = 0;
    s_ac.rate = 0;

    pci_device_t dev;
    pci_bar_t bar0, bar1;
    if (!ac97_find(&dev, &bar0, &bar1)) {
        kernel_debug("AC97: ses denetleyicisi bulunamadi");
        return -1;
    }
    if (!bar0.is_io || !bar1.is_io || bar0.address == 0 || bar1.address == 0) {
        kernel_debug("AC97: gecersiz BAR (NAM/NABM IO olmali)");
        return -1;
    }

    s_ac.nam = (uint16_t)bar0.address;
    s_ac.nabm = (uint16_t)bar1.address;
    s_ac.bus = dev.bus; s_ac.slot = dev.slot; s_ac.func = dev.func; s_ac.irq = dev.irq;

    pci_enable_bus_mastering(dev.bus, dev.slot, dev.func);

    nam_write(NAM_RESET, 0x0000);
    ac97_delay();

    uint16_t id1 = nam_read(NAM_VENDOR_ID1);
    uint16_t id2 = nam_read(NAM_VENDOR_ID2);

    nam_write(NAM_POWERDOWN, 0x0000);
    nam_write(NAM_MASTER_VOL, 0x0000);
    nam_write(NAM_PCM_OUT_VOL, 0x0000);
    nam_write(NAM_EXT_AUDIO_CTRL, 0x0009);

    s_ac.present = true;
    ac97_reset_dma();
    ac97_set_rate(48000);
    ac97_set_volume(80);

    kernel_debug("AC97: NAM=0x%X NABM=0x%X IRQ=%d codec=%04X:%04X",
                 s_ac.nam, s_ac.nabm, s_ac.irq, id1, id2);
    return 0;
}
