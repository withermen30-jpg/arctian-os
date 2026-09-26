#ifndef ARCTIAN_BLOCKDEV_H
#define ARCTIAN_BLOCKDEV_H

#include <stdint.h>


#define BLOCKDEV_MAX 16

typedef enum {
    BDEV_KIND_ATA  = 0,
    BDEV_KIND_AHCI = 1,
    BDEV_KIND_NVME = 2
} bdev_kind_t;

typedef struct {
    char      name[24];
    int       kind;
    uint64_t  sectors;
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

int blockdev_register(const bdev_ops_t *ops);

int blockdev_init(void);
int blockdev_count(void);
int blockdev_info(int idx, blockdev_info_t *out);

int blockdev_read(int idx, uint64_t lba, uint32_t count, void *buf);
int blockdev_write(int idx, uint64_t lba, uint32_t count, const void *buf);
int blockdev_flush(int idx);

void blk_ata_scan(void);
void blk_ahci_scan(void);
void blk_nvme_scan(void);

void blk_mmio_map(uint64_t phys, uint64_t size);

uint32_t blk_pci_readl(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void     blk_pci_writel(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val);
uint64_t blk_pci_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar);
void     blk_pci_enable(uint8_t bus, uint8_t slot, uint8_t func);

#endif
