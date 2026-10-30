#ifndef ARCTIAN_PNG_H
#define ARCTIAN_PNG_H

#include <stdint.h>
#include <stddef.h>

typedef void *(*png_alloc_fn)(size_t size);
typedef void  (*png_free_fn)(void *ptr);

/*
 * PNG cozucu. Desteklenen renk tipleri: 0 (gray), 2 (RGB), 3 (palet),
 * 4 (gray+alfa), 6 (RGBA). Bit derinlikleri 1/2/4/8/16, tRNS seffafligi
 * ve Adam7 interlace desteklenir.
 * Cikti: ARGB8888 (0xAARRGGBB) tamponu; alloc ile ayrilir.
 * Basarida 0 doner ve *w/*h doldurulur.
 */
int png_decode(const uint8_t *data, uint32_t len,
               png_alloc_fn alloc, png_free_fn freep,
               uint32_t **out, int *w, int *h);

int png_decode_argb(const uint8_t *data, uint32_t len,
                    png_alloc_fn alloc, png_free_fn freep,
                    uint32_t **out, int *w, int *h);

#endif /* ARCTIAN_PNG_H */
