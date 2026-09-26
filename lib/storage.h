#ifndef ARCTIAN_STORAGE_H
#define ARCTIAN_STORAGE_H

#include <stdint.h>

int storage_init(void);
int storage_read(uint32_t lba, uint32_t count, void *buf);
int storage_write(uint32_t lba, uint32_t count, const void *buf);

uint64_t storage_sector_count(void);

#endif
