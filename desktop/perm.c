/**
 * Arctian 0.7 - ozne, karar deposu ve kapilar
 *
 * Kutulu uygulama acilinca ozne kaydi olusur: kimlik (kaynak + ad),
 *       pencere basligi, guvenilirlik ve ozel dizin (C:/uygulamalar/<ad>/).
 * Karar deposu: (ozne, tur, kaynak) anahtari ile once / always / deny.
 *       always ve deny C:/etc/izinler.cfg dosyasina yazilir, acilista okunur.
 * Dosya kapisi: tek bogaz caps_fs_guard; oradan perm_policy() gecer.
 * Komut kapisi: wolf_exec ve "uygulama ac" cagrilari ayni depoya duser.
 *
 * Ozne etkinken bile politika yalnizca UYGULAMA KODU icinde uygulanir
 * (caps_dispatch_begin/end). Boylece ayni is parcaciginda calisan masaustu
 * kodu (dosya yoneticisi, terminal, ayarlar) ozne adina sorulmaz ve
 * "deny" kaydi masaustu islemlerini kilitlemez.
 */
#include "dsk.h"
#include "caps.h"

/* ------------------------------------------------------------------ */
/* Cekirdek ozne durumuna ASI uzerinden erisim (0.7)                    */
/*                                                                    */
/* desktop ve kernel_plus ayri modullerdir. Durum cekirdekte tutulur    */
/* (dosya kapisi ayni kaynagi gorsun diye); masaustu g_asi->sandbox     */
/* uzerinden erisir.                                                   */
/* ------------------------------------------------------------------ */
static asi_sandbox_t *p_sb(void) {
    return (g_asi && g_asi->sandbox) ? g_asi->sandbox : 0;
}

void caps_subject_begin(const char *id, const char *title,
                        const char *home, int trusted) {
    asi_sandbox_t *s = p_sb();
    if (s && s->subject_begin)
        s->subject_begin(id, title, home, trusted);
}

void caps_subject_end(void) {
    asi_sandbox_t *s = p_sb();
    if (s && s->subject_end) s->subject_end();
}

const char *caps_subject_id(void) {
    asi_sandbox_t *s = p_sb();
    return (s && s->subject_id) ? s->subject_id() : "";
}

const char *caps_subject_title(void) {
    asi_sandbox_t *s = p_sb();
    return (s && s->subject_title) ? s->subject_title() : "";
}

const char *caps_subject_home(void) {
    asi_sandbox_t *s = p_sb();
    return (s && s->subject_home) ? s->subject_home() : "";
}

int caps_subject_trusted(void) {
    asi_sandbox_t *s = p_sb();
    return (s && s->subject_trusted) ? s->subject_trusted() : 1;
}

int caps_subject_active(void) {
    asi_sandbox_t *s = p_sb();
    return (s && s->subject_active) ? s->subject_active() : 0;
}

void caps_dispatch_begin(void) {
    asi_sandbox_t *s = p_sb();
    if (s && s->dispatch_begin) s->dispatch_begin();
}

void caps_dispatch_end(void) {
    asi_sandbox_t *s = p_sb();
    if (s && s->dispatch_end) s->dispatch_end();
}

int caps_dispatch_active(void) {
    asi_sandbox_t *s = p_sb();
    return (s && s->dispatch_active) ? s->dispatch_active() : 0;
}

int caps_last_error(void) {
    asi_sandbox_t *s = p_sb();
    return (s && s->last_error) ? s->last_error() : 0;
}
/* ------------------------------------------------------------------ */
/* Yardimcilar                                                         */
/* ------------------------------------------------------------------ */

static int s_policy_busy;

static int p_len(const char *s) { int n = 0; if (s) while (s[n]) n++; return n; }

static void p_cpy(char *d, int cap, const char *s) {
    int i = 0;
    if (cap <= 0) return;
    if (!s) { d[0] = 0; return; }
    for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}

