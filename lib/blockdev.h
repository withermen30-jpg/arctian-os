#ifndef ARCTIAN_BLOCKDEV_H
#define ARCTIAN_BLOCKDEV_H

#include <stdint.h>

/* Birlesik blok cihaz katmani: ATA (IDE), AHCI (SATA/M.2-SATA), NVMe (M.2/PCIe).
 * Tum okuma/yazma 512-byte sektor birimindedir. */

#define BLOCKDEV_MAX 16

typedef enum {
    BDEV_KIND_ATA  = 0,
    BDEV_KIND_AHCI = 1,
    BDEV_KIND_NVME = 2,
    BDEV_KIND_USB  = 3
} bdev_kind_t;

typedef struct {
    char      name[24];
    int       kind;
    uint64_t  sectors;   /* toplam 512-byte sektor (0 = bilinmiyor) */
    int       removable;
} blockdev_info_t;

typedef struct {
    int      (*read)(void *ctx, uint64_t lba, uint32_t count, void *buf);
    int      (*write)(void *ctx, uint64_t lba, uint32_t count, const void *buf);
    int      (*flush)(void *ctx);
    void     *ctx;
    uint64_t  sectors;
    int       kind;
    int       removable;
    char      name[24];
} bdev_ops_t;

/* Suruculiler tarafindan cagrilir; 0 = basarili. */
int blockdev_register(const bdev_ops_t *ops);

/* Tum denetleyicileri tarar. Bulunan cihaz sayisini doner. */
int blockdev_init(void);

/* Cekirdek USBB'yi taradiktan sonra cagrilir; blockdev_init bu kancayi
 * ATA/AHCI/NVMe taramasindan hemen sonra calistirir. */
void blockdev_set_scan_hook(void (*fn)(void));
int blockdev_count(void);
int blockdev_info(int idx, blockdev_info_t *out);

int blockdev_read(int idx, uint64_t lba, uint32_t count, void *buf);
int blockdev_write(int idx, uint64_t lba, uint32_t count, const void *buf);
int blockdev_flush(int idx);

/* Blok onbellek (write-back). Varsayilan kapali; kernel_plus acar. */
void blockdev_cache_enable(int on);
int  blockdev_cache_enabled(void);
void blockdev_cache_flush_all(void);
void blockdev_cache_invalidate(void);
void blockdev_cache_stats(uint32_t *hits, uint32_t *misses);

/* ATA bus-master DMA denetimi (test/tani icin; varsayilan acik). */
void blk_ata_set_dma(int on);
int  blk_ata_dma_enabled(void);

/* Surucu kesif fonksiyonlari (blockdev_init tarafindan cagrilir). */
void blk_ata_scan(void);
void blk_ahci_scan(void);
void blk_nvme_scan(void);

/* Blok suruculere ozel MMIO mapper (mmio.c'den bagimsiz). */
void blk_mmio_map(uint64_t phys, uint64_t size);

/* Paylasilan PCI config yardimcilari (blockdev.c). */
uint32_t blk_pci_readl(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void     blk_pci_writel(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val);
uint64_t blk_pci_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar);
void     blk_pci_enable(uint8_t bus, uint8_t slot, uint8_t func);

#endif /* ARCTIAN_BLOCKDEV_H */
