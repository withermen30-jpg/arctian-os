/**
 * .wlf paketleyici (OS ici) — 0.7
 *
 * Wolf bytecode programini (wolfc ciktisi) WLF1 basligiyla sarar. Ayni
 * bicimi Windows SDK'sindaki `wlfpack.py` de uretir; boylece iki tarafta
 * olusan dosyalar birbirine takilabilir.
 */
#include "dsk.h"

/* ------------------------------------------------------------------ */
/* Baslik alanlari (wlf.h ile ayni; packed)                            */
/* ------------------------------------------------------------------ */

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static int name_ok(const char *s) {
    int n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return 0;
    }
    return n > 0 && n < WLF_NAME_LEN;
}

/**
 * Bytecode .wlf uretir. `out` en az WLF_HEADER_SIZE + prog_len kadar olmali.
 * Donus: toplam boyut, hata ise 0.
 */
uint32_t wlf_pack_bytecode(const char *name, const char *title,
                           const char *version, const char *author,
                           const char *category, int win_w, int win_h,
                           uint16_t flags, const uint8_t *prog, uint32_t prog_len,
                           uint8_t *out, uint32_t cap);

/* 0.7: meta blogu ile paketle. `meta` = "role=cli\nhandles=py\n" gibi ham
 * satirlar (0/ bos ise blok yazilmaz). */
uint32_t wlf_pack_bytecode_ex(const char *name, const char *title,
                              const char *version, const char *author,
                              const char *category, int win_w, int win_h,
                              uint16_t flags, const char *meta,
                              const uint8_t *prog, uint32_t prog_len,
                              uint8_t *out, uint32_t cap);

uint32_t wlf_pack_bytecode(const char *name, const char *title,
                           const char *version, const char *author,
                           const char *category, int win_w, int win_h,
                           uint16_t flags, const uint8_t *prog, uint32_t prog_len,
                           uint8_t *out, uint32_t cap) {
    return wlf_pack_bytecode_ex(name, title, version, author, category,
                                win_w, win_h, flags, 0, prog, prog_len, out, cap);
}

uint32_t wlf_pack_bytecode_ex(const char *name, const char *title,
                              const char *version, const char *author,
                              const char *category, int win_w, int win_h,
                              uint16_t flags, const char *meta,
                              const uint8_t *prog, uint32_t prog_len,
                              uint8_t *out, uint32_t cap) {
    if (!name_ok(name) || !prog || prog_len == 0) return 0;

    uint32_t mlen = 0;
    if (meta) while (meta[mlen]) mlen++;
    if (mlen > 2000) return 0;
    uint32_t mblk = mlen ? WLF_META_HDR + mlen : 0;

    uint32_t off = WLF_HEADER_SIZE + mblk;
    uint32_t total = off + prog_len;
    if (total > cap) return 0;

    uint8_t *p = out;
    for (uint32_t i = 0; i < off; i++) p[i] = 0;
    put32(p + 0, WLF_MAGIC);
    put16(p + 4, WLF_VERSION);
    put16(p + 6, WLF_KIND_SCRIPT);
    put16(p + 8, flags);
    /* win_w/win_h: int32 alanlar */
    put32(p + 12, (uint32_t)(win_w > 0 ? win_w : 520));
    put32(p + 16, (uint32_t)(win_h > 0 ? win_h : 400));
    put32(p + 20, 0);                 /* load_addr (bytecode icin 0) */
    put32(p + 24, 0);                 /* image_size */
    put32(p + 28, 0);                 /* bss_size */
    put32(p + 32, 0);                 /* entry_off */
    put32(p + 36, off);               /* payload_off */
    put32(p + 40, prog_len);          /* payload_len */
    put32(p + 44, 0);                 /* icon_len */
    /* checksum: ikon(0) + payload */
    uint32_t s = 0x12345678u;
    for (uint32_t i = 0; i < prog_len; i++) s = (s << 1) ^ (s >> 31) ^ prog[i];
    put32(p + 48, s);
    bin_copy((char *)p + 52, WLF_NAME_LEN, name);
    bin_copy((char *)p + 84, WLF_TITLE_LEN, title && title[0] ? title : name);
    bin_copy((char *)p + 148, WLF_VER_LEN, version && version[0] ? version : "1.0");
    bin_copy((char *)p + 164, WLF_AUTHOR_LEN, author ? author : "");
    bin_copy((char *)p + 204, WLF_CATEGORY_LEN, category && category[0] ? category : "Genel");
    if (mblk) {
        uint8_t *m = p + WLF_HEADER_SIZE;
        put32(m, WLF_META_MAGIC);
        put16(m + 4, (uint16_t)mlen);
        for (uint32_t i = 0; i < mlen; i++) m[WLF_META_HDR + i] = (uint8_t)meta[i];
    }
    for (uint32_t i = 0; i < prog_len; i++) p[off + i] = prog[i];
    return total;
}