static int p_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static int p_starts(const char *s, const char *p) {
    if (!s || !p) return 0;
    for (; *p; p++, s++) if (*s != *p) return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Karar deposu                                                         */
/* ------------------------------------------------------------------ */

#define DEC_MAX   128
#define DEC_ID    48
#define DEC_SRC   96

#define IZIN_PATH    "C:/etc/izinler.cfg"

typedef struct {
    char     id[DEC_ID];
    char     src[DEC_SRC];
    uint8_t  type;      /* PERM_PATH / PERM_EXEC / PERM_NET */
    uint8_t  verd;      /* PERM_ONCE / PERM_ALWAYS / PERM_DENY */
} dec_t;

static dec_t s_dec[DEC_MAX];
static int   s_dec_n;
static int   s_dec_loaded;

static const char *type_word(int t) {
    if (t == PERM_T_EXEC) return "exec";
    if (t == PERM_T_NET)  return "net";
    return "path";
}

static int type_of(const char *w) {
    if (p_eq(w, "exec")) return PERM_T_EXEC;
    if (p_eq(w, "net"))  return PERM_T_NET;
    return PERM_T_PATH;
}

/* Yol normallestirme: \ -> /, tekrarlayan / tek olur, sondaki / kalkar.
 * ".." iceren yol reddedilir (0): plana gore sorulmaz, dogrudan inkar. */
int perm_norm(char *out, int cap, const char *in) {
    if (!out || cap <= 0) return 0;
    if (!in) { out[0] = 0; return 0; }

    /* ".." kontrolu (normallestirmeden once, her bicimde) */
    for (const char *p = in; *p; p++) {
        if (p[0] == '.' && p[1] == '.' &&
            (p == in || p[-1] == '/' || p[-1] == '\\') &&
            (p[2] == 0 || p[2] == '/' || p[2] == '\\'))
            return 0;
    }

    char tmp[192];
    int n = 0;
    for (int i = 0; in[i] && n < (int)sizeof(tmp) - 1; i++) {
        char c = in[i];
        if (c == '\\') c = '/';
        if (c == '/' && n > 0 && tmp[n - 1] == '/') continue;
        tmp[n++] = c;
    }
    tmp[n] = 0;
    while (n > 1 && tmp[n - 1] == '/') { n--; tmp[n] = 0; }
    p_cpy(out, cap, tmp);
    return 1;
}

static void dir_of(const char *p, char *out, int cap) {
    int n = p_len(p);
    while (n > 3 && p[n - 1] != '/') n--;
    if (n > 3) n--;
    if (n < 3) n = 3;
    int k = 0;
    for (int i = 0; i < n && k < cap - 1; i++) out[k++] = p[i];
    out[k] = 0;
}

static int path_prefix_eq(const char *rec, const char *q) {
    int n = p_len(rec);
    if (n <= 0) return 0;
    if (!p_starts(q, rec)) return 0;
    return q[n] == 0 || q[n] == '/';
}

static int dec_find(const char *id, int type, const char *src) {
    for (int i = 0; i < s_dec_n; i++) {
        if (s_dec[i].type != type) continue;
        if (!p_eq(s_dec[i].id, id)) continue;
        if (p_eq(s_dec[i].src, src)) return i;
        if (type == PERM_T_PATH && path_prefix_eq(s_dec[i].src, src)) return i;
    }
    return -1;
}

void perm_dec_load(void);
void perm_dec_save(void);

void perm_dec_clear(void) {
    s_dec_n = 0;
    s_dec_loaded = 1;
}

int perm_dec_count(void) { return s_dec_n; }

/* Belirli bir oznenin tum kararlarini sil (oz-test temizligi). */
void perm_dec_drop(const char *id) {
    if (!id || !id[0]) return;
    if (!s_dec_loaded) perm_dec_load();
    for (int i = s_dec_n - 1; i >= 0; i--) {
        if (!p_eq(s_dec[i].id, id)) continue;
        for (int k = i; k < s_dec_n - 1; k++) s_dec[k] = s_dec[k + 1];
        s_dec_n--;
    }
    perm_dec_save();
}

/* Karar oku. Yoksa PERM_ASK. */
int perm_dec_get(const char *id, int type, const char *src) {
    if (!id || !id[0]) return PERM_ASK;
    if (!s_dec_loaded) perm_dec_load();
    int i = dec_find(id, type, src);
    return i < 0 ? PERM_ASK : (int)s_dec[i].verd;
}

/* once karari tuket ("cagri bitince silinir").     */
void perm_dec_consume(const char *id, int type, const char *src) {
    int i = dec_find(id, type, src);
    if (i < 0) return;
    for (int k = i; k < s_dec_n - 1; k++) s_dec[k] = s_dec[k + 1];
    s_dec_n--;
    perm_dec_save();
}

/* Karar yaz. once bellekte; always/deny dosyaya da yazilir. Doluysa -1. */
int perm_dec_set(const char *id, int type, const char *src, int verd) {
    if (!id || !id[0] || !src) return -1;
    if (verd != PERM_ALWAYS && verd != PERM_DENY && verd != PERM_ONCE) return -1;
    if (!s_dec_loaded) perm_dec_load();
    int i = dec_find(id, type, src);
    if (i < 0) {
        if (s_dec_n >= DEC_MAX) return -1;
        i = s_dec_n++;
        p_cpy(s_dec[i].id, DEC_ID, id);
        p_cpy(s_dec[i].src, DEC_SRC, src);
        s_dec[i].type = (uint8_t)type;
    }
    s_dec[i].verd = (uint8_t)verd;
    if (verd == PERM_ALWAYS || verd == PERM_DENY) perm_dec_save();
    return 0;
}

/* Kayit bicimi: dpk:kronometre path C:/belgeler allow            */
void perm_dec_save(void) {
    int prev_busy;
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in || !g_asi->fs->write) return;
    prev_busy = s_policy_busy;
    s_policy_busy = 1;
    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/etc");
    void *h = g_asi->fs->create_in("C:/etc", "izinler.cfg", 0);
    if (!h) { s_policy_busy = prev_busy; return; }
    char line[DEC_ID + DEC_SRC + 24];
    for (int i = 0; i < s_dec_n; i++) {
        if (s_dec[i].verd != PERM_ALWAYS && s_dec[i].verd != PERM_DENY) continue;
        int n = 0;
        const char *p = s_dec[i].id;
        while (*p && n < (int)sizeof(line) - 2) line[n++] = *p++;
        line[n++] = ' ';
        p = type_word(s_dec[i].type);
        while (*p && n < (int)sizeof(line) - 2) line[n++] = *p++;
        line[n++] = ' ';
        p = s_dec[i].src;
        while (*p && n < (int)sizeof(line) - 4) line[n++] = *p++;
        line[n++] = ' ';
        p = (s_dec[i].verd == PERM_ALWAYS) ? "allow" : "deny";
        while (*p && n < (int)sizeof(line) - 2) line[n++] = *p++;
        line[n++] = '\n';
        line[n] = 0;
        g_asi->fs->write(h, line, (uint32_t)n);
    }
    s_policy_busy = prev_busy;
}

