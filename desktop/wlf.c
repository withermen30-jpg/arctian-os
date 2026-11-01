/**
 * .wlf (libWolf) uygulama yukleyicisi ve dosya islemleri — 0.7
 *
 * Iki tur desteklenir:
 *   WLF_KIND_NATIVE : x86-64 makine kodu; libWolf'a linklenmistir ve isletim
 *                     sistemi sembolune ihtiyac duymaz. `wolf_main(api)`
 *                     cagrilir.
 *   WLF_KIND_SCRIPT : (isim tarihsel) gomulu Wolf bytecode. Isletim sistemi
 *                     icindeki `wf build` bu turu uretir; VM calistirir.
 *                     API ayni libWolf'tur, uygulama yine C ile yazilir.
 *
 * Kayit: .wlf dosyalari C:/apps altindan taranir ve DPK'larla ayni uygulama
 * siciline (dyn_app_t) eklenir; boylece baslat menusu, gorev cubugu, masaustu
 * simgeleri ve `apps` komutu otomatik olarak gorur.
 */
#include "dsk.h"


/* ---- Seri tanilama (COM1) ----
 * .wlf kurulum/calistirma sonuclari seri porta yazilir; boylece sorun
 * gorsel okumaya gerek kalmadan teshis edilebilir. Kisa ve seyrektir. */
static inline void wlf_outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port));
}
static inline uint8_t wlf_inb(uint16_t port) {
    uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port)); return v;
}
static void wlf_puts(const char *s) {
    for (; s && *s; s++) {
        while (!(wlf_inb(0x3FD) & 0x20)) { }
        wlf_outb(0x3F8, (uint8_t)*s);
    }
}
static void wlf_puti(int v) {
    char b[12]; int n = 0, k = 0;
    if (v < 0) { wlf_puts("-"); v = -v; }
    if (!v) b[n++] = '0';
    while (v && n < 12) { b[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) { char c = b[--n]; while (!(wlf_inb(0x3FD) & 0x20)) { } wlf_outb(0x3F8, (uint8_t)c); }
    (void)k;
}
/* Terminalden gelen ham komutu seri porta yazar (teshis). */
void wlf_diag(const char *tag, const char *s) {
    wlf_puts("[wlf] ");
    wlf_puts(tag ? tag : "?");
    wlf_puts(" '");
    wlf_puts(s ? s : "");
    wlf_puts("'\n");
}

#define WLF_DBG(tag, name, rc) do { \
    wlf_puts("[wlf] " tag " "); wlf_puts(name ? name : "?"); \
    wlf_puts(" rc="); wlf_puti(rc); wlf_puts("\n"); } while (0)

/* .wlf paket tamponu: DPK tamponundan ayri (256 KB sinir). */
static uint8_t s_wlf_buf[WLF_MAX_PACKAGE];

/* VM giris noktasi (wlf_vm.c) */
int wlf_vm_run(const uint8_t *code, uint32_t len, lv_obj_t *body, asi_t *asi);

/* ------------------------------------------------------------------ */
/* Meta blogu ("WLFM")                                                 */
/* ------------------------------------------------------------------ */

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Meta blogunun uzunlugunu dondurur (blok yoksa 0). Bozuksa -1.
 * Blok = "WLFM" + u16 len + len bayt key=value. */
static int32_t wlf_meta_len(const uint8_t *buf, uint32_t size) {
    if (!buf || size < WLF_HEADER_SIZE + WLF_META_HDR) return 0;
    if (rd32(buf + WLF_HEADER_SIZE) != WLF_META_MAGIC) return 0;
    uint32_t n = rd16(buf + WLF_HEADER_SIZE + 4);
    if ((uint64_t)WLF_HEADER_SIZE + WLF_META_HDR + n > size) return -1;
    return (int32_t)n;
}

/* Ikonun dosya icindeki offseti (meta blogu varsa atlanir). */
static uint32_t wlf_icon_off(const uint8_t *buf, uint32_t size) {
    int32_t m = wlf_meta_len(buf, size);
    if (m <= 0) return WLF_HEADER_SIZE;
    return WLF_HEADER_SIZE + WLF_META_HDR + (uint32_t)m;
}

static int wlf_meta_uint(const char *s, int n, unsigned dflt, unsigned *out) {
    unsigned v = 0;
    int any = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') { v = v * 10 + (unsigned)(c - '0'); any = 1; }
        else if (c != ' ') break;
    }
    *out = any ? v : dflt;
    return any;
}

