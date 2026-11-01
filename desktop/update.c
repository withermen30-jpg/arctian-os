/*
 * Arctian guncelleme mekanizmasi.
 *
 *   - Sunucudan satir tabanli surum manifesti ceker: GET {base}/v1/update
 *   - Manifestte listelenen modul paketlerini indirir (net->download)
 *   - Boyut + CRC32 (IEEE) ile dogrular
 *   - DuckStore kurulum hattina (store_install) verir
 *   - Uygulanan surumu "C:/etc/update.cfg" icinde kalici tutar
 *
 * Manifest format ayrintilari icin update.h'ye bakin.
 */
#include "update.h"
#include "version.h"
#include "sig.h"
#include "slot.h"
#include <stdint.h>

#define UPD_CFG_PATH "C:/etc/update.cfg"
#define UPD_BUF_CAP  (192u * 1024u)   /* DPK_MAX_PACKAGE (~176K) + pay */
/* Manifest govdesinin imza dogrulamada kabul edilen ust siniri. Sunucu
 * tarafinda manifesto bu kadar buyemez; siniri asan manifesto dogrulanamaz
 * sayilir ve guncelleme reddedilir. */
#define UPDATE_MANIFEST_MAX (64u * 1024u)

static asi_t   *s_asi;
static char     s_base[96];
static char     s_applied[24];
static uint8_t *s_buf;
static int      s_buf_cap;
static update_manifest_t s_man;
static update_status_t   s_st;
static int  (*s_install)(const void *dpk, uint32_t size, char *err, int errcap);
static void (*s_notify)(void);

/* 0.7           */
static int       s_man_verified;
static int       s_ready;
static int     (*s_auto_fn)(void);
static int     (*s_delay_fn)(void);
static uint64_t  s_test_now;

/* ---- kucuk yardimcilar ---- */
static int u_len(const char *s) { int n = 0; if (s) while (s[n]) n++; return n; }

static void u_cpy(char *d, int cap, const char *s) {
    int i = 0;
    if (cap <= 0) return;
    if (!s) { d[0] = 0; return; }
    for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}

static void u_cat(char *d, int cap, const char *s) {
    int i = u_len(d);
    if (i > cap - 1) i = cap - 1;
    for (; s && *s && i < cap - 1; i++) d[i] = *s++;
    if (cap > 0) d[i] = 0;
}

static int u_eq(const char *a, const char *b) {
    int i = 0;
    for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}

static uint32_t u_atoi(const char *s) {
    uint32_t v = 0;
    while (s && *s >= '0' && *s <= '9') { v = v * 10u + (uint32_t)(*s - '0'); s++; }
    return v;
}

static void u_itoa(uint32_t v, char *o, int cap) {
    char t[12]; int n = 0, k = 0;
    if (cap <= 0) return;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (n && k < cap - 1) o[k++] = t[--n];
    o[k] = 0;
}

