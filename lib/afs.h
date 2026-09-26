#ifndef ARCTIAN_AFS_H
#define ARCTIAN_AFS_H

#include <stdint.h>
#include "asi.h"

#define AFS_BASE_LBA   20000u
#define AFS_VERSION    2u

void        afs_format(void);
void        afs_mount(void);
int         afs_count(void);

void       *afs_create_handle(const char *name);
void       *afs_find_handle(const char *name);
int         afs_write_handle(void *h, const void *data, uint32_t size);
uint32_t    afs_read_handle(void *h, void *buf, uint32_t size);
int         afs_remove(const char *name);
const char *afs_name(int idx);
uint32_t    afs_size(int idx);

asi_fs_t   *afs_asi(void);

#endif
