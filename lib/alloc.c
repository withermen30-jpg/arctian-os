#include "alloc.h"
#include "memory_map.h"
#include <stdint.h>

static uintptr_t heap_ptr = HEAP_BASE;

void *kmalloc(size_t size) {
    uintptr_t p = heap_ptr;
    p = (p + 15) & ~(uintptr_t)15;
    heap_ptr = p + size;
    if (heap_ptr > HEAP_BASE + HEAP_MAX) return 0;
    return (void *)p;
}

void kfree(void *ptr) {
    (void)ptr;
}
