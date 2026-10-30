#ifndef ARCTIAN_VOLFS_H
#define ARCTIAN_VOLFS_H

#include "asi.h"

/* AFS tabanli birim yonlendiricisi kurar (C: -> AFS, D: -> FAT32/USB).
 * base: mevcut (AFS) asi_fs_t. Yeni bir vtable dondurur. */
asi_fs_t *volfs_install(asi_fs_t *base);

#endif /* ARCTIAN_VOLFS_H */
