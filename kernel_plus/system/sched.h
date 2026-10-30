#ifndef ARCTIAN_SCHED_H
#define ARCTIAN_SCHED_H

#include <stdint.h>

#define SCHED_MAX_TASKS 8

typedef int sched_tid_t;

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DONE
} sched_state_t;

void        sched_init(void);
sched_tid_t sched_spawn(const char *name, void (*fn)(void *), void *arg);
sched_tid_t sched_spawn_ex(const char *name, void (*fn)(void *), void *arg, uint64_t cr3);
sched_tid_t sched_spawn_stack(const char *name, void (*fn)(void *), void *arg,
                              uint64_t cr3, void *stack, uint32_t stack_size);
int         sched_state(sched_tid_t tid);
uint64_t    sched_current_cr3(void);
void        sched_yield(void);
void        sched_sleep_ms(uint32_t ms);
void        sched_exit(void);
void        sched_tick(void);
sched_tid_t sched_current(void);
int         sched_task_count(void);
uint32_t    sched_task_ticks(sched_tid_t tid);
uint32_t    sched_total_ticks(void);
int         sched_selftest(void);

void        sched_block_current(void);
void        sched_wake(sched_tid_t tid);
void        sched_reap(sched_tid_t tid);

void        sched_set_owner(sched_tid_t tid, void *owner);
void       *sched_owner(sched_tid_t tid);
void       *sched_owner_current(void);

uint64_t    sched_irq_save(void);
void        sched_irq_restore(uint64_t flags);

#endif
