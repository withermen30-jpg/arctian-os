#ifndef ARCTIAN_ISO9660_H
#define ARCTIAN_ISO9660_H

#include <stdint.h>

int iso9660_init(void);
int iso9660_find(const char *name, uint32_t *lba, uint32_t *size);

#endif
