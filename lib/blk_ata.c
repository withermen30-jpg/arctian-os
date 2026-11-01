/* ATA (IDE / legacy SATA) blok cihazi. LBA28, bus-master DMA + PIO yedegi. */
#include "blockdev.h"
#include "kernel.h"
#include "memory_map.h"

#define ATA_DATA     0
#define ATA_SECCOUNT 2
#define ATA_LBA0     3
#define ATA_LBA1     4
#define ATA_LBA2     5
#define ATA_HDDEVSEL 6
#define ATA_STATUS   7
#define ATA_COMMAND  7

#define ATA_SR_BSY   0x80
#define ATA_SR_DRQ   0x08
#define ATA_SR_DF    0x20
#define ATA_SR_ERR   0x01

#define ATA_DMA_MAX  128          /* tek komutta en fazla 64 KB */

#define BM_CMD   0
#define BM_STATUS 2
#define BM_PRDT  4

static const uint16_t ATA_PORTS[2] = { 0x1F0, 0x170 };

typedef struct {
    uint16_t io;
    uint16_t bm;
    uint8_t  drive;
    uint8_t  dma;
} ata_ctx_t;

static ata_ctx_t s_ctx[4];
static int s_ctxn = 0;

typedef struct {
    uint32_t base;
    uint16_t count;
    uint16_t flags;
} __attribute__((packed)) ata_prd_t;

static uint16_t  s_bm_base[2] = { 0, 0 };
static ata_prd_t s_prdt[2] __attribute__((aligned(4096)));
static uint8_t  *const s_dma = (uint8_t *)(uintptr_t)(BLK_SCRATCH_BASE + 0x80000u);
static int       s_dma_on = 1;

void blk_ata_set_dma(int on) { s_dma_on = on ? 1 : 0; }
int  blk_ata_dma_enabled(void) { return s_dma_on; }

static void dly(uint16_t io) { for (int i = 0; i < 4; i++) (void)inb(io + 7); }

