/**
 * Arctian basit bellek ayirici (HEAP bolgesi)
 */
#include "alloc.h"
#include "memory_map.h"
#include <stdint.h>

typedef struct block {
    uint32_t magic;
    uint32_t free;
    uint64_t size;
    struct block *next;
    uint64_t pad;
} block_t;

#define BLK_MAGIC 0xA110C0DEu
#define BLK_HDR   ((size_t)sizeof(block_t))

static uintptr_t heap_ptr = HEAP_BASE;
static block_t  *free_list = 0;
static size_t    used_bytes = 0;

void *kmalloc(size_t size) {
    if (size == 0) size = 1;
    size = (size + 15) & ~(size_t)15;

    block_t **pp = &free_list;
    while (*pp) {
        block_t *b = *pp;
        if (b->size >= size) {
            size_t rem = b->size - size;
            if (rem >= BLK_HDR + 16) {
                block_t *nb = (block_t *)((char *)(b + 1) + size);
                nb->magic = BLK_MAGIC;
                nb->free = 1;
                nb->size = rem - BLK_HDR;
                nb->next = b->next;
                b->size = size;
                b->next = nb;
            }
            *pp = b->next;
            b->free = 0;
            b->next = 0;
            used_bytes += b->size;
            return (void *)(b + 1);
        }
        pp = &b->next;
    }

    uintptr_t p = (heap_ptr + 15) & ~(uintptr_t)15;
    if (p + BLK_HDR + size > HEAP_BASE + HEAP_MAX) return 0;
    block_t *b = (block_t *)p;
    b->magic = BLK_MAGIC;
    b->free = 0;
    b->size = size;
    b->next = 0;
    heap_ptr = p + BLK_HDR + size;
    used_bytes += size;
    return (void *)(b + 1);
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_t *b = (block_t *)((char *)ptr - BLK_HDR);
    if (b->magic != BLK_MAGIC || b->free) return;
    b->free = 1;
    if (used_bytes >= b->size) used_bytes -= b->size;

    block_t **pp = &free_list;
    block_t *prev = 0;
    while (*pp && (uintptr_t)*pp < (uintptr_t)b) { prev = *pp; pp = &(*pp)->next; }
    b->next = *pp;
    *pp = b;

    if (b->next && (char *)(b + 1) + b->size == (char *)b->next) {
        b->size += BLK_HDR + b->next->size;
        b->next = b->next->next;
    }
    if (prev && (char *)(prev + 1) + prev->size == (char *)b) {
        prev->size += BLK_HDR + b->size;
        prev->next = b->next;
    }
}

size_t kmalloc_used(void) {
    return used_bytes;
}

size_t kmalloc_total(void) {
    return (size_t)HEAP_MAX;
}
