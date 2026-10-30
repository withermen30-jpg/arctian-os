#ifndef ARCTIAN_ATAPI_H
#define ARCTIAN_ATAPI_H

#include <stdint.h>

/* Birincil/ikincil kanal master/slave tarar (0x1F0 / 0x170). */
int atapi_init(void);
int atapi_read(uint32_t lba, uint32_t count, void *buf); /* 2048 byte sektor */

#endif /* ARCTIAN_ATAPI_H */
