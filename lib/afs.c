/**
 * ArctianFS (AFS) v2 - diske kalici basit dosya sistemi (tek seviye)
 *
 * Dinamik kapasite: alan ve dosya sayisi secili diskin gercek boyutundan
 * turetilir. Dosya tablosu header icindedir; header boyu dosya sayisina gore
 * buyur. Boylece 250 GB'lik bir SSD'de yuzlerce uygulama saklanabilir.
 *
 * Yerlesim: [header N sektor][veri alani...]
 *
 * Header alanlari (little-endian, packed):
 *   0  magic        "!AFS"
 *   4  version      AFS_VERSION
 *   8  max_files    dosya tablosu kapasitesi (32..512)
 *   12 count        kullanilan dosya sayisi
 *   16 data_next    veri alaninda siradaki bos LBA
 *   20 hdr_sectors  header'in sektor sayisi
 *   24 capacity     kullanilabilir son LBA (haric)
 *   28 flags        rezerve
 *   32 files[max_files]
 */
#include "afs.h"
#include "storage.h"
#include "diskcrypt_hdr.h"
#include <stddef.h>

#define AFS_MAGIC          0x53464121u  /* "!AFS" */
#define AFS_MAX_FILES_HARD  512u
#define AFS_HEADER_BASE     32u         /* magic..flags */
#define AFS_ENTRY_SIZE      (AFS_NAME_MAX + 28u)  /* ad + lba + size + used + uid + gid + mode + pad */
#define AFS_HDR_MAX_SEC \
    ((AFS_HEADER_BASE + AFS_MAX_FILES_HARD * AFS_ENTRY_SIZE + 511u) / 512u)
#define AFS_CLUSTER_SEC     4096u       /* dosya basina ortalama rezerv (max_files hesabi) */
#define AFS_RESERVE_SEC     2048u       /* diskin sonunda birakilan emniyet alani */

typedef struct __attribute__((packed)) {
    char     name[AFS_NAME_MAX];
    uint32_t lba;
    uint32_t size;
    uint32_t used;
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    uint32_t pad;
} afs_file_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t max_files;
    uint32_t count;
    uint32_t data_next;
    uint32_t hdr_sectors;
    uint32_t capacity;
    uint32_t flags;
    afs_file_t files[AFS_MAX_FILES_HARD];
} afs_hdr_t;

static afs_hdr_t H;
static int mounted = 0;
static int s_locked = 0;

void afs_set_locked(int v) { s_locked = v ? 1 : 0; }
int  afs_is_locked(void) { return s_locked; }

int afs_disk_is_encrypted(void) {
    uint32_t hdr[128];
    if (storage_read(DISKCRYPT_HDR_LBA, 1, hdr) != 0) return 0;
    return hdr[0] == DISKCRYPT_MAGIC && hdr[1] == DISKCRYPT_VERSION;
}

static uint32_t s_uid = 0;
static uint32_t s_gid = 0;

void afs_set_subject(int uid, int gid) {
    s_uid = (uint32_t)uid;
    s_gid = (uint32_t)gid;
}

void afs_get_subject(int *uid, int *gid) {
    if (uid) *uid = (int)s_uid;
    if (gid) *gid = (int)s_gid;
}

#define AFS_R 4u
#define AFS_W 2u
#define AFS_X 1u

static int perm_ok(uint32_t fuid, uint32_t fgid, uint32_t mode, uint32_t want) {
    if (s_uid == 0) return 1;
    uint32_t bits;
    if (s_uid == fuid) bits = (mode >> 6) & 7u;
    else if (s_gid == fgid) bits = (mode >> 3) & 7u;
    else bits = mode & 7u;
    return (bits & want) == want;
}

static void set_owner(afs_file_t *f, int is_dir) {
    f->uid = s_uid;
    f->gid = s_gid;
    f->mode = is_dir ? 0755u : 0644u;
    f->pad = 0;
}

static void afs_ensure_tree(void);

static int seq(const char *a, const char *b) {
    int i = 0; for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}
static void cpy(char *d, const char *s) {
    int i = 0; for (; s[i] && i < (int)AFS_NAME_MAX - 1; i++) d[i] = s[i]; d[i] = '\0';
}

static int slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }

static int prefix_of(const char *s, const char *p) {
    for (int i = 0; p[i]; i++) if (s[i] != p[i]) return 0;
    return 1;
}

