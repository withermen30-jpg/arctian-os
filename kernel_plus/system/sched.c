#include "sched.h"
#include "isr.h"
#include "serial.h"
#include "paging.h"

#define SCHED_STACK_SIZE   (32u * 1024u)
#define SCHED_SLICE_TICKS  5u

typedef struct sched_task {
    uint64_t rsp;
    uint64_t fpu[64] __attribute__((aligned(16)));
    volatile int state;
    int id;
    const char *name;
    uint8_t *stack_base;
    uint32_t wake_tick;
    uint64_t switches;
    uint64_t cr3;
    void *owner;
    uint64_t ticks;
} sched_task_t;

extern void arctian_sched_switch(uint64_t *from_rsp, uint64_t *to_rsp,
                                 void *from_fpu, void *to_fpu);
extern void arctian_task_entry(void);

static sched_task_t s_tasks[SCHED_MAX_TASKS] __attribute__((aligned(16)));
static uint8_t s_stacks[SCHED_MAX_TASKS][SCHED_STACK_SIZE] __attribute__((aligned(16)));
static sched_task_t *s_current;
static volatile uint32_t s_tick;
static volatile uint64_t s_total_ticks;
static volatile int s_ready;

uint64_t sched_irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

void sched_irq_restore(uint64_t flags) {
    if (flags & 0x200ull) __asm__ volatile("sti" ::: "memory");
}

static void sched_switch_to(sched_task_t *from, sched_task_t *to) {
    if (to->cr3 && to->cr3 != from->cr3) vmm_switch(to->cr3);
    arctian_sched_switch(&from->rsp, &to->rsp, from->fpu, to->fpu);
}

static void schedule(void) {
    sched_task_t *cur = s_current;
    if (!cur) return;

    sched_task_t *next = 0;
    for (int k = 1; k < SCHED_MAX_TASKS; k++) {
        int idx = (cur->id + k) % SCHED_MAX_TASKS;
        sched_task_t *t = &s_tasks[idx];
        if (t->state == TASK_READY) { next = t; break; }
    }

    if (!next) {
        if (cur->state != TASK_RUNNING && s_tasks[0].state == TASK_READY)
            next = &s_tasks[0];
        if (!next) return;
    }
    if (next == cur) return;

    if (cur->state == TASK_RUNNING) cur->state = TASK_READY;
    next->state = TASK_RUNNING;
    s_current = next;
    cur->switches++;
    sched_switch_to(cur, next);
}

static void sched_idle(void) {
    while (s_current && s_current->state == TASK_BLOCKED)
        __asm__ volatile("sti; hlt" ::: "memory");
}

void sched_tick(void) {
    if (!s_ready) return;
    s_tick++;
    s_total_ticks++;
    if (s_current && s_current->state == TASK_RUNNING) s_current->ticks++;

    for (int i = 0; i < SCHED_MAX_TASKS; i++) {
        sched_task_t *t = &s_tasks[i];
        if (t->state == TASK_BLOCKED &&
            (int32_t)(s_tick - t->wake_tick) >= 0) {
            t->state = TASK_READY;
        }
    }

    if ((s_tick % SCHED_SLICE_TICKS) != 0) return;
    schedule();
}

void sched_yield(void) {
    if (!s_ready) return;
    uint64_t f = sched_irq_save();
    schedule();
    sched_irq_restore(f);
}

void sched_sleep_ms(uint32_t ms) {
    if (!s_ready || ms == 0) return;
    uint64_t f = sched_irq_save();
    s_current->wake_tick = s_tick + ms;
    s_current->state = TASK_BLOCKED;
    schedule();
    sched_idle();
    sched_irq_restore(f);
}

void sched_exit(void) {
    uint64_t f = sched_irq_save();
    s_current->state = TASK_DONE;
    schedule();
    sched_irq_restore(f);
    for (;;) __asm__ volatile("hlt");
}

sched_tid_t sched_spawn(const char *name, void (*fn)(void *), void *arg) {
    return sched_spawn_ex(name, fn, arg, 0);
}

static sched_tid_t sched_alloc_task(const char *name, void (*fn)(void *), void *arg,
                                    uint64_t cr3, void *stack, uint32_t stack_size) {
    if (!s_ready || !fn) return -1;
    uint64_t f = sched_irq_save();

    int idx = -1;
    for (int i = 1; i < SCHED_MAX_TASKS; i++) {
        if (s_tasks[i].state == TASK_UNUSED) { idx = i; break; }
    }
    if (idx < 0) { sched_irq_restore(f); return -1; }

    sched_task_t *t = &s_tasks[idx];
    void *base = stack ? stack : s_stacks[idx];
    uint32_t size = stack ? stack_size : SCHED_STACK_SIZE;
    uintptr_t top = ((uintptr_t)base + size) & ~(uintptr_t)15;
    uint64_t *fr = (uint64_t *)(top - 64);
    fr[0] = 0;
    fr[1] = 0;
    fr[2] = (uint64_t)(uintptr_t)arg;
    fr[3] = (uint64_t)(uintptr_t)fn;
    fr[4] = 0;
    fr[5] = 0;
    fr[6] = 0x202;
    fr[7] = (uint64_t)(uintptr_t)&arctian_task_entry;

    for (int i = 0; i < 64; i++) t->fpu[i] = 0;
    ((uint32_t *)t->fpu)[6] = 0x1F80u;

    t->rsp = (uint64_t)(uintptr_t)fr;
    t->state = TASK_READY;
    t->id = idx;
    t->name = name;
    t->stack_base = (uint8_t *)base;
    t->wake_tick = 0;
    t->switches = 0;
    t->cr3 = cr3 ? cr3 : vmm_current();
    t->owner = 0;

    sched_irq_restore(f);
    serial_printf("[sched] task olustur: %s (id=%d)\n", name ? name : "?", idx);
    return idx;
}