void perm_dec_load(void) {
    s_dec_n = 0;
    s_dec_loaded = 1;
    if (!g_asi || !g_asi->fs || !g_asi->fs->read_path) return;
    static char buf[6144];
    uint32_t len = g_asi->fs->read_path(IZIN_PATH, buf, sizeof(buf) - 1);
    if (!len) return;
    buf[len] = 0;

    uint32_t i = 0;
    while (i < len) {
        uint32_t ls = i;
        while (i < len && buf[i] != '\n' && buf[i] != '\r') i++;
        uint32_t le = i;
        while (i < len && (buf[i] == '\n' || buf[i] == '\r')) i++;
        if (le <= ls) continue;

        /* boluklara gore 4 alan: id tur kaynak karar */
        char id[DEC_ID], typ[8], verd[8], src[DEC_SRC];
        int f = 0, a = 0, b = 0;
        char *L = buf + ls;
        int Llen = (int)(le - ls);
        int cut[3] = { -1, -1, -1 };
        for (int k = 0; k < Llen && f < 3; k++)
            if (L[k] == ' ' || L[k] == '\t') {
                if (k + 1 < Llen && L[k + 1] != ' ' && L[k + 1] != '\t') cut[f++] = k;
            }
        if (cut[0] < 0 || cut[1] < 0 || cut[2] < 0) continue;

        a = 0; b = cut[0]; if (b > DEC_ID - 1) b = DEC_ID - 1;
        for (int k = a; k < b; k++) id[k - a] = L[k];
        id[b - a] = 0;

        a = cut[0] + 1; b = cut[1]; if (b - a > 7) b = a + 7;
        for (int k = a; k < b; k++) typ[k - a] = L[k];
        typ[b - a] = 0;

        a = cut[2] + 1; b = Llen; if (b - a > 7) b = a + 7;
        for (int k = a; k < b; k++) verd[k - a] = L[k];
        verd[b - a] = 0;

        a = cut[1] + 1; b = cut[2];
        int sl = b - a; if (sl > DEC_SRC - 1) sl = DEC_SRC - 1;
        for (int k = 0; k < sl; k++) src[k] = L[a + k];
        src[sl] = 0;

        int v = p_eq(verd, "allow") ? PERM_ALWAYS : (p_eq(verd, "deny") ? PERM_DENY : -1);
        if (v < 0) continue;
        if (s_dec_n >= DEC_MAX) break;
        int e = s_dec_n++;
        p_cpy(s_dec[e].id, DEC_ID, id);
        p_cpy(s_dec[e].src, DEC_SRC, src);
        s_dec[e].type = (uint8_t)type_of(typ);
        s_dec[e].verd = (uint8_t)v;
    }
}

