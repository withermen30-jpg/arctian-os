/* Birlesik blok cihaz cekirdegi + PCI yardimcilari. */
#include "blockdev.h"
#include "mmio.h"
#include "kernel.h"
#include "memory_map.h"

static bdev_ops_t s_ops[BLOCKDEV_MAX];
static int s_count = 0;
static void (*s_scan_hook)(void) = 0;

/* ---- Blok onbellek (write-back, 4-yollu set-associative) ----
 * Yalnizca kernel_plus acar; main_kernel/install kapali kalir, boylece
 * moduller arasi onbellek tutarsizligi olusmaz. Sifreleme durumu
 * degistiginde storage_set_crypt onbellegi bosaltip gecersiz kilar. */
#define BC_SETS     128
#define BC_WAYS     4
#define BC_SECTORS  (BC_SETS * BC_WAYS)
#define BC_BYPASS   256

typedef struct {
    uint8_t  data[512];
    uint64_t lba;
    int      dev;
    uint8_t  valid;
    uint8_t  dirty;
    uint32_t age;
} bc_line_t;

static bc_line_t *const s_bc = (bc_line_t *)(uintptr_t)BLK_SCRATCH_BASE;
static uint32_t  s_bc_clock = 1;
static int       s_bc_on = 0;
static uint32_t  s_bc_hits = 0;
static uint32_t  s_bc_misses = 0;

static int bd_raw_read(int idx, uint64_t lba, uint32_t count, void *buf) {
    return s_ops[idx].read(s_ops[idx].ctx, lba, count, buf);
}
static int bd_raw_write(int idx, uint64_t lba, uint32_t count, const void *buf) {
    return s_ops[idx].write(s_ops[idx].ctx, lba, count, buf);
}

static uint32_t bc_set(int dev, uint64_t lba) {
    return (uint32_t)((lba + (uint64_t)dev * 131u) & (BC_SETS - 1));
}

static bc_line_t *bc_find(int dev, uint64_t lba) {
    bc_line_t *base = &s_bc[bc_set(dev, lba) * BC_WAYS];
    for (int w = 0; w < BC_WAYS; w++)
        if (base[w].valid && base[w].dev == dev && base[w].lba == lba)
            return &base[w];
    return 0;
}

static bc_line_t *bc_slot(int dev, uint64_t lba) {
    bc_line_t *base = &s_bc[bc_set(dev, lba) * BC_WAYS];
    bc_line_t *v = 0;
    for (int w = 0; w < BC_WAYS; w++)
        if (!base[w].valid) { v = &base[w]; break; }
    if (!v) {
        v = &base[0];
        for (int w = 1; w < BC_WAYS; w++)
            if (base[w].age < v->age) v = &base[w];
    }
    if (v->valid && v->dirty) bd_raw_write(v->dev, v->lba, 1, v->data);
    v->dev = dev;
    v->lba = lba;
    v->valid = 1;
    v->dirty = 0;
    v->age = s_bc_clock++;
    return v;
}

static int bc_read(int idx, uint64_t lba, uint32_t count, void *buf) {
    uint8_t *p = (uint8_t *)buf;
    uint32_t i = 0;
    while (i < count) {
        bc_line_t *l = bc_find(idx, lba + i);
        if (l) {
            memcpy(p + (size_t)i * 512u, l->data, 512);
            l->age = s_bc_clock++;
            s_bc_hits++;
            i++;
            continue;
        }
        uint32_t j = i + 1;
        while (j < count && !bc_find(idx, lba + j)) j++;
        if (bd_raw_read(idx, lba + i, j - i, p + (size_t)i * 512u) != 0) return -1;
        for (uint32_t k = i; k < j; k++) {
            bc_line_t *v = bc_slot(idx, lba + k);
            memcpy(v->data, p + (size_t)k * 512u, 512);
            s_bc_misses++;
        }
        i = j;
    }
    return 0;
}

static int bc_write(int idx, uint64_t lba, uint32_t count, const void *buf) {
    const uint8_t *p = (const uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) {
        bc_line_t *l = bc_find(idx, lba + i);
        if (!l) l = bc_slot(idx, lba + i);
        memcpy(l->data, p + (size_t)i * 512u, 512);
        l->dirty = 1;
        l->age = s_bc_clock++;
    }
    return 0;
}

