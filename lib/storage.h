#ifndef ARCTIAN_STORAGE_H
#define ARCTIAN_STORAGE_H

#include <stdint.h>

/* Secili (boot/kurulu) blok cihazi uzerinden 512-byte sektor IO. */
int storage_init(void);
int storage_read(uint32_t lba, uint32_t count, void *buf);
int storage_write(uint32_t lba, uint32_t count, const void *buf);

/* Secili cihazin toplam 512-byte sektor sayisi (0 = bilinmiyor). */
uint64_t storage_sector_count(void);

/* Biriken (write-back) yazimlari diske indir. */
int storage_flush(void);

typedef struct {
    int (*read)(uint32_t lba, uint32_t count, void *buf);
    int (*write)(uint32_t lba, uint32_t count, const void *buf);
    uint32_t base_lba;
    uint32_t end_lba;
} storage_crypt_t;

void storage_set_crypt(const storage_crypt_t *c);
int  storage_crypt_active(void);
int  storage_raw_read(uint32_t lba, uint32_t count, void *buf);
int  storage_raw_write(uint32_t lba, uint32_t count, const void *buf);

#endif /* ARCTIAN_STORAGE_H */