/* Disk boyutundan kullanilabilir son LBA'yi hesaplar. Dis sect. */
static uint32_t compute_capacity(void) {
    uint64_t secs = storage_sector_count();
    if (secs == 0) {              /* storage henuz hazir degilse tembel baslat */
        storage_init();
        secs = storage_sector_count();
    }
    uint64_t cap;
    if (secs == 0) {
        return AFS_BASE_LBA + 65536u;   /* bilinmeyen disk: guvenli varsayilan */
    }
    cap = secs > AFS_RESERVE_SEC ? secs - AFS_RESERVE_SEC : secs;
    if (cap > 0xFFFFFFFEull) cap = 0xFFFFFFFEull;
    if (cap < AFS_BASE_LBA + AFS_CLUSTER_SEC) cap = AFS_BASE_LBA + AFS_CLUSTER_SEC;
    return (uint32_t)cap;
}

static void hdr_sync_size(void) {
    uint32_t bytes = AFS_HEADER_BASE + H.max_files * AFS_ENTRY_SIZE;
    H.hdr_sectors = (bytes + 511u) / 512u;
}

/* ---- Butunluk katmani: CRC32 + metadata gunlugu (journal) ----
 * Header, dosya sisteminin tek commit noktasidir. Yazma sirasinda reset/guc
 * kesintisi header'i yarim birakabilir. Bunu onlemek icin once niyet (yeni
 * header goruntusu) diskin sonundaki gunluk bolgesine yazilir ve commit
 * edilir; sonra header'a uygulanir. Acilista yarim kalan commit gunlukten
 * geri oynatilir (replay). Boylece FS daima son tutarli duruma doner.
 *
 * Journal konumu diskin sonuna yakin sabittir; yeni formatlarda capacity
 * AFS_JOURNAL_SEC kadar kirpilir, boylece gunluk sifreli bolgenin icinde
 * kalir. Eski disklerde gunluk yalnizca veriyle cakismadigi surece kullanilir. */

#define AFS_JOURNAL_MAGIC  0x4A464121u   /* "!AFJ" */
#define AFS_JOURNAL_SEC    128u          /* gunluk icin ayrilan sektor sayisi */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;
    uint32_t hdr_sectors;
    uint32_t crc;
    uint32_t state;      /* 1 = gecerli (commit edilmis) */
    uint32_t reserved;
} afs_jrec_t;

static uint32_t s_jseq = 0;

static uint32_t crc32_buf(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
    return ~c;
}

static uint32_t hdr_bytes(void) {
    return AFS_HEADER_BASE + H.max_files * AFS_ENTRY_SIZE;
}

/* Header'in kendi CRC'si (flags alani sifirlanarak hesaplanir). */
static uint32_t hdr_crc(void) {
    uint32_t saved = H.flags;
    H.flags = 0;
    uint32_t c = crc32_buf(&H, hdr_bytes());
    H.flags = saved;
    return c;
}

/* Gunluk bolgesinin LBA'si; 0 = gunluk kullanilamaz (dogrudan yazim). */
static uint32_t journal_lba(void) {
    uint64_t secs = storage_sector_count();
    uint64_t base = (uint64_t)AFS_BASE_LBA + AFS_RESERVE_SEC + AFS_JOURNAL_SEC;
    if (secs < base) return 0;
    uint64_t j = secs - AFS_RESERVE_SEC - AFS_JOURNAL_SEC;
    if (j <= (uint64_t)AFS_BASE_LBA) return 0;
    if (j + 1u + AFS_HDR_MAX_SEC > secs) return 0;
    return (uint32_t)j;
}

static void journal_clear(uint32_t jl) {
    uint8_t sec[512];
    afs_jrec_t *rec = (afs_jrec_t *)sec;
    if (storage_read(jl, 1, sec) != 0) return;
    if (rec->magic != AFS_JOURNAL_MAGIC) return;
    rec->state = 0;
    storage_write(jl, 1, sec);
    storage_flush();
}

/* Header'i diske yazar; once gunluge niyet yazip commit eder (crash-safe). */
static int hdr_commit(void) {
    H.version = AFS_VERSION;
    hdr_sync_size();
    H.flags = 0;
    H.flags = hdr_crc();

    uint32_t jl = journal_lba();
    if (jl == 0 || H.data_next > jl || H.hdr_sectors + 1u > AFS_JOURNAL_SEC) {
        /* Gunluk yok ya da veriyle cakisir: dogrudan yaz (geriye uyumlu). */
        return storage_write(AFS_BASE_LBA, H.hdr_sectors, &H);
    }

    /* 1) Yeni header'i gunluk yukune yaz. */
    if (storage_write(jl + 1u, H.hdr_sectors, &H) != 0) return -1;
    storage_flush();
    /* 2) Gunluk kaydini commit et (state=1). Kayit tek sektore sigar; tampon
     *    512 bayt olmali cunku storage katmani daima 1 tam sektor yazar. */
    uint8_t sec[512];
    afs_jrec_t *rec = (afs_jrec_t *)sec;
    for (int i = 0; i < 512; i++) sec[i] = 0;
    rec->magic = AFS_JOURNAL_MAGIC;
    rec->seq = ++s_jseq;
    rec->hdr_sectors = H.hdr_sectors;
    rec->crc = crc32_buf(&H, hdr_bytes());
    rec->state = 1;
    if (storage_write(jl, 1, sec) != 0) return -1;
    storage_flush();
    /* 3) Header'a uygula. */
    if (storage_write(AFS_BASE_LBA, H.hdr_sectors, &H) != 0) return -1;
    storage_flush();
    /* 4) Gunlugu temizle (state=0). */
    rec->state = 0;
    storage_write(jl, 1, sec);
    storage_flush();
    return 0;
}

