/**
 * Arctian storage facade: birlesik blok katmanina delege eder.
 * (ATA / AHCI-SATA / NVMe destegi blockdev + blk_* suruculerinde.)
 */
#include "storage.h"
#include "blockdev.h"
#include "superblock.h"
#include "kernel.h"

static int s_cur = -1;
static storage_crypt_t s_crypt;
static int s_crypt_on = 0;

int storage_init(void) {
    int n = blockdev_init();
    if (n <= 0) { s_cur = -1; return -1; }

    /* Superblock tasiyan diski (kurulu sistem) tercih et. */
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

int storage_raw_read(uint32_t lba, uint32_t count, void *buf) {
    if (s_cur < 0) return -1;
    return blockdev_read(s_cur, lba, count, buf);
}

int storage_raw_write(uint32_t lba, uint32_t count, const void *buf) {
    if (s_cur < 0) return -1;
    return blockdev_write(s_cur, lba, count, buf);
}

void storage_set_crypt(const storage_crypt_t *c) {
    /* Sifreleme durumu degisirken onbellekteki sektorlerin anlami degisir:
     * once kirli veriyi diske indir, sonra onbellegi temizle. */
    blockdev_cache_flush_all();
    blockdev_cache_invalidate();
    if (!c) { s_crypt_on = 0; return; }
    s_crypt = *c;
    s_crypt_on = 1;
}

int storage_flush(void) {
    if (s_cur < 0) return -1;
    return blockdev_flush(s_cur);
}

int storage_crypt_active(void) { return s_crypt_on; }

int storage_read(uint32_t lba, uint32_t count, void *buf) {
    if (s_cur < 0) return -1;
    if (!s_crypt_on) return blockdev_read(s_cur, lba, count, buf);
    uint32_t base = s_crypt.base_lba, end = s_crypt.end_lba;
    /* lba+count uint32'te tasabilir (2^32 sektör ~>2TB sinirinda); bugun
     * istek yanlis siniflanip sifreli bolgeye duz veri / tersi yazilabiliyordu.
     * Aralik matematisi 64 bite tasinir. */
    uint64_t e = (uint64_t)lba + count;
    if (lba >= end || e <= base) return blockdev_read(s_cur, lba, count, buf);
    if (lba >= base && e <= end) return s_crypt.read(lba, count, buf);
    uint32_t done = 0;
    if (lba < base) {
        uint32_t n = base - lba;
        if (blockdev_read(s_cur, lba, n, buf) != 0) return -1;
        done = n;
    }
    uint32_t l2 = lba + done;
    uint32_t n2 = count - done;
    if ((uint64_t)l2 + n2 > end) n2 = (uint32_t)(end - l2);
    if (n2 && s_crypt.read(l2, n2, (uint8_t *)buf + done * 512u) != 0) return -1;
    done += n2;
    if (done < count)
        if (blockdev_read(s_cur, lba + done, count - done, (uint8_t *)buf + done * 512u) != 0) return -1;
    return 0;
}

int storage_write(uint32_t lba, uint32_t count, const void *buf) {
    if (s_cur < 0) return -1;
    if (!s_crypt_on) return blockdev_write(s_cur, lba, count, buf);
    uint32_t base = s_crypt.base_lba, end = s_crypt.end_lba;
    /* storage_read'teki ile ayni 64-bit aralik gerekcesi */
    uint64_t e = (uint64_t)lba + count;
    if (lba >= end || e <= base) return blockdev_write(s_cur, lba, count, buf);
    if (lba >= base && e <= end) return s_crypt.write(lba, count, buf);
    uint32_t done = 0;
    if (lba < base) {
        uint32_t n = base - lba;
        if (blockdev_write(s_cur, lba, n, buf) != 0) return -1;
        done = n;
    }
    uint32_t l2 = lba + done;
    uint32_t n2 = count - done;
    if ((uint64_t)l2 + n2 > end) n2 = (uint32_t)(end - l2);
    if (n2 && s_crypt.write(l2, n2, (const uint8_t *)buf + done * 512u) != 0) return -1;
    done += n2;
    if (done < count)
        if (blockdev_write(s_cur, lba + done, count - done, (const uint8_t *)buf + done * 512u) != 0) return -1;
    return 0;
}

uint64_t storage_sector_count(void) {
    if (s_cur < 0) return 0;
    blockdev_info_t info;
    if (blockdev_info(s_cur, &info) != 0) return 0;
    return info.sectors;
}
