#ifndef ARCTIAN_FS_H
#define ARCTIAN_FS_H

#include <stdint.h>

typedef struct fs_node fs_node_t;

void       fs_init(void);
fs_node_t *fs_root(void);
fs_node_t *fs_create(fs_node_t *dir, const char *name, int is_dir);
fs_node_t *fs_find(fs_node_t *dir, const char *name);
int        fs_write(fs_node_t *file, const void *data, uint32_t size);
uint32_t   fs_read(fs_node_t *file, void *buf, uint32_t size);

#endif
