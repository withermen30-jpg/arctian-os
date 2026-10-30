#ifndef ARCTIAN_SYNC_H
#define ARCTIAN_SYNC_H

#include <stdint.h>
#include "sched.h"

#define WAITQ_MAX 16

typedef struct {
    int tids[WAITQ_MAX];
    int n;
} waitq_t;

typedef struct {
    volatile int v;
} spinlock_t;

typedef struct {
    volatile int locked;
    volatile int owner;
    waitq_t wq;
} mutex_t;

typedef struct {
    volatile int count;
    waitq_t wq;
} sem_t;

void waitq_init(waitq_t *q);
void waitq_add(waitq_t *q, int tid);
int  waitq_pop(waitq_t *q);
int  waitq_remove(waitq_t *q, int tid);

void spin_init(spinlock_t *l);
void spin_lock(spinlock_t *l);
void spin_unlock(spinlock_t *l);

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
int  mutex_trylock(mutex_t *m);

void sem_init(sem_t *s, int value);
void sem_wait(sem_t *s);
int  sem_trywait(sem_t *s);
void sem_post(sem_t *s);

int  sync_selftest(void);

#endif
