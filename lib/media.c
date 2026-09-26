#include "media.h"
#include "storage.h"
#include "atapi.h"
#include "iso9660.h"
#include "sbio.h"
#include "amod.h"
#include "kernel.h"

#define BOOT_MEDIA_ADDR 0x7100

static int is_cd = 0;

int media_init(void) {
    uint8_t src = *(volatile uint8_t *)BOOT_MEDIA_ADDR;
#ifdef ARCTIAN_BLK_SELFTEST
    { char m[40]; int n=0; const char *p="[mk] boot_media="; while(*p) m[n++]=*p++; m[n++]=(char)('0'+(src%10)); m[n++]='\n'; m[n]=0; extern void serial_puts(const char*); serial_puts(m); }
#endif
    is_cd = (src == 2);
    if (is_cd) {
        if (atapi_init() != 0) return -1;
        if (iso9660_init() != 0) return -1;
        return 0;
    }
    return storage_init();
}

int media_is_cd(void) { return is_cd; }

int media_read(uint32_t lba, uint32_t count, void *buf) {
    if (!is_cd) return storage_read(lba, count, buf);

    uint8_t *out = (uint8_t *)buf;
    uint32_t off = lba * 512u;
    uint32_t bytes = count * 512u;
    static uint8_t sec[2048];
    while (bytes) {
        uint32_t s = off / 2048u;
        uint32_t so = off % 2048u;
        uint32_t n = 2048u - so;
        if (n > bytes) n = bytes;
        int ok = 0;
        for (int attempt = 0; attempt < 4 && !ok; attempt++) {
            if (atapi_read(s, 1, sec) == 0) ok = 1;
        }
        if (!ok) return -1;
        for (uint32_t i = 0; i < n; i++) out[i] = sec[so + i];
        out += n;
        off += n;
        bytes -= n;
    }
    return 0;
}

int media_installed(void) {
    if (is_cd) return 0;
    superblock_t sb;
    if (sb_load(&sb) != 0 || sb.magic != SUPERBLOCK_MAGIC) return 0;
    return (sb.flags & SB_FLAG_INSTALLED) ? 1 : 0;
}

int media_set_installed(void) {
    if (is_cd) return -1;
    superblock_t sb;
    if (sb_load(&sb) != 0 || sb.magic != SUPERBLOCK_MAGIC) return -1;
    sb.flags |= SB_FLAG_INSTALLED;
    return sb_store(&sb);
}

static const char *iso_name_for(const char *name) {
    if (strcmp(name, "kernel_plus") == 0) return "KPLUS";
    if (strcmp(name, "install") == 0) return "INSTALL";
    if (strcmp(name, "desktop") == 0) return "DESKTOP";
    return name;
}

int media_find_module(const char *name, module_entry_t *out) {
    if (is_cd) {
        uint32_t lba2048 = 0, size = 0;
        if (iso9660_find(iso_name_for(name), &lba2048, &size) != 0) return -1;
        uint32_t lba512 = lba2048 * 4u;

        uint8_t hdr[512];
        if (media_read(lba512, 1, hdr) != 0) return -1;
        const amod_header_t *h = (const amod_header_t *)hdr;
        if (h->magic != AMOD_MAGIC) return -1;

        for (int i = 0; i < 31; i++) {
            out->name[i] = name[i];
            if (!name[i]) break;
        }
        out->name[31] = '\0';
        out->type = h->type;
        out->abi_version = h->abi_version;
        out->flags = h->flags;
        out->load_addr = h->load_addr;
        out->image_lba = lba512;
        out->image_sectors = 0;
        out->image_size = h->image_size;
        out->entry_off = h->entry_off;
        out->checksum = h->checksum;
        return 0;
    }

    superblock_t sb;
    if (sb_load(&sb) != 0 || sb.magic != SUPERBLOCK_MAGIC) return -1;
    module_entry_t table[MAX_MODULES];
    if (storage_read(sb.module_tab_lba, sb.module_tab_sectors, table) != 0) return -1;
    for (uint32_t i = 0; i < sb.module_count; i++) {
        if (strcmp(table[i].name, name) == 0) {
            *out = table[i];
            return 0;
        }
    }
    return -1;
}