/* Yarim kalan commit'i geri oynat (acilista cagrilir). */
static void journal_replay(void) {
    uint32_t jl = journal_lba();
    if (jl == 0) return;
    uint8_t sec[512];
    afs_jrec_t *rec = (afs_jrec_t *)sec;
    if (storage_read(jl, 1, sec) != 0) return;
    if (rec->magic != AFS_JOURNAL_MAGIC || rec->state != 1 ||
        rec->hdr_sectors == 0 || rec->hdr_sectors > AFS_HDR_MAX_SEC) return;
    if (storage_read(jl + 1u, rec->hdr_sectors, &H) != 0) return;
    if (H.magic != AFS_MAGIC || H.version != AFS_VERSION ||
        H.max_files < 32u || H.max_files > AFS_MAX_FILES_HARD) { journal_clear(jl); return; }
    if (crc32_buf(&H, hdr_bytes()) != rec->crc) { journal_clear(jl); return; }
    storage_write(AFS_BASE_LBA, rec->hdr_sectors, &H);
    storage_flush();
    journal_clear(jl);
}

void afs_format(void) {
    H.magic = AFS_MAGIC;
    H.version = AFS_VERSION;
    H.capacity = compute_capacity();
    /* Gunluk icin sonda AFS_JOURNAL_SEC sektor birak (sifreli bolgede kalir). */
    if (H.capacity > AFS_BASE_LBA + AFS_JOURNAL_SEC)
        H.capacity -= AFS_JOURNAL_SEC;
    H.flags = 0;

    uint32_t room = H.capacity - AFS_BASE_LBA;
    uint32_t mf = room / AFS_CLUSTER_SEC;
    if (mf < 32u) mf = 32u;
    if (mf > AFS_MAX_FILES_HARD) mf = AFS_MAX_FILES_HARD;
    H.max_files = mf;
    H.count = 0;
    hdr_sync_size();
    H.data_next = AFS_BASE_LBA + H.hdr_sectors;

    for (uint32_t i = 0; i < AFS_MAX_FILES_HARD; i++) {
        H.files[i].used = 0;
        H.files[i].size = 0;
        H.files[i].lba = 0;
        H.files[i].uid = 0;
        H.files[i].gid = 0;
        H.files[i].mode = 0;
        H.files[i].pad = 0;
        H.files[i].name[0] = 0;
    }
    mounted = 1;
    hdr_commit();
    afs_ensure_tree();
}

void afs_mount(void) {
    uint32_t first[128];   /* 512-byte ilk sektor */
    if (s_locked) return;
    if (storage_sector_count() == 0) storage_init();
    journal_replay();      /* yarim kalan commit'i tamamla */
    if (storage_read(AFS_BASE_LBA, 1, first) != 0 ||
        first[0] != AFS_MAGIC || first[1] != AFS_VERSION) {
        if (afs_disk_is_encrypted()) return;
        afs_format();
        return;
    }
    H.magic = first[0];
    H.version = first[1];
    H.max_files = first[2];
    H.count = first[3];
    H.data_next = first[4];
    H.hdr_sectors = first[5];
    H.capacity = first[6];
    H.flags = first[7];

    if (H.max_files < 32u || H.max_files > AFS_MAX_FILES_HARD ||
        H.hdr_sectors == 0 || H.hdr_sectors > AFS_HDR_MAX_SEC) {
        if (afs_disk_is_encrypted()) return;
        afs_format();
        return;
    }
    if (storage_read(AFS_BASE_LBA, H.hdr_sectors, &H) != 0) {
        if (afs_disk_is_encrypted()) return;
        afs_format();
        return;
    }
    /* Header CRC dogrula (eski disklerde flags=0 -> dogrulama atlanir). */
    if (H.flags != 0 && hdr_crc() != H.flags) {
        if (afs_disk_is_encrypted()) return;
        afs_format();
        return;
    }
    mounted = 1;
    afs_ensure_tree();
}

