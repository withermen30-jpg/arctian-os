/**
 * Arctian modul yukleyici
 * Modul imaji: 512 byte AMOD basligi + duz binary.
 *
 * Buyuk moduller (orn. desktop ~4 MB) tek bir dev okuma yerine sektor
 * gruplari halinde, hata durumunda tekrar denenerek okunur. Bu, gercek
 * donanimda buyuk tek parca okumalarda olusan zaman asimi/hata sorunlarini
 * onler. Donus: 0 basarili, -1 baslik okunamadi, -2 gecersiz baslik,
 * -3 govde okunamadi.
 */
#include "loader.h"
#include "amod.h"
#include "media.h"
#include "kernel.h"
#include "paging.h"

#define MODULE_CHUNK_SECTORS 256
#define MODULE_RETRIES       4

int module_find_by_name(const char *name, module_entry_t *out) {
    return media_find_module(name, out);
}

int module_load(const module_entry_t *entry) {
    uint8_t hdr[512];
    if (media_read(entry->image_lba, 1, hdr) != 0) return -1;

    const amod_header_t *h = (const amod_header_t *)hdr;
    if (h->magic != AMOD_MAGIC) return -2;

    uint32_t size = h->image_size;
    uint32_t sectors = (size + 511u) / 512u;
    uint8_t *dst = (uint8_t *)(uintptr_t)entry->load_addr;

    vmm_map_kernel_range((uint64_t)(uintptr_t)entry->load_addr,
                         (uint64_t)size + (uint64_t)h->bss_size);

    uint32_t done = 0;
    while (done < sectors) {
        uint32_t chunk = sectors - done;
        if (chunk > MODULE_CHUNK_SECTORS) chunk = MODULE_CHUNK_SECTORS;

        int ok = 0;
        for (int attempt = 0; attempt < MODULE_RETRIES && !ok; attempt++) {
            if (media_read(entry->image_lba + 1u + done, chunk,
                           dst + (size_t)done * 512u) == 0)
                ok = 1;
        }
        if (!ok) return -3;
        done += chunk;
    }

    if (h->bss_size) {
        memset(dst + size, 0, h->bss_size);
    }
    return 0;
}