void blockdev_cache_enable(int on) { s_bc_on = on ? 1 : 0; }
int  blockdev_cache_enabled(void) { return s_bc_on; }

void blockdev_cache_flush_all(void) {
    for (int i = 0; i < BC_SECTORS; i++) {
        if (s_bc[i].valid && s_bc[i].dirty) {
            bd_raw_write(s_bc[i].dev, s_bc[i].lba, 1, s_bc[i].data);
            s_bc[i].dirty = 0;
        }
    }
}

void blockdev_cache_invalidate(void) {
    for (int i = 0; i < BC_SECTORS; i++) {
        s_bc[i].valid = 0;
        s_bc[i].dirty = 0;
    }
}

void blockdev_cache_stats(uint32_t *hits, uint32_t *misses) {
    if (hits) *hits = s_bc_hits;
    if (misses) *misses = s_bc_misses;
}

void blockdev_set_scan_hook(void (*fn)(void)) { s_scan_hook = fn; }

int blockdev_register(const bdev_ops_t *ops) {
    if (!ops || s_count >= BLOCKDEV_MAX) return -1;
    s_ops[s_count] = *ops;
    s_count++;
    return 0;
}

int blockdev_init(void) {
    s_count = 0;
    blockdev_cache_invalidate();
    mmio_reset();
    blk_ata_scan();
    blk_ahci_scan();
    blk_nvme_scan();
    if (s_scan_hook) s_scan_hook();
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
    if (!s_bc_on || count == 0 || count > BC_BYPASS)
        return bd_raw_read(idx, lba, count, buf);
    return bc_read(idx, lba, count, buf);
}

int blockdev_write(int idx, uint64_t lba, uint32_t count, const void *buf) {
    if (idx < 0 || idx >= s_count || !s_ops[idx].write) return -1;
    if (!s_bc_on || count == 0 || count > BC_BYPASS)
        return bd_raw_write(idx, lba, count, buf);
    return bc_write(idx, lba, count, buf);
}

int blockdev_flush(int idx) {
    if (idx < 0 || idx >= s_count) return -1;
    if (s_bc_on) blockdev_cache_flush_all();
    if (!s_ops[idx].flush) return 0;
    return s_ops[idx].flush(s_ops[idx].ctx);
}

/* ---- Blok suruculere ozel basit MMIO mapper ----
 * mmio.c'deki paylasimli havuzlar bazi baglamlarda bozulabildigi icin,
 * her modulun kendi statik PD tablosuyla bagimsiz calisir. */
static uint64_t blk_pd_pool[4][512] __attribute__((aligned(4096)));
static int blk_pd_used = 0;

void blk_mmio_map(uint64_t phys, uint64_t size) {
    if (size == 0) size = 0x1000;
    if (phys + size <= 0x40000000ULL) return;   /* <1GB zaten mapli */

    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & ~0xFFFULL);
    int i4 = (int)((phys >> 39) & 0x1FF);
    if (!(pml4[i4] & 1)) return;                /* ust seviye yok: atla */
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[i4] & ~0xFFFULL);

    uint64_t start = phys & ~0x1FFFFFULL;
    uint64_t end = (phys + size + 0x1FFFFFULL) & ~0x1FFFFFULL;

    for (uint64_t a = start; a < end; a += 0x40000000ULL) {
        int i3 = (int)((a >> 30) & 0x1FF);
        if (pdpt[i3] & 0x80) continue;          /* 1GB sayfa: zaten mapli */
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

/* ---- PCI config uzayi (lib tarafi; kernel_plus'tan bagimsiz) ---- */
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
    if (lo & 1) return (uint64_t)(lo & ~0x3u);           /* IO */
    uint64_t addr = (uint64_t)(lo & ~0xFu);
    if (((lo >> 1) & 3) == 2) {
        uint32_t hi = blk_pci_readl(bus, slot, func, (uint8_t)(0x10 + bar * 4 + 4));
        addr |= (uint64_t)hi << 32;
    }
    return addr;
}

void blk_pci_enable(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t cmd = blk_pci_readl(bus, slot, func, 0x04);
    cmd |= 0x0006; /* memory space + bus master */
    blk_pci_writel(bus, slot, func, 0x04, cmd);
}
