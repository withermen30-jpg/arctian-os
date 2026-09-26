#include "blockdev.h"
#include "mmio.h"
#include "kernel.h"

static bdev_ops_t s_ops[BLOCKDEV_MAX];
static int s_count = 0;

int blockdev_register(const bdev_ops_t *ops) {
    if (!ops || s_count >= BLOCKDEV_MAX) return -1;
    s_ops[s_count] = *ops;
    s_count++;
    return 0;
}

int blockdev_init(void) {
    s_count = 0;
    mmio_reset();
    blk_ata_scan();
    blk_ahci_scan();
    blk_nvme_scan();
    return s_count;
}

int blockdev_count(void) { return s_count; }

int blockdev_info(int idx, blockdev_info_t *out) {
    if (idx < 0 || idx >= s_count || !out) return -1;
    for (int i = 0; i < 24; i++) out->name[i] = s_ops[idx].name[i];
    out->kind = s_ops[idx].kind;
    out->sectors = s_ops[idx].sectors;
    out->removable = s_ops[idx].removable;
    return 0;
}

int blockdev_read(int idx, uint64_t lba, uint32_t count, void *buf) {
    if (idx < 0 || idx >= s_count || !s_ops[idx].read) return -1;
    return s_ops[idx].read(s_ops[idx].ctx, lba, count, buf);
}

int blockdev_write(int idx, uint64_t lba, uint32_t count, const void *buf) {
    if (idx < 0 || idx >= s_count || !s_ops[idx].write) return -1;
    return s_ops[idx].write(s_ops[idx].ctx, lba, count, buf);
}

int blockdev_flush(int idx) {
    if (idx < 0 || idx >= s_count) return -1;
    if (!s_ops[idx].flush) return 0;
    return s_ops[idx].flush(s_ops[idx].ctx);
}

static uint64_t blk_pd_pool[4][512] __attribute__((aligned(4096)));
static int blk_pd_used = 0;

void blk_mmio_map(uint64_t phys, uint64_t size) {
    if (size == 0) size = 0x1000;
    if (phys + size <= 0x40000000ULL) return;

    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & ~0xFFFULL);
    int i4 = (int)((phys >> 39) & 0x1FF);
    if (!(pml4[i4] & 1)) return;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[i4] & ~0xFFFULL);

    uint64_t start = phys & ~0x1FFFFFULL;
    uint64_t end = (phys + size + 0x1FFFFFULL) & ~0x1FFFFFULL;

    for (uint64_t a = start; a < end; a += 0x40000000ULL) {
        int i3 = (int)((a >> 30) & 0x1FF);
        if (pdpt[i3] & 0x80) continue;
        uint64_t *pd;
        if (pdpt[i3] & 1) {
            pd = (uint64_t *)(uintptr_t)(pdpt[i3] & ~0xFFFULL);
        } else {
            if (blk_pd_used >= 4) return;
            pd = blk_pd_pool[blk_pd_used++];
            for (int k = 0; k < 512; k++) pd[k] = 0;
            pdpt[i3] = (uint64_t)(uintptr_t)pd | 0x3;
        }
        uint64_t seg_end = a + 0x40000000ULL;
        if (seg_end > end) seg_end = end;
        for (uint64_t b = a; b < seg_end; b += 0x200000ULL)
            pd[(b >> 21) & 0x1FF] = b | 0x83ULL;
    }
    __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3));
}

uint32_t blk_pci_readl(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) |
                    ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

void blk_pci_writel(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) |
                    ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

uint64_t blk_pci_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar) {
    uint32_t lo = blk_pci_readl(bus, slot, func, (uint8_t)(0x10 + bar * 4));
    if (lo & 1) return (uint64_t)(lo & ~0x3u);
    uint64_t addr = (uint64_t)(lo & ~0xFu);
    if (((lo >> 1) & 3) == 2) {
        uint32_t hi = blk_pci_readl(bus, slot, func, (uint8_t)(0x10 + bar * 4 + 4));
        addr |= (uint64_t)hi << 32;
    }
    return addr;
}

void blk_pci_enable(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t cmd = blk_pci_readl(bus, slot, func, 0x04);
    cmd |= 0x0006;
    blk_pci_writel(bus, slot, func, 0x04, cmd);
}
