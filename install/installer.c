#include <stdint.h>
#include <stdbool.h>
#include "kernel.h"
#include "serial.h"
#include "media.h"
#include "iso9660.h"
#include "blockdev.h"
#include "superblock.h"
#include "amod.h"
#include "printf.h"

#define CHUNK_SECS 128u
static uint8_t chunk[CHUNK_SECS * 512];

static void put_dec(uint32_t v) {
    char b[12];
    int i = 0;
    if (v == 0) { kprintf("0"); return; }
    while (v && i < 11) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i) kprintf("%c", b[--i]);
}

static int iso_size(const char *name, uint32_t *size) {
    uint32_t lba = 0, sz = 0;
    if (iso9660_find(name, &lba, &sz) != 0) return -1;
    *size = sz;
    return 0;
}

static int copy_iso_to_disk(const char *name, int target, uint32_t dst_lba,
                            uint32_t *out_size, uint8_t hdr[512]) {
    uint32_t lba2048 = 0, size = 0;
    if (iso9660_find(name, &lba2048, &size) != 0) return -1;
    if (out_size) *out_size = size;

    uint32_t total_secs = (size + 511u) / 512u;
    uint32_t lba512 = lba2048 * 4u;

    if (hdr) {
        if (media_read(lba512, 1, hdr) != 0) return -1;
    }

    uint32_t done = 0;
    while (done < total_secs) {
        uint32_t n = total_secs - done;
        if (n > CHUNK_SECS) n = CHUNK_SECS;
        if (media_read(lba512 + done, n, chunk) != 0) return -1;
        if (blockdev_write(target, dst_lba + done, n, chunk) != 0) return -1;
        done += n;
    }
    blockdev_flush(target);
    return (int)total_secs;
}

static int pick_target(int n) {
    for (int i = 0; i < n; i++) {
        superblock_t sb;
        if (blockdev_read(i, SUPERBLOCK_LBA, 1, &sb) == 0) {
            if (sb.magic == SUPERBLOCK_MAGIC) continue;
        }
        return i;
    }
    return n > 0 ? 0 : -1;
}

void install_main(void *arg) __attribute__((section(".text.entry")));
void install_main(void *arg) {
    (void)arg;
    terminal_initialize();
    show_arctian_header();

    serial_puts("[inst] kalici kurulum baslatiliyor\n");
    terminal_writestring("\nArctian kalici kurulum\n");
    terminal_writestring("----------------------\n");

    if (media_init() != 0) {
        terminal_writestring("Boot medyasi baslatilamadi.\n");
        serial_puts("[inst] media_init fail\n");
        return;
    }

    if (!media_is_cd()) {
        terminal_writestring("Canli ISO/CD ortami bulunamadi; kurulum atlandi.\n");
        serial_puts("[inst] CD degil, kurulum atlandi\n");
        return;
    }

    int n = blockdev_init();
    terminal_writestring("Bulunan disk sayisi: ");
    put_dec((uint32_t)n);
    terminal_writestring("\n");
    if (n <= 0) {
        terminal_writestring("Kurulum icin uygun disk bulunamadi.\n");
        return;
    }

    int target = pick_target(n);
    if (target < 0) { terminal_writestring("Hedef disk yok.\n"); return; }

    blockdev_info_t info;
    blockdev_info(target, &info);
    terminal_writestring("Hedef disk: ");
    terminal_writestring(info.name);
    terminal_writestring(" (");
    put_dec((uint32_t)(info.sectors / 2048));
    terminal_writestring(" MB)\n");

    uint32_t hd_size = 0;
    uint8_t hdr[512];
    if (copy_iso_to_disk("HDIMG", target, 0, &hd_size, hdr) < 0) {
        terminal_writestring("HATA: onyukleme imaji okunamadi (HDIMG).\n");
        serial_puts("[inst] HDIMG yok\n");
        return;
    }
    terminal_writestring("Onyukleyici yazildi.\n");

    static const char *mods[3] = { "INSTALL", "KPLUS", "DESKTOP" };
    module_entry_t table[MAX_MODULES];
    int mc = 0;
    uint32_t next_lba = MODULE_DATA_START_LBA;

    for (int m = 0; m < 3; m++) {
        uint32_t fsize = 0;
        uint8_t mhdr[512];
        terminal_writestring("Yaziliyor: ");
        terminal_writestring(mods[m]);
        terminal_writestring("... ");
        int secs = copy_iso_to_disk(mods[m], target, next_lba, &fsize, mhdr);
        if (secs < 0) {
            terminal_writestring("HATA\n");
            return;
        }
        const amod_header_t *h = (const amod_header_t *)mhdr;
        if (h->magic != AMOD_MAGIC) {
            terminal_writestring("HATA (AMOD basligi)\n");
            return;
        }

        module_entry_t *e = &table[mc++];
        for (int i = 0; i < 32; i++) e->name[i] = h->name[i];
        e->name[31] = '\0';
        e->type = h->type;
        e->abi_version = h->abi_version;
        e->flags = h->flags;
        e->load_addr = h->load_addr;
        e->image_lba = next_lba;
        e->image_sectors = (uint32_t)secs;
        e->image_size = h->image_size;
        e->entry_off = h->entry_off;
        e->checksum = h->checksum;

        next_lba += (uint32_t)secs;
        terminal_writestring("OK\n");
    }

    uint32_t tab_secs = (uint32_t)((mc * sizeof(module_entry_t) + 511) / 512);
    static uint8_t tabbuf[4096];
    for (unsigned i = 0; i < sizeof(tabbuf); i++) tabbuf[i] = 0;
    for (int i = 0; i < mc; i++) {
        const uint8_t *src = (const uint8_t *)&table[i];
        for (unsigned j = 0; j < sizeof(module_entry_t); j++)
            tabbuf[i * sizeof(module_entry_t) + j] = src[j];
    }
    if (blockdev_write(target, MODULE_TABLE_LBA, tab_secs, tabbuf) != 0) {
        terminal_writestring("HATA: modul tablosu yazilamadi.\n");
        return;
    }

    superblock_t sb;
    for (unsigned i = 0; i < sizeof(sb); i++) ((uint8_t *)&sb)[i] = 0;
    sb.magic = SUPERBLOCK_MAGIC;
    sb.version = SUPERBLOCK_VERSION;
    sb.header_size = (uint16_t)sizeof(superblock_t);
    sb.flags = SB_FLAG_INSTALLED;
    sb.module_count = (uint32_t)mc;
    sb.module_tab_lba = MODULE_TABLE_LBA;
    sb.module_tab_sectors = tab_secs;
    sb.state_lba = STATE_LBA;
    sb.reserved = 0;
    sb.checksum = amod_checksum((const uint8_t *)&sb, (uint32_t)(sizeof(sb) - 4));
    if (blockdev_write(target, SUPERBLOCK_LBA, 1, &sb) != 0) {
        terminal_writestring("HATA: superblock yazilamadi.\n");
        return;
    }
    blockdev_flush(target);

    terminal_writestring("\nKURULUM TAMAMLANDI. Diski cikarip yeniden baslatin.\n");
    serial_puts("[inst] kurulum tamamlandi\n");

    for (volatile uint64_t i = 0; i < 30000000ULL; i++) asm volatile ("pause");
}