int afs_count(void) { if (!mounted) afs_mount(); return (int)H.count; }

uint32_t afs_data_next(void) { if (!mounted) afs_mount(); return H.data_next; }

static int find_idx(const char *name) {
    for (uint32_t i = 0; i < H.max_files; i++)
        if (H.files[i].used && seq(H.files[i].name, name)) return (int)i;
    return -1;
}

void *afs_create_handle(const char *name) {
    if (!mounted) afs_mount();
    int ex = find_idx(name);
    if (ex >= 0) return (void *)(intptr_t)(ex + 1);
    for (uint32_t i = 0; i < H.max_files; i++) {
        if (!H.files[i].used) {
            H.files[i].used = 1;
            H.files[i].size = 0;
            H.files[i].lba = H.data_next;
            set_owner(&H.files[i], afs_is_dir(name));
            cpy(H.files[i].name, name);
            H.count++;
            hdr_commit();
            return (void *)(intptr_t)(i + 1);
        }
    }
    return 0;
}

void *afs_find_handle(const char *name) {
    if (!mounted) afs_mount();
    int i = find_idx(name);
    return i < 0 ? 0 : (void *)(intptr_t)(i + 1);
}

/* ---- Yeniden adlandirma / tasima (0.6) ----
 * Yerel rename: veri kopyalanmaz, yalnizca ad degisir. Klasorlerde alt
 * ogelerin on eki de guncellenir. Donus: 0 = tamam, -1 = kaynak yok/izin,
 * -2 = hedef zaten var, -3 = ad cok uzun. */
int afs_rename(const char *from, const char *to) {
    if (!mounted) afs_mount();
    if (!from || !to || !from[0] || !to[0]) return -1;

    /* Klasorler depoda "/" ile biten adla durur: "C:/a" -> "C:/a/".
     * Cagiran taraf sonda egik cizgi vermeyebilir; iki bicimi de dene (0.6). */
    char fbuf[AFS_NAME_MAX], tbuf[AFS_NAME_MAX];
    cpy(fbuf, from);
    cpy(tbuf, to);
    int fi = find_idx(fbuf);
    if (fi < 0) {
        int l = slen(fbuf);
        if (l > 0 && l < (int)AFS_NAME_MAX - 1 && fbuf[l - 1] != '/') {
            fbuf[l] = '/'; fbuf[l + 1] = '\0';
            fi = find_idx(fbuf);
        }
    }
    if (fi < 0) return -1;

    int isd = afs_is_dir(fbuf);
    if (isd) {
        int l = slen(tbuf);
        if (l > 0 && l < (int)AFS_NAME_MAX - 1 && tbuf[l - 1] != '/') {
            tbuf[l] = '/'; tbuf[l + 1] = '\0';
        }
    }
    if (slen(tbuf) >= (int)AFS_NAME_MAX) return -3;
    if (find_idx(tbuf) >= 0) return -2;

    if (!perm_ok(H.files[fi].uid, H.files[fi].gid, H.files[fi].mode, AFS_W)) return -1;

    int fl = slen(fbuf);
    cpy(H.files[fi].name, tbuf);

    /* Klasor: "eski/..." -> "yeni/..." */
    if (isd) {
        for (uint32_t j = 0; j < H.max_files; j++) {
            if ((int)j == fi || !H.files[j].used) continue;
            if (!prefix_of(H.files[j].name, fbuf)) continue;
            char nb[AFS_NAME_MAX];
            int n = 0;
            for (int k = 0; tbuf[k] && n < (int)AFS_NAME_MAX - 1; k++) nb[n++] = tbuf[k];
            for (int k = fl; H.files[j].name[k] && n < (int)AFS_NAME_MAX - 1; k++)
                nb[n++] = H.files[j].name[k];
            nb[n] = '\0';
            cpy(H.files[j].name, nb);
        }
    }
    hdr_commit();
    return 0;
}

int afs_write_handle(void *h, const void *data, uint32_t size) {
    int i = (int)(intptr_t)h - 1;
    if (i < 0 || i >= (int)H.max_files || !H.files[i].used) return -1;
    if (!perm_ok(H.files[i].uid, H.files[i].gid, H.files[i].mode, AFS_W)) return -1;
    uint32_t secs = (size + 511) / 512;
    if (secs == 0) secs = 1;
    if (H.files[i].lba + secs > H.capacity) return -1;
    if (storage_write(H.files[i].lba, secs, data) != 0) return -1;
    H.files[i].size = size;
    if (H.files[i].lba + secs > H.data_next) H.data_next = H.files[i].lba + secs;
    hdr_commit();
    return 0;
}

