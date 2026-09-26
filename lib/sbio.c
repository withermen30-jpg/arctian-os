#include "sbio.h"
#include "storage.h"
#include <stddef.h>
#include <stdint.h>

int sb_load(superblock_t *out) {
    uint8_t buf[512];
    if (storage_read(SUPERBLOCK_LBA, 1, buf) != 0) return -1;
    for (size_t i = 0; i < sizeof(superblock_t); i++)
        ((uint8_t *)out)[i] = buf[i];
    return 0;
}

int sb_store(const superblock_t *in) {
    uint8_t buf[512];
    for (int i = 0; i < 512; i++) buf[i] = 0;
    for (size_t i = 0; i < sizeof(superblock_t); i++)
        buf[i] = ((const uint8_t *)in)[i];
    return storage_write(SUPERBLOCK_LBA, 1, buf);
}
