#include "storage.h"
#include "blockdev.h"
#include "superblock.h"
#include "kernel.h"

static int s_cur = -1;

int storage_init(void) {
    int n = blockdev_init();
    if (n <= 0) { s_cur = -1; return -1; }

    s_cur = -1;
    for (int i = 0; i < n; i++) {
        uint8_t buf[512];
        if (blockdev_read(i, SUPERBLOCK_LBA, 1, buf) != 0) continue;
        uint32_t magic;
        memcpy(&magic, buf, 4);
        if (magic == SUPERBLOCK_MAGIC) { s_cur = i; break; }
    }
    if (s_cur < 0) s_cur = 0;
    return 0;
}

int storage_read(uint32_t lba, uint32_t count, void *buf) {
    if (s_cur < 0) return -1;
    return blockdev_read(s_cur, lba, count, buf);
}

int storage_write(uint32_t lba, uint32_t count, const void *buf) {
    if (s_cur < 0) return -1;
    return blockdev_write(s_cur, lba, count, buf);
}

uint64_t storage_sector_count(void) {
    if (s_cur < 0) return 0;
    blockdev_info_t info;
    if (blockdev_info(s_cur, &info) != 0) return 0;
    return info.sectors;
}
