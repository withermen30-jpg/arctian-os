#include "blockdev.h"
#include "mmio.h"
#include "kernel.h"
#ifdef ARCTIAN_BLK_SELFTEST
#include "serial.h"
#define BDBG(...) serial_printf(__VA_ARGS__)
#else
#define BDBG(...) do { } while (0)
#endif

#define AHCI_MAX_PORT 8

static uint8_t  s_cl[AHCI_MAX_PORT][1024]  __attribute__((aligned(1024)));
static uint8_t  s_fis[AHCI_MAX_PORT][256]  __attribute__((aligned(256)));
static uint8_t  s_ct[AHCI_MAX_PORT][256]   __attribute__((aligned(128)));
static uint8_t  s_dma[65536]               __attribute__((aligned(4096)));

typedef struct {
    volatile uint32_t *p;
    uint8_t *cl;
    uint8_t *fis;
    uint8_t *ct;
    int      port;
} ahci_ctx_t;

static ahci_ctx_t s_ctx[AHCI_MAX_PORT];
static int s_ctxn = 0;

static int ahci_port_stop(volatile uint32_t *p) {
    p[0x18/4] &= ~1u;
    long i;
    for (i = 0; i < 1000000L; i++) if (!(p[0x18/4] & (1u << 15))) break;
    p[0x18/4] &= ~(1u << 4);
    for (i = 0; i < 1000000L; i++) if (!(p[0x18/4] & (1u << 14))) break;
    return 0;
}

static int ahci_cmd(ahci_ctx_t *c, int write, uint8_t *fis, int fis_dw,
                    void *buf, uint32_t bytes) {
    volatile uint32_t *p = c->p;
    long t;

    for (t = 0; t < 2000000L; t++) {
        if (!(p[0x38/4] & 1u) && !(p[0x20/4] & 0x88)) break;
    }
    p[0x10/4] = 0xFFFFFFFFu;

    uint32_t *h = (uint32_t *)c->cl;
    h[0] = (uint32_t)(fis_dw & 0x1F);
    if (write) h[0] |= (1u << 6);
    int prdtl = bytes ? 1 : 0;
    h[0] |= ((uint32_t)prdtl << 16);
    h[1] = 0;
    h[2] = (uint32_t)(uintptr_t)c->ct;
    h[3] = 0;

    memcpy(c->ct, fis, (size_t)fis_dw * 4);

    if (bytes) {
        uint32_t *prd = (uint32_t *)(c->ct + 0x80);
        prd[0] = (uint32_t)(uintptr_t)buf;
        prd[1] = 0;
        prd[2] = 0;
        prd[3] = ((bytes - 1) & 0x3FFFFFu) | (1u << 31);
    }

    p[0x38/4] = 1u;

    int err = 0;
    for (t = 0; t < 10000000L; t++) {
        uint32_t is = p[0x10/4];
        if (is & ((1u << 28) | (1u << 29) | (1u << 5))) {
            BDBG("[ahci] is=%08X tfd=%08X\n", is, p[0x20/4]);
            p[0x10/4] = is; err = -1; break;
        }
        if (!(p[0x38/4] & 1u)) break;
    }
    if ((p[0x38/4] & 1u) && !err) { BDBG("[ahci] timeout ci=%08X\n", p[0x38/4]); err = -1; }
    p[0x10/4] = p[0x10/4];
    return err;
}

static void ahci_fis(uint8_t *f, uint8_t cmd, uint64_t lba, uint16_t count) {
    for (int i = 0; i < 20; i++) f[i] = 0;
    f[0] = 0x27;
    f[1] = 0x80;
    f[2] = cmd;
    f[4] = (uint8_t)lba;
    f[5] = (uint8_t)(lba >> 8);
    f[6] = (uint8_t)(lba >> 16);
    f[7] = 0xE0;
    f[8] = (uint8_t)(lba >> 24);
    f[9] = (uint8_t)(lba >> 32);
    f[10] = (uint8_t)(lba >> 40);
    f[12] = (uint8_t)count;
    f[13] = (uint8_t)(count >> 8);
}

