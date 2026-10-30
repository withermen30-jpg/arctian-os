#include "thread.h"
#include "ipc.h"
#include "serial.h"

#define KTHREAD_MAX 8

typedef struct {
    sched_tid_t tid;
    void (*fn)(void *);
    void *arg;
    volatile int done;
    volatile int used;
    char name[24];
    waitq_t joiners;
} kthread_rec_t;

static kthread_rec_t s_kth[KTHREAD_MAX];

static kthread_rec_t *kthread_rec(kthread_t tid) {
    for (int i = 0; i < KTHREAD_MAX; i++)
        if (s_kth[i].used && s_kth[i].tid == tid) return &s_kth[i];
    return 0;
}

static void kthread_trampoline(void *arg) {
    kthread_rec_t *t = (kthread_rec_t *)arg;
    if (t->fn) t->fn(t->arg);
    uint64_t f = sched_irq_save();
    t->done = 1;
    int w;
    while ((w = waitq_pop(&t->joiners)) >= 0) sched_wake(w);
    sched_irq_restore(f);
    sched_exit();
}

kthread_t kthread_create(const char *name, void (*fn)(void *), void *arg) {
    if (!fn) return -1;
    uint64_t f = sched_irq_save();

    int slot = -1;
    for (int i = 0; i < KTHREAD_MAX; i++)
        if (!s_kth[i].used) { slot = i; break; }
    if (slot < 0) { sched_irq_restore(f); return -1; }

    kthread_rec_t *t = &s_kth[slot];
    t->used = 1;
    t->done = 0;
    t->fn = fn;
    t->arg = arg;
    waitq_init(&t->joiners);
    int i = 0;
    if (name) for (; name[i] && i < (int)sizeof(t->name) - 1; i++) t->name[i] = name[i];
    t->name[i] = 0;

    sched_tid_t tid = sched_spawn_ex(t->name, kthread_trampoline, t, 0);
    if (tid < 0) { t->used = 0; sched_irq_restore(f); return -1; }
    t->tid = tid;
    sched_set_owner(tid, sched_owner_current());

    sched_irq_restore(f);
    serial_printf("[thread] olustur: %s (tid=%d)\n", t->name, tid);
    return tid;
}

int kthread_join(kthread_t tid) {
    for (;;) {
        uint64_t f = sched_irq_save();
        kthread_rec_t *r = kthread_rec(tid);
        if (!r) { sched_irq_restore(f); return -1; }
        if (r->done) {
            while (sched_state(r->tid) != TASK_DONE) {
                sched_irq_restore(f);
                sched_yield();
                f = sched_irq_save();
            }
            sched_reap(r->tid);
            r->used = 0;
            sched_irq_restore(f);
            return 0;
        }
        waitq_add(&r->joiners, sched_current());
        sched_block_current();
        sched_irq_restore(f);
    }
}

void kthread_exit(void) {
    uint64_t f = sched_irq_save();
    sched_tid_t self = sched_current();
    kthread_rec_t *r = kthread_rec(self);
    if (r) {
        r->done = 1;
        int w;
        while ((w = waitq_pop(&r->joiners)) >= 0) sched_wake(w);
    }
    (void)f;
    sched_exit();
}

kthread_t kthread_self(void) {
    return sched_current();
}

int kthread_count(void) {
    int n = 0;
    for (int i = 0; i < KTHREAD_MAX; i++) if (s_kth[i].used) n++;
    return n;
}

static volatile int s_thread_flag;

static void thread_probe(void *arg) {
    (void)arg;
    for (volatile int i = 0; i < 100000; i++) __asm__ volatile("pause");
    s_thread_flag = 7;
}

int kthread_selftest(void) {
    serial_puts("[thread] ==== THREAD SELF-TEST ====\n");
    s_thread_flag = 0;
    kthread_t t = kthread_create("prob", thread_probe, 0);
    if (t < 0) {
        serial_puts("[thread] olusturma basarisiz\n");
        return -1;
    }
    kthread_join(t);
    int ok = (s_thread_flag == 7);
    serial_printf("[thread] join sonucu: bayrak=%d -> %s\n",
                  s_thread_flag, ok ? "OK" : "HATA");
    return ok ? 0 : -1;
}

int lab_selftest(void) {
    int a = sync_selftest();
    int b = kthread_selftest();
    int c = ipc_selftest();
    int ok = (a == 0 && b == 0 && c == 0);
    serial_printf("[lab] TUMU: sync=%d thread=%d ipc=%d -> %s\n",
                  a, b, c, ok ? "OK" : "HATA");
    return ok ? 0 : -1;
}