uint32_t afs_read_handle(void *h, void *buf, uint32_t size) {
    int i = (int)(intptr_t)h - 1;
    if (i < 0 || i >= (int)H.max_files || !H.files[i].used) return 0;
    if (!perm_ok(H.files[i].uid, H.files[i].gid, H.files[i].mode, AFS_R)) return 0;
    uint32_t n = size < H.files[i].size ? size : H.files[i].size;
    uint32_t secs = (n + 511) / 512;
    if (secs && storage_read(H.files[i].lba, secs, buf) != 0) return 0;
    return n;
}

int afs_remove(const char *name) {
    if (!mounted) afs_mount();
    int i = find_idx(name);
    if (i < 0) return -1;
    if (!perm_ok(H.files[i].uid, H.files[i].gid, H.files[i].mode, AFS_W)) return -1;
    H.files[i].used = 0;
    H.files[i].size = 0;
    H.files[i].name[0] = 0;
    if (H.count) H.count--;
    hdr_commit();
    return 0;
}

const char *afs_name(int idx) {
    if (!mounted) afs_mount();
    if (idx < 0 || idx >= (int)H.max_files || !H.files[idx].used) return 0;
    return H.files[idx].name;
}

uint32_t afs_size(int idx) {
    if (!mounted) afs_mount();
    if (idx < 0 || idx >= (int)H.max_files || !H.files[idx].used) return 0;
    return H.files[idx].size;
}

int afs_stat(const char *path, asi_stat_t *out) {
    if (!mounted) afs_mount();
    int i = find_idx(path);
    if (i < 0 || !out) return -1;
    out->uid = H.files[i].uid;
    out->gid = H.files[i].gid;
    out->mode = H.files[i].mode;
    out->is_dir = afs_is_dir(H.files[i].name) ? 1u : 0u;
    out->size = H.files[i].size;
    return 0;
}

int afs_stat_handle(void *h, asi_stat_t *out) {
    int i = (int)(intptr_t)h - 1;
    if (i < 0 || i >= (int)H.max_files || !H.files[i].used || !out) return -1;
    out->uid = H.files[i].uid;
    out->gid = H.files[i].gid;
    out->mode = H.files[i].mode;
    out->is_dir = afs_is_dir(H.files[i].name) ? 1u : 0u;
    out->size = H.files[i].size;
    return 0;
}

int afs_chmod(const char *path, uint32_t mode) {
    if (!mounted) afs_mount();
    int i = find_idx(path);
    if (i < 0) return -1;
    if (s_uid != 0 && s_uid != H.files[i].uid) return -1;
    H.files[i].mode = mode & 07777u;
    hdr_commit();
    return 0;
}

int afs_chown(const char *path, int uid, int gid) {
    if (!mounted) afs_mount();
    int i = find_idx(path);
    if (i < 0) return -1;
    if (s_uid != 0) return -1;
    H.files[i].uid = (uint32_t)uid;
    H.files[i].gid = (uint32_t)gid;
    hdr_commit();
    return 0;
}

int afs_perm_selftest(void) {
    if (!mounted) afs_mount();
    afs_set_subject(0, 0);
    afs_remove("afs_perm_test");
    void *h = afs_create_handle("afs_perm_test");
    if (!h) { afs_set_subject(0, 0); return -1; }
    if (afs_write_handle(h, "abc", 3) != 0) { afs_remove("afs_perm_test"); afs_set_subject(0, 0); return -2; }
    if (afs_chmod("afs_perm_test", 0640u) != 0) { afs_remove("afs_perm_test"); afs_set_subject(0, 0); return -3; }

    int owner_ok, other_deny;
    afs_set_subject(1000, 1000);
    other_deny = (afs_write_handle(h, "x", 1) != 0);

    afs_set_subject(0, 0);
    owner_ok = (afs_write_handle(h, "x", 1) == 0);

    int chown_deny;
    afs_set_subject(1000, 1000);
    chown_deny = (afs_chown("afs_perm_test", 1000, 1000) != 0);

    afs_set_subject(0, 0);
    afs_remove("afs_perm_test");
    return (owner_ok && other_deny && chown_deny) ? 0 : -4;
}

/* ---- Denetim (fsck) ----
 * Header icindeki dosya tablosunu dogrular: isim, LBA/sinir tutarliligi,
 * count ve data_next. fix=1 ise tutarsiz girdileri budar ve duzeltir. */
