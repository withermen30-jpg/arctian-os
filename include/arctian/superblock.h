#ifndef ARCTIAN_SUPERBLOCK_H
#define ARCTIAN_SUPERBLOCK_H

#include <stdint.h>

#define SUPERBLOCK_MAGIC      0x54435241u
#define SUPERBLOCK_VERSION    1u
#define SUPERBLOCK_LBA        2048u
#define SUPERBLOCK_SECTORS    1u
#define MODULE_TABLE_LBA      2049u
#define STATE_LBA             2050u
#define MODULE_DATA_START_LBA 4096u
#define MAX_MODULES           32

#define SB_FLAG_INSTALLED   0x00000001u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t module_count;
    uint32_t module_tab_lba;
    uint32_t module_tab_sectors;
    uint32_t state_lba;
    uint32_t reserved;
    uint32_t checksum;
} superblock_t;

typedef struct __attribute__((packed)) {
    char     name[32];
    uint16_t type;
    uint16_t abi_version;
    uint32_t flags;
    uint32_t load_addr;
    uint32_t image_lba;
    uint32_t image_sectors;
    uint32_t image_size;
    uint32_t entry_off;
    uint32_t checksum;
} module_entry_t;

#endif
