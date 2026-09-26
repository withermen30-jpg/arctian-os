#include "mmio.h"
#ifdef ARCTIAN_BLK_SELFTEST
#include "serial.h"
#define MDBG(...) serial_printf(__VA_ARGS__)
#else
#define MDBG(...) do { } while (0)
#endif

#define MMIO_MAX_PDPT 4
#define MMIO_MAX_PD   8

static uint64_t pdpt_pool[MMIO_MAX_PDPT][512] __attribute__((aligned(4096)));
static int pdpt_used = 0;
static uint64_t pd_pool[MMIO_MAX_PD][512] __attribute__((aligned(4096)));
static int pd_used = 0;

static void zero_page(uint64_t *p) {
    volatile uint64_t *v = p;
    for (int i = 0; i < 512; i++) v[i] = 0;
}

void mmio_reset(void) {
    pdpt_used = 0;
    pd_used = 0;
}

int mmio_map(uint64_t phys, uint64_t size) {
    if (size == 0) size = 0x1000;
    uint64_t start = phys & ~0x1FFFFFULL;
    uint64_t end = (phys + size + 0x1FFFFFULL) & ~0x1FFFFFULL;
    if (end <= 0x40000000ULL) return 0;

    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & ~0xFFFULL);

    int pml4_i = (int)((start >> 39) & 0x1FF);
    MDBG("[mmio] phys=%llX start=%llX pml4=%llX e0=%llX\n",
         (unsigned long long)phys, (unsigned long long)start,
         (unsigned long long)(uintptr_t)pml4, (unsigned long long)pml4[pml4_i]);
    uint64_t *pdpt;
    if (!(pml4[pml4_i] & 1)) {
        if (pdpt_used >= MMIO_MAX_PDPT) return -1;
        pdpt = pdpt_pool[pdpt_used++];
        zero_page(pdpt);
        pml4[pml4_i] = (uint64_t)(uintptr_t)pdpt | 0x3;
    } else {
        pdpt = (uint64_t *)(uintptr_t)(pml4[pml4_i] & ~0xFFFULL);
    }

    for (uint64_t a = start; a < end; a += 0x40000000ULL) {
        int pdp_i = (int)((a >> 30) & 0x1FF);
        uint64_t *pd;
        if (!(pdpt[pdp_i] & 1)) {
            if (pd_used >= MMIO_MAX_PD) return -1;
            pd = pd_pool[pd_used++];
            zero_page(pd);
            pdpt[pdp_i] = (uint64_t)(uintptr_t)pd | 0x3;
        } else {
            pd = (uint64_t *)(uintptr_t)(pdpt[pdp_i] & ~0xFFFULL);
        }
        MDBG("[mmio] pdp_i=%d entry=%llX pd=%llX used=%d\n", pdp_i,
             (unsigned long long)pdpt[pdp_i],
             (unsigned long long)(uintptr_t)pd, pd_used);

        uint64_t seg_end = a + 0x40000000ULL;
        if (seg_end > end) seg_end = end;
        for (uint64_t b = a; b < seg_end; b += 0x200000ULL) {
            int pd_i = (int)((b >> 21) & 0x1FF);
            pd[pd_i] = b | 0x83ULL;
        }
    }

    MDBG("[mmio] loop done, reload cr3=%llX\n", (unsigned long long)cr3);
    asm volatile ("mov %0, %%cr3" :: "r"(cr3));
    MDBG("[mmio] done\n");
    return 0;
}
