#include "blockdev.h"
#include "mmio.h"
#include "kernel.h"
#ifdef ARCTIAN_BLK_SELFTEST
#include "serial.h"
#define NDBG(...) serial_printf(__VA_ARGS__)
#else
#define NDBG(...) do { } while (0)
#endif

#define NVME_AQ_ENTRIES 16
#define NVME_IO_ENTRIES 64
#define NVME_PAGE       4096

static uint32_t s_asq[NVME_AQ_ENTRIES * 16] __attribute__((aligned(NVME_PAGE)));
static uint32_t s_acq[NVME_AQ_ENTRIES * 4]  __attribute__((aligned(NVME_PAGE)));
static uint32_t s_iosq[NVME_IO_ENTRIES * 16] __attribute__((aligned(NVME_PAGE)));
static uint32_t s_iocq[NVME_IO_ENTRIES * 4]  __attribute__((aligned(NVME_PAGE)));
static uint8_t  s_dma[NVME_PAGE]            __attribute__((aligned(NVME_PAGE)));
static uint8_t  s_ident[2 * NVME_PAGE]      __attribute__((aligned(NVME_PAGE)));

static volatile uint32_t *g_mmio;
static uint32_t g_stride;
static int      g_phase = 1;
static uint32_t g_asq_tail, g_acq_head;
static uint32_t g_sq_tail, g_cq_head;
static int      g_io_phase = 1;
static uint32_t g_nsid = 1;
static uint32_t g_lba_shift = 9;
static uint64_t g_nsze = 0;
static int      g_ready = 0;

#define NVME_REG_CAP   0x00
#define NVME_REG_CC    0x14
#define NVME_REG_CST   0x1C
#define NVME_REG_AQA   0x24
#define NVME_REG_ASQ   0x28
#define NVME_REG_ACQ   0x30

static uint32_t nvme_rd(uint32_t off) { return g_mmio[off / 4]; }
static void nvme_wr(uint32_t off, uint32_t v) { g_mmio[off / 4] = v; }

static void nvme_doorbell(uint32_t qid, uint32_t tail, int is_cq) {
    uint32_t off = 0x1000 + (qid * 2 + (is_cq ? 1 : 0)) * g_stride;
    g_mmio[off / 4] = tail;
}

static int nvme_wait_ready(int ready) {
    for (long t = 0; t < 10000000L; t++) {
        uint32_t cst = nvme_rd(NVME_REG_CST);
        if (ready ? (cst & 1) : !(cst & 1)) return 0;
    }
    return -1;
}

static int nvme_admin_once(uint32_t *sqe, uint32_t *result, int wait_iters) {
    uint32_t *slot = &s_asq[g_asq_tail * 16];
    for (int i = 0; i < 16; i++) slot[i] = sqe[i];
    g_asq_tail = (g_asq_tail + 1) % NVME_AQ_ENTRIES;
    nvme_doorbell(0, g_asq_tail, 0);

    for (long t = 0; t < wait_iters; t++) {
        if ((t & 0xFF) == 0) outb(0x80, 0);
        uint32_t *cqe = &s_acq[g_acq_head * 4];
        uint32_t dw3 = cqe[3];
        uint16_t sf = (uint16_t)(dw3 >> 16);
        if ((int)(sf & 1) == g_phase) {
            uint32_t status = (sf >> 1) & 0x7FFF;
            if (result) *result = cqe[0];
            g_acq_head = (g_acq_head + 1) % NVME_AQ_ENTRIES;
            if (g_acq_head == 0) g_phase ^= 1;
            nvme_doorbell(0, g_acq_head, 1);
            return status == 0 ? 0 : -1;
        }
    }
    return -1;
}

static int nvme_admin(uint32_t *sqe, uint32_t *result) {
    for (int attempt = 0; attempt < 12; attempt++) {
        if (nvme_admin_once(sqe, result, 400000) == 0) return 0;
    }
    NDBG("[nvme] admin fail phase=%d head=%d tail=%d\n", g_phase, g_acq_head, g_asq_tail);
    return -1;
}