sched_tid_t sched_spawn_ex(const char *name, void (*fn)(void *), void *arg, uint64_t cr3) {
    return sched_alloc_task(name, fn, arg, cr3, 0, 0);
}

sched_tid_t sched_spawn_stack(const char *name, void (*fn)(void *), void *arg,
                              uint64_t cr3, void *stack, uint32_t stack_size) {
    return sched_alloc_task(name, fn, arg, cr3, stack, stack_size);
}

int sched_state(sched_tid_t tid) {
    if (tid < 0 || tid >= SCHED_MAX_TASKS) return TASK_UNUSED;
    return s_tasks[tid].state;
}

uint64_t sched_current_cr3(void) {
    return vmm_current();
}

void sched_block_current(void) {
    if (!s_ready) return;
    uint64_t f = sched_irq_save();
    if (s_current && s_current->state == TASK_RUNNING) {
        s_current->state = TASK_BLOCKED;
        s_current->wake_tick = s_tick + 0x40000000u;
    }
    schedule();
    sched_idle();
    sched_irq_restore(f);
}

void sched_wake(sched_tid_t tid) {
    if (tid < 0 || tid >= SCHED_MAX_TASKS) return;
    uint64_t f = sched_irq_save();
    if (s_tasks[tid].state == TASK_BLOCKED) s_tasks[tid].state = TASK_READY;
    sched_irq_restore(f);
}

void sched_reap(sched_tid_t tid) {
    if (tid <= 0 || tid >= SCHED_MAX_TASKS) return;
    uint64_t f = sched_irq_save();
    if (s_tasks[tid].state == TASK_DONE) {
        s_tasks[tid].state = TASK_UNUSED;
        s_tasks[tid].owner = 0;
        s_tasks[tid].name = 0;
    }
    sched_irq_restore(f);
}

void sched_set_owner(sched_tid_t tid, void *owner) {
    if (tid < 0 || tid >= SCHED_MAX_TASKS) return;
    s_tasks[tid].owner = owner;
}

void *sched_owner(sched_tid_t tid) {
    if (tid < 0 || tid >= SCHED_MAX_TASKS) return 0;
    return s_tasks[tid].owner;
}

void *sched_owner_current(void) {
    return s_current ? s_current->owner : 0;
}

sched_tid_t sched_current(void) {
    return s_current ? s_current->id : -1;
}

int sched_task_count(void) {
    int n = 0;
    for (int i = 0; i < SCHED_MAX_TASKS; i++)
        if (s_tasks[i].state != TASK_UNUSED) n++;
    return n;
}

uint32_t sched_task_ticks(sched_tid_t tid) {
    if (tid < 0 || tid >= SCHED_MAX_TASKS) return 0;
    return (uint32_t)s_tasks[tid].ticks;
}

uint32_t sched_total_ticks(void) {
    return (uint32_t)s_total_ticks;
}

void sched_init(void) {
    for (int i = 0; i < SCHED_MAX_TASKS; i++) {
        s_tasks[i].rsp = 0;
        s_tasks[i].state = TASK_UNUSED;
        s_tasks[i].id = i;
        s_tasks[i].name = 0;
        s_tasks[i].stack_base = 0;
        s_tasks[i].wake_tick = 0;
        s_tasks[i].switches = 0;
        s_tasks[i].cr3 = 0;
        s_tasks[i].owner = 0;
        s_tasks[i].ticks = 0;
    }
    s_tasks[0].state = TASK_RUNNING;
    s_tasks[0].name = "main";
    s_tasks[0].cr3 = vmm_current();
    s_current = &s_tasks[0];
    s_tick = 0;
    s_ready = 1;
    arctian_set_sched_tick(sched_tick);
    serial_puts("[sched] preemptive scheduler aktif (round-robin)\n");
}

static volatile uint64_t s_count1;
static volatile uint64_t s_count2;
static volatile int s_stop;

static void sched_counter_task(void *arg) {
    volatile uint64_t *c = (volatile uint64_t *)arg;
    while (!s_stop) {
        (*c)++;
        for (volatile int i = 0; i < 2000; i++) __asm__ volatile("pause");
    }
    sched_exit();
}

static void sched_monitor_task(void *arg) {
    (void)arg;
    for (int i = 0; i < 40; i++) sched_sleep_ms(10);
    uint64_t a = s_count1;
    uint64_t b = s_count2;
    int ok = (a > 0 && b > 0);
    serial_printf("[sched] tick testi: sayac1=%u sayac2=%u -> %s\n",
                  (unsigned)a, (unsigned)b, ok ? "EVET (ikisi de ilerledi)"
                                               : "HAYIR");
    s_stop = 1;
    sched_exit();
}

int sched_selftest(void) {
    s_count1 = 0;
    s_count2 = 0;
    s_stop = 0;

    sched_tid_t a = sched_spawn("sayac1", sched_counter_task, (void *)&s_count1);
    sched_tid_t b = sched_spawn("sayac2", sched_counter_task, (void *)&s_count2);
    sched_tid_t m = sched_spawn("izleyici", sched_monitor_task, 0);
    if (a < 0 || b < 0 || m < 0) {
        serial_puts("[sched] selftest: task olusturulamadi\n");
        return -1;
    }
    serial_puts("[sched] selftest basladi: 2 sayac + izleyici (UI ile paralel)\n");
    return 0;
}