/* ------------------------------------------------------------------ */
/* Ozne                                                                */
/* ------------------------------------------------------------------ */

static char s_id[48];
static char s_title[48];
static char s_home[64];
static char s_pkg[80];
static int  s_trusted;
static int  s_subj_open;

/* kimlikten ozel dizin: dpk:kronometre -> C:/uygulamalar/kronometre/ */
static void home_of(const char *id, char *out, int cap) {
    const char *ad = id;
    for (const char *p = id; *p; p++) if (*p == ':') ad = p + 1;
    p_cpy(out, cap, "C:/uygulamalar/");
    int n = p_len(out);
    for (int i = 0; ad[i] && n < cap - 1; i++) out[n++] = ad[i];
    out[n] = 0;
}

/* Kutulu (guvenilir olmayan) uygulama icin ozne ac. 0 = basarili. */
int subj_open(const char *id, const char *title, int trusted, const char *pkg) {
    if (!id || !id[0]) return -1;
    p_cpy(s_id, sizeof(s_id), id);
    p_cpy(s_title, sizeof(s_title), title ? title : id);
    home_of(id, s_home, sizeof(s_home));
    p_cpy(s_pkg, sizeof(s_pkg), pkg ? pkg : "");
    s_trusted = trusted ? 1 : 0;

    if (g_asi && g_asi->fs && g_asi->fs->mkdir) {
        /* Yalnizca yoksa olustur. */
        if (!g_asi->fs->find_in)
            g_asi->fs->mkdir("C:/uygulamalar");
        else if (!g_asi->fs->find_in("C:/", "uygulamalar/"))
            g_asi->fs->mkdir("C:/uygulamalar");
        char nm[64];
        const char *ad = s_home + 15;
        p_cpy(nm, sizeof(nm), ad);
        int q = p_len(nm);
        if (q > 0 && q < (int)sizeof(nm) - 1 && nm[q - 1] != '/') {
            nm[q++] = '/';
            nm[q] = 0;
        }
        if (!g_asi->fs->find_in)
            g_asi->fs->mkdir(s_home);
        else if (!g_asi->fs->find_in("C:/uygulamalar", nm))
            g_asi->fs->mkdir(s_home);
    }
    caps_subject_begin(s_id, s_title, s_home, s_trusted);
    s_subj_open = 1;
    return 0;
}

void subj_close(void) {
    caps_subject_end();
    s_subj_open = 0;
    s_id[0] = 0;
    s_title[0] = 0;
    s_home[0] = 0;
    s_pkg[0] = 0;
    s_trusted = 0;
}

int subj_is_open(void) { return s_subj_open; }
const char *subj_home(void) { return s_home; }
const char *subj_pkg(void) { return s_pkg; }
const char *subj_id(void) { return s_id; }
int subj_trusted(void) { return s_trusted; }

static int under(const char *path, const char *dir) {
    int n = p_len(dir);
    if (n <= 0) return 0;
    if (!p_starts(path, dir)) return 0;
    /* sinir: C:/uygulamalar/kapi, C:/uygulamalar/kapi2 ile ayni degil */
    return path[n] == 0 || path[n] == '/';
}

/* (kendi paket dosyasi denetimi perm_policy icinde pkg_of ile yapilir) */

/* ------------------------------------------------------------------ */
/* Davranis (dispatch) isareti + acik uygulama govdeleri                */
/* ------------------------------------------------------------------ */

/* Uygulama kodu yalnizca iki yerde calisir: pencere girisinde (entry) ve
 * olaylarinda (__wrap_lv_obj_send_event). Politika sadece bu anlarda
 * gecer; masaustu kodu ozne adina sorulmaz. */

