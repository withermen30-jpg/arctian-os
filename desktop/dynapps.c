/**
 * Dinamik uygulama sicili (DPK + WLF)
 *
 * Iki paket bicimi ayni sicile kaydolur:
 *   .dpk : DuckStore paketi (native, desktop sembollerine linklenmis)
 *   .wlf : Wolf uygulamasi (native makine kodu ya da Wolf bytecode)
 *
 * Sicil kayitlari baslat menusu, gorev cubugu, masaustu simgeleri ve
 * `apps`/`wf list` komutlari tarafindan kullanilir.
 */
#include "dsk.h"

/* ------------------------------------------------------------------ */
/* Kucuk yardimcilar                                                   */
/* ------------------------------------------------------------------ */

static int starts_with(const char *s, const char *p) {
    if (!s || !p) return 0;
    for (int i = 0; p[i]; i++) if (s[i] != p[i]) return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* DPK ABI imzasi                                                      */
/* ------------------------------------------------------------------ */

/* "arctian-<adres>" — arctian_notify'in gercek adresi. Paketleyici ayni
 * degeri `nm desktop.elf` ile okur; boylece adres kaymasi yakalanir. */
const char *dpk_abi_signature(void) {
    static char s[DPK_ABI_LEN];
    static int ready;
    if (!ready) {
        uintptr_t a = (uintptr_t)(void *)&arctian_notify;
        char hex[20];
        int n = 0, k = 0;
        char t[20];
        if (!a) t[n++] = '0';
        while (a && n < 18) { int d = (int)(a & 0xF); t[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); a >>= 4; }
        const char *pre = "arctian-";
        for (int i = 0; pre[i] && k < (int)sizeof(s) - 1; i++) s[k++] = pre[i];
        while (n && k < (int)sizeof(s) - 1) s[k++] = t[--n];
        s[k] = 0;
        ready = 1;
    }
    return s;
}

/* ------------------------------------------------------------------ */
/* DPK dogrulama / meta                                                */
/* ------------------------------------------------------------------ */

int dpk_is_package_name(const char *nm) {
    int n = slen2(nm);
    if (n < 5) return 0;
    return nm[n - 4] == '.' && nm[n - 3] == 'd' && nm[n - 2] == 'p' &&
           nm[n - 1] == 'k';
}

int dpk_validate(const uint8_t *buf, uint32_t size, const dpk_header_t **out) {
    if (!buf || size < DPK_HEADER_SIZE) return -1;
    /* 0.7: imza zorunlu. Govde = dosya - imza blogu.      */
    int sg = sig_verify_pkg(buf, size);
    if (sg == -1) return -4;          /* imzasiz paket reddedilir */
    if (sg != 0) return -4;           /* bozuk imza reddedilir */
    uint32_t body = sig_body_len(buf, size);
    const dpk_header_t *h = (const dpk_header_t *)buf;
    if (h->magic != DPK_MAGIC || h->version != DPK_VERSION) return -1;
    int idlen = 0;
    while (idlen < DPK_ID_LEN && h->id[idlen]) idlen++;
    if (idlen < 1 || idlen > 26) return -1;
    if (h->image_size == 0) return -1;
    if (h->code_off != DPK_HEADER_SIZE + h->meta_len + h->icon_len) return -1;
    if (h->code_off < DPK_HEADER_SIZE || h->code_off > body) return -1;
    if ((uint64_t)h->code_off + h->image_size > body) return -1;
    if ((uint64_t)h->load_addr < APP_BASE) return -1;
    if ((uint64_t)h->load_addr + h->image_size + h->bss_size >
        (uint64_t)APP_BASE + APP_MAX) return -1;
    if (dpk_checksum(buf + DPK_HEADER_SIZE, body - DPK_HEADER_SIZE) != h->checksum)
        return -1;
    /* 0.6: ABI imzasi. DPK makine kodu desktop sembol adreslerine mutlak
     * linklenir; ABI uyusmazsa calistirmak page fault verir. Erken ve net
     * reddet. Imza bos ise (eski paketleyici) denetim atlanir. */
    {
        const char *sig = dpk_abi_signature();
        int has_abi = 0;
        for (int i = 0; i < DPK_ABI_LEN && h->abi[i]; i++) has_abi = 1;
        if (has_abi && sig && sig[0]) {
            int same = 1;
            for (int i = 0; i < DPK_ABI_LEN; i++) {
                if (h->abi[i] != sig[i]) { same = 0; break; }
                if (!sig[i]) break;
            }
            if (!same) return -2;   /* ABI uyumsuz */
        }
    }
    if (out) *out = h;
    return 0;
}

/* Izin metni: "net,fs,dev" -> CAP_* (dsk.h'de bildirilmis) */
uint32_t perms_parse(const char *s) {
    uint32_t p = 0;
    const char *q = s;
    while (q && *q) {
        while (*q == ',' || *q == ' ' || *q == '\t') q++;
        if (starts_with(q, "net")) p |= CAP_NET;
        else if (starts_with(q, "fs")) p |= CAP_FS;
        else if (starts_with(q, "dev")) p |= CAP_DEV;
        else if (starts_with(q, "all")) p |= CAP_ALL;
        while (*q && *q != ',' && *q != ' ') q++;
    }
    return p;
}

void dpk_parse_meta(const uint8_t *m, uint32_t len, dyn_app_t *d) {
    /* 0.7: varsayilan yetki CAP_ALL degil. Paket ne bildirdiyse o
     * gecer; diski asilan sey yetki degil, kapinin (perm.c) kararidir. */
    d->perms = 0;
    uint32_t i = 0;
    while (i < len) {
        uint32_t ls = i;
        while (i < len && m[i] != '\n' && m[i] != '\r') i++;
        uint32_t le = i;
        while (i < len && (m[i] == '\n' || m[i] == '\r')) i++;
        if (le <= ls) continue;
        uint32_t eq = ls;
        while (eq < le && m[eq] != '=') eq++;
        if (eq >= le) continue;
        const char *key = (const char *)m + ls;
        int klen = (int)(eq - ls);
        char tmp[200];
        pct_decode(tmp, sizeof(tmp), (const char *)m + eq + 1, (int)(le - eq - 1));

        if (key_eq(key, klen, DPK_META_TITLE))
            bin_copy(d->title, sizeof(d->title), tmp);
        else if (key_eq(key, klen, DPK_META_CATEGORY))
            bin_copy(d->category, sizeof(d->category), tmp);
        else if (key_eq(key, klen, DPK_META_VERSION))
            bin_copy(d->version, sizeof(d->version), tmp);
        else if (key_eq(key, klen, DPK_META_PUBLISHER))
            bin_copy(d->publisher, sizeof(d->publisher), tmp);
        else if (key_eq(key, klen, DPK_META_DESCRIPTION))
            bin_copy(d->desc, sizeof(d->desc), tmp);
        else if (key_eq(key, klen, DPK_META_WINDOW)) {
            int w = 0, h = 0, n = 0;
            const char *p = tmp;
            while (*p >= '0' && *p <= '9') { w = w * 10 + (*p - '0'); p++; n++; }
            while (*p == ' ' || *p == ',' || *p == 'x') p++;
            while (*p >= '0' && *p <= '9') { h = h * 10 + (*p - '0'); p++; n++; }
            if (n && w > 120 && h > 80) { d->win_w = w; d->win_h = h; }
        }
        else if (key_eq(key, klen, DPK_META_PERMISSIONS))
            d->perms = perms_parse(tmp);
    }
}

/* ------------------------------------------------------------------ */
/* Sicil                                                              */
/* ------------------------------------------------------------------ */

int dyn_find(const char *id) {
    for (int i = 0; i < g_dyn_count; i++)
        if (s_eq(g_dyn[i].id, id)) return i;
    return -1;
}

dyn_app_t *dyn_register(const char *id) {
    int e = dyn_find(id);
    if (e >= 0) return &g_dyn[e];
    if (g_dyn_count >= MAX_DYN_APPS) return 0;
    dyn_app_t *d = &g_dyn[g_dyn_count++];
    for (unsigned k = 0; k < sizeof(*d); k++) ((char *)d)[k] = 0;
    bin_copy(d->id, sizeof(d->id), id);
    bin_copy(d->fname, sizeof(d->fname), "C:/apps/");
    int l = slen2(d->fname);
    for (int j = 0; id[j] && l < (int)sizeof(d->fname) - 6; j++) d->fname[l++] = id[j];
    d->fname[l] = '.'; d->fname[l + 1] = 'w'; d->fname[l + 2] = 'l';
    d->fname[l + 3] = 'f'; d->fname[l + 4] = 0;
    d->kind = 0;
    d->role = WLF_ROLE_UI;
    d->perms = 0;
    return d;
}

/* ------------------------------------------------------------------ */
/* Dosya erisimi                                                       */
/* ------------------------------------------------------------------ */

int afs_read_at(const char *path, void *buf, uint32_t cap, uint32_t *out_len) {
    if (!g_asi || !g_asi->fs || !path) return -1;
    uint32_t n = 0;

    /* 0.6 duzeltmesi: once YOL tabanli okuyucu. Eski surum yalnizca
     * find(0, path) kullaniyordu; bu, alt klasordeki dosyalari
     * ("C:/apps/x.wlf") bulamiyor, kurulum sessizce basarisiz oluyordu. */
    if (g_asi->fs->read_path) {
        n = g_asi->fs->read_path(path, buf, cap);
    }
    if (n == 0 && g_asi->fs->find && g_asi->fs->read) {
        void *h = g_asi->fs->find(0, path);
        if (!h && g_asi->fs->find_in) {
            /* "C:/apps/x.wlf" -> find_in("C:/apps", "x.wlf") */
            char dir[200], name[80];
            bin_copy(dir, sizeof(dir), path);
            int last = -1;
            for (int i = 0; dir[i]; i++) if (dir[i] == '/') last = i;
            if (last > 0) {
                bin_copy(name, sizeof(name), path + last + 1);
                dir[last] = 0;
                h = g_asi->fs->find_in(dir, name);
            }
        }
        if (h) n = g_asi->fs->read(h, buf, cap);
    }
    if (out_len) *out_len = n;
    return n > 0 ? 0 : -1;
}

int afs_read_file(const char *name, void *buf, uint32_t cap, uint32_t *out_len) {
    return afs_read_at(name, buf, cap, out_len);
}

/* Kullanici diski C:/apps altina paket yazar (yalniz .wlf). */
int afs_write_app_pkg(const char *id, const void *buf, uint32_t len) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in || !g_asi->fs->write) return -1;
    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/apps");
    char base[40];
    int l = 0;
    for (int i = 0; id[i] && l < (int)sizeof(base) - 5; i++) base[l++] = id[i];
    base[l] = '.'; base[l + 1] = 'w'; base[l + 2] = 'l'; base[l + 3] = 'f'; base[l + 4] = 0;
    void *h = g_asi->fs->create_in("C:/apps", base, 0);
    if (!h) return -1;
    return g_asi->fs->write(h, buf, len);
}