int wlf_meta_read(const uint8_t *buf, uint32_t size, wlf_meta_t *out) {
    if (!out) return -1;
    for (unsigned i = 0; i < sizeof(*out); i++) ((uint8_t *)out)[i] = 0;
    out->role = WLF_ROLE_UI;
    if (!buf || size < WLF_HEADER_SIZE) return -1;

    int32_t mlen = wlf_meta_len(buf, size);
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

        if (klen == 4 && k[0] == 'r' && k[1] == 'o' && k[2] == 'l' && k[3] == 'e') {
            unsigned r = 0;
            if (wlf_meta_uint(v, vlen, WLF_ROLE_UI, &r)) {
                if (r > WLF_ROLE_RUNTIME) r = WLF_ROLE_UI;
                out->role = (uint16_t)r;
            } else if (vlen == 3 && v[0] == 'c' && v[1] == 'l' && v[2] == 'i') {
                out->role = WLF_ROLE_CLI;
            } else if (vlen == 2 && v[0] == 'u' && v[1] == 'i') {
                out->role = WLF_ROLE_UI;
            } else if (vlen >= 3 && v[0] == 'r' && v[1] == 'u' && v[2] == 'n') {
                out->role = WLF_ROLE_RUNTIME;
            }
        } else if (klen == 7 && k[0] == 'h' && k[1] == 'a' && k[2] == 'n' &&
                   k[3] == 'd' && k[4] == 'l' && k[5] == 'e' && k[6] == 's') {
            int n = vlen < (int)sizeof(out->handles) - 1 ? vlen : (int)sizeof(out->handles) - 1;
            for (int j = 0; j < n; j++) out->handles[j] = v[j];
            out->handles[n] = 0;
        } else if (klen == 5 && k[0] == 'e' && k[1] == 'n' && k[2] == 't' &&
                   k[3] == 'r' && k[4] == 'y') {
            int n = vlen < (int)sizeof(out->entry) - 1 ? vlen : (int)sizeof(out->entry) - 1;
            for (int j = 0; j < n; j++) out->entry[j] = v[j];
            out->entry[n] = 0;
        } else if (klen == 8 && k[0] == 't' && k[1] == 'e' && k[2] == 'r' &&
                   k[3] == 'm' && k[4] == 'i' && k[5] == 'n' && k[6] == 'a' && k[7] == 'l') {
            unsigned t = 0;
            if (wlf_meta_uint(v, vlen, 0, &t) && t) out->flags |= WLF_META_TERMINAL;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Ad / dogrulama                                                      */
/* ------------------------------------------------------------------ */

int wlf_is_package_name(const char *nm) {
    int n = slen2(nm);
    if (n < 5) return 0;
    return nm[n - 4] == '.' && nm[n - 3] == 'w' && nm[n - 2] == 'l' &&
           nm[n - 1] == 'f';
}

static int wlf_name_ok(const char *s, int max) {
    int n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return 0;
    }
    return n > 0 && n < max;
}

int wlf_validate(const uint8_t *buf, uint32_t size, const wlf_header_t **out) {
    if (!buf || size < WLF_HEADER_SIZE) return -1;
    /* 0.7: imza zorunlu.      */
    if (sig_verify_pkg(buf, size) != 0) return -13;
    uint32_t body = sig_body_len(buf, size);
    const wlf_header_t *h = (const wlf_header_t *)buf;
    if (h->magic != WLF_MAGIC) return -1;
    if (h->version != WLF_VERSION) return -2;
    if (h->kind != WLF_KIND_NATIVE && h->kind != WLF_KIND_SCRIPT) return -3;
    if (h->role > WLF_ROLE_RUNTIME) return -3;
    if (!wlf_name_ok(h->name, WLF_NAME_LEN)) return -4;
    if (h->payload_len == 0) return -5;
    if (h->payload_off < WLF_HEADER_SIZE) return -6;
    if ((uint64_t)h->payload_off + h->payload_len > body) return -7;
    /* 0.7: meta blogu bozuksa ya da ikon/payload ust uste binerse reddet. */
    if (wlf_meta_len(buf, size) < 0) return -14;
    uint32_t icon_off = wlf_icon_off(buf, size);
    if ((uint64_t)icon_off + h->icon_len > body) return -8;
    if (h->payload_off < icon_off + h->icon_len) return -8;
    if (h->kind == WLF_KIND_NATIVE) {
        if (h->load_addr < WLF_BASE) return -9;
        if ((uint64_t)h->load_addr + h->image_size + h->bss_size >
            (uint64_t)WLF_BASE + WLF_MAX) return -10;
        if (h->entry_off >= h->image_size) return -11;
    }
    /* checksum: ikon + payload */
    const uint8_t *ikon = buf + icon_off;
    uint32_t s = 0x12345678u;
    for (uint32_t i = 0; i < h->icon_len; i++) s = (s << 1) ^ (s >> 31) ^ ikon[i];
    const uint8_t *pl = buf + h->payload_off;
    for (uint32_t i = 0; i < h->payload_len; i++) s = (s << 1) ^ (s >> 31) ^ pl[i];
    if (s != h->checksum) return -12;
    if (out) *out = h;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Sicile aktarma                                                      */
/* ------------------------------------------------------------------ */

void wlf_apply_package(dyn_app_t *d, const uint8_t *buf, uint32_t size,
                       const wlf_header_t *h) {
    (void)size;
    d->kind = (h->kind == WLF_KIND_NATIVE) ? 1 : 2;
    d->load_addr = h->load_addr;
    d->image_size = h->image_size;
    d->bss_size = h->bss_size;
    d->entry_off = h->entry_off;
    d->flags = h->flags;
    d->win_w = h->win_w > 0 ? h->win_w : 560;
    d->win_h = h->win_h > 0 ? h->win_h : 400;

    /* 0.7: rol + meta. Meta yoksa header.role gecerli (eski paket = UI). */
    wlf_meta_t m;
    int has_meta = 0;
    if (wlf_meta_len(buf, size) > 0) {
        has_meta = (wlf_meta_read(buf, size, &m) == 0);
    }
    if (!has_meta) {
        for (unsigned i = 0; i < sizeof(m); i++) ((uint8_t *)&m)[i] = 0;
    }
    d->role = has_meta ? m.role : h->role;
    if (d->role > WLF_ROLE_RUNTIME) d->role = WLF_ROLE_UI;
    bin_copy(d->handles, sizeof(d->handles), m.handles);
    bin_copy(d->entry_name, sizeof(d->entry_name), m.entry);
    d->terminal = (m.flags & WLF_META_TERMINAL) ? 1 : 0;

    /* Izinler: .wlf bayraklari -> CAP_*. Fail-closed: yetki her zaman
     * bayraklardan turertilir; manifest'in caps alani yalniz UI susudur.
     * Eskiden flags==0 paket CAP_ALL aliyordu, yani "bayragsiz" demek
     * "tum yetkiler" demekti; paket sahibinin unutmasi buyuk yetki veriyordu.
     * flags==0 artik en gerilemeli varsayilanla (yalniz kendi verisi) calisir. */
    uint32_t p = 0;
    if (h->flags & WLF_FLAG_FS)   p |= CAP_FS;
    if (h->flags & WLF_FLAG_NET)  p |= CAP_NET;
    if (h->flags & WLF_FLAG_EXEC) p |= CAP_DEV;   /* calistirma/uygulama acma */
    if (p == 0) p = CAP_FS;   /* en azindan kendi verisi */
    d->perms = p;
    if (h->icon_len) {
        const uint8_t *icon = buf + wlf_icon_off(buf, size);
        if (h->icon_len >= 8 && icon[0] == 0x89 && icon[1] == 'P' &&
            icon[2] == 'N' && icon[3] == 'G') {
            unsigned char *ic = icon_dup_bin(icon, h->icon_len);
            if (ic) { d->icon_png = ic; d->icon_png_len = h->icon_len; d->icon_svg = 0; }
        } else {
            char *ic = icon_dup(icon, h->icon_len);
            if (ic) { d->icon_svg = ic; d->icon_png = 0; d->icon_png_len = 0; }
        }
    }
}

int wlf_scan_one(const char *full_path) {
    uint32_t len = 0;
    int rr = afs_read_at(full_path, s_wlf_buf, sizeof(s_wlf_buf), &len);
    if (rr != 0) return -1;
    const wlf_header_t *h = 0;
    int vv = wlf_validate(s_wlf_buf, len, &h);
    if (vv != 0) { WLF_DBG("scan-dogrula", full_path, vv); return -1; }
    char id[WLF_NAME_LEN];
    bin_copy(id, sizeof(id), h->name);
    dyn_app_t *d = dyn_register(id);
    if (!d) return -1;
    bin_copy(d->fname, sizeof(d->fname), full_path);
    if (h->title[0]) bin_copy(d->title, sizeof(d->title), h->title);
    if (h->category[0]) bin_copy(d->category, sizeof(d->category), h->category);
    if (h->ver[0]) bin_copy(d->version, sizeof(d->version), h->ver);
    if (h->author[0]) bin_copy(d->publisher, sizeof(d->publisher), h->author);
    wlf_apply_package(d, s_wlf_buf, len, h);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Calistirma                                                          */
/* ------------------------------------------------------------------ */

/* Native: kodu load_addr'e kopyala, bss'i sifirla, wolf_main(api) cagir. */
static int wlf_run_native(dyn_app_t *d, const uint8_t *payload, uint32_t plen,
                          lv_obj_t *body, asi_t *asi) {
    (void)asi;
    if (plen < d->image_size) return -1;
    uint8_t *dst = (uint8_t *)(uintptr_t)d->load_addr;
    for (uint32_t i = 0; i < d->image_size; i++) dst[i] = payload[i];
    if (d->bss_size) for (uint32_t i = 0; i < d->bss_size; i++) dst[d->image_size + i] = 0;

    wolf_api_t *api = wlf_api_get(body);
    if (!api) return -1;

    typedef int (*wolf_main_fn)(const wolf_api_t *);
    wolf_main_fn fn = (wolf_main_fn)(uintptr_t)(d->load_addr + d->entry_off);
    int rc = fn(api);
    return rc;
}

void wlf_run_entry(dyn_app_t *d, lv_obj_t *body, asi_t *asi) {
    if (!d) return;
    wlf_host_reset(body);
    uint32_t len = 0;
    if (afs_read_at(d->fname, s_wlf_buf, sizeof(s_wlf_buf), &len) != 0) {
        wlf_host_error("Paket diskten okunamadi");
        return;
    }
    const wlf_header_t *h = 0;
    if (wlf_validate(s_wlf_buf, len, &h) != 0) {
        wlf_host_error("Paket bozuk (dogrulama hatasi)");
        return;
    }
    const uint8_t *payload = s_wlf_buf + h->payload_off;
    if (h->kind == WLF_KIND_NATIVE) {
        int rc = wlf_run_native(d, payload, h->payload_len, body, asi);
        if (rc != 0) wlf_host_error("Uygulama hata kodu dondurdu");
    } else {
        int rc = wlf_vm_run(payload, h->payload_len, body, asi);
        if (rc != 0) wlf_host_error("Wolf program hatasi");
    }
}

/* ------------------------------------------------------------------ */
/* Terminalde calistirma (rol = cli / runtime)                          */
/* ------------------------------------------------------------------ */

int wlf_run_cli(dyn_app_t *d, const char *arg, void (*puts)(const char *s)) {
    if (!d) return -1;
    uint32_t len = 0;
    if (afs_read_at(d->fname, s_wlf_buf, sizeof(s_wlf_buf), &len) != 0) {
        if (puts) puts("Paket diskten okunamadi\n");
        return -1;
    }
    const wlf_header_t *h = 0;
    if (wlf_validate(s_wlf_buf, len, &h) != 0) {
        if (puts) puts("Paket bozuk (dogrulama hatasi)\n");
        return -1;
    }
    /* Pencere yok: .wlf kendi kabi gorunmez bir kapsayiciya cizilir; metin
     * ciktisi terminale gider. Boylece UI cizmeyen backend uygulamalari da
     * ayni ikili bicimle calisir. */
    lv_obj_t *body = lv_obj_create(lv_screen_active());
    if (!body) return -1;
    lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
    wlf_host_set_sink(puts);
    wlf_host_set_arg(arg);
    wlf_host_reset(body);
    const uint8_t *payload = s_wlf_buf + h->payload_off;
    int rc;
    if (h->kind == WLF_KIND_NATIVE)
        rc = wlf_run_native(d, payload, h->payload_len, body, g_asi);
    else
        rc = wlf_vm_run(payload, h->payload_len, body, g_asi);
    wlf_host_set_sink(0);
    wlf_host_set_arg(0);
    lv_obj_delete(body);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Dosya islemleri                                                     */
/* ------------------------------------------------------------------ */

static int wlf_write_app(const char *name, const void *buf, uint32_t len) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in || !g_asi->fs->write) return -1;
    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/apps");
    char fn[WLF_NAME_LEN + 8];
    int l = 0;
    for (int i = 0; name[i] && l < (int)sizeof(fn) - 5; i++) fn[l++] = name[i];
    fn[l] = '.'; fn[l + 1] = 'w'; fn[l + 2] = 'l'; fn[l + 3] = 'f'; fn[l + 4] = 0;
    void *h = g_asi->fs->create_in("C:/apps", fn, 0);
    if (!h) return -1;
    return g_asi->fs->write(h, buf, len);
}

int wlf_install_file(const char *path, char *err, int cap) {
    if (!g_asi || !g_asi->fs) { bin_copy(err, cap, "Dosya sistemi yok"); return -1; }
    uint32_t len = 0;
    if (afs_read_at(path, s_wlf_buf, sizeof(s_wlf_buf), &len) != 0) {
        bin_copy(err, cap, "Dosya okunamadi");
        return -1;
    }
    const wlf_header_t *h = 0;
    int v = wlf_validate(s_wlf_buf, len, &h);
    if (v != 0) {
        bin_copy(err, cap, "Gecersiz .wlf (kod -1)");
        return -1;
    }
    if (wlf_write_app(h->name, s_wlf_buf, len) != 0) {
        WLF_DBG("dosya-yaz", h->name, -1);
        bin_copy(err, cap, "C:/apps'a yazilamadi");
        return -1;
    }
    WLF_DBG("dosya-kuruldu", h->name, 0);
    /* Sicile hemen ekle (yeniden baslatmadan gorunur). */
    dyn_app_t *ex = 0;
    int idx = dyn_find(h->name);
    if (idx >= 0) ex = &g_dyn[idx];
    if (!ex) wlf_scan_one(path);
    else {
        char full[64];
        int l = 0;
        const char *pre = "C:/apps/";
        for (int i = 0; pre[i] && l < (int)sizeof(full) - 8; i++) full[l++] = pre[i];
        for (int i = 0; h->name[i] && l < (int)sizeof(full) - 6; i++) full[l++] = h->name[i];
        full[l] = 0;
        wlf_scan_one(full);
    }
    return 0;
}

int wlf_install_url(const char *url, char *err, int cap) {
    if (!g_asi || !g_asi->net || !g_asi->net->download) {
        bin_copy(err, cap, "Ag servisi yok");
        return -1;
    }
    static uint8_t *buf;
    const uint32_t cap_buf = WLF_MAX_PACKAGE;
    if (!buf) buf = (uint8_t *)g_asi->mem->alloc(cap_buf);
    if (!buf) { bin_copy(err, cap, "Bellek yetersiz"); return -1; }

    uint32_t len = 0;
    int drc = g_asi->net->download(url, buf, cap_buf, &len);
    if (drc != 0 || len < WLF_HEADER_SIZE) {
        bin_copy(err, cap, "Indirme basarisiz");
        return -1;
    }
    const wlf_header_t *h = 0;
    int v = wlf_validate(buf, len, &h);
    if (v != 0) {
        WLF_DBG("url-dogrula", url, v);
        bin_copy(err, cap, "Indirilen dosya gecerli .wlf degil");
        return -1;
    }
    if (wlf_write_app(h->name, buf, len) != 0) {
        WLF_DBG("url-yaz", h->name, -1);
        bin_copy(err, cap, "C:/apps'a yazilamadi");
        return -1;
    }
    WLF_DBG("url-kuruldu", h->name, 0);
    char full[64];
    int l = 0;
    const char *pre = "C:/apps/";
    for (int i = 0; pre[i] && l < (int)sizeof(full) - 8; i++) full[l++] = pre[i];
    for (int i = 0; h->name[i] && l < (int)sizeof(full) - 6; i++) full[l++] = h->name[i];
    full[l] = '.';
    full[l + 1] = 'w'; full[l + 2] = 'l'; full[l + 3] = 'f'; full[l + 4] = 0;
    wlf_scan_one(full);
    return 0;
}

int wlf_remove(const char *id) {
    int idx = dyn_find(id);
    if (idx < 0) return -1;
    dyn_app_t *d = &g_dyn[idx];
    if (d->open) return -2;
    if (g_asi && g_asi->fs && g_asi->fs->remove && d->fname[0])
        g_asi->fs->remove(d->fname);
    for (int i = idx; i < g_dyn_count - 1; i++) g_dyn[i] = g_dyn[i + 1];
    if (g_dyn_count > idx) g_dyn_count--;
    return 0;
}
