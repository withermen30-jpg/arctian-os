#ifndef ARCTIAN_MMIO_H
#define ARCTIAN_MMIO_H

#include <stdint.h>

/* Fiziksel MMIO bolgesini (PCI BAR) sayfa tablolarina map eder (2MB sayfalar).
 * Adres identity oldugu icin donen pointer = bar.address kullanilabilir.
 * Basarida 0. */
int mmio_map(uint64_t phys, uint64_t size);

/* Sayfa tablosu havuz sayaclarini sifirlar (main_kernel .bss guvenligi). */
void mmio_reset(void);

#endif /* ARCTIAN_MMIO_H */