static int ahci_transfer(ahci_ctx_t *c, int write, uint64_t lba, uint32_t count,
                         void *buf) {
    uint8_t *p = (uint8_t *)buf;
    while (count) {
        uint32_t chunk = count;
        if (chunk > 128) chunk = 128;
        uint8_t fis[20];
        ahci_fis(fis, write ? 0x35 : 0x25, lba, (uint16_t)chunk);
        BDBG("[ahci] xfer wr=%d lba=%d chunk=%d fis=%02X%02X%02X%02X %02X%02X%02X%02X c=%02X%02X\n",
             write, (int)lba, (int)chunk,
             fis[0], fis[1], fis[2], fis[3], fis[4], fis[5], fis[6], fis[7], fis[12], fis[13]);
        if (!write) {
            if (ahci_cmd(c, 0, fis, 5, s_dma, chunk * 512) != 0) return -1;
            BDBG("[ahci] rd dma0=%02X%02X dma510=%02X%02X\n",
                 s_dma[0], s_dma[1], s_dma[510], s_dma[511]);
            memcpy(p, s_dma, chunk * 512);
        } else {
            memcpy(s_dma, p, chunk * 512);
            if (ahci_cmd(c, 1, fis, 5, s_dma, chunk * 512) != 0) return -1;
        }
        p += chunk * 512;
        lba += chunk;
        count -= chunk;
    }
    return 0;
}

static int ahci_read(void *ctx, uint64_t lba, uint32_t count, void *buf) {
    return ahci_transfer((ahci_ctx_t *)ctx, 0, lba, count, buf);
}
static int ahci_write(void *ctx, uint64_t lba, uint32_t count, const void *buf) {
    return ahci_transfer((ahci_ctx_t *)ctx, 1, lba, count, (void *)buf);
}
static int ahci_flush(void *ctx) {
    ahci_ctx_t *c = (ahci_ctx_t *)ctx;
    uint8_t fis[20];
    ahci_fis(fis, 0xE7, 0, 0);
    return ahci_cmd(c, 0, fis, 5, 0, 0);
}

static int ahci_port_init(ahci_ctx_t *c) {
    volatile uint32_t *p = c->p;

    ahci_port_stop(p);
    p[0x00/4] = (uint32_t)(uintptr_t)c->cl;
    p[0x04/4] = 0;
    p[0x08/4] = (uint32_t)(uintptr_t)c->fis;
    p[0x0C/4] = 0;
    p[0x10/4] = 0xFFFFFFFFu;
    p[0x30/4] = 0xFFFFFFFFu;
    memset(c->cl, 0, 1024);
    memset(c->fis, 0, 256);
    p[0x18/4] |= (1u << 4) | (1u << 2) | (1u << 1) | (1u << 28);
    for (long t = 0; t < 1000000L; t++) if (!(p[0x18/4] & (1u << 14))) break;
    p[0x18/4] |= 1u;
    BDBG("[ahci] clb=%08X fb=%08X cmd=%08X\n",
         p[0x00/4], p[0x08/4], p[0x18/4]);
    return 0;
}