int afs_fsck(int fix, int *problems, int *fixed) {
    if (!mounted) afs_mount();
    int prob = 0, fixd = 0;
    uint32_t data_start = AFS_BASE_LBA + H.hdr_sectors;
    uint32_t max_end = data_start;
    uint32_t used = 0;

    for (uint32_t i = 0; i < H.max_files; i++) {
        afs_file_t *f = &H.files[i];
        if (!f->used) continue;
        used++;
        int isd = afs_is_dir(f->name);
        int bad = 0;
        if (f->name[0] == 0) bad = 1;
        else if (!isd) {
            uint32_t secs = (f->size + 511u) / 512u;
            if (secs == 0) secs = 1;
            if (f->lba < data_start || f->lba >= H.capacity ||
                secs > H.capacity - f->lba) bad = 1;
            else if (f->lba + secs > max_end) max_end = f->lba + secs;
        }
        if (bad) {
            prob++;
            if (fix) {
                f->used = 0; f->name[0] = 0; f->size = 0; f->lba = 0;
                used--; fixd++;
            }
        }
    }

    if (H.hdr_sectors == 0 || H.hdr_sectors > AFS_HDR_MAX_SEC) {
        prob++; if (fix) { hdr_sync_size(); fixd++; }
    }
    if (H.count != used) { prob++; if (fix) { H.count = used; fixd++; } }
    if (H.data_next < max_end) { prob++; if (fix) { H.data_next = max_end; fixd++; } }
    if (H.data_next > H.capacity) { prob++; if (fix) { H.data_next = H.capacity; fixd++; } }

    if (fix && prob) hdr_commit();
    if (problems) *problems = prob;
    if (fixed) *fixed = fixd;
    return prob ? 1 : 0;
}

/* Journal + CRC + fsck self-testi. Gercek bir "yazma sirasinda reset"
 * senaryosunu taklit eder: gecerli bir journal kaydi yazip header'a
 * uygulamadan yeniden baglanir; replay'in girdiyi geri getirdigini dogrular. */
int afs_journal_selftest(void) {
    if (!mounted) afs_mount();
    uint32_t jl = journal_lba();
    if (jl == 0) return -1;                 /* gunluk kullanilamaz */

    afs_set_subject(0, 0);
    afs_remove("afs_jrn_test");
    afs_remove("afs_jrn_replay");

    /* Test oncesi tutarli durumu sakla; sonda aynen geri yukle. */
    uint32_t saved_count = H.count;
    uint32_t saved_dn = H.data_next;

    void *h = afs_create_handle("afs_jrn_test");
    if (!h) return -2;
    if (afs_write_handle(h, "journal-data", 12) != 0) {
        afs_remove("afs_jrn_test");
        return -3;
    }

    /* Normal commit sonrasi header CRC gecerli olmali. */
    if (H.flags == 0 || hdr_crc() != H.flags) {
        afs_remove("afs_jrn_test");
        return -4;
    }

    /* Ek girdi iceren bir header'i yalnizca gunluge yaz (header'a uygulama):
     * commit ile header yazimi arasinda reset olmus gibi. */
    int idx = -1;
    for (uint32_t i = 0; i < H.max_files; i++)
        if (!H.files[i].used) { idx = (int)i; break; }
    if (idx < 0) { afs_remove("afs_jrn_test"); return -5; }

    H.files[idx].used = 1;
    H.files[idx].size = 0;
    H.files[idx].lba = H.data_next;
    set_owner(&H.files[idx], 0);
    cpy(H.files[idx].name, "afs_jrn_replay");
    H.count++;
    H.version = AFS_VERSION;
    hdr_sync_size();
    H.flags = 0; H.flags = hdr_crc();

    int ok = 0;
    uint8_t sec[512];
    afs_jrec_t *rec = (afs_jrec_t *)sec;
    for (int i = 0; i < 512; i++) sec[i] = 0;
    rec->magic = AFS_JOURNAL_MAGIC;
    rec->seq = ++s_jseq;
    rec->hdr_sectors = H.hdr_sectors;
    rec->crc = crc32_buf(&H, hdr_bytes());
    rec->state = 1;
    if (storage_write(jl + 1u, H.hdr_sectors, &H) == 0) {
        storage_flush();
        if (storage_write(jl, 1, sec) == 0) {
            storage_flush();
            /* Header'i diskten geri yukle: commit ile header yazimi arasinda
             * reset olmus gibi (header eski, gunluk yeni). */
            storage_read(AFS_BASE_LBA, H.hdr_sectors, &H);
            journal_replay();   /* yarim kalan commit'i tamamla */
            ok = (find_idx("afs_jrn_replay") >= 0);
        }
    }

    /* Temizlik: sahte girdi + test dosyasi silinir, data_next geri alinir. */
    for (uint32_t i = 0; i < H.max_files; i++) {
        afs_file_t *f = &H.files[i];
        if (!f->used) continue;
        if (seq(f->name, "afs_jrn_replay") || seq(f->name, "afs_jrn_test")) {
            f->used = 0; f->name[0] = 0; f->size = 0; f->lba = 0;
        }
    }
    H.count = saved_count;
    H.data_next = saved_dn;
    hdr_commit();

    int clean = 0;
    { int p = 0, f = 0; clean = (afs_fsck(0, &p, &f) == 0 && p == 0); }
    return (ok && clean) ? 0 : (ok ? -7 : -6);
}