#define WATCH_MAX 8
typedef struct {
    lv_obj_t *body;
    char id[48];
    char title[48];
    char home[64];
    char pkg[80];
    int  trusted;
} watch_t;

static watch_t s_watch[WATCH_MAX];
static int     s_watch_n;
static int     s_in;            /* ic ice dispatch sayaci */

void perm_dispatch_begin(void) { s_in++; caps_dispatch_begin(); }
void perm_dispatch_end(void) {
    if (s_in <= 0) return;
    s_in--;
    caps_dispatch_end();
}

/* Pencere acilirken: kayit + ev dizini + ozne. */
int perm_app_open(lv_obj_t *body, const char *id, const char *title,
                  int trusted, const char *pkg) {
    if (!body) return -1;
    if (subj_open(id, title, trusted, pkg) != 0) return -1;

    watch_t *w = 0;
    for (int i = 0; i < s_watch_n; i++) if (s_watch[i].body == body) w = &s_watch[i];
    if (!w) {
        if (s_watch_n >= WATCH_MAX) return -1;
        w = &s_watch[s_watch_n++];
        w->body = body;
    }
    p_cpy(w->id, sizeof(w->id), s_id);
    p_cpy(w->title, sizeof(w->title), s_title);
    p_cpy(w->home, sizeof(w->home), s_home);
    p_cpy(w->pkg, sizeof(w->pkg), s_pkg);
    w->trusted = s_trusted;
    return 0;
}

void perm_app_close(lv_obj_t *body) {
    char id[48];
    id[0] = 0;
    for (int i = 0; i < s_watch_n; i++) {
        if (s_watch[i].body != body) continue;
        p_cpy(id, sizeof(id), s_watch[i].id);
        for (int k = i; k < s_watch_n - 1; k++) s_watch[k] = s_watch[k + 1];
        s_watch_n--;
        break;
    }
    /* Etkin ozne bu uygulamaya aitse kapat. */
    if (id[0] && p_eq(caps_subject_id(), id)) subj_close();
}

/* Govede silinince kayit da kapanir (launch.c LV_EVENT_DELETE hedefi). */
void perm_body_del_cb(lv_event_t *e) {
    perm_app_close((lv_obj_t *)lv_event_get_user_data(e));
}

/* olay hedefi hangi acik uygulamaya ait? (yoksa -1) */
static int watch_of(lv_obj_t *o) {
    for (int i = 0; i < s_watch_n; i++) {
        lv_obj_t *p = o;
        while (p) {
            if (p == s_watch[i].body) return i;
            p = lv_obj_get_parent(p);
        }
    }
    return -1;
}

/* Uygulama olayina giris: ozne kur + dispatch isaretle. 1 = uygulamaya ait. */
int perm_begin_obj(lv_obj_t *o) {
    int w = watch_of(o);
    if (w < 0) return 0;
    if (s_in == 0)
        caps_subject_begin(s_watch[w].id, s_watch[w].title,
                           s_watch[w].home, s_watch[w].trusted);
    s_in++;
    caps_dispatch_begin();
    return 1;
}

void perm_end_obj(void) {
    if (s_in <= 0) return;
    s_in--;
    caps_dispatch_end();
}

/* (__wrap_lv_obj_send_event icin) */
int perm_obj_in_app(lv_obj_t *o) { return watch_of(o) >= 0; }

/* ------------------------------------------------------------------ */
/* Dosya kapisi                                                         */
/* ------------------------------------------------------------------ */

/* "kendi paket dosyasini okumak serbesttir"              */
static const char *pkg_of(const char *id) {
    for (int i = 0; i < s_watch_n; i++)
        if (p_eq(s_watch[i].id, id)) return s_watch[i].pkg;
    return (s_id[0] && p_eq(s_id, id)) ? s_pkg : "";
}

/* caps_fs_guard bu fonksiyonu cagirir: 0 = izin, != 0 = reddet. */

