#include "kernel.h"
#include "asi.h"
#include "blockdev.h"
#include "fat32.h"
#include "volfs.h"

static asi_fs_t *s_base = 0;
static asi_fs_t  s_vol;
static int       s_tried = 0;

static int is_d(const char *p) {
    return p && (p[0] == 'D' || p[0] == 'd') && p[1] == ':';
}

static const char *dpath(const char *p) {
    p += 2;
    if (*p == 0) return "/";
    return p;
}

static void vol_ensure(void) {
    if (s_tried) return;
    s_tried = 1;

    int n = blockdev_count();
    for (int i = 0; i < n; i++) {
        blockdev_info_t in;
        if (blockdev_info(i, &in) != 0) continue;
        if (in.kind == BDEV_KIND_USB) {
            fat32_mount(i);
            return;
        }
    }
}

static uint32_t v_read_path(const char *path, void *buf, uint32_t size) {
    if (is_d(path)) { vol_ensure(); return fat_read_path(dpath(path), buf, size); }
    return s_base->read_path ? s_base->read_path(path, buf, size) : 0;
}

static int v_count_dir(const char *path) {
    if (is_d(path)) { vol_ensure(); return fat_count_dir(dpath(path)); }
    return s_base->count_dir ? s_base->count_dir(path) : 0;
}

static int v_list_dir(const char *path, int idx, char *out, int cap, int *is_dir) {
    if (is_d(path)) { vol_ensure(); return fat_list_dir(dpath(path), idx, out, cap, is_dir); }
    return s_base->list_dir ? s_base->list_dir(path, idx, out, cap, is_dir) : -1;
}

static int v_stat(const char *path, asi_stat_t *out) {
    if (is_d(path)) {
        vol_ensure();
        uint32_t size = 0;
        int isdir = 0;
        if (fat_stat(dpath(path), &size, &isdir) != 0) return -1;
        if (out) {
            out->uid = 0;
            out->gid = 0;
            out->mode = isdir ? 0755 : 0644;
            out->is_dir = (uint32_t)isdir;
            out->size = size;
        }
        return 0;
    }
    return s_base->stat ? s_base->stat(path, out) : -1;
}

static int v_mkdir(const char *path) {
    if (is_d(path)) return -1;
    return s_base->mkdir ? s_base->mkdir(path) : -1;
}

static void *v_create_in(const char *path, const char *name, int is_dir) {
    if (is_d(path)) return 0;
    return s_base->create_in ? s_base->create_in(path, name, is_dir) : 0;
}

static void *v_find_in(const char *path, const char *name) {
    if (is_d(path)) return 0;
    return s_base->find_in ? s_base->find_in(path, name) : 0;
}

static void v_set_subject(int uid, int gid) {
    if (s_base->set_subject) s_base->set_subject(uid, gid);
}

asi_fs_t *volfs_install(asi_fs_t *base) {
    s_base = base;
    s_vol = *base;

    s_vol.read_path = v_read_path;
    s_vol.count_dir = v_count_dir;
    s_vol.list_dir = v_list_dir;
    s_vol.stat = v_stat;
    s_vol.mkdir = v_mkdir;
    s_vol.create_in = v_create_in;
    s_vol.find_in = v_find_in;
    s_vol.set_subject = v_set_subject;

    return &s_vol;
}
