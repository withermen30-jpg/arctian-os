#ifndef ARCTIAN_DPK_H
#define ARCTIAN_DPK_H

#include <stdint.h>

#define DPK_MAGIC       0x314B5044u
#define DPK_VERSION     1u
#define DPK_ABI_LEN     24
#define DPK_ID_LEN      32
#define DPK_MAX_PACKAGE (176u * 1024u)

#define DPK_META_TITLE       "title"
#define DPK_META_CATEGORY    "category"
#define DPK_META_VERSION     "version"
#define DPK_META_PUBLISHER   "publisher"
#define DPK_META_DESCRIPTION "description"
#define DPK_META_WINDOW      "window"

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t abi_version;
    uint32_t load_addr;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t entry_off;
    uint32_t meta_len;
    uint32_t icon_len;
    uint32_t code_off;
    uint32_t flags;
    uint32_t checksum;
    char     id[DPK_ID_LEN];
    char     abi[DPK_ABI_LEN];
} dpk_header_t;

#define DPK_HEADER_SIZE ((uint32_t)sizeof(dpk_header_t))

static inline uint32_t dpk_checksum(const uint8_t *data, uint32_t len) {
    uint32_t s = 0x12345678u;
    for (uint32_t i = 0; i < len; i++) s = (s << 1) ^ (s >> 31) ^ data[i];
    return s;
}

#endif
