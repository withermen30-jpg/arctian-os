#ifndef ARCTIAN_THREAD_H
#define ARCTIAN_THREAD_H

#include "sched.h"
#include "sync.h"

typedef sched_tid_t kthread_t;

kthread_t kthread_create(const char *name, void (*fn)(void *), void *arg);
int       kthread_join(kthread_t t);
void      kthread_exit(void) __attribute__((noreturn));
kthread_t kthread_self(void);
int       kthread_count(void);
int       kthread_selftest(void);
int       lab_selftest(void);

#endif
