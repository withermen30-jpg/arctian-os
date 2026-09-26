#ifndef ARCTIAN_SBIO_H
#define ARCTIAN_SBIO_H

#include "superblock.h"

int sb_load(superblock_t *out);
int sb_store(const superblock_t *in);

#endif
