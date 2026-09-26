#include "fs.h"
#include <stddef.h>

#define FS_MAX_NODES 64
#define FS_POOL_SIZE (64u * 1024u)

struct fs_node {
    char name[32];
    int is_dir;
    fs_node_t *parent;
    fs_node_t *child;
    fs_node_t *next;
    uint8_t *data;
    uint32_t size;
};

static fs_node_t nodes[FS_MAX_NODES];
static int node_count = 0;
static uint8_t pool[FS_POOL_SIZE];
static uint32_t pool_used = 0;
static fs_node_t *root_node = 0;

static int name_eq(const char *a, const char *b) {
    int i = 0;
    for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}

static void name_cpy(char *d, const char *s) {
    int i = 0;
    for (; s[i] && i < 31; i++) d[i] = s[i];
    d[i] = '\0';
}

void fs_init(void) {
    node_count = 0;
    pool_used = 0;
    root_node = &nodes[node_count++];
    name_cpy(root_node->name, "/");
    root_node->is_dir = 1;
    root_node->parent = 0;
    root_node->child = 0;
    root_node->next = 0;
    root_node->data = 0;
    root_node->size = 0;
}

fs_node_t *fs_root(void) { return root_node; }

fs_node_t *fs_create(fs_node_t *dir, const char *name, int is_dir) {
    if (!dir || !dir->is_dir) return 0;
    if (node_count >= FS_MAX_NODES) return 0;
    fs_node_t *n = &nodes[node_count++];
    name_cpy(n->name, name);
    n->is_dir = is_dir;
    n->parent = dir;
    n->child = 0;
    n->next = dir->child;
    n->data = 0;
    n->size = 0;
    dir->child = n;
    return n;
}

fs_node_t *fs_find(fs_node_t *dir, const char *name) {
    if (!dir) return 0;
    for (fs_node_t *c = dir->child; c; c = c->next)
        if (name_eq(c->name, name)) return c;
    return 0;
}

int fs_write(fs_node_t *file, const void *data, uint32_t size) {
    if (!file || file->is_dir) return -1;
    if (pool_used + size > FS_POOL_SIZE) return -1;
    file->data = &pool[pool_used];
    const uint8_t *s = (const uint8_t *)data;
    for (uint32_t i = 0; i < size; i++) pool[pool_used + i] = s[i];
    pool_used += size;
    file->size = size;
    return 0;
}

uint32_t fs_read(fs_node_t *file, void *buf, uint32_t size) {
    if (!file || file->is_dir || !file->data) return 0;
    uint32_t n = size < file->size ? size : file->size;
    uint8_t *d = (uint8_t *)buf;
    for (uint32_t i = 0; i < n; i++) d[i] = file->data[i];
    return n;
}
