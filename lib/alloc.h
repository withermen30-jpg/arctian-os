#ifndef ARCTIAN_ALLOC_H
#define ARCTIAN_ALLOC_H

#include <stddef.h>

void *kmalloc(size_t size);
void  kfree(void *ptr);

#endif
