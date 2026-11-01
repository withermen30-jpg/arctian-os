#ifndef ARCTIAN_AFS_H
#define ARCTIAN_AFS_H

#include <stdint.h>
#include "asi.h"

#define AFS_BASE_LBA   20000u  /* disktaki temel LBA (modul bolgesinin sonrasi) */
#define AFS_VERSION    4u      /* header surumu; uymayan disk yeniden formatlanir */
#define AFS_NAME_MAX   64u     /* dosya/klasor adi azami bayt (+ sonlandirici) */

/* Kullanici diski (C:/) kok yolu. Sistem dosyalari (ayar, .dpk) ham kokte
 * tutulur; bunlar masaustunde gizlidir. C: diski kullaniciya aittir. */
#define AFS_USER_ROOT     "C:/"
#define AFS_USER_APPS     "C:/apps"
#define AFS_USER_DOWNLOAD "C:/indirilenler"
#define AFS_USER_DOCS     "C:/belgeler"
#define AFS_USER_DESKTOP  "C:/masaustu"
#define AFS_USER_LOGS     "C:/logs"

void        afs_format(void);
void        afs_mount(void);
int         afs_count(void);

void       *afs_create_handle(const char *name);
void       *afs_find_handle(const char *name);
int         afs_write_handle(void *h, const void *data, uint32_t size);
uint32_t    afs_read_handle(void *h, void *buf, uint32_t size);
uint32_t    afs_read_at(void *h, void *buf, uint32_t size, uint32_t off);
int         afs_remove(const char *name);
const char *afs_name(int idx);
uint32_t    afs_size(int idx);

/* ---- Klasor (yol tabanli) API ----
 * path ornegi: "C:/apps". Adlar '/' ile ayrilir; klasor adi '/' ile biter. */
int         afs_is_dir(const char *name);
int         afs_ensure_dir(const char *path);                  /* 0 = hazir */
void       *afs_create_in(const char *path, const char *name, int is_dir);
void       *afs_find_in(const char *path, const char *name);
uint32_t    afs_read_path(const char *path, void *buf, uint32_t size);
int         afs_count_dir(const char *path);
int         afs_list_dir(const char *path, int idx, char *out, int cap, int *is_dir);

void        afs_set_subject(int uid, int gid);
void        afs_get_subject(int *uid, int *gid);
int         afs_chmod(const char *path, uint32_t mode);
int         afs_chown(const char *path, int uid, int gid);
int         afs_stat(const char *path, asi_stat_t *out);
int         afs_stat_handle(void *h, asi_stat_t *out);
int         afs_rename(const char *from, const char *to);   /* 0/-1/-2/-3 */
int         afs_perm_selftest(void);

/* Butunluk: metadata gunlugu (journal) + denetim (fsck). */
int         afs_fsck(int fix, int *problems, int *fixed);
int         afs_journal_selftest(void);

void        afs_set_locked(int v);
int         afs_is_locked(void);
int         afs_disk_is_encrypted(void);
uint32_t    afs_data_next(void);

asi_fs_t   *afs_asi(void);

#endif /* ARCTIAN_AFS_H */
