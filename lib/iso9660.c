#include "iso9660.h"
#include "atapi.h"
#include <stdint.h>

#define SECT 2048

static uint32_t root_lba;
static uint32_t root_size;
static int ready = 0;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static char upper(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    return c;
}

static int normalize(const char *src, int len, char *out, int outsz) {
    int n = 0;
    if (len >= 2 && src[len - 2] == ';' && src[len - 1] == '1') len -= 2;
    if (len >= 4 &&
        (src[len - 4] == '.') &&
        upper(src[len - 3]) == 'B' &&
        upper(src[len - 2]) == 'I' &&
        upper(src[len - 1]) == 'N') {
        len -= 4;
    }
    for (int i = 0; i < len && n < outsz - 1; i++) out[n++] = upper(src[i]);
    out[n] = '\0';
    return n;
}

int iso9660_init(void) {
    uint8_t buf[SECT];
    if (atapi_read(16, 1, buf) != 0) return -1;
    if (!(buf[0] == 1 && buf[1] == 'C' && buf[2] == 'D' &&
          buf[3] == '0' && buf[4] == '0' && buf[5] == '1'))
        return -1;

    const uint8_t *rec = buf + 156;
    root_lba = le32(rec + 2);
    root_size = le32(rec + 10);
    ready = 1;
    return 0;
}

int iso9660_find(const char *name, uint32_t *lba, uint32_t *size) {
    if (!ready) return -1;

    char want[64];
    int wl = 0;
    for (int i = 0; name[i] && wl < 63; i++) want[wl++] = upper(name[i]);
    want[wl] = '\0';

    static uint8_t dir[16384];
    uint32_t total = root_size;
    if (total > sizeof(dir)) total = sizeof(dir);
    uint32_t sectors = (total + SECT - 1) / SECT;
    for (uint32_t s = 0; s < sectors; s++) {
        if (atapi_read(root_lba + s, 1, dir + s * SECT) != 0) return -1;
    }

    uint32_t off = 0;
    while (off + 33 <= total) {
        uint8_t rlen = dir[off];
        if (rlen == 0) {
            off = ((off / SECT) + 1) * SECT;
            continue;
        }
        if (off + rlen > total) break;

        uint8_t flags = dir[off + 25];
        uint8_t nlen = dir[off + 32];
        if (!(flags & 0x02)) {
            char norm[64];
            normalize((const char *)(dir + off + 33), nlen, norm, sizeof(norm));
            int eq = 1;
            for (int i = 0; i < 64; i++) {
                if (want[i] != norm[i]) { eq = 0; break; }
                if (!want[i]) break;
            }
            if (eq) {
                *lba = le32(dir + off + 2);
                *size = le32(dir + off + 10);
                return 0;
            }
        }
        off += rlen;
    }
    return -1;
}
