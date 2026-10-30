#include "disk.h"
#include "kernel.h"

#define ATA_DATA 0
#define ATA_ERROR 1
#define ATA_SECCOUNT0 2
#define ATA_LBA0 3
#define ATA_LBA1 4
#define ATA_LBA2 5
#define ATA_HDDEVSEL 6
#define ATA_COMMAND 7
#define ATA_STATUS 7
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF 0x20
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

static disk_t disks[DISK_MAX];
static int disks_found = 0;

static uint8_t ata_read8(uint16_t io, uint8_t reg) {
    return inb(io + reg);
}

static void ata_write8(uint16_t io, uint8_t reg, uint8_t value) {
    outb(io + reg, value);
}

static void ata_delay(uint16_t io) {
    ata_read8(io, ATA_STATUS);
    ata_read8(io, ATA_STATUS);
    ata_read8(io, ATA_STATUS);
    ata_read8(io, ATA_STATUS);
}

static int ata_wait(uint16_t io, bool drq) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = ata_read8(io, ATA_STATUS);
        if (!(s & ATA_SR_BSY)) {
            if (s & (ATA_SR_ERR | ATA_SR_DF))
                return -1;
            if (!drq || (s & ATA_SR_DRQ))
                return 0;
        }
    }
    return -1;
}

static void copy_model(char *dst, uint16_t *id) {
    int p = 0;
    for (int i = 27; i <= 46; i++) {
        dst[p++] = (char)(id[i] >> 8);
        dst[p++] = (char)(id[i] & 0xFF);
    }
    while (p > 0 && dst[p - 1] == ' ')
        p--;
    dst[p] = 0;
}

static bool identify(uint16_t io, uint8_t drive, disk_t *out) {
    uint16_t id[256];
    ata_write8(io, ATA_HDDEVSEL, 0xA0 | (drive << 4));
    ata_delay(io);
    ata_write8(io, ATA_SECCOUNT0, 0);
    ata_write8(io, ATA_LBA0, 0);
    ata_write8(io, ATA_LBA1, 0);
    ata_write8(io, ATA_LBA2, 0);
    ata_write8(io, ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay(io);
    if (ata_read8(io, ATA_STATUS) == 0)
        return false;
    if (ata_wait(io, true) != 0)
        return false;
    for (int i = 0; i < 256; i++)
        id[i] = inw(io + ATA_DATA);
    out->present = true;
    out->io = io;
    out->drive = drive;
    out->sectors = ((uint32_t)id[61] << 16) | id[60];
    copy_model(out->name, id);
    if (!out->name[0])
        strcpy(out->name, drive ? "ATA Slave" : "ATA Master");
    return true;
}

void disk_init(void) {
    disks_found = 0;
    uint16_t ports[2] = { 0x1F0, 0x170 };
    for (int p = 0; p < 2; p++) {
        for (int d = 0; d < 2; d++) {
            if (disks_found >= DISK_MAX)
                return;
            disk_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            if (identify(ports[p], d, &tmp)) {
                disks[disks_found++] = tmp;
            }
        }
    }
}

int disk_count(void) {
    return disks_found;
}

disk_t *disk_get(int index) {
    if (index < 0 || index >= disks_found)
        return 0;
    return &disks[index];
}

static int ata_write_sector(disk_t *disk, uint32_t lba, const uint16_t *data) {
    uint16_t io = disk->io;
    uint8_t drive = disk->drive;
    if (ata_wait(io, false) != 0)
        return -1;
    ata_write8(io, ATA_HDDEVSEL, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    ata_delay(io);
    ata_write8(io, ATA_SECCOUNT0, 1);
    ata_write8(io, ATA_LBA0, (uint8_t)lba);
    ata_write8(io, ATA_LBA1, (uint8_t)(lba >> 8));
    ata_write8(io, ATA_LBA2, (uint8_t)(lba >> 16));
    ata_write8(io, ATA_COMMAND, ATA_CMD_WRITE_PIO);
    if (ata_wait(io, true) != 0)
        return -1;
    for (int i = 0; i < 256; i++)
        outw(io + ATA_DATA, data[i]);
    ata_write8(io, ATA_COMMAND, 0xE7);
    return ata_wait(io, false);
}

int disk_install_arctian(int index, int *written, int total) {
    disk_t *disk = disk_get(index);
    if (!disk)
        return -1;
    const uint16_t *boot = (const uint16_t *)0x7C00;
    const uint16_t *payload = (const uint16_t *)0x10000;
    int sectors = total;
    if (sectors <= 0)
        sectors = 1025;
    if (disk->sectors && (uint32_t)sectors >= disk->sectors)
        return -1;
    if (ata_write_sector(disk, 0, boot) != 0)
        return -1;
    if (written)
        *written = 1;
    for (int s = 1; s < sectors; s++) {
        if (ata_write_sector(disk, (uint32_t)s, payload + (s - 1) * 256) != 0)
            return -1;
        if (written)
            *written = s + 1;
    }
    return 0;
}