static int wait(uint16_t io, int drq) {
    for (long i = 0; i < 2000000L; i++) {
        uint8_t s = inb(io + 7);
        if (s & ATA_SR_BSY) continue;
        if (s & (ATA_SR_ERR | ATA_SR_DF)) return -1;
        if (!drq || (s & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

static int ata_pio(void *ctx, uint64_t lba, uint32_t count, void *buf, int write) {
    ata_ctx_t *c = (ata_ctx_t *)ctx;
    if (lba > 0x0FFFFFFFu) return -1;
    uint8_t *p = (uint8_t *)buf;
    for (; count; count--, lba++, p += 512) {
        if (wait(c->io, 0) != 0) return -1;
        outb(c->io + 6, (uint8_t)(0xE0 | (c->drive << 4) | ((lba >> 24) & 0x0F)));
        dly(c->io);
        outb(c->io + 2, 1);
        outb(c->io + 3, (uint8_t)lba);
        outb(c->io + 4, (uint8_t)(lba >> 8));
        outb(c->io + 5, (uint8_t)(lba >> 16));
        outb(c->io + 7, write ? 0x30 : 0x20);
        if (wait(c->io, 1) != 0) return -1;
        uint16_t *w = (uint16_t *)p;
        if (write) {
            for (int i = 0; i < 256; i++) outw(c->io + 0, w[i]);
            outb(c->io + 7, 0xE7);
            (void)wait(c->io, 0);
        } else {
            for (int i = 0; i < 256; i++) w[i] = inw(c->io + 0);
        }
    }
    return 0;
}

static void ata_find_bmdma(void) {
    for (int bus = 0; bus < 4; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = blk_pci_readl((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x00);
                if ((id & 0xFFFFu) == 0xFFFFu || id == 0) continue;
                uint32_t cls = blk_pci_readl((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x08);
                if (((cls >> 24) & 0xFFu) != 0x01u || ((cls >> 16) & 0xFFu) != 0x01u) continue;
                uint32_t bar4 = blk_pci_readl((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x20);
                if (!(bar4 & 1u)) continue;
                s_bm_base[0] = (uint16_t)(bar4 & 0xFFFCu);
                s_bm_base[1] = (uint16_t)(s_bm_base[0] + 8u);
                uint32_t cmd = blk_pci_readl((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x04);
                blk_pci_writel((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x04, cmd | 0x0007u);
                return;
            }
        }
    }
}

static int ata_dma_one(ata_ctx_t *c, uint32_t lba, uint32_t count, int write) {
    uint16_t io = c->io;
    uint16_t bm = c->bm;
    uint32_t bytes = count * 512u;

    if (wait(io, 0) != 0) return -1;

    s_prdt[0].base = (uint32_t)(uintptr_t)s_dma;
    s_prdt[0].count = (uint16_t)(bytes & 0xFFFFu);
    s_prdt[0].flags = 0x8000;

    outb(bm + BM_STATUS, 0x06);
    outl(bm + BM_PRDT, (uint32_t)(uintptr_t)s_prdt);
    outb(bm + BM_CMD, (uint8_t)(write ? 0x00 : 0x08));

    outb(io + 6, (uint8_t)(0xE0 | (c->drive << 4) | ((lba >> 24) & 0x0F)));
    dly(io);
    outb(io + 2, (uint8_t)(count & 0xFFu));
    outb(io + 3, (uint8_t)lba);
    outb(io + 4, (uint8_t)(lba >> 8));
    outb(io + 5, (uint8_t)(lba >> 16));
    outb(io + 7, write ? 0xCA : 0xC8);
    outb(bm + BM_CMD, (uint8_t)((write ? 0x00 : 0x08) | 0x01));

    int rc = -1;
    uint8_t last = 0;
    for (long i = 0; i < 100000000L; i++) {
        last = inb(bm + BM_STATUS);
        if (!(last & 0x01)) { rc = (last & 0x02) ? -1 : 0; break; }
    }
    outb(bm + BM_CMD, write ? 0x00 : 0x08);
    outb(bm + BM_STATUS, 0x06);

    uint8_t ast = inb(io + 7);
    if (ast & (ATA_SR_ERR | ATA_SR_DF)) rc = -1;
    return rc;
}

static int ata_dma(void *ctx, uint64_t lba, uint32_t count, void *buf, int write) {
    ata_ctx_t *c = (ata_ctx_t *)ctx;
    if (!s_dma_on || !c->bm || lba > 0x0FFFFFFFu) return -1;
    uint8_t *p = (uint8_t *)buf;
    while (count) {
        uint32_t chunk = count > ATA_DMA_MAX ? ATA_DMA_MAX : count;
        uint32_t bytes = chunk * 512u;
        if (write) memcpy(s_dma, p, bytes);
        if (ata_dma_one(c, (uint32_t)lba, chunk, write) != 0) return -1;
        if (!write) memcpy(p, s_dma, bytes);
        lba += chunk;
        count -= chunk;
        p += bytes;
    }
    return 0;
}

/* Aygit yazilim reset'i (SRST): DMA takilip birakildiginda surucu hala
 * eski komutunu isliyor olabilir; dogrudan PIO'ya dusmek o komutun atik
 * verisini "okundu" sayardi. Once DMA motoru durdurulur, sonra SRST. */
static void ata_reset(ata_ctx_t *c) {
    if (c->bm) {
        outb(c->bm + BM_CMD, 0x00);
        outb(c->bm + BM_STATUS, 0x06);
    }
    outb(c->io + 0x206, 0x04);              /* SRST bitini bas */
    for (volatile int i = 0; i < 4000; i++) {}
    outb(c->io + 0x206, 0x00);              /* birak */
    (void)wait(c->io, 0);
}

static int ata_read(void *ctx, uint64_t lba, uint32_t count, void *buf) {
    ata_ctx_t *c = (ata_ctx_t *)ctx;
    if (c->dma && s_dma_on) {
        if (ata_dma(ctx, lba, count, buf, 0) == 0) return 0;
        c->dma = 0;
        ata_reset(c);
    }
    return ata_pio(ctx, lba, count, buf, 0);
}
static int ata_write(void *ctx, uint64_t lba, uint32_t count, const void *buf) {
    ata_ctx_t *c = (ata_ctx_t *)ctx;
    if (c->dma && s_dma_on) {
        if (ata_dma(ctx, lba, count, (void *)buf, 1) == 0) return 0;
        c->dma = 0;
        ata_reset(c);
    }
    return ata_pio(ctx, lba, count, (void *)buf, 1);
}
static int ata_flush(void *ctx) {
    ata_ctx_t *c = (ata_ctx_t *)ctx;
    outb(c->io + 7, 0xE7);
    return wait(c->io, 0);
}

static int ata_identify(uint16_t io, uint8_t drive, uint64_t *sectors_out, int *dma_out) {
    outb(io + 6, (uint8_t)(0xA0 | (drive << 4)));
    for (int i = 0; i < 4; i++) (void)inb(io + 7);
    outb(io + 2, 0); outb(io + 3, 0); outb(io + 4, 0); outb(io + 5, 0);
    outb(io + 7, 0xEC);
    for (int i = 0; i < 4; i++) (void)inb(io + 7);
    uint8_t st = inb(io + 7);
    if (st == 0) return 0;
    if (wait(io, 1) != 0) return 0;

    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(io + 0);

    uint32_t lba28 = ((uint32_t)id[61] << 16) | id[60];
    *sectors_out = lba28;
    if (dma_out) *dma_out = (id[49] & 0x0100) ? 1 : 0;
    return 1;
}

void blk_ata_scan(void) {
    s_ctxn = 0;
    ata_find_bmdma();
    for (int ch = 0; ch < 2; ch++) {
        for (int dr = 0; dr < 2; dr++) {
            uint64_t secs = 0;
            int dmacap = 0;
            if (!ata_identify(ATA_PORTS[ch], (uint8_t)dr, &secs, &dmacap)) continue;
            if (secs == 0) continue;
            if (s_ctxn >= 4) return;

            ata_ctx_t *c = &s_ctx[s_ctxn++];
            c->io = ATA_PORTS[ch];
            c->bm = s_bm_base[ch];
            c->drive = (uint8_t)dr;
            c->dma = (uint8_t)(dmacap && s_bm_base[ch] ? 1 : 0);

            bdev_ops_t ops;
            for (int i = 0; i < 24; i++) ops.name[i] = 0;
            const char *pfx = "ata";
            for (int i = 0; pfx[i]; i++) ops.name[i] = pfx[i];
            ops.name[3] = (char)('0' + ch);
            ops.name[4] = (char)('0' + dr);
            ops.kind = BDEV_KIND_ATA;
            ops.sectors = secs;
            ops.removable = 0;
            ops.ctx = c;
            ops.read = ata_read;
            ops.write = ata_write;
            ops.flush = ata_flush;
            blockdev_register(&ops);
        }
    }
}