static int ahci_identify(ahci_ctx_t *c, uint64_t *sectors) {
    uint8_t fis[20];
    ahci_fis(fis, 0xEC, 0, 1);
    fis[7] = 0xA0;
    for (int i = 0; i < 512; i++) s_dma[i] = 0xAA;
    int rc = ahci_cmd(c, 0, fis, 5, s_dma, 512);
    BDBG("[ahci] prefill0=%04X %04X %04X\n",
         ((uint16_t *)s_dma)[0], ((uint16_t *)s_dma)[1], ((uint16_t *)s_dma)[2]);
    {
        uint32_t *hh = (uint32_t *)c->cl;
        uint32_t *prd = (uint32_t *)(c->ct + 0x80);
        BDBG("[ahci] rc=%d h0=%08X h1=%08X h2=%08X ct0=%08X prd0=%08X prd3=%08X\n",
             rc, hh[0], hh[1], hh[2],
             ((uint32_t *)c->ct)[0], prd[0], prd[3]);
        BDBG("[ahci] tfd=%08X is=%08X ci=%08X\n",
             c->p[0x20/4], c->p[0x10/4], c->p[0x38/4]);
    }
    if (rc != 0) return -1;
    uint16_t *id = (uint16_t *)s_dma;
    BDBG("[ahci] id[0]=%04X id[60]=%04X id[61]=%04X\n", id[0], id[60], id[61]);
    uint64_t lba48 = 0;
    if (id[83] & (1u << 10)) {
        lba48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    }
    uint32_t lba28 = ((uint32_t)id[61] << 16) | id[60];
    *sectors = lba48 ? lba48 : lba28;
    return 0;
}

void blk_ahci_scan(void) {
    BDBG("[ahci] scan\n");
    s_ctxn = 0;
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t id = blk_pci_readl((uint8_t)bus, (uint8_t)slot, 0, 0x08);
            uint8_t cc = (uint8_t)(id >> 24), sc = (uint8_t)(id >> 16), pi = (uint8_t)(id >> 8);
            if (cc != 0x01 || sc != 0x06 || pi != 0x01) continue;

            blk_pci_enable((uint8_t)bus, (uint8_t)slot, 0);
            uint64_t abar = blk_pci_bar((uint8_t)bus, (uint8_t)slot, 0, 5) & ~0xFULL;
            BDBG("[ahci] bus=%d slot=%d bar5=%llX\n", bus, slot, (unsigned long long)abar);
            if (!abar) continue;
            blk_mmio_map(abar, 0x2000);

            volatile uint32_t *hba = (volatile uint32_t *)(uintptr_t)abar;
            hba[0x04/4] |= (1u << 31);
            uint32_t pi_ports = hba[0x0C/4];
            BDBG("[ahci] cap=%08X pi=%08X\n", hba[0x00/4], pi_ports);
            if (pi_ports == 0) pi_ports = 0x1;

            for (int pr = 0; pr < 32 && s_ctxn < AHCI_MAX_PORT; pr++) {
                if (!(pi_ports & (1u << pr))) continue;
                volatile uint32_t *pp = (volatile uint32_t *)(uintptr_t)(abar + 0x100 + 0x80 * pr);
                uint32_t ssts = pp[0x28/4];
                uint32_t sig = pp[0x24/4];
                BDBG("[ahci] port=%d ssts=%08X sig=%08X\n", pr, ssts, sig);
                if ((ssts & 0xF) != 3) continue;
                if (sig != 0x00000101) continue;

                ahci_ctx_t *c = &s_ctx[s_ctxn];
                c->p = pp;
                c->cl = s_cl[s_ctxn];
                c->fis = s_fis[s_ctxn];
                c->ct = s_ct[s_ctxn];
                c->port = pr;

                if (ahci_port_init(c) != 0) continue;
                uint64_t secs = 0;
                int ir = ahci_identify(c, &secs);
                BDBG("[ahci] identify=%d secs=%d\n", ir, (int)secs);
                if (ir != 0 || secs == 0) { ahci_port_stop(pp); continue; }

                bdev_ops_t ops;
                for (int i = 0; i < 24; i++) ops.name[i] = 0;
                ops.name[0] = 's'; ops.name[1] = 'a'; ops.name[2] = 't'; ops.name[3] = 'a';
                ops.name[4] = (char)('0' + (pr % 10));
                ops.kind = BDEV_KIND_AHCI;
                ops.sectors = secs;
                ops.removable = 0;
                ops.ctx = c;
                ops.read = ahci_read;
                ops.write = ahci_write;
                ops.flush = ahci_flush;
                blockdev_register(&ops);
                s_ctxn++;
            }
        }
    }
}
