#ifndef ARCTIAN_PNG_H
#define ARCTIAN_PNG_H

#include <stdint.h>
#include <stddef.h>

typedef void *(*png_alloc_fn)(size_t size);
typedef void  (*png_free_fn)(void *ptr);

int png_decode(const uint8_t *data, uint32_t len,
               png_alloc_fn alloc, png_free_fn freep,
               uint32_t **out, int *w, int *h);

#endif
