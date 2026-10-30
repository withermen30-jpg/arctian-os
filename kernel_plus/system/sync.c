#include "sync.h"
#include "thread.h"
#include "serial.h"

void waitq_init(waitq_t *q) {
    q->n = 0;
}

void waitq_add(waitq_t *q, int tid) {
    if (q->n < WAITQ_MAX) q->tids[q->n++] = tid;
}

int waitq_pop(waitq_t *q) {
    if (q->n <= 0) return -1;
    int t = q->tids[0];
    for (int i = 1; i < q->n; i++) q->tids[i - 1] = q->tids[i];
    q->n--;
    return t;
}

int waitq_remove(waitq_t *q, int tid) {
    for (int i = 0; i < q->n; i++) {
        if (q->tids[i] != tid) continue;
        for (int j = i + 1; j < q->n; j++) q->tids[j - 1] = q->tids[j];
        q->n--;
        return 1;
    }
    return 0;
}

void spin_init(spinlock_t *l) { l->v = 0; }

void spin_lock(spinlock_t *l) {
    while (__atomic_exchange_n(&l->v, 1, __ATOMIC_ACQUIRE)) {
        while (l->v) __asm__ volatile("pause" ::: "memory");
    }
}

void spin_unlock(spinlock_t *l) {
    __atomic_store_n(&l->v, 0, __ATOMIC_RELEASE);
}

void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner = -1;
    waitq_init(&m->wq);
}

int mutex_trylock(mutex_t *m) {
    uint64_t f = sched_irq_save();
    int ok = 0;
    if (!m->locked) {
        m->locked = 1;
        m->owner = sched_current();
        ok = 1;
    }
    sched_irq_restore(f);
    return ok;
}

void mutex_lock(mutex_t *m) {
    for (;;) {
        uint64_t f = sched_irq_save();
        if (!m->locked) {
            m->locked = 1;
            m->owner = sched_current();
            sched_irq_restore(f);
            return;
        }
        waitq_add(&m->wq, sched_current());
        sched_block_current();
        sched_irq_restore(f);
    }
}

void mutex_unlock(mutex_t *m) {
    uint64_t f = sched_irq_save();
    m->locked = 0;
    m->owner = -1;
    int w = waitq_pop(&m->wq);
    if (w >= 0) sched_wake(w);
    sched_irq_restore(f);
}

void sem_init(sem_t *s, int value) {
    s->count = value;
    waitq_init(&s->wq);
}

int sem_trywait(sem_t *s) {
    uint64_t f = sched_irq_save();
    int ok = 0;
    if (s->count > 0) { s->count--; ok = 1; }
    sched_irq_restore(f);
    return ok;
}

void sem_wait(sem_t *s) {
    for (;;) {
        uint64_t f = sched_irq_save();
        if (s->count > 0) {
            s->count--;
            sched_irq_restore(f);
            return;
        }
        waitq_add(&s->wq, sched_current());
        sched_block_current();
        sched_irq_restore(f);
    }
}

void sem_post(sem_t *s) {
    uint64_t f = sched_irq_save();
    s->count++;
    int w = waitq_pop(&s->wq);
    if (w >= 0) sched_wake(w);
    sched_irq_restore(f);
}

static volatile int s_sync_counter;
static mutex_t s_sync_mutex;

static void sync_add_task(void *arg) {
    (void)arg;
    for (int i = 0; i < 20000; i++) {
        mutex_lock(&s_sync_mutex);
        s_sync_counter++;
        mutex_unlock(&s_sync_mutex);
    }
}

static sem_t s_sync_sem;
static volatile int s_sync_got;

static void sync_sem_task(void *arg) {
    (void)arg;
    sem_wait(&s_sync_sem);
    s_sync_got = 42;
}

int sync_selftest(void) {
    serial_puts("[sync] ==== SENKRONIZASYON SELF-TEST ====\n");

    s_sync_counter = 0;
    mutex_init(&s_sync_mutex);
    kthread_t t1 = kthread_create("ekle-1", sync_add_task, 0);
    kthread_t t2 = kthread_create("ekle-2", sync_add_task, 0);
    if (t1 < 0 || t2 < 0) {
        serial_puts("[sync] mutex testi: thread olusturulamadi\n");
        return -1;
    }
    kthread_join(t1);
    kthread_join(t2);
    int mok = (s_sync_counter == 40000);
    serial_printf("[sync] mutex: sayac=%d (beklenen 40000) -> %s\n",
                  s_sync_counter, mok ? "OK" : "HATA");

    s_sync_got = 0;
    sem_init(&s_sync_sem, 0);
    kthread_t t3 = kthread_create("semafor", sync_sem_task, 0);
    sched_sleep_ms(1);
    sem_post(&s_sync_sem);
    kthread_join(t3);
    int sok = (s_sync_got == 42);
    serial_printf("[sync] semafor: deger=%d (beklenen 42) -> %s\n",
                  s_sync_got, sok ? "OK" : "HATA");

    int ok = mok && sok;
    serial_printf("[sync] selftest %s\n", ok ? "OK" : "HATA");
    return ok ? 0 : -1;
}