static int nvme_io_once(uint32_t *sqe, int wait_iters) {
    uint32_t *slot = &s_iosq[g_sq_tail * 16];
    for (int i = 0; i < 16; i++) slot[i] = sqe[i];
    g_sq_tail = (g_sq_tail + 1) % NVME_IO_ENTRIES;
    nvme_doorbell(1, g_sq_tail, 0);

    for (long t = 0; t < wait_iters; t++) {
        if ((t & 0xFF) == 0) outb(0x80, 0);
        uint32_t *cqe = &s_iocq[g_cq_head * 4];
        uint32_t dw3 = cqe[3];
        uint16_t sf = (uint16_t)(dw3 >> 16);
        if ((int)(sf & 1) == g_io_phase) {
            uint32_t status = (sf >> 1) & 0x7FFF;
            g_cq_head = (g_cq_head + 1) % NVME_IO_ENTRIES;
            if (g_cq_head == 0) g_io_phase ^= 1;
            nvme_doorbell(1, g_cq_head, 1);
            return status == 0 ? 0 : -1;
        }
    }
    return -1;
}

static int nvme_io(uint32_t *sqe) {
    for (int attempt = 0; attempt < 12; attempt++) {
        if (nvme_io_once(sqe, 400000) == 0) return 0;
    }
    return -1;
}

static void nvme_mk_sqe(uint32_t *s, uint8_t opcode, uint32_t cid) {
    for (int i = 0; i < 16; i++) s[i] = 0;
    s[0] = (uint32_t)opcode | (cid << 16);
}

static int nvme_transfer(uint64_t lba512, uint32_t count512, void *buf, int write) {
    uint32_t per = (1u << g_lba_shift) / 512u;
    if (per == 0) per = 1;
    uint8_t *p = (uint8_t *)buf;

    while (count512) {
        if (lba512 % per) return -1;
        uint32_t lba = (uint32_t)(lba512 / per);
        uint32_t avail = count512 / per;
        uint32_t maxs = NVME_PAGE / (1u << g_lba_shift);
        if (maxs == 0) maxs = 1;
        uint32_t blocks = (avail < maxs) ? avail : maxs;
        if (blocks == 0) return -1;
        uint32_t chunk_bytes = blocks * (1u << g_lba_shift);

        uint32_t s[16];
        nvme_mk_sqe(s, write ? 0x01 : 0x02, (uint32_t)(lba & 0xFFFF));
        s[1] = g_nsid;
        s[6] = (uint32_t)(uintptr_t)s_dma;
        s[7] = 0;
        s[10] = lba;
        s[11] = 0;
        s[12] = (blocks - 1) & 0xFFFF;

        if (write) memcpy(s_dma, p, chunk_bytes);
        if (nvme_io(s) != 0) return -1;
        if (!write) memcpy(p, s_dma, chunk_bytes);

        p += chunk_bytes;
        lba512 += chunk_bytes / 512;
        count512 -= chunk_bytes / 512;
    }
    return 0;
}

static int nvme_read(void *ctx, uint64_t lba, uint32_t count, void *buf) {
    (void)ctx; return nvme_transfer(lba, count, buf, 0);
}
static int nvme_write(void *ctx, uint64_t lba, uint32_t count, const void *buf) {
    (void)ctx; return nvme_transfer(lba, count, (void *)buf, 1);
}
static int nvme_flush(void *ctx) { (void)ctx; return 0; }

static void nvme_register(void) {
    bdev_ops_t ops;
    for (int i = 0; i < 24; i++) ops.name[i] = 0;
    const char *nm = "nvme0";
    for (int i = 0; nm[i]; i++) ops.name[i] = nm[i];
    ops.kind = BDEV_KIND_NVME;
    ops.sectors = g_nsze << (g_lba_shift - 9);
    ops.removable = 0;
    ops.ctx = 0;
    ops.read = nvme_read;
    ops.write = nvme_write;
    ops.flush = nvme_flush;
    blockdev_register(&ops);
}

