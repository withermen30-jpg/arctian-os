/* I/O olcum: blok onbellegi ve ATA bus-master DMA etkisini gosterir.     */
#include "kernel.h"
#include "blockdev.h"
#include "superblock.h"
#include "afs.h"
#include "clock.h"
#include "serial.h"

#define BENCH_SECTORS 128
#define BENCH_REPEATS 32

static uint8_t s_buf[BENCH_SECTORS * 512] __attribute__((aligned(4096)));

static int bench_dev(void) {
    int n = blockdev_count();
    for (int i = 0; i < n; i++) {
        uint8_t sec[512];
        if (blockdev_read(i, SUPERBLOCK_LBA, 1, sec) != 0) continue;
        uint32_t magic;
        memcpy(&magic, sec, 4);
        if (magic == SUPERBLOCK_MAGIC) return i;
    }
    return n > 0 ? 0 : -1;
}

void io_bench(void) {
    int dev = bench_dev();
    if (dev < 0) { serial_printf("[iobench] blok cihaz yok\n"); return; }

    uint32_t lba = AFS_BASE_LBA;
    uint32_t mb = (BENCH_SECTORS * BENCH_REPEATS * 512u) >> 20;
    if (mb == 0) mb = 1;

    blockdev_cache_enable(0);

    blk_ata_set_dma(0);
    uint64_t t0 = clock_ms();
    for (int r = 0; r < BENCH_REPEATS; r++) blockdev_read(dev, lba, BENCH_SECTORS, s_buf);
    uint64_t t1 = clock_ms();

    blk_ata_set_dma(1);
    for (int r = 0; r < BENCH_REPEATS; r++) blockdev_read(dev, lba, BENCH_SECTORS, s_buf);
    uint64_t t2 = clock_ms();

    blockdev_cache_invalidate();
    uint64_t c0 = clock_ms();
    for (int r = 0; r < BENCH_REPEATS; r++) blockdev_read(dev, lba, BENCH_SECTORS, s_buf);
    uint64_t c1 = clock_ms();

    blockdev_cache_enable(1);
    blockdev_cache_invalidate();
    blockdev_read(dev, lba, BENCH_SECTORS, s_buf);
    uint32_t h0, m0, h1, m1;
    blockdev_cache_stats(&h0, &m0);
    uint64_t c2 = clock_ms();
    for (int r = 0; r < BENCH_REPEATS; r++) blockdev_read(dev, lba, BENCH_SECTORS, s_buf);
    uint64_t c3 = clock_ms();
    blockdev_cache_stats(&h1, &m1);

    serial_printf("[iobench] %u MB okuma: PIO %u ms, DMA %u ms, hiz %ux\n",
                  mb, (unsigned)(t1 - t0), (unsigned)(t2 - t1),
                  (t2 - t1) ? (unsigned)((t1 - t0) / (t2 - t1)) : 0u);
    serial_printf("[iobench] onbellek: soguk %u ms, sicak %u ms, hit=%u miss=%u\n",
                  (unsigned)(c1 - c0), (unsigned)(c3 - c2),
                  (unsigned)(h1 - h0), (unsigned)(m1 - m0));
}
