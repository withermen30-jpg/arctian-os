#ifndef ARCTIAN_ALLOC_H
#define ARCTIAN_ALLOC_H

#include <stddef.h>

void  *kmalloc(size_t size);
void   kfree(void *ptr);
/* Blokun kullanilabilir boyutu; gecerli olmayan pointer icin 0. */
size_t ksize(void *ptr);
size_t kmalloc_used(void);
size_t kmalloc_total(void);

#endif /* ARCTIAN_ALLOC_H */
