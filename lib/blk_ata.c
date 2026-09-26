#include "blockdev.h"
#include "kernel.h"

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

static const uint16_t ATA_PORTS[2] = { 0x1F0, 0x170 };

typedef struct {
    uint16_t io;
    uint8_t  drive;
} ata_ctx_t;

static ata_ctx_t s_ctx[4];
static int s_ctxn = 0;

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

static int ata_rw(void *ctx, uint64_t lba, uint32_t count, void *buf, int write) {
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

static int ata_read(void *ctx, uint64_t lba, uint32_t count, void *buf) {
    return ata_rw(ctx, lba, count, buf, 0);
}
static int ata_write(void *ctx, uint64_t lba, uint32_t count, const void *buf) {
    return ata_rw(ctx, lba, count, (void *)buf, 1);
}
static int ata_flush(void *ctx) {
    ata_ctx_t *c = (ata_ctx_t *)ctx;
    outb(c->io + 7, 0xE7);
    return wait(c->io, 0);
}

static int ata_identify(uint16_t io, uint8_t drive, uint64_t *sectors_out) {
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
    return 1;
}

void blk_ata_scan(void) {
    s_ctxn = 0;
    for (int ch = 0; ch < 2; ch++) {
        for (int dr = 0; dr < 2; dr++) {
            uint64_t secs = 0;
            if (!ata_identify(ATA_PORTS[ch], (uint8_t)dr, &secs)) continue;
            if (secs == 0) continue;
            if (s_ctxn >= 4) return;

            ata_ctx_t *c = &s_ctx[s_ctxn++];
            c->io = ATA_PORTS[ch];
            c->drive = (uint8_t)dr;

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