/* ---- Klasor (yol tabanli) katman ----
 * Klasorler adi '/' ile biten sifir boyutlu girdilerdir. Adlar '/' ile
 * ayrilir. C: kullanici diski acilista otomatik olusturulur. */

int afs_is_dir(const char *name) {
    if (!name) return 0;
    int n = 0; while (name[n]) n++;
    return n > 0 && name[n - 1] == '/';
}

static int pref_match(const char *nm, const char *p, int pl) {
    for (int i = 0; i < pl; i++) if (nm[i] != p[i]) return 0;
    return 1;
}

static int direct_child(const char *rest) {
    /* `rest`, klasor onekinden sonraki kisim: "apps/" veya "a.txt".
     * Yalnizca dogrudan cocuk kabul edilir: 0 egik cizgi ya da
     * yalnizca sonda tek egik cizgi (klasor). "etc/users" gibi alt
     * yollar REDDEDILIR. (Eski surumde kosul hep dogruydu ve alt
     * klasor dosyalari kok listesine siziyordu.) */
    if (!rest || !rest[0]) return 0;
    for (int i = 0; rest[i]; i++) {
        if (rest[i] == '/') return rest[i + 1] == 0;
    }
    return 1;
}

static int dir_prefix(const char *path, char *prefix, int cap) {
    if (!path || !path[0] || cap < 3) return 0;
    int o = 0;
    for (int i = 0; path[i] && o < cap - 1; i++) prefix[o++] = path[i];
    if (o > 0 && prefix[o - 1] != '/') prefix[o++] = '/';
    prefix[o] = 0;
    return o;
}

static void path_join(char *dst, int cap, const char *dir, const char *name, int is_dir) {
    int o = 0;
    if (dir && dir[0]) {
        for (int i = 0; dir[i] && o < cap - 1; i++) dst[o++] = dir[i];
        if (o > 0 && dst[o - 1] != '/' && o < cap - 1) dst[o++] = '/';
    }
    if (name) for (int i = 0; name[i] && o < cap - 1; i++) dst[o++] = name[i];
    if (is_dir && o > 0 && dst[o - 1] != '/' && o < cap - 1) dst[o++] = '/';
    dst[o] = 0;
}

int afs_ensure_dir(const char *path) {
    if (!path || !path[0]) return -1;
    if (!mounted) afs_mount();
    char buf[AFS_NAME_MAX];
    int n = 0;
    for (int i = 0; path[i] && n < (int)sizeof(buf) - 1; i++) buf[n++] = path[i];
    if (n > 0 && buf[n - 1] != '/' && n < (int)sizeof(buf) - 1) buf[n++] = '/';
    buf[n] = 0;
    for (int k = 1; k < n; k++) {
        if (buf[k] == '/') {
            char save = buf[k + 1];
            buf[k + 1] = 0;
            afs_create_handle(buf);
            buf[k + 1] = save;
        }
    }
    return 0;
}

static void afs_ensure_tree(void) {
    afs_ensure_dir(AFS_USER_ROOT);
    afs_ensure_dir(AFS_USER_APPS);
    afs_ensure_dir(AFS_USER_DOWNLOAD);
    afs_ensure_dir(AFS_USER_DOCS);
    afs_ensure_dir(AFS_USER_DESKTOP);
    afs_ensure_dir(AFS_USER_LOGS);
}

void *afs_create_in(const char *path, const char *name, int is_dir) {
    if (!mounted) afs_mount();
    char full[AFS_NAME_MAX];
    path_join(full, sizeof(full), path, name, is_dir);
    if (!full[0]) return 0;
    return afs_create_handle(full);
}

void *afs_find_in(const char *path, const char *name) {
    if (!mounted) afs_mount();
    char full[AFS_NAME_MAX];
    path_join(full, sizeof(full), path, name, 0);
    if (!full[0]) return 0;
    return afs_find_handle(full);
}

