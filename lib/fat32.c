#include "fat32.h"
#include "blockdev.h"
#include "kernel.h"

typedef struct {
    int      bdev;
    int      mounted;
    int      is_fat32;
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint32_t fat_size;
    uint32_t root_dir_sectors;
    uint32_t first_fat_sector;
    uint32_t first_root_dir_sector;
    uint32_t first_data_sector;
    uint32_t count_of_clusters;
    uint32_t root_cluster;
    uint32_t total_sectors;
    uint32_t bps512;
    char     label[12];
} fat32_t;

static fat32_t s_fs;
static uint8_t s_sec[4096];
static uint8_t s_fatbuf[4096];
static uint8_t s_fat_cache[4096];
static uint32_t s_fat_cache_lba;
static int s_fat_cache_valid;

static void str_copy_n(char *dst, const char *src, int cap) {
    int i = 0;
    for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

static int str_eq_ci(const char *a, const char *b) {
    for (;; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
}

static int rd_sector(fat32_t *fs, uint32_t lba, uint8_t *buf) {
    return blockdev_read(fs->bdev, (uint64_t)lba * fs->bps512, fs->bps512, buf);
}

static int rd_fat(fat32_t *fs, uint32_t lba, uint8_t *buf) {
    if (s_fat_cache_valid && s_fat_cache_lba == lba) {
        memcpy(buf, s_fat_cache, fs->bytes_per_sector);
        return 0;
    }
    if (rd_sector(fs, lba, buf) != 0) return -1;
    memcpy(s_fat_cache, buf, fs->bytes_per_sector);
    s_fat_cache_lba = lba;
    s_fat_cache_valid = 1;
    return 0;
}

static uint32_t fat_next(fat32_t *fs, uint32_t cluster) {
    if (cluster < 2) return 0;
    if (fs->is_fat32) {
        uint32_t off = cluster * 4;
        uint32_t sec = fs->first_fat_sector + off / fs->bytes_per_sector;
        uint32_t idx = off % fs->bytes_per_sector;
        if (rd_fat(fs, sec, s_fatbuf) != 0) return 0x0FFFFFFF;
        uint32_t v = (uint32_t)s_fatbuf[idx] | ((uint32_t)s_fatbuf[idx + 1] << 8) |
                     ((uint32_t)s_fatbuf[idx + 2] << 16) | ((uint32_t)s_fatbuf[idx + 3] << 24);
        v &= 0x0FFFFFFF;
        if (v >= 0x0FFFFFF8 || v == 0) return 0x0FFFFFFF;
        return v;
    } else {
        uint32_t off = cluster * 2;
        uint32_t sec = fs->first_fat_sector + off / fs->bytes_per_sector;
        uint32_t idx = off % fs->bytes_per_sector;
        if (rd_fat(fs, sec, s_fatbuf) != 0) return 0xFFFFFFFF;
        uint32_t v = (uint32_t)s_fatbuf[idx] | ((uint32_t)s_fatbuf[idx + 1] << 8);
        if (v >= 0xFFF8 || v == 0) return 0xFFFFFFFF;
        return v;
    }
}

static int fat_is_eoc(fat32_t *fs, uint32_t c) {
    return fs->is_fat32 ? (c >= 0x0FFFFFF8) : (c >= 0xFFF8);
}

static uint32_t cluster_lba(fat32_t *fs, uint32_t cluster) {
    return fs->first_data_sector + (cluster - 2) * fs->sectors_per_cluster;
}

static void make_short(const uint8_t *de, char *out, int cap) {
    int p = 0;
    for (int i = 0; i < 8; i++) {
        char c = (char)de[i];
        if (c == ' ') break;
        if (i == 0 && c == 0x05) c = (char)0xE5;
        if (p < cap - 1) out[p++] = c;
    }
    int has_ext = 0;
    for (int i = 8; i < 11; i++) if (de[i] != ' ') { has_ext = 1; break; }
    if (has_ext) {
        if (p < cap - 1) out[p++] = '.';
        for (int i = 8; i < 11; i++) {
            char c = (char)de[i];
            if (c == ' ') break;
            if (p < cap - 1) out[p++] = c;
        }
    }
    out[p] = 0;
}

/* Dizin yuruyusu; target indeksli gorunur girdiyi bulur.
 * 0 = bulundu, 1 = liste sonu, <0 = hata. */
static int dir_iter(fat32_t *fs, uint32_t start_cluster, int root16,
                    int target, fat_dirent_t *out) {
    char lfn[261];
    memset(lfn, 0, sizeof(lfn));
    int lfn_have = 0;
    int idx = 0;

    uint32_t cluster = start_cluster;
    uint32_t lba;
    uint32_t nsec;

    if (root16) {
        lba = fs->first_root_dir_sector;
        nsec = fs->root_dir_sectors;
    } else {
        if (cluster < 2) return 1;
        lba = cluster_lba(fs, cluster);
        nsec = fs->sectors_per_cluster;
    }

    for (;;) {
        for (uint32_t s = 0; s < nsec; s++) {
            if (rd_sector(fs, lba + s, s_sec) != 0) return -1;
            for (int e = 0; e + 32 <= fs->bytes_per_sector; e += 32) {
                uint8_t *de = s_sec + e;
                uint8_t first = de[0];
                if (first == 0x00) return 1;
                if (first == 0xE5) { lfn_have = 0; continue; }
                uint8_t attr = de[11];
                if ((attr & 0x3F) == 0x0F) {
                    int ord = de[0] & 0x1F;
                    if (de[0] & 0x40) memset(lfn, 0, sizeof(lfn));
                    if (ord >= 1 && ord <= 20) {
                        char *pp = lfn + (ord - 1) * 13;
                        static const int o[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
                        for (int k = 0; k < 13; k++) {
                            uint16_t ch = (uint16_t)(de[o[k]] | (de[o[k] + 1] << 8));
                            if (ch == 0) pp[k] = 0;
                            else if (ch < 0x80) pp[k] = (char)ch;
                            else pp[k] = '?';
                        }
                    }
                    lfn_have = 1;
                    continue;
                }
                if (attr & 0x08) { lfn_have = 0; continue; }

                char sname[16];
                make_short(de, sname, sizeof(sname));
                int skip = (sname[0] == '.' &&
                            (sname[1] == 0 || (sname[1] == '.' && sname[2] == 0)));
                if (!skip) {
                    if (idx == target) {
                        if (lfn_have && lfn[0]) str_copy_n(out->name, lfn, sizeof(out->name));
                        else str_copy_n(out->name, sname, sizeof(out->name));
                        out->is_dir = (attr & 0x10) ? 1 : 0;
                        out->first_cluster = (uint32_t)(de[26] | (de[27] << 8)) |
                                             ((uint32_t)(de[20] | (de[21] << 8)) << 16);
                        out->size = (uint32_t)de[28] | ((uint32_t)de[29] << 8) |
                                    ((uint32_t)de[30] << 16) | ((uint32_t)de[31] << 24);
                        return 0;
                    }
                    idx++;
                }
                lfn_have = 0;
            }
        }
        if (root16) return 1;
        uint32_t nextc = fat_next(fs, cluster);
        if (fat_is_eoc(fs, nextc)) return 1;
        cluster = nextc;
        lba = cluster_lba(fs, cluster);
        nsec = fs->sectors_per_cluster;
    }
}

static int resolve(fat32_t *fs, const char *path, fat_dirent_t *out) {
    uint32_t cluster = fs->is_fat32 ? fs->root_cluster : 0;
    int root16 = fs->is_fat32 ? 0 : 1;

    const char *p = path;
    while (*p == '/') p++;
    if (*p == 0) {
        out->is_dir = 1;
        out->size = 0;
        out->first_cluster = cluster;
        str_copy_n(out->name, "/", sizeof(out->name));
        return 0;
    }

    char comp[260];
    while (*p) {
        int n = 0;
        while (*p && *p != '/') { if (n < 255) comp[n++] = *p; p++; }
        comp[n] = 0;
        while (*p == '/') p++;

        fat_dirent_t de;
        int found = 0;
        for (int i = 0; ; i++) {
            int r = dir_iter(fs, cluster, root16, i, &de);
            if (r != 0) break;
            if (str_eq_ci(de.name, comp)) { found = 1; break; }
        }
        if (!found) return -1;
        if (*p == 0) { *out = de; return 0; }
        if (!de.is_dir) return -1;
        cluster = de.first_cluster;
        root16 = 0;
    }
    return -1;
}

int fat32_mount(int bdev) {
    memset(&s_fs, 0, sizeof(s_fs));
    s_fat_cache_valid = 0;

    uint8_t bs[512];
    if (blockdev_read(bdev, 0, 1, bs) != 0) return -1;
    if (bs[510] != 0x55 || bs[511] != 0xAA) return -2;

    uint16_t bps = (uint16_t)(bs[11] | (bs[12] << 8));
    uint8_t spc = bs[13];
    uint16_t reserved = (uint16_t)(bs[14] | (bs[15] << 8));
    uint8_t nfats = bs[16];
    uint16_t root_entries = (uint16_t)(bs[17] | (bs[18] << 8));
    uint16_t fat16sz = (uint16_t)(bs[22] | (bs[23] << 8));
    uint32_t total16 = (uint32_t)(bs[19] | (bs[20] << 8));
    uint32_t total32 = (uint32_t)bs[32] | ((uint32_t)bs[33] << 8) |
                       ((uint32_t)bs[34] << 16) | ((uint32_t)bs[35] << 24);
    uint32_t total = total16 ? total16 : total32;
    uint32_t fat_sz = fat16sz ? fat16sz :
        ((uint32_t)bs[36] | ((uint32_t)bs[37] << 8) |
         ((uint32_t)bs[38] << 16) | ((uint32_t)bs[39] << 24));
    uint32_t root_cluster = fat16sz ? 0 :
        ((uint32_t)bs[44] | ((uint32_t)bs[45] << 8) |
         ((uint32_t)bs[46] << 16) | ((uint32_t)bs[47] << 24));

    if (bps < 512 || bps > 4096 || (bps & (bps - 1))) return -3;
    if (spc == 0 || nfats == 0 || fat_sz == 0 || total == 0) return -4;

    uint32_t root_dir_sectors = ((uint32_t)root_entries * 32 + (bps - 1)) / bps;
    uint32_t first_fat = reserved;
    uint32_t first_root = reserved + (uint32_t)nfats * fat_sz;
    uint32_t first_data = first_root + root_dir_sectors;
    if (total <= first_data) return -5;

    s_fs.bdev = bdev;
    s_fs.is_fat32 = (fat16sz == 0);
    s_fs.bytes_per_sector = bps;
    s_fs.sectors_per_cluster = spc;
    s_fs.reserved_sectors = reserved;
    s_fs.num_fats = nfats;
    s_fs.fat_size = fat_sz;
    s_fs.root_dir_sectors = root_dir_sectors;
    s_fs.first_fat_sector = first_fat;
    s_fs.first_root_dir_sector = first_root;
    s_fs.first_data_sector = first_data;
    s_fs.count_of_clusters = (total - first_data) / spc;
    s_fs.root_cluster = s_fs.is_fat32 ? (root_cluster ? root_cluster : 2) : 0;
    s_fs.total_sectors = total;
    s_fs.bps512 = bps / 512;

    for (int i = 0; i < 11; i++) s_fs.label[i] = (char)bs[71 + i];
    s_fs.label[11] = 0;

    s_fs.mounted = 1;
    return 0;
}

int fat32_mounted(void) { return s_fs.mounted; }

int fat_count_dir(const char *path) {
    if (!s_fs.mounted) return 0;
    fat_dirent_t de;
    if (resolve(&s_fs, path, &de) != 0 || !de.is_dir) return 0;
    uint32_t cluster = de.first_cluster;
    int root16 = 0;
    if (!s_fs.is_fat32 && (path[0] == '/' && path[1] == 0)) {
        root16 = 1;
    } else if (path[0] == '/' && path[1] == 0) {
        cluster = s_fs.root_cluster;
    }
    int n = 0;
    fat_dirent_t tmp;
    while (dir_iter(&s_fs, cluster, root16, n, &tmp) == 0) n++;
    return n;
}

int fat_list_dir(const char *path, int idx, char *out, int cap, int *is_dir) {
    if (!s_fs.mounted) return -1;
    fat_dirent_t de;
    if (resolve(&s_fs, path, &de) != 0 || !de.is_dir) return -1;
    uint32_t cluster = de.first_cluster;
    int root16 = 0;
    if (!s_fs.is_fat32 && path[0] == '/' && path[1] == 0) root16 = 1;
    else if (path[0] == '/' && path[1] == 0) cluster = s_fs.root_cluster;

    int r = dir_iter(&s_fs, cluster, root16, idx, &de);
    if (r != 0) return r;
    str_copy_n(out, de.name, cap);
    if (is_dir) *is_dir = de.is_dir;
    return 0;
}

uint32_t fat_read_path(const char *path, void *buf, uint32_t size) {
    if (!s_fs.mounted || !buf) return 0;
    fat_dirent_t de;
    if (resolve(&s_fs, path, &de) != 0 || de.is_dir) return 0;

    uint32_t toread = de.size < size ? de.size : size;
    uint32_t done = 0;
    uint32_t cluster = de.first_cluster;
    uint8_t *out = (uint8_t *)buf;

    while (done < toread && cluster >= 2) {
        uint32_t lba = cluster_lba(&s_fs, cluster);
        for (uint32_t s = 0; s < s_fs.sectors_per_cluster && done < toread; s++) {
            if (rd_sector(&s_fs, lba + s, s_sec) != 0) return done;
            uint32_t chunk = s_fs.bytes_per_sector;
            if (chunk > toread - done) chunk = toread - done;
            memcpy(out + done, s_sec, chunk);
            done += chunk;
        }
        cluster = fat_next(&s_fs, cluster);
        if (fat_is_eoc(&s_fs, cluster)) break;
    }
    return done;
}

int fat_stat(const char *path, uint32_t *size, int *is_dir) {
    if (!s_fs.mounted) return -1;
    fat_dirent_t de;
    if (resolve(&s_fs, path, &de) != 0) return -1;
    if (size) *size = de.size;
    if (is_dir) *is_dir = de.is_dir;
    return 0;
}
