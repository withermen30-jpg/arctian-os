/**
 * WLF1 — Arctian Wolf uygulama paketi (.wlf)
 * =========================================
 *
 * `.exe` benzeri tek dosyalik uygulama bicimi. Iki tur vardir:
 *
 *   WLF_KIND_NATIVE (0) : x86-64 freestanding kod. libWolf'a linklenmistir;
 *                         isletim sistemi sembolu kullanmaz. Windows/host
 *                         SDK'si ile (gcc + ld) derlenir.
 *   WLF_KIND_SCRIPT (1) : gomulu Wolf bytecode. Kaynak C-altkumesidir;
 *                         isletim sistemi icindeki `wf build` (wolfc.c)
 *                         derler, `wlf_vm.c` calistirir. Derleyici
 *                         gerektigi icin native tur host'ta uretilir.
 *
 * Yerlesim:
 *
 *   [wlf_header_t][meta: "WLFM" u16 len + key=value satirlari]
 *                 [icon (PNG/SVG, istege bagli)][payload]
 *
 *  - meta blogu istege baglidir; yoksa (eski paket) ikon header'dan hemen
 *    sonra baslar. Boylesi geriye uyumludur ve checksum'u degistirmez.
 *  - payload native icin kod image'i, script icin Wolf bytecode'dur.
 *  - checksum: icon + payload uzerinde wlf_checksum() (meta disaridadir;
 *    meta imza kapsamindadir).
 *  - Native kod `load_addr` adresine yuklenir, `bss_size` sifirlanir ve
 *    `load_addr + entry_off` adresindeki
 *        int wolf_main(const wolf_api_t *api);
 *    cagrilir.
 *
 * Uygulama rolu (0.7): bir .wlf yalnizca pencere acmak zorunda degildir.
 *   role=ui      : pencere acilir (klasik uygulama).
 *   role=cli     : terminal acilir, program terminalde calisir.
 *   role=runtime : yorumlayici/calisma zamani; yalnizca dosya verilerek
 *                  ("birlikte ac") calisir, terminal acilir.
 * Meta anahtarlari: role, handles, entry, terminal.
 *   handles=py,pyw   -> bu calisma zamani hangi uzantilari isler
 *   entry=python     -> kullaniciya gosterilen giris adi
 *   terminal=1       -> calisirken terminal penceresi gorunur
 *
 * Surum uyumlulugu: magic + version. Alanlar yalnizca SONA eklenir.
 */
#ifndef ARCTIAN_WLF_H
#define ARCTIAN_WLF_H

#include <stdint.h>

#define WLF_MAGIC        0x31464C57u   /* "WLF1" little-endian */
#define WLF_VERSION      1u

#define WLF_KIND_NATIVE  0u
#define WLF_KIND_SCRIPT  1u

/* Uygulama rolu (header.role; reserved0'in yerini aldi — ayni offset). */
#define WLF_ROLE_UI      0u   /* pencere acar */
#define WLF_ROLE_CLI     1u   /* terminalde calisir */
#define WLF_ROLE_RUNTIME 2u   /* dosya ile calisan calisma zamani (dil, backend) */

/* Meta blogu: header'dan hemen sonra "WLFM" + u16 boyut + key=value satirlari. */
#define WLF_META_MAGIC   0x4D464C57u   /* "WLFM" little-endian */
#define WLF_META_HDR     6u            /* 4 bayt sihir + 2 bayt uzunluk */

#define WLF_HANDLES_LEN  40
#define WLF_ENTRY_LEN    24

/* wlf_meta_t.flags */
#define WLF_META_TERMINAL 0x0001u      /* terminal=1 */

#define WLF_NAME_LEN     32
#define WLF_TITLE_LEN    64
#define WLF_VER_LEN      16
#define WLF_AUTHOR_LEN   40
#define WLF_CATEGORY_LEN 24

/* Paket boyutu siniri: ag tamponu ve bellek butcesiyle uyumlu. */
#define WLF_MAX_PACKAGE  (256u * 1024u)

/* Bayraklar: izinler + pencere davranisi */
#define WLF_FLAG_FS      0x0001u
#define WLF_FLAG_NET     0x0002u
#define WLF_FLAG_EXEC    0x0004u
#define WLF_FLAG_RESIZE  0x0100u

#define WLF_DEFAULT_LOAD 0x03000000u   /* varsayilan native yukleme adresi */

typedef struct __attribute__((packed)) {
    uint32_t magic;          /* WLF_MAGIC */
    uint16_t version;        /* WLF_VERSION */
    uint16_t kind;           /* WLF_KIND_* */
    uint16_t flags;          /* WLF_FLAG_* */
    uint16_t role;           /* WLF_ROLE_* (eski paketlerde 0 = UI) */
    int32_t  win_w;          /* istenen pencere genisligi (0 = varsayilan) */
    int32_t  win_h;
    uint32_t load_addr;      /* native: yukleme adresi */
    uint32_t image_size;     /* native: kod boyutu */
    uint32_t bss_size;       /* native: image sonrasi sifirlanacak alan */
    uint32_t entry_off;      /* native: load_addr'a gore giris offseti */
    uint32_t payload_off;    /* header'dan payload offseti */
    uint32_t payload_len;    /* payload boyutu */
    uint32_t icon_len;       /* ikon boyutu (payload'dan hemen once) */
    uint32_t checksum;       /* icon + payload uzerinde wlf_checksum */
    char     name[WLF_NAME_LEN];        /* kimlik: a-z0-9-_ */
    char     title[WLF_TITLE_LEN];
    char     ver[WLF_VER_LEN];
    char     author[WLF_AUTHOR_LEN];
    char     category[WLF_CATEGORY_LEN];
} wlf_header_t;

#define WLF_HEADER_SIZE ((uint32_t)sizeof(wlf_header_t))

/* Cozulmus meta blogu. */
typedef struct {
    uint16_t role;                     /* WLF_ROLE_* */
    uint16_t flags;                    /* WLF_META_* */
    char     handles[WLF_HANDLES_LEN]; /* "py,pyw" */
    char     entry[WLF_ENTRY_LEN];     /* "python" */
} wlf_meta_t;

/** Meta blogunu cozer. Blok yoksa role=UI, handles bos doner ve 0 doner.
 *  Bozuk blokta -1 doner. */
int wlf_meta_read(const uint8_t *buf, uint32_t size, wlf_meta_t *out);

/** DPC/checksum ile ayni algoritma (dongusel, 32 bit). */
static inline uint32_t wlf_checksum(const uint8_t *data, uint32_t len) {
    uint32_t s = 0x12345678u;
    for (uint32_t i = 0; i < len; i++) s = (s << 1) ^ (s >> 31) ^ data[i];
    return s;
}

#endif /* ARCTIAN_WLF_H */
