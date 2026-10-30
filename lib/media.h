#ifndef ARCTIAN_MEDIA_H
#define ARCTIAN_MEDIA_H

#include <stdint.h>
#include "superblock.h"

/* Boot medyasini (ATA disk veya ATAPI CD) yonetir. */
int  media_init(void);
int  media_is_cd(void);
int  media_read(uint32_t lba512, uint32_t count, void *buf);

int  media_installed(void);
int  media_set_installed(void);

/* Modulu isimle bulur (HDD: superblock tablosu, CD: ISO9660). */
int  media_find_module(const char *name, module_entry_t *out);

#endif /* ARCTIAN_MEDIA_H */
