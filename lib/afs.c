#include "afs.h"
#include "storage.h"
#include <stddef.h>

#define AFS_MAGIC          0x53464121u
#define AFS_MAX_FILES_HARD  512u
#define AFS_HEADER_BASE     32u
#define AFS_ENTRY_SIZE      44u
#define AFS_HDR_MAX_SEC \
    ((AFS_HEADER_BASE + AFS_MAX_FILES_HARD * AFS_ENTRY_SIZE + 511u) / 512u)
#define AFS_CLUSTER_SEC     4096u
#define AFS_RESERVE_SEC     2048u

typedef struct __attribute__((packed)) {
    char     name[32];
    uint32_t lba;
    uint32_t size;
    uint32_t used;
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

static int seq(const char *a, const char *b) {
    int i = 0; for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}
static void cpy(char *d, const char *s) {
    int i = 0; for (; s[i] && i < 31; i++) d[i] = s[i]; d[i] = '\0';
}

static uint32_t compute_capacity(void) {
    uint64_t secs = storage_sector_count();
    if (secs == 0) {
        storage_init();
        secs = storage_sector_count();
    }
    uint64_t cap;
    if (secs == 0) {
        return AFS_BASE_LBA + 65536u;
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

void afs_format(void) {
    H.magic = AFS_MAGIC;
    H.version = AFS_VERSION;
    H.capacity = compute_capacity();
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
        H.files[i].name[0] = 0;
    }
    mounted = 1;
    storage_write(AFS_BASE_LBA, H.hdr_sectors, &H);
}

void afs_mount(void) {
    uint32_t first[128];
    if (storage_read(AFS_BASE_LBA, 1, first) != 0 ||
        first[0] != AFS_MAGIC || first[1] != AFS_VERSION) {
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
        afs_format();
        return;
    }
    if (storage_read(AFS_BASE_LBA, H.hdr_sectors, &H) != 0) {
        afs_format();
        return;
    }
    mounted = 1;
}

int afs_count(void) { if (!mounted) afs_mount(); return (int)H.count; }

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
            cpy(H.files[i].name, name);
            H.count++;
            storage_write(AFS_BASE_LBA, H.hdr_sectors, &H);
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

int afs_write_handle(void *h, const void *data, uint32_t size) {
    int i = (int)(intptr_t)h - 1;
    if (i < 0 || i >= (int)H.max_files || !H.files[i].used) return -1;
    uint32_t secs = (size + 511) / 512;
    if (secs == 0) secs = 1;
    if (H.files[i].lba + secs > H.capacity) return -1;
    if (storage_write(H.files[i].lba, secs, data) != 0) return -1;
    H.files[i].size = size;
    if (H.files[i].lba + secs > H.data_next) H.data_next = H.files[i].lba + secs;
    storage_write(AFS_BASE_LBA, H.hdr_sectors, &H);
    return 0;
}

uint32_t afs_read_handle(void *h, void *buf, uint32_t size) {
    int i = (int)(intptr_t)h - 1;
    if (i < 0 || i >= (int)H.max_files || !H.files[i].used) return 0;
    uint32_t n = size < H.files[i].size ? size : H.files[i].size;
    uint32_t secs = (n + 511) / 512;
    if (secs && storage_read(H.files[i].lba, secs, buf) != 0) return 0;
    return n;
}

int afs_remove(const char *name) {
    if (!mounted) afs_mount();
    int i = find_idx(name);
    if (i < 0) return -1;
    H.files[i].used = 0;
    H.files[i].size = 0;
    H.files[i].name[0] = 0;
    if (H.count) H.count--;
    storage_write(AFS_BASE_LBA, H.hdr_sectors, &H);
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

static void    *w_root(void) { return 0; }
static void    *w_create(void *d, const char *n, int isdir) { (void)d; (void)isdir; return afs_create_handle(n); }
static void    *w_find(void *d, const char *n) { (void)d; return afs_find_handle(n); }
static int      w_write(void *f, const void *data, uint32_t sz) { return afs_write_handle(f, data, sz); }
static uint32_t w_read(void *f, void *buf, uint32_t sz) { return afs_read_handle(f, buf, sz); }
static int      w_count(void) { return afs_count(); }
static const char *w_name(int idx) { return afs_name(idx); }
static uint32_t w_size(int idx) { return afs_size(idx); }
static int      w_remove(const char *n) { return afs_remove(n); }

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
    return &fs;
}
