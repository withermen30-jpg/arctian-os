#ifndef ARCTIAN_ATAPI_H
#define ARCTIAN_ATAPI_H

#include <stdint.h>

int atapi_init(void);
int atapi_read(uint32_t lba, uint32_t count, void *buf);

#endif
