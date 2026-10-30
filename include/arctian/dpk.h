/**
 * Arctian DuckStore Package (DPK1) - indirilebilir uygulama paketi formati.
 *
 * Bir DPK dosyasi tek bir AFS dosyasidir (<id>.dpk). Icerik sirasi:
 *
 *   [dpk_header_t] [meta][ikon SVG][kod image]
 *
 *  - meta    : satir tabanli "key=value" metni (degerler percent-encoded).
 *  - ikon    : SVG kaynak baytlari (opsiyonel, icon_len=0 olabilir).
 *  - kod     : desktop modulunun sembollerine linklenmis duz binary.
 *
 * Kod, header'daki load_addr adresine yuklenir; bss_size kadar alan
 * sifirlanir; entry_off adresindeki fonksiyon (win, asi) ile cagrilir.
 */
#ifndef ARCTIAN_DPK_H
#define ARCTIAN_DPK_H

#include <stdint.h>

#define DPK_MAGIC       0x314B5044u  /* "DPK1" little-endian */
#define DPK_VERSION     1u
#define DPK_ABI_LEN     24
#define DPK_ID_LEN      32
#define DPK_MAX_PACKAGE (176u * 1024u)  /* ag tamponu siniri (~192KB) */

/* Meta alanlari (satir tabanli). Degerler percent-encoded. */
#define DPK_META_TITLE       "title"
#define DPK_META_CATEGORY    "category"
#define DPK_META_VERSION     "version"
#define DPK_META_PUBLISHER   "publisher"
#define DPK_META_DESCRIPTION "description"
#define DPK_META_WINDOW      "window"   /* "w,h" */
#define DPK_META_PERMISSIONS "permissions"  /* "net,fs,dev" */

typedef struct __attribute__((packed)) {
    uint32_t magic;             /* DPK_MAGIC */
    uint16_t version;           /* DPK_VERSION */
    uint16_t abi_version;       /* amod ABI (bilgi) */
    uint32_t load_addr;         /* kodun yuklenecegi adres */
    uint32_t image_size;        /* kod boyutu */
    uint32_t bss_size;          /* image sonrasi sifirlanacak alan */
    uint32_t entry_off;         /* load_addr'a gore giris offseti */
    uint32_t meta_len;          /* meta bayt sayisi */
    uint32_t icon_len;          /* SVG bayt sayisi */
    uint32_t code_off;          /* header'dan kodun offseti */
    uint32_t flags;             /* rezerve */
    uint32_t checksum;          /* meta+ikon+kod uzerinde dpk_checksum */
    char     id[DPK_ID_LEN];
    char     abi[DPK_ABI_LEN];  /* desktop build etiketi */
} dpk_header_t;

#define DPK_HEADER_SIZE ((uint32_t)sizeof(dpk_header_t))

static inline uint32_t dpk_checksum(const uint8_t *data, uint32_t len) {
    uint32_t s = 0x12345678u;
    for (uint32_t i = 0; i < len; i++) s = (s << 1) ^ (s >> 31) ^ data[i];
    return s;
}

#endif /* ARCTIAN_DPK_H */
