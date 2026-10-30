#ifndef ARCTIAN_ISO9660_H
#define ARCTIAN_ISO9660_H

#include <stdint.h>

int iso9660_init(void);
/* Dosyayi isimle bulur (uzanti/;1 yoksayilir, buyuk/kucuk harf duyarsiz).
 * lba: 2048 byte CD sektor LBA, size: byte cinsinden boyut. */
int iso9660_find(const char *name, uint32_t *lba, uint32_t *size);

#endif /* ARCTIAN_ISO9660_H */