/* CRC32 (IEEE 802.3, 0xEDB88320 geri besleme) */
static uint32_t u_crc32(const uint8_t *d, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

static uint32_t u_hex(const char *s) {
    uint32_t v = 0;
    for (int i = 0; s && s[i] && i < 8; i++) {
        char c = s[i]; int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = (v << 4) | (uint32_t)d;
    }
    return v;
}

/* Nokta ile ayrilmis sayisal surum karsilastirma: a>b -> 1, a==b -> 0, a<b -> -1 */
static int u_ver_cmp(const char *a, const char *b) {
    for (int part = 0; part < 4; part++) {
        int na = 0, nb = 0;
        while (*a >= '0' && *a <= '9') { na = na * 10 + (*a - '0'); a++; }
        while (*b >= '0' && *b <= '9') { nb = nb * 10 + (*b - '0'); b++; }
        if (na != nb) return na < nb ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

/* ---- manifest ayristirma ---- */
static void u_file_defaults(update_file_t *f) {
    u_cpy(f->id, sizeof(f->id), "");
    u_cpy(f->version, sizeof(f->version), "");
    u_cpy(f->package, sizeof(f->package), "");
    f->size = 0;
    f->crc = 0;
}

static void u_manifest_reset(void) {
    u_cpy(s_man.version, sizeof(s_man.version), "");
    u_cpy(s_man.notes, sizeof(s_man.notes), "");
    s_man.count = 0;
    s_man.released = 0;
    s_man.has_image = 0;
    u_cpy(s_man.image.id, sizeof(s_man.image.id), "");
    u_cpy(s_man.image.sha256, sizeof(s_man.image.sha256), "");
    u_cpy(s_man.image.package, sizeof(s_man.image.package), "");
    s_man.image.size = 0;
    u_cpy(s_man.sig, sizeof(s_man.sig), "");
    s_man_verified = 0;
}

/* sig= satiri cikarilmis govde uzerinde imza dogrula.
 * Onceki hali: govde 8191 bayta kirpilip imza o on ek uzerinde dogrulanir,
 * manifest ise tam parse edilirdi; ~8 KB sonrasi (url/notlar) imzayi bozmadan
 * degistirilebilirdi. Artik sigalan baytlar ile parse edilen baytlar ayni
 * olmak zorunda: govde tampona sigmiyorsa dogrulama basta basarisiz doner. */
static int u_manifest_verify(const char *t, uint32_t n, uint32_t skip_s, uint32_t skip_e) {
    if (!s_man.sig[0]) return 0;
    uint32_t body_len = n - (skip_e - skip_s);
    if (body_len >= UPDATE_MANIFEST_MAX) return 0;
    static char body[UPDATE_MANIFEST_MAX];
    uint32_t bl = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (i >= skip_s && i < skip_e) continue;
        body[bl++] = t[i];
    }
    body[bl] = 0;
    return sig_verify_hex(body, bl, s_man.sig) == 0;
}

static int u_parse(const char *t, uint32_t n) {
    u_manifest_reset();
    update_file_t *cur = 0;
    int mode = 0;              /* 0 = ust duzey, 1 = @file, 2 = @image */
    uint32_t sig_s = 0, sig_e = 0;
    uint32_t i = 0;
    while (i < n) {
        uint32_t ls = i;
        uint32_t j = i;
        while (j < n && t[j] != '\n' && t[j] != '\r') j++;
        uint32_t ln = j - i;
        const char *L = t + i;
        i = j;
        while (i < n && (t[i] == '\n' || t[i] == '\r')) i++;
        if (ln == 0 || L[0] == '#') continue;

        if (ln >= 5 && L[0] == '@' && L[1] == 'f' && L[2] == 'i' && L[3] == 'l' && L[4] == 'e') {
            if (s_man.count >= UPDATE_MAX_FILES) { mode = 0; continue; }
            cur = &s_man.files[s_man.count++];
            u_file_defaults(cur);
            mode = 1;
            continue;
        }
        if (ln >= 6 && L[0] == '@' && L[1] == 'i' && L[2] == 'm' && L[3] == 'a' && L[4] == 'g' && L[5] == 'e') {
            s_man.has_image = 1;
            mode = 2;
            continue;
        }
        uint32_t eq = 0;
        while (eq < ln && L[eq] != '=') eq++;
        if (eq == 0 || eq >= ln) continue;

        char key[24];
        uint32_t kl = eq, kc = kl < sizeof(key) - 1 ? kl : sizeof(key) - 1;
        for (uint32_t z = 0; z < kc; z++) key[z] = L[z];
        key[kc] = 0;

        char val[544];
        uint32_t vl = ln - eq - 1, vc = vl < sizeof(val) - 1 ? vl : sizeof(val) - 1;
        for (uint32_t z = 0; z < vc; z++) val[z] = L[eq + 1 + z];
        val[vc] = 0;

        if (u_eq(key, "sig")) {
            u_cpy(s_man.sig, sizeof(s_man.sig), val);
            sig_s = ls;
            sig_e = i;
            continue;
        }
        if (u_eq(key, "released")) s_man.released = u_atoi(val);

        if (mode == 1 && cur) {
            if (u_eq(key, "id")) u_cpy(cur->id, sizeof(cur->id), val);
            else if (u_eq(key, "version")) u_cpy(cur->version, sizeof(cur->version), val);
            else if (u_eq(key, "size")) cur->size = u_atoi(val);
            else if (u_eq(key, "crc")) cur->crc = u_hex(val);
            else if (u_eq(key, "package")) u_cpy(cur->package, sizeof(cur->package), val);
        } else if (mode == 2) {
            if (u_eq(key, "id")) u_cpy(s_man.image.id, sizeof(s_man.image.id), val);
            else if (u_eq(key, "size")) s_man.image.size = u_atoi(val);
            else if (u_eq(key, "sha256")) u_cpy(s_man.image.sha256, sizeof(s_man.image.sha256), val);
            else if (u_eq(key, "package")) u_cpy(s_man.image.package, sizeof(s_man.image.package), val);
        } else {
            if (u_eq(key, "version")) u_cpy(s_man.version, sizeof(s_man.version), val);
            else if (u_eq(key, "notes")) u_cpy(s_man.notes, sizeof(s_man.notes), val);
            else if (u_eq(key, "released")) s_man.released = u_atoi(val);
            else if (u_eq(key, "sig")) {
                u_cpy(s_man.sig, sizeof(s_man.sig), val);
                sig_s = ls;
                sig_e = i;      /* satir sonu dahil */
            }
        }
    }
    /* 0.7: imzasiz manifesto kabul edilmez.          */
    if (s_man.sig[0]) s_man_verified = u_manifest_verify(t, n, sig_s, sig_e);
    return s_man.count;
}

/* ---- kalici surum (C:/etc/update.cfg) ---- */
static void cfg_load(void) {
    u_cpy(s_applied, sizeof(s_applied), ARCTIAN_VERSION);
    if (!s_asi || !s_asi->fs || !s_asi->fs->read_path) return;
    char buf[192];
    uint32_t n = s_asi->fs->read_path(UPD_CFG_PATH, buf, sizeof(buf) - 1);
    if (!n) return;
    buf[n] = 0;
    const char *p = buf;
    while (*p) {
        const char *e = p;
        while (*e && *e != '\n' && *e != '\r') e++;
        if (e - p >= 8 && p[0] == 'v' && p[1] == 'e' && p[2] == 'r' && p[3] == 's' &&
            p[4] == 'i' && p[5] == 'o' && p[6] == 'n' && p[7] == '=') {
            int k = 0;
            for (const char *v = p + 8; v < e && k < (int)sizeof(s_applied) - 1; v++)
                s_applied[k++] = *v;
            s_applied[k] = 0;
        }
        p = e;
        while (*p == '\n' || *p == '\r') p++;
    }
}

static void cfg_save(const char *ver) {
    if (!s_asi || !s_asi->fs || !s_asi->fs->create_in || !s_asi->fs->write) return;
    if (s_asi->fs->mkdir) s_asi->fs->mkdir("C:/etc");
    void *h = s_asi->fs->create_in("C:/etc", "update.cfg", 0);
    if (!h) return;
    char line[64];
    u_cpy(line, sizeof(line), "version=");
    u_cat(line, sizeof(line), ver);
    u_cat(line, sizeof(line), "\n");
    s_asi->fs->write(h, line, (uint32_t)u_len(line));
    u_cpy(s_applied, sizeof(s_applied), ver);
}

/* ---- API ---- */
static void u_set_state(int st, const char *msg) {
    s_st.state = st;
    if (msg) u_cpy(s_st.message, sizeof(s_st.message), msg);
}

void update_init(asi_t *asi) {
    s_asi = asi;
    u_cpy(s_base, sizeof(s_base), ARCTIAN_UPDATE_URL);
    s_buf = (asi && asi->mem && asi->mem->alloc) ? (uint8_t *)asi->mem->alloc(UPD_BUF_CAP) : 0;
    s_buf_cap = s_buf ? (int)UPD_BUF_CAP : 0;
    u_manifest_reset();
    s_st.state = UPDATE_IDLE;
    s_st.progress = 0;
    s_st.cur = 0;
    s_st.total = 0;
    u_cpy(s_st.version, sizeof(s_st.version), "");
    u_cpy(s_st.message, sizeof(s_st.message), "Hazir");
    cfg_load();
}

void update_set_base_url(const char *url) { if (url && url[0]) u_cpy(s_base, sizeof(s_base), url); }
const char *update_base_url(void) { return s_base; }
const char *update_current_version(void) { return ARCTIAN_VERSION; }
const char *update_applied_version(void) { return s_applied; }
const update_status_t   *update_status(void)   { return &s_st; }
const update_manifest_t *update_manifest(void) { return &s_man; }
int update_has_update(void) { return s_st.state == UPDATE_AVAILABLE; }

void update_set_installer(int (*fn)(const void *, uint32_t, char *, int)) { s_install = fn; }
void update_set_notify(void (*fn)(void)) { s_notify = fn; }

int update_check(void) {
    if (!s_asi || !s_asi->net || !s_asi->net->http_get) {
        u_set_state(UPDATE_ERROR, "Ag servisi yok");
        return -1;
    }
    u_set_state(UPDATE_CHECKING, "Kontrol ediliyor...");
    char url[160];
    u_cpy(url, sizeof(url), s_base);
    u_cat(url, sizeof(url), "/v1/update");

    uint32_t len = 0;
    const char *body = s_asi->net->http_get(url, &len);
    if (!body || !len) {
        u_set_state(UPDATE_ERROR, "Sunucuya ulasilamadi");
        return -2;
    }
    int nf = u_parse(body, len);
    if (!s_man.version[0]) {
        u_set_state(UPDATE_ERROR, "Gecersiz manifest");
        return -3;
    }
    /* 0.7: imzasiz/bozuk manifesto surum yukseltmez.      */
    if (!s_man_verified) {
        u_set_state(UPDATE_ERROR, "Manifesto imzasi gecersiz");
        return -4;
    }
    u_cpy(s_st.version, sizeof(s_st.version), s_man.version);

    if (u_ver_cmp(s_man.version, s_applied) > 0) {
        char m[96];
        u_cpy(m, sizeof(m), "Yeni surum: ");
        u_cat(m, sizeof(m), s_man.version);
        if (nf > 0) {
            char t[8];
            u_cat(m, sizeof(m), "  (");
            u_itoa((uint32_t)nf, t, sizeof(t));
            u_cat(m, sizeof(m), t);
            u_cat(m, sizeof(m), " modul)");
        }
        u_set_state(UPDATE_AVAILABLE, m);
        return 1;
    }
    u_set_state(UPDATE_UPTODATE, "Sistem guncel");
    return 0;
}

int update_apply(void) {
    if (!s_asi || !s_asi->net || !s_asi->net->download) {
        u_set_state(UPDATE_ERROR, "Ag servisi yok");
        return -1;
    }
    if (!s_buf || !s_buf_cap) {
        u_set_state(UPDATE_ERROR, "Bellek yetersiz");
        return -1;
    }
    if (s_man.count <= 0) {
        u_set_state(UPDATE_ERROR, "Once guncelleme kontrol edilmeli");
        return -1;
    }
    u_set_state(UPDATE_DOWNLOADING, "Indiriliyor...");
    s_st.total = s_man.count;

    for (int i = 0; i < s_man.count; i++) {
        update_file_t *f = &s_man.files[i];
        s_st.cur = i + 1;
        s_st.progress = (int)((long)i * 100L / s_man.count);

        char m[96];
        u_cpy(m, sizeof(m), "Indiriliyor: ");
        u_cat(m, sizeof(m), f->id[0] ? f->id : "?");
        u_cpy(s_st.message, sizeof(s_st.message), m);

        if (!f->package[0]) continue;

        char url[200];
        u_cpy(url, sizeof(url), s_base);
        u_cat(url, sizeof(url), f->package);

        uint32_t len = 0;
        int rc = s_asi->net->download(url, s_buf, (uint32_t)s_buf_cap, &len);
        if (rc != 0 || !len) { u_set_state(UPDATE_ERROR, "Indirme hatasi"); return -2; }
        if (f->size && len != f->size) { u_set_state(UPDATE_ERROR, "Boyut uyusmuyor"); return -3; }
        if (f->crc && u_crc32(s_buf, len) != f->crc) {
            u_set_state(UPDATE_ERROR, "CRC dogrulama hatasi");
            return -4;
        }
        if (s_install) {
            char err[64];
            err[0] = 0;
            if (s_install(s_buf, len, err, sizeof(err)) != 0) {
                u_set_state(UPDATE_ERROR, err[0] ? err : "Kurulum hatasi");
                return -5;
            }
        }
    }

    s_st.progress = 100;
    s_st.cur = s_st.total;
    cfg_save(s_man.version);

    char m[96];
    u_cpy(m, sizeof(m), "Guncellendi: ");
    u_cat(m, sizeof(m), s_man.version);
    u_set_state(UPDATE_DONE, m);
    if (s_notify) s_notify();
    return 0;
}

/* ==================================================================== */
/* 0.7                                                                  */
/* ==================================================================== */

int update_eligible(uint32_t released, int delay_days, uint64_t now_unix) {
    if (delay_days <= 0) return 1;             /* gecikme yok: saat aranmaz */
    if (!now_unix) return 0;                   /* saat bilinmiyor */
    if (!released) return 0;
    uint64_t need = (uint64_t)released + (uint64_t)delay_days * 86400ull;
    return now_unix >= need ? 1 : 0;
}

static uint64_t u_now(void) {
    if (s_test_now) return s_test_now;
    if (s_asi && s_asi->time && s_asi->time->unix_now) {
        uint64_t v = s_asi->time->unix_now();
        /* RTC/NTP yoksa cekirdek 0 dondurur. */
        return v;
    }
    return 0;
}

int update_eligible_now(void) {
    int delay = s_delay_fn ? s_delay_fn() : 0;
    if (delay < 0) delay = 0;
    if (delay > 30) delay = 30;
    return update_eligible(s_man.released, delay, u_now());
}

int update_manifest_verified(void) { return s_man_verified; }

void update_set_settings(int (*auto_fn)(void), int (*delay_fn)(void)) {
    s_auto_fn = auto_fn;
    s_delay_fn = delay_fn;
}

void update_set_ready(int ready) { s_ready = ready ? 1 : 0; }
int  update_ready(void) { return s_ready; }

static int u_auto_on(void) {
    if (!s_auto_fn) return 1;                  /* varsayilan: acik */
    return s_auto_fn() ? 1 : 0;
}

int update_shutdown_step(void) {
    if (!u_auto_on()) return 0;                /* Kapali -> ag yok, yuva yok */
    if (!s_ready) return 0;                    /* indirme bitmedi: beklemeden kapan */
    if (!update_eligible_now()) return 0;      /* gecikme dolmadi */
    int rc = slot_switch_pending();
    if (rc == 0) {
        s_ready = 0;
        if (s_notify) s_notify();
        return 1;
    }
    return -1;                                 /* hata kapanisi durdurmaz */
}

int update_boot_step(void) {
    /* Kapanista is bitmediyse acilista tamamlanir; ag denetimi 5 sn ile
     * sinirlidir (burada bloklayici ag cagrisi yapilmaz). */
    return update_shutdown_step();
}

int update_install_now(void) {
    if (!s_ready) { u_set_state(UPDATE_ERROR, "Paket hazir degil"); return -1; }
    if (slot_switch_pending() != 0) { u_set_state(UPDATE_ERROR, "Yuva yazilamadi"); return -1; }
    s_ready = 0;
    u_set_state(UPDATE_DONE, "Kurulum icin yeniden baslatma bekleniyor");
    if (s_notify) s_notify();
    return 1;
}

void update_test_set_now(uint64_t unix_now) { s_test_now = unix_now; }

void update_test_reset(void) {
    u_manifest_reset();
    s_ready = 0;
    s_test_now = 0;
}

int update_test_parse(const char *text, uint32_t len) {
    return u_parse(text, len);
}