static int perm_policy_inner(const char *path, int is_write) {
    const char *id = caps_subject_id();
    if (!id || !id[0]) return 0;
    if (caps_subject_trusted()) return 0;                 /* guvenilen arac */
    if (!caps_dispatch_active()) return 0;                /* masaustu kodu */

    if (!path) return -1;
    char n[192];
    if (!perm_norm(n, sizeof(n), path)) return -1;        /* ".." -> inkar */

    if (under(n, caps_subject_home())) return 0;          /* kendi dizini */
    {
        const char *pk = pkg_of(id);
        if (pk && pk[0] && p_eq(n, pk)) return 0;          /* kendi paketi */
    }

    int v = perm_dec_get(id, PERM_T_PATH, n);
    if (v == PERM_ALWAYS) return 0;
    if (v == PERM_DENY)   return -1;
    if (v == PERM_ONCE)   { perm_dec_consume(id, PERM_T_PATH, n); return 0; }

    /* Kullanicinin kendisi karar verir.     */
    v = consent_ask(PERM_T_PATH, n);
    if (v == PERM_ALWAYS) {
        char dd[192];
        dir_of(n, dd, sizeof(dd));
        if (p_eq(dd, "C:/")) perm_dec_set(id, PERM_T_PATH, n, PERM_ALWAYS);
        else                  perm_dec_set(id, PERM_T_PATH, dd, PERM_ALWAYS);
        return 0;
    }
    if (v == PERM_ONCE)   { return 0; }                   /* bu cagri icin */
    {
        char dd[192];
        dir_of(n, dd, sizeof(dd));
        if (p_eq(dd, "C:/")) perm_dec_set(id, PERM_T_PATH, n, PERM_DENY);
        else                  perm_dec_set(id, PERM_T_PATH, dd, PERM_DENY);
    }
    return -1;
}

int perm_policy(const char *path, int is_write) {
    if (s_policy_busy) return 0;
    s_policy_busy = 1;
    int r = perm_policy_inner(path, is_write);
    s_policy_busy = 0;
    return r;
}

/* Dosya yoneticisi / "ac ile" kullanicinin sectigi yol:
 * o yol icin `once` kaydi acilir; uygulamanin sonraki istegi sorulmaz. */
void consent_grant_user_path(const char *id, const char *path) {
    if (!id || !path) return;
    char n[192];
    if (!perm_norm(n, sizeof(n), path)) return;
    perm_dec_set(id, PERM_T_PATH, n, PERM_ONCE);
}

/* ------------------------------------------------------------------ */
/* Komut kapisi                                                         */
/* ------------------------------------------------------------------ */

int perm_exec(const char *cmd) {
    const char *id = caps_subject_id();
    if (!id || !id[0]) return 0;
    if (caps_subject_trusted()) return 0;     /* terminal kullaniciya sorar */
    if (!caps_dispatch_active()) return 0;
    if (!cmd || !cmd[0]) return -1;

    int v = perm_dec_get(id, PERM_T_EXEC, cmd);
    if (v == PERM_ALWAYS) return 0;
    if (v == PERM_DENY)   return -1;
    if (v == PERM_ONCE)   { perm_dec_consume(id, PERM_T_EXEC, cmd); return 0; }

    v = consent_ask(PERM_T_EXEC, cmd);
    if (v == PERM_ALWAYS) { perm_dec_set(id, PERM_T_EXEC, cmd, PERM_ALWAYS); return 0; }
    if (v == PERM_ONCE)   return 0;
    perm_dec_set(id, PERM_T_EXEC, cmd, PERM_DENY);
    return -1;
}

/* Tek ag izni, uygulama basina.                          */
int perm_net(void) {
    const char *id = caps_subject_id();
    if (!id || !id[0]) return 0;
    if (caps_subject_trusted()) return 0;
    if (!caps_dispatch_active()) return 0;

    int v = perm_dec_get(id, PERM_T_NET, "*");
    if (v == PERM_ALWAYS) return 0;
    if (v == PERM_DENY)   return -1;
    if (v == PERM_ONCE)   { perm_dec_consume(id, PERM_T_NET, "*"); return 0; }

    v = consent_ask(PERM_T_NET, "*");
    if (v == PERM_ALWAYS) { perm_dec_set(id, PERM_T_NET, "*", PERM_ALWAYS); return 0; }
    if (v == PERM_ONCE)   return 0;
    perm_dec_set(id, PERM_T_NET, "*", PERM_DENY);
    return -1;
}

/* Kapilari kur: dosya kapisina politikayi ver (bir kez). */
void perm_init(void) {
    asi_sandbox_t *s = (g_asi && g_asi->sandbox) ? g_asi->sandbox : 0;
    if (s && s->set_policy) s->set_policy(perm_policy);
    perm_dec_load();
}