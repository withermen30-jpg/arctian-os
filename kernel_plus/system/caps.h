#ifndef ARCTIAN_CAPS_H
#define ARCTIAN_CAPS_H

#include <stdint.h>
#include "asi.h"

void     caps_init(asi_t *asi);
void     caps_fs_guard_install(asi_fs_t **slot);

void     caps_set(uint32_t mask);
void     caps_clear(void);
void     caps_enter(int pid);
uint32_t caps_current(void);
int      caps_check(uint32_t cap);
int      caps_last_error(void);
int      caps_syscall_check(uint32_t cap);
void     caps_set_uid(int uid, int gid);
uint32_t caps_uid(void);
uint32_t caps_gid(void);void caps_syscall_subject(uint32_t *uid, uint32_t *gid);
int      caps_selftest(void);

/* --- 0.7: ozne + dosya kapisi politikasi ------------------- */
/* Politika: 0 = izin, != 0 = reddet (SANDBOX_EPERM). */
typedef int (*caps_policy_fn)(const char *path, int is_write);
void     caps_set_policy(caps_policy_fn fn);
void     caps_subject_begin(const char *id, const char *title,
                            const char *home, int trusted);
void     caps_subject_end(void);
const char *caps_subject_id(void);
const char *caps_subject_title(void);
const char *caps_subject_home(void);
int      caps_subject_trusted(void);
int      caps_subject_active(void);
/* Uygulama kodunun calistigi anlar; politika yalnizca bunlarda uygulanir. */
void     caps_dispatch_begin(void);
void     caps_dispatch_end(void);
int      caps_dispatch_active(void);

#endif
