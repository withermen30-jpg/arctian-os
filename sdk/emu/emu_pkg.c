/**
 * emu_pkg.c — .wlf paketini yukler ve OS ile AYNI kurallarla dogrular.
 *
 * Kontroller (desktop/wlf.c wlf_validate ile birebir):
 *   magic/version, sinirlar (payload/ikon/govde), checksum (icon+payload),
 *   native icin yukleme adresi araligi. Imza varsa sig_verify_pkg ile
 *   dogrulanir; imzasiz paket uyarilarak yuklenir (gelistirme onizlemesi —
 *   OS'de imza ZORUNLUDUR).
 */
#include "emu.h"
#include "dsk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMU_WLF_MAX (WLF_MAX_PACKAGE + SIG_TRAILER_LEN)

static int wlf_meta_len_local(const uint8_t *buf, uint32_t size) {
    if (size < WLF_HEADER_SIZE + WLF_META_HDR) return 0;
    uint32_t magic;
    memcpy(&magic, buf + WLF_HEADER_SIZE, 4);
    if (magic != WLF_META_MAGIC) return 0;
    uint16_t len;
    memcpy(&len, buf + WLF_HEADER_SIZE + 4, 2);
    if ((uint32_t)WLF_HEADER_SIZE + WLF_META_HDR + len > size) return -1;
    return (int)len;
}

/* OS'taki wlf_meta_read'in ayni mantigi (desktop/wlf.c). */
static int meta_read(const uint8_t *buf, uint32_t size, wlf_meta_t *out) {
    memset(out, 0, sizeof *out);
    out->role = WLF_ROLE_UI;
    int mlen = wlf_meta_len_local(buf, size);
    if (mlen < 0) return -1;
    if (mlen == 0) return 0;
    const uint8_t *m = buf + WLF_HEADER_SIZE + WLF_META_HDR;
    uint32_t i = 0;
    while (i < (uint32_t)mlen) {
        uint32_t ls = i;
        while (i < (uint32_t)mlen && m[i] != '\n' && m[i] != '\r') i++;
        uint32_t le = i;
        while (i < (uint32_t)mlen && (m[i] == '\n' || m[i] == '\r')) i++;
        if (le <= ls) continue;
        uint32_t eq = ls;
        while (eq < le && m[eq] != '=') eq++;
        if (eq >= le) continue;
        const char *k = (const char *)m + ls;
        int klen = (int)(eq - ls);
        const char *v = (const char *)m + eq + 1;
        int vlen = (int)(le - eq - 1);
        if (klen == 4 && !memcmp(k, "role", 4)) {
            if (vlen == 3 && !memcmp(v, "cli", 3)) out->role = WLF_ROLE_CLI;
            else if (vlen == 2 && !memcmp(v, "ui", 2)) out->role = WLF_ROLE_UI;
            else if (vlen >= 3 && !memcmp(v, "run", 3)) out->role = WLF_ROLE_RUNTIME;
            else if (vlen == 1 && v[0] >= '0' && v[0] <= '2')
                out->role = (uint16_t)(v[0] - '0');
        } else if (klen == 7 && !memcmp(k, "handles", 7)) {
            int n = vlen < (int)sizeof(out->handles) - 1 ? vlen
                                                        : (int)sizeof(out->handles) - 1;
            memcpy(out->handles, v, (size_t)n);
            out->handles[n] = 0;
        } else if (klen == 5 && !memcmp(k, "entry", 5)) {
            int n = vlen < (int)sizeof(out->entry) - 1 ? vlen
                                                       : (int)sizeof(out->entry) - 1;
            memcpy(out->entry, v, (size_t)n);
            out->entry[n] = 0;
        } else if (klen == 8 && !memcmp(k, "terminal", 8)) {
            if (vlen >= 1 && v[0] == '1') out->flags |= WLF_META_TERMINAL;
        }
    }
    return 0;
}

static void cstr(char *dst, int cap, const char *src, int srclen) {
    int n = 0;
    while (n < srclen && src[n] && n < cap - 1) { dst[n] = src[n]; n++; }
    dst[n] = 0;
}

