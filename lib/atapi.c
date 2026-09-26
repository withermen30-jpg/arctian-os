#include "atapi.h"
#include "kernel.h"

#define R_DATA  0
#define R_FEAT  1
#define R_SEC   2
#define R_LBA0  3
#define R_LBA1  4
#define R_LBA2  5
#define R_DEV   6
#define R_CMD   7

#define SR_BSY  0x80
#define SR_DRDY 0x40
#define SR_DF   0x20
#define SR_DRQ  0x08
#define SR_ERR  0x01

static const uint16_t PORTS[2] = { 0x1F0, 0x170 };
static uint16_t s_io = 0x170;
static uint8_t  s_drive = 0;
static int s_present = 0;

static void dly(void) { for (int i = 0; i < 4; i++) (void)inb(s_io + R_CMD); }

static int wait_bsy_clear(void) {
    for (long i = 0; i < 2000000L; i++) {
        uint8_t s = inb(s_io + R_CMD);
        if (!(s & SR_BSY)) return 0;
    }
    return -1;
}

static int wait_drq(void) {
    for (long i = 0; i < 2000000L; i++) {
        uint8_t s = inb(s_io + R_CMD);
        if (s & (SR_ERR | SR_DF)) return -1;
        if (!(s & SR_BSY) && (s & SR_DRQ)) return 0;
    }
    return -1;
}

static uint8_t devsel(void) { return (uint8_t)(0xA0 | (s_drive << 4)); }

static int probe_at(uint16_t io, uint8_t drive) {
    s_io = io;
    s_drive = drive;

    outb(io + R_DEV, devsel());
    dly();
    outb(io + R_SEC, 0);
    outb(io + R_LBA0, 0);
    outb(io + R_LBA1, 0);
    outb(io + R_LBA2, 0);
    outb(io + R_CMD, 0xA1);
    dly();

    uint8_t st = inb(io + R_CMD);
    if (st == 0x00 || st == 0xFF) return 0;
    if (wait_bsy_clear() != 0) return 0;

    if (wait_drq() != 0) return 0;
    for (int i = 0; i < 256; i++) (void)inw(io + R_DATA);
    return 1;
}

int atapi_init(void) {
    s_present = 0;
    for (int ch = 0; ch < 2; ch++) {
        for (int d = 0; d < 2; d++) {
            if (probe_at(PORTS[ch], (uint8_t)d)) {
                s_present = 1;
                return 0;
            }
        }
    }
    return -1;
}

int atapi_read(uint32_t lba, uint32_t count, void *buf) {
    if (!s_present) return -1;
    uint16_t *p = (uint16_t *)buf;

    for (uint32_t sec = 0; sec < count; sec++, lba++) {
        if (wait_bsy_clear() != 0) return -1;

        outb(s_io + R_DEV, devsel());
        dly();
        outb(s_io + R_FEAT, 0);
        outb(s_io + R_SEC, 0);
        outb(s_io + R_LBA0, 0);
        outb(s_io + R_LBA1, 0x00);
        outb(s_io + R_LBA2, 0x08);
        outb(s_io + R_CMD, 0xA0);
        dly();

        if (wait_drq() != 0) return -1;

        uint8_t pkt[12];
        pkt[0] = 0x28;
        pkt[1] = 0;
        pkt[2] = (uint8_t)(lba >> 24);
        pkt[3] = (uint8_t)(lba >> 16);
        pkt[4] = (uint8_t)(lba >> 8);
        pkt[5] = (uint8_t)(lba);
        pkt[6] = 0;
        pkt[7] = 0;
        pkt[8] = 1;
        pkt[9] = 0;
        pkt[10] = 0;
        pkt[11] = 0;

        for (int i = 0; i < 6; i++) outw(s_io + R_DATA, *(uint16_t *)&pkt[i * 2]);

        if (wait_drq() != 0) return -1;

        for (int i = 0; i < 1024; i++) p[i] = inw(s_io + R_DATA);
        p += 1024;

        if (wait_bsy_clear() != 0) return -1;
    }
    return 0;
}
