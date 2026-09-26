#ifndef ARCTIAN_MMIO_H
#define ARCTIAN_MMIO_H

#include <stdint.h>

int mmio_map(uint64_t phys, uint64_t size);

void mmio_reset(void);

#endif