char *icon_dup(const uint8_t *p, uint32_t n) {
    if (!p || n == 0) return 0;
    char *s = (char *)g_asi->mem->alloc(n + 1);
    if (!s) return 0;
    for (uint32_t i = 0; i < n; i++) s[i] = (char)p[i];
    s[n] = 0;
    return s;
}

unsigned char *icon_dup_bin(const uint8_t *p, uint32_t n) {
    if (!p || n == 0) return 0;
    unsigned char *s = (unsigned char *)g_asi->mem->alloc(n + 1);
    if (!s) return 0;
    for (uint32_t i = 0; i < n; i++) s[i] = p[i];
    s[n] = 0;
    return s;
}

void dyn_apply_package(dyn_app_t *d, const uint8_t *buf, uint32_t size,
                       const dpk_header_t *h) {
    (void)size;
    const uint8_t *meta = buf + DPK_HEADER_SIZE;
    const uint8_t *icon = meta + h->meta_len;
    dpk_parse_meta(meta, h->meta_len, d);
    if (!d->title[0]) bin_copy(d->title, sizeof(d->title), d->id);
    if (!d->category[0]) bin_copy(d->category, sizeof(d->category), "Genel");
    d->kind = 0;
    d->load_addr = h->load_addr;
    d->image_size = h->image_size;
    d->bss_size = h->bss_size;
    d->entry_off = h->entry_off;
    if (h->icon_len) {
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

/* ------------------------------------------------------------------ */
/* Tarama                                                             */
/* ------------------------------------------------------------------ */

void dyn_scan_one(const char *full_path) {
    /* 0.7: uygulama = yalnizca .wlf. Baska uzanti taranmaz. */
    if (!wlf_is_package_name(full_path)) return;
    wlf_scan_one(full_path);
}

void dyn_scan_afs(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->count) return;

    /* Kullanici diski: C:/apps altindaki .wlf uygulamalari */
    if (g_asi->fs->count_dir && g_asi->fs->list_dir) {
        int n = g_asi->fs->count_dir("C:/apps");
        for (int i = 0; i < n && g_dyn_count < MAX_DYN_APPS; i++) {
            char child[64]; int isdir = 0;
            if (g_asi->fs->list_dir("C:/apps", i, child, sizeof(child), &isdir) != 0) continue;
            if (isdir) continue;
            if (!wlf_is_package_name(child)) continue;
            char full[96];
            int l = 0;
            const char *root = "C:/apps/";
            for (int k = 0; root[k] && l < (int)sizeof(full) - 1; k++) full[l++] = root[k];
            for (int k = 0; child[k] && l < (int)sizeof(full) - 1; k++) full[l++] = child[k];
            full[l] = 0;
            dyn_scan_one(full);
        }
    }

    /* Geriye donuk uyumluluk: ham kokteki .wlf dosyalari */
    int n = g_asi->fs->count();
    for (int i = 0; i < n && g_dyn_count < MAX_DYN_APPS; i++) {
        const char *nm = g_asi->fs->name(i);
        if (!nm || !nm[0]) continue;
        if (!wlf_is_package_name(nm)) continue;
        int slash = 0;
        for (int k = 0; nm[k]; k++) if (nm[k] == '/') { slash = 1; break; }
        if (slash) continue;
        dyn_scan_one(nm);
    }
}

/* ------------------------------------------------------------------ */
/* Uygulama sicili gorunumu (arctian_app_t)                            */
/* ------------------------------------------------------------------ */

const arctian_app_t *app_get(int i) {
    if (i >= 0 && i < arctian_app_count) return &arctian_apps[i];
    int d = i - arctian_app_count;
    if (d < 0 || d >= g_dyn_count) return 0;
    dyn_app_t *a = &g_dyn[d];
    static arctian_app_t v;
    for (unsigned k = 0; k < sizeof(v); k++) ((char *)&v)[k] = 0;
    bin_copy(v.name, sizeof(v.name), a->id);
    bin_copy(v.title, sizeof(v.title), a->title[0] ? a->title : a->id);
    bin_copy(v.category, sizeof(v.category), a->category[0] ? a->category : "Genel");
    /* 0.7: handles artik gercek deger (runtime/cli uygulamalari). */
    v.handles = a->handles;
    v.icon_svg = a->icon_svg ? a->icon_svg : "";
    v.icon_png = a->icon_png;
    v.icon_png_len = a->icon_png_len;
    v.entry = dyn_app_entry;
    v.win_w = a->win_w;
    v.win_h = a->win_h;
    return &v;
}

int app_total(void) { return arctian_app_count + g_dyn_count; }

uint32_t dyn_perms_for(int idx) {
    if (idx < arctian_app_count) return CAP_ALL;   /* gomulu arac guvenilir */
    int d = idx - arctian_app_count;
    if (d < 0 || d >= g_dyn_count) return 0;
    return g_dyn[d].perms;
}

void dyn_app_entry(lv_obj_t *win, asi_t *asi) {
    if (g_launch_dyn < 0 || g_launch_dyn >= g_dyn_count) return;
    dyn_app_t *d = &g_dyn[g_launch_dyn];
    d->open = 1;
    if (d->kind != 0) {
        wlf_run_entry(d, win, asi);
        return;
    }
    arctian_app_entry_t fn =
        (arctian_app_entry_t)(uintptr_t)(d->load_addr + d->entry_off);
    fn(win, asi);
}

void arctian_dyn_begin_launch(int idx) { g_launch_dyn = idx; }

void arctian_dyn_mark_closed(int idx) {
    if (idx >= 0 && idx < g_dyn_count) g_dyn[idx].open = 0;
}

int arctian_dyn_preload(int d, char *err, int cap) {
    if (d < 0 || d >= g_dyn_count) { bin_copy(err, cap, "Gecersiz uygulama."); return -1; }
    dyn_app_t *a = &g_dyn[d];
    if (a->kind != 0) {
        /* .wlf: yukleme calisma aninda yapilir (bytecode VM ya da native
         * kopyalama). On yukleme gerekmez. */
        return 0;
    }
    for (int i = 0; i < g_dyn_count; i++) {
        if (i == d) continue;
        if (g_dyn[i].open && g_dyn[i].load_addr == a->load_addr) {
            bin_copy(err, cap, "Bellek cakismasi: ayni bolgede acik bir uygulama var.");
            return -1;
        }
    }
    uint32_t len = 0;
    if (afs_read_file(a->fname, g_pkg_buf, sizeof(g_pkg_buf), &len) != 0) {
        bin_copy(err, cap, "Paket diskten okunamadi.");
        return -1;
    }
    const dpk_header_t *h = 0;
    if (dpk_validate(g_pkg_buf, len, &h) != 0) {
        bin_copy(err, cap, "Paket bozuk (dogrulama hatasi).");
        return -1;
    }
    if (h->load_addr != a->load_addr) {
        bin_copy(err, cap, "Paket surumu uyumsuz.");
        return -1;
    }
    uint8_t *dst = (uint8_t *)(uintptr_t)h->load_addr;
    for (uint32_t i = 0; i < h->image_size; i++) dst[i] = g_pkg_buf[h->code_off + i];
    if (h->bss_size) for (uint32_t i = 0; i < h->bss_size; i++) dst[h->image_size + i] = 0;
    return 0;
}