void blk_nvme_scan(void) {
    if (g_ready) { nvme_register(); return; }

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t id = blk_pci_readl((uint8_t)bus, (uint8_t)slot, 0, 0x08);
            uint8_t cc = (uint8_t)(id >> 24), sc = (uint8_t)(id >> 16), pi = (uint8_t)(id >> 8);
            if (cc != 0x01 || sc != 0x08 || pi != 0x02) continue;

            blk_pci_enable((uint8_t)bus, (uint8_t)slot, 0);
            uint64_t bar = blk_pci_bar((uint8_t)bus, (uint8_t)slot, 0, 0) & ~0xFULL;
            NDBG("[nvme] bus=%d slot=%d bar=%llX\n", bus, slot, (unsigned long long)bar);
            if (!bar) continue;
            blk_mmio_map(bar, 0x4000);
            g_mmio = (volatile uint32_t *)(uintptr_t)bar;

            uint64_t cap = (uint64_t)nvme_rd(NVME_REG_CAP) |
                           ((uint64_t)nvme_rd(NVME_REG_CAP + 4) << 32);
            uint32_t dstrd = (uint32_t)((cap >> 32) & 0xF);
            g_stride = 4u << dstrd;
            NDBG("[nvme] cap=%llX dstrd=%d\n", (unsigned long long)cap, dstrd);

            nvme_wr(NVME_REG_CC, 0);
            if (nvme_wait_ready(0) != 0) { NDBG("[nvme] disable-timeout cst=%08X\n", nvme_rd(NVME_REG_CST)); continue; }

            nvme_wr(NVME_REG_AQA, (NVME_AQ_ENTRIES - 1) | ((NVME_AQ_ENTRIES - 1) << 16));
            nvme_wr(NVME_REG_ASQ, (uint32_t)(uintptr_t)s_asq);
            nvme_wr(NVME_REG_ASQ + 4, 0);
            nvme_wr(NVME_REG_ACQ, (uint32_t)(uintptr_t)s_acq);
            nvme_wr(NVME_REG_ACQ + 4, 0);

            uint32_t ccfg = (6u << 16) | (4u << 20) | 1u;
            nvme_wr(NVME_REG_CC, ccfg);
            NDBG("[nvme] cc_wr=%08X cc_rd=%08X cst=%08X aqa=%08X asq=%08X acq=%08X\n",
                 ccfg, nvme_rd(NVME_REG_CC), nvme_rd(NVME_REG_CST),
                 nvme_rd(NVME_REG_AQA), nvme_rd(NVME_REG_ASQ), nvme_rd(NVME_REG_ACQ));
            if (nvme_wait_ready(1) != 0) { NDBG("[nvme] enable-timeout cst=%08X\n", nvme_rd(NVME_REG_CST)); continue; }

            uint32_t s[16];
            nvme_mk_sqe(s, 0x06, 1);
            s[1] = 1;
            s[6] = (uint32_t)(uintptr_t)s_ident;
            s[10] = 0;
            if (nvme_admin(s, 0) != 0) { NDBG("[nvme] identify-ns fail\n"); continue; }

            uint64_t nsze;
            memcpy(&nsze, s_ident + 0, 8);
            uint8_t flbas = s_ident[26];
            uint8_t lbaf_idx = flbas & 0xF;
            uint8_t lbads = s_ident[128 + lbaf_idx * 4 + 2];
            if (lbads >= 9 && lbads <= 12) g_lba_shift = lbads;
            else g_lba_shift = 9;
            g_nsze = nsze;
            g_nsid = 1;
            NDBG("[nvme] nsze=%d lbads=%d\n", (int)nsze, (int)lbads);

            nvme_mk_sqe(s, 0x05, 2);
            s[6] = (uint32_t)(uintptr_t)s_iocq;
            s[10] = 1 | ((NVME_IO_ENTRIES - 1) << 16);
            s[11] = 1;
            { int r1 = nvme_admin(s, 0); NDBG("[nvme] create-cq r=%d\n", r1); if (r1) continue; }

            nvme_mk_sqe(s, 0x01, 3);
            s[6] = (uint32_t)(uintptr_t)s_iosq;
            s[10] = 1 | ((NVME_IO_ENTRIES - 1) << 16);
            s[11] = 1 | (1u << 16);
            { int r2 = nvme_admin(s, 0); NDBG("[nvme] create-sq r=%d\n", r2); if (r2) continue; }

            nvme_register();
            g_ready = 1;
            return;
        }
    }
}
