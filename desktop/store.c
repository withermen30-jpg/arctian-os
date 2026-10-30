#include "dsk.h"


/* ---- Store API ----
 * 0.7: uygulama bicimi tek: .wlf. .dpk artik kurulmaz; gelen baytlar
 * .wlf degilse acik bir hatayla reddedilir (sessiz basarisizlik yok). */
int store_install(const void *pkg, uint32_t size, char *err, int errcap) {
    if (err && errcap > 0) err[0] = 0;
    const uint8_t *b = (const uint8_t *)pkg;
    if (!b || size < WLF_HEADER_SIZE) {
        bin_copy(err, errcap, "Paket cok kucuk.");
        return -1;
    }
    uint32_t magic = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                     ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    if (magic != WLF_MAGIC) {
        bin_copy(err, errcap,
                 "Yalnizca .wlf uygulamalari kurulabilir. "
                 "Eski .dpk bicimi 0.7'de kaldirildi.");
        return -1;
    }
    const wlf_header_t *h = 0;
    int vr = wlf_validate(b, size, &h);
    if (vr != 0) {
        bin_copy(err, errcap, "Gecersiz veya imzasiz .wlf.");
        return -1;
    }
    char id[WLF_NAME_LEN];
    bin_copy(id, sizeof(id), h->name);
    dyn_app_t *d = dyn_register(id);
    if (!d) { bin_copy(err, errcap, "Uygulama limiti doldu."); return -1; }
    if (afs_write_app_pkg(id, pkg, size) != 0) {
        bin_copy(err, errcap, "Diske yazilamadi (AFS).");
        return -1;
    }
    bin_copy(d->fname, sizeof(d->fname), "C:/apps/");
    int fl = slen2(d->fname);
    for (int k = 0; id[k] && fl < (int)sizeof(d->fname) - 6; k++) d->fname[fl++] = id[k];
    d->fname[fl] = '.'; d->fname[fl + 1] = 'w'; d->fname[fl + 2] = 'l'; d->fname[fl + 3] = 'f'; d->fname[fl + 4] = 0;
    wlf_scan_one(d->fname);
    rebuild_shell_ui();
    return 0;
}


int store_uninstall(const char *id) {
    int e = dyn_find(id);
    if (e < 0) return -1;
    if (g_dyn[e].open) return -2;
    if (g_asi && g_asi->fs && g_asi->fs->remove) g_asi->fs->remove(g_dyn[e].fname);
    for (int i = e; i < g_dyn_count - 1; i++) g_dyn[i] = g_dyn[i + 1];
    g_dyn_count--;
    rebuild_shell_ui();
    return 0;
}


int store_launch(const char *id) {
    int e = dyn_find(id);
    if (e < 0) return -1;
    open_registry_app(arctian_app_count + e);
    return 0;
}


int store_installed(const char *id) { return dyn_find(id) >= 0 ? 1 : 0; }


const char *store_installed_version(const char *id) {
    int e = dyn_find(id);
    return e < 0 ? "" : g_dyn[e].version;
}

int store_count_installed(void) { return g_dyn_count; }

const char *store_installed_id(int i) { return (i >= 0 && i < g_dyn_count) ? g_dyn[i].id : ""; }

const char *store_installed_title(int i) {
    return (i >= 0 && i < g_dyn_count) ? (g_dyn[i].title[0] ? g_dyn[i].title : g_dyn[i].id) : "";
}

const char *store_installed_version_at(int i) {
    return (i >= 0 && i < g_dyn_count) ? g_dyn[i].version : "";
}

