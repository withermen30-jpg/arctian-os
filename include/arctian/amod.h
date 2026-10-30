/**
 * Arctian Module (AMOD) - duz .bin modul basligi
 * Modul dosyalarinin en basina packer tarafindan eklenir.
 */
#ifndef ARCTIAN_AMOD_H
#define ARCTIAN_AMOD_H

#include <stdint.h>

#define AMOD_MAGIC          0x444F4D41u /* "AMOD" little-endian */
#define AMOD_ABI_VERSION    2u
#define AMOD_NAME_LEN       32
#define AMOD_VERSION_LEN    16

typedef enum {
    AMOD_TYPE_NONE       = 0,
    AMOD_TYPE_KERNEL     = 1,
    AMOD_TYPE_INSTALL    = 2,
    AMOD_TYPE_DRIVER_SVC = 3,
    AMOD_TYPE_DESKTOP    = 4,
    AMOD_TYPE_APP        = 5
} amod_type_t;

#define AMOD_FLAG_REQUIRED  0x00000001u /* yoksa sistem acilmaz */
#define AMOD_FLAG_RELOC     0x00000002u /* gelecekte relocation destegi */

typedef struct __attribute__((packed)) {
    uint32_t magic;                 /* AMOD_MAGIC */
    uint16_t abi_version;           /* AMOD_ABI_VERSION */
    uint16_t type;                  /* amod_type_t */
    uint32_t load_addr;             /* tercih edilen yukleme adresi */
    uint32_t image_size;            /* header HARIC yuklenecek boyut */
    uint32_t bss_size;              /* image sonrasi sifirlanacak alan */
    uint32_t entry_off;             /* load_addr'a gore giris offseti */
    uint32_t checksum;              /* image uzerinde basit toplama */
    char     name[AMOD_NAME_LEN];
    char     version[AMOD_VERSION_LEN];
    uint32_t flags;
} amod_header_t;

#define AMOD_HEADER_SIZE ((uint32_t)sizeof(amod_header_t))

static inline uint32_t amod_checksum(const uint8_t *data, uint32_t len) {
    uint32_t s = 0x12345678u;
    for (uint32_t i = 0; i < len; i++) {
        s = (s << 1) ^ (s >> 31) ^ data[i];
    }
    return s;
}

#endif /* ARCTIAN_AMOD_H */