uint32_t afs_read_path(const char *path, void *buf, uint32_t size) {
    void *h = afs_find_handle(path);
    if (!h) return 0;
    return afs_read_handle(h, buf, size);
}

int afs_count_dir(const char *path) {
    if (!mounted) afs_mount();
    char p[AFS_NAME_MAX];
    int pl = dir_prefix(path, p, sizeof(p));
    if (pl <= 0) return 0;
    int c = 0;
    for (uint32_t i = 0; i < H.max_files; i++) {
        if (!H.files[i].used) continue;
        const char *nm = H.files[i].name;
        if (!pref_match(nm, p, pl)) continue;
        if (direct_child(nm + pl)) c++;
    }
    return c;
}

int afs_list_dir(const char *path, int idx, char *out, int cap, int *is_dir) {
    if (!mounted) afs_mount();
    char p[AFS_NAME_MAX];
    int pl = dir_prefix(path, p, sizeof(p));
    if (pl <= 0) return -1;
    int c = 0;
    for (uint32_t i = 0; i < H.max_files; i++) {
        if (!H.files[i].used) continue;
        const char *nm = H.files[i].name;
        if (!pref_match(nm, p, pl)) continue;
        const char *rest = nm + pl;
        if (!direct_child(rest)) continue;
        if (c == idx) {
            int rl = 0; while (rest[rl]) rl++;
            int isd = (rl > 0 && rest[rl - 1] == '/');
            int copy = isd ? rl - 1 : rl;
            if (copy > cap - 1) copy = cap - 1;
            if (out) { for (int k = 0; k < copy; k++) out[k] = rest[k]; out[copy] = 0; }
            if (is_dir) *is_dir = isd;
            return 0;
        }
        c++;
    }
    return -1;
}

/* ---- ASI sarmalayicilari ---- */
static void    *w_root(void) { return 0; }
static void    *w_create(void *d, const char *n, int isdir) { (void)d; (void)isdir; return afs_create_handle(n); }
static void    *w_find(void *d, const char *n) { (void)d; return afs_find_handle(n); }
static int      w_write(void *f, const void *data, uint32_t sz) { return afs_write_handle(f, data, sz); }
static uint32_t w_read(void *f, void *buf, uint32_t sz) { return afs_read_handle(f, buf, sz); }
static int      w_count(void) { return afs_count(); }
static const char *w_name(int idx) { return afs_name(idx); }
static uint32_t w_size(int idx) { return afs_size(idx); }
static int      w_remove(const char *n) { return afs_remove(n); }
static int      w_mkdir(const char *path) { return afs_ensure_dir(path); }
static void    *w_create_in(const char *path, const char *n, int isdir) { return afs_create_in(path, n, isdir); }
static void    *w_find_in(const char *path, const char *n) { return afs_find_in(path, n); }
static uint32_t w_read_path(const char *path, void *buf, uint32_t sz) { return afs_read_path(path, buf, sz); }
static int      w_count_dir(const char *path) { return afs_count_dir(path); }
static int      w_list_dir(const char *path, int idx, char *out, int cap, int *isdir) { return afs_list_dir(path, idx, out, cap, isdir); }
static int      w_stat(const char *path, asi_stat_t *out) { return afs_stat(path, out); }
static int      w_stat_handle(void *f, asi_stat_t *out) { return afs_stat_handle(f, out); }
static int      w_chmod(const char *path, uint32_t mode) { return afs_chmod(path, mode); }
static int      w_chown(const char *path, int uid, int gid) { return afs_chown(path, uid, gid); }
static void     w_set_subject(int uid, int gid) { afs_set_subject(uid, gid); }
static int      w_fsck(int fix, int *problems, int *fixed) { return afs_fsck(fix, problems, fixed); }
static int      w_rename(const char *from, const char *to) { return afs_rename(from, to); }

asi_fs_t *afs_asi(void) {
    static asi_fs_t fs;
    fs.root = w_root;
    fs.create = w_create;
    fs.find = w_find;
    fs.write = w_write;
    fs.read = w_read;
    fs.count = w_count;
    fs.name = w_name;
    fs.size = w_size;
    fs.remove = w_remove;
    fs.mkdir = w_mkdir;
    fs.create_in = w_create_in;
    fs.find_in = w_find_in;
    fs.read_path = w_read_path;
    fs.count_dir = w_count_dir;
    fs.list_dir = w_list_dir;
    fs.stat = w_stat;
    fs.stat_handle = w_stat_handle;
    fs.chmod = w_chmod;
    fs.chown = w_chown;
    fs.set_subject = w_set_subject;
    fs.fsck = w_fsck;
    fs.rename = w_rename;
    return &fs;
}
