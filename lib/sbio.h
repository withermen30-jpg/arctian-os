#ifndef ARCTIAN_SBIO_H
#define ARCTIAN_SBIO_H

#include "superblock.h"

/* Superblock'u 512 baytlik tampon uzerinden guvenli okur/yazar. */
int sb_load(superblock_t *out);
int sb_store(const superblock_t *in);

#endif /* ARCTIAN_SBIO_H */