int pkg_load(const char *path, emu_pkg_t *pkg, char *err, int errcap) {
    memset(pkg, 0, sizeof *pkg);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errcap, "dosya acilamadi: %s", path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > EMU_WLF_MAX) {
        fclose(f);
        snprintf(err, errcap, "gecersiz boyut (%ld bayt; en fazla %d)",
                 sz, EMU_WLF_MAX);
        return -1;
    }
    uint8_t *raw = (uint8_t *)malloc((size_t)sz);
    if (!raw || fread(raw, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(raw);
        snprintf(err, errcap, "okuma hatasi");
        return -1;
    }
    fclose(f);

    pkg->raw = raw;
    pkg->raw_len = (uint32_t)sz;

    if (sz < WLF_HEADER_SIZE) {
        snprintf(err, errcap, "dosya cok kucuk");
        goto fail;
    }
    const wlf_header_t *h = (const wlf_header_t *)raw;
    if (h->magic != WLF_MAGIC) {
        snprintf(err, errcap, "gecersiz sihir 0x%08X (WLF1 bekleniyor)", h->magic);
        goto fail;
    }
    if (h->version != WLF_VERSION) {
        snprintf(err, errcap, "surum %u desteklenmiyor", h->version);
        goto fail;
    }
    pkg->body_len = sig_body_len(raw, (uint32_t)sz);
    if (pkg->body_len < WLF_HEADER_SIZE) {
        snprintf(err, errcap, "govde cok kucuk");
        goto fail;
    }

    int mlen = wlf_meta_len_local(raw, pkg->body_len);
    if (mlen < 0) {
        snprintf(err, errcap, "meta blogu bozuk");
        goto fail;
    }
    uint32_t meta_sz = (uint32_t)WLF_HEADER_SIZE +
                       (mlen ? (uint32_t)WLF_META_HDR + (uint32_t)mlen : 0);
    if (h->payload_off < meta_sz ||
        (uint64_t)h->payload_off + h->payload_len > pkg->body_len) {
        snprintf(err, errcap, "payload sinirlari govde disinda");
        goto fail;
    }
    if (h->payload_off < meta_sz + h->icon_len ||
        (uint64_t)h->payload_off - h->icon_len < meta_sz) {
        snprintf(err, errcap, "ikon alani cakisiyor");
        goto fail;
    }
    uint32_t ck = wlf_checksum(raw + h->payload_off - h->icon_len,
                               h->icon_len + h->payload_len);
    if (ck != h->checksum) {
        snprintf(err, errcap, "checksum uyusmuyor (0x%08X != 0x%08X)", ck,
                 h->checksum);
        goto fail;
    }
    if (h->kind == WLF_KIND_NATIVE) {
        if (h->load_addr < 0x02000000u) {
            snprintf(err, errcap, "yukleme adresi cok dusuk 0x%X", h->load_addr);
            goto fail;
        }
        if ((uint64_t)h->image_size + h->bss_size > 64u * 1024u * 1024u ||
            h->image_size > h->payload_len ||
            h->entry_off >= h->image_size) {
            snprintf(err, errcap, "native alanlar gecersiz");
            goto fail;
        }
    } else if (h->kind != WLF_KIND_SCRIPT) {
        snprintf(err, errcap, "bilinmeyen tur %u", h->kind);
        goto fail;
    }

    pkg->hdr = h;
    pkg->icon = raw + h->payload_off - h->icon_len;
    pkg->icon_len = h->icon_len;
    pkg->payload = raw + h->payload_off;
    pkg->payload_len = h->payload_len;
    meta_read(raw, pkg->body_len, &pkg->meta);

    int sv = sig_verify_pkg(raw, (uint32_t)sz);   /* 0=gecerli, -1=yok, -2=bozuk */
    if (sv == 0)       pkg->sig_ok = 1;   /* gecerli  */
    else if (sv == -1) pkg->sig_ok = 0;   /* imzasiz gelistirme paketi */
    else               pkg->sig_ok = -1;  /* imza var ama gecersiz */
    return 0;

fail:
    free(raw);
    memset(pkg, 0, sizeof *pkg);
    return -1;
}

void pkg_free(emu_pkg_t *pkg) {
    free(pkg->raw);
    memset(pkg, 0, sizeof *pkg);
}
