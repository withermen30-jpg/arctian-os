/**
 * emu_png.c — minimal PNG yazici (8-bit RGB, filter 0, stored deflate).
 * Ek kütüphane yok; emu ekran goruntuları icin yeterli.
 */
#include "emu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];
static int crc_ready;

static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = 1;
}

static uint32_t crc32_upd(uint32_t c, const uint8_t *p, size_t n) {
    if (!crc_ready) crc_init();
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static void be32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);  b[3] = (uint8_t)v;
}

static void wchunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t c = crc32_upd(0xFFFFFFFFu, (const uint8_t *)type, 4);
    if (len) c = crc32_upd(c, data, len);
    uint8_t tail[4];
    be32(tail, c ^ 0xFFFFFFFFu);
    fwrite(tail, 1, 4, f);
}

int emu_write_png(const char *path, const uint8_t *argb, int w, int h) {
    if (!path || !argb || w <= 0 || h <= 0) return -1;
    /* Ham satirlar: her satir basinda filter bayti 0, RGB 24-bit. */
    size_t raw_len = (size_t)w * 3 + 1;
    uint8_t *raw = (uint8_t *)malloc(raw_len * (size_t)h);
    if (!raw) return -1;
    for (int y = 0; y < h; y++) {
        uint8_t *d = raw + (size_t)y * raw_len;
        d[0] = 0;
        const uint32_t *s = (const uint32_t *)argb + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            d[1 + x * 3 + 0] = (uint8_t)(s[x] >> 16);
            d[1 + x * 3 + 1] = (uint8_t)(s[x] >> 8);
            d[1 + x * 3 + 2] = (uint8_t)(s[x]);
        }
    }

    /* zlib akisi: 2 bayt header + stored bloklar + adler32 */
    size_t blocks = (raw_len * (size_t)h + 65534) / 65535 + 1;
    size_t zl_len = 2 + blocks * 5 + raw_len * (size_t)h + 4;
    uint8_t *zl = (uint8_t *)malloc(zl_len);
    if (!zl) { free(raw); return -1; }
    size_t o = 0;
    zl[o++] = 0x78; zl[o++] = 0x01;
    size_t pos = 0, total = raw_len * (size_t)h;
    do {
        size_t chunk = total - pos > 65535 ? 65535 : total - pos;
        int final = (pos + chunk >= total) ? 1 : 0;
        zl[o++] = (uint8_t)final;
        zl[o++] = (uint8_t)(chunk & 0xFF);
        zl[o++] = (uint8_t)(chunk >> 8);
        zl[o++] = (uint8_t)(~chunk & 0xFF);
        zl[o++] = (uint8_t)((~chunk >> 8) & 0xFF);
        memcpy(zl + o, raw + pos, chunk);
        o += chunk;
        pos += chunk;
    } while (pos < total);
    /* adler32 */
    uint32_t a = 1, bsum = 0;
    for (size_t i = 0; i < total; i++) {
        a = (a + raw[i]) % 65521;
        bsum = (bsum + a) % 65521;
    }
    uint8_t ad[4];
    be32(ad, (bsum << 16) | a);
    memcpy(zl + o, ad, 4);
    o += 4;

    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(zl); return -1; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;   /* bit derinligi */
    ihdr[9] = 2;   /* renk tipi: RGB */
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    wchunk(f, "IHDR", ihdr, 13);
    wchunk(f, "IDAT", zl, (uint32_t)o);
    wchunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw);
    free(zl);
    return 0;
}
