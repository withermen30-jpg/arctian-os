#include "ipc.h"
#include "thread.h"
#include "serial.h"

static void mem_copy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

void pipe_init(pipe_t *p, uint32_t cap) {
    if (cap == 0 || cap > PIPE_BUF_MAX) cap = PIPE_BUF_MAX;
    p->cap = cap;
    p->head = p->tail = p->count = 0;
    p->closed = 0;
    waitq_init(&p->rd_wq);
    waitq_init(&p->wr_wq);
}

int pipe_write(pipe_t *p, const void *src, uint32_t n) {
    const uint8_t *s = (const uint8_t *)src;
    uint32_t done = 0;
    while (done < n) {
        uint64_t f = sched_irq_save();
        uint32_t space = p->cap - p->count;
        if (space > 0) {
            uint32_t chunk = n - done;
            if (chunk > space) chunk = space;
            for (uint32_t i = 0; i < chunk; i++) {
                p->buf[p->tail] = s[done + i];
                p->tail = (p->tail + 1) % p->cap;
            }
            p->count += chunk;
            done += chunk;
            int w = waitq_pop(&p->rd_wq);
            if (w >= 0) sched_wake(w);
            sched_irq_restore(f);
            continue;
        }
        if (p->closed) { sched_irq_restore(f); return (int)done; }
        waitq_add(&p->wr_wq, sched_current());
        sched_block_current();
        sched_irq_restore(f);
    }
    return (int)done;
}

int pipe_read(pipe_t *p, void *dst, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    uint32_t done = 0;
    while (done < n) {
        uint64_t f = sched_irq_save();
        if (p->count > 0) {
            uint32_t chunk = n - done;
            if (chunk > p->count) chunk = p->count;
            for (uint32_t i = 0; i < chunk; i++) {
                d[done + i] = p->buf[p->head];
                p->head = (p->head + 1) % p->cap;
            }
            p->count -= chunk;
            done += chunk;
            int w = waitq_pop(&p->wr_wq);
            if (w >= 0) sched_wake(w);
            sched_irq_restore(f);
            continue;
        }
        if (p->closed) { sched_irq_restore(f); return (int)done; }
        waitq_add(&p->rd_wq, sched_current());
        sched_block_current();
        sched_irq_restore(f);
    }
    return (int)done;
}

void pipe_close(pipe_t *p) {
    uint64_t f = sched_irq_save();
    p->closed = 1;
    int w;
    while ((w = waitq_pop(&p->rd_wq)) >= 0) sched_wake(w);
    while ((w = waitq_pop(&p->wr_wq)) >= 0) sched_wake(w);
    sched_irq_restore(f);
}

void mq_init(mq_t *q, uint32_t msg_size, uint32_t cap) {
    if (msg_size == 0 || msg_size > MQ_MSG_MAX) msg_size = MQ_MSG_MAX;
    if (cap == 0 || cap > MQ_MAX_SLOTS) cap = MQ_MAX_SLOTS;
    q->msg_size = msg_size;
    q->cap = cap;
    q->head = q->count = 0;
    q->closed = 0;
    waitq_init(&q->rd_wq);
    waitq_init(&q->wr_wq);
}

int mq_send(mq_t *q, const void *msg) {
    for (;;) {
        uint64_t f = sched_irq_save();
        if (q->count < q->cap) {
            uint32_t slot = (q->head + q->count) % q->cap;
            mem_copy(q->slots[slot], (const uint8_t *)msg, q->msg_size);
            q->count++;
            int w = waitq_pop(&q->rd_wq);
            if (w >= 0) sched_wake(w);
            sched_irq_restore(f);
            return 0;
        }
        if (q->closed) { sched_irq_restore(f); return -1; }
        waitq_add(&q->wr_wq, sched_current());
        sched_block_current();
        sched_irq_restore(f);
    }
}

int mq_recv(mq_t *q, void *msg) {
    for (;;) {
        uint64_t f = sched_irq_save();
        if (q->count > 0) {
            mem_copy((uint8_t *)msg, q->slots[q->head], q->msg_size);
            q->head = (q->head + 1) % q->cap;
            q->count--;
            int w = waitq_pop(&q->wr_wq);
            if (w >= 0) sched_wake(w);
            sched_irq_restore(f);
            return 0;
        }
        if (q->closed) { sched_irq_restore(f); return -1; }
        waitq_add(&q->rd_wq, sched_current());
        sched_block_current();
        sched_irq_restore(f);
    }
}

void mq_close(mq_t *q) {
    uint64_t f = sched_irq_save();
    q->closed = 1;
    int w;
    while ((w = waitq_pop(&q->rd_wq)) >= 0) sched_wake(w);
    while ((w = waitq_pop(&q->wr_wq)) >= 0) sched_wake(w);
    sched_irq_restore(f);
}

typedef struct {
    uint8_t data[SHM_REGION_MAX];
    uint32_t size;
    int refs;
    int used;
} shm_reg_t;

static shm_reg_t s_shm[SHM_MAX_REGIONS];

static shm_reg_t *shm_find(void *p) {
    for (int i = 0; i < SHM_MAX_REGIONS; i++)
        if (s_shm[i].used && s_shm[i].data == (uint8_t *)p) return &s_shm[i];
    return 0;
}

void *shm_alloc(uint32_t size) {
    if (size == 0 || size > SHM_REGION_MAX) return 0;
    uint64_t f = sched_irq_save();
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (s_shm[i].used) continue;
        s_shm[i].used = 1;
        s_shm[i].refs = 1;
        s_shm[i].size = size;
        sched_irq_restore(f);
        return s_shm[i].data;
    }
    sched_irq_restore(f);
    return 0;
}

int shm_retain(void *p) {
    shm_reg_t *r = shm_find(p);
    if (!r) return -1;
    uint64_t f = sched_irq_save();
    r->refs++;
    sched_irq_restore(f);
    return r->refs;
}

int shm_release(void *p) {
    shm_reg_t *r = shm_find(p);
    if (!r) return -1;
    uint64_t f = sched_irq_save();
    if (--r->refs <= 0) {
        r->used = 0;
        r->refs = 0;
        r->size = 0;
    }
    int refs = r->refs;
    sched_irq_restore(f);
    return refs;
}

int shm_size(void *p) {
    shm_reg_t *r = shm_find(p);
    return r ? (int)r->size : -1;
}

int shm_count(void) {
    int n = 0;
    for (int i = 0; i < SHM_MAX_REGIONS; i++) if (s_shm[i].used) n++;
    return n;
}

#define PC_COUNT 200

static pipe_t s_pc_pipe;
static volatile int s_pc_sum;

static void pc_producer(void *arg) {
    (void)arg;
    for (int i = 1; i <= PC_COUNT; i++) {
        int v = i;
        pipe_write(&s_pc_pipe, &v, sizeof(v));
    }
    pipe_close(&s_pc_pipe);
}

static void pc_consumer(void *arg) {
    (void)arg;
    int v;
    int sum = 0;
    while (pipe_read(&s_pc_pipe, &v, sizeof(v)) == (int)sizeof(v)) sum += v;
    s_pc_sum = sum;
}

static mq_t s_pc_mq;

static void mq_sender(void *arg) {
    (void)arg;
    for (int i = 0; i < 8; i++) {
        int v = i * 10;
        mq_send(&s_pc_mq, &v);
    }
    mq_close(&s_pc_mq);
}

static volatile int s_mq_sum;

static void mq_receiver(void *arg) {
    (void)arg;
    int v;
    int sum = 0;
    while (mq_recv(&s_pc_mq, &v) == 0) sum += v;
    s_mq_sum = sum;
}

static volatile int s_shm_seen;

static void shm_writer(void *arg) {
    uint8_t *p = (uint8_t *)arg;
    for (int i = 0; i < 64; i++) p[i] = (uint8_t)(i ^ 0x5A);
    shm_release(p);
}

int ipc_selftest(void) {
    serial_puts("[ipc] ==== IPC SELF-TEST ====\n");

    pipe_init(&s_pc_pipe, 256);
    s_pc_sum = 0;
    kthread_t prod = kthread_create("uretici", pc_producer, 0);
    kthread_t cons = kthread_create("tuketici", pc_consumer, 0);
    kthread_join(prod);
    kthread_join(cons);
    int expect = PC_COUNT * (PC_COUNT + 1) / 2;
    int pok = (s_pc_sum == expect);
    serial_printf("[ipc] pipe uretici/tuketici: toplam=%d (beklenen %d) -> %s\n",
                  s_pc_sum, expect, pok ? "OK" : "HATA");

    mq_init(&s_pc_mq, sizeof(int), 4);
    s_mq_sum = 0;
    kthread_t ms = kthread_create("mesaj-gonderen", mq_sender, 0);
    kthread_t mr = kthread_create("mesaj-alan", mq_receiver, 0);
    kthread_join(ms);
    kthread_join(mr);
    int mexpect = 0;
    for (int i = 0; i < 8; i++) mexpect += i * 10;
    int mok = (s_mq_sum == mexpect);
    serial_printf("[ipc] mesaj kuyrugu: toplam=%d (beklenen %d) -> %s\n",
                  s_mq_sum, mexpect, mok ? "OK" : "HATA");

    void *shm = shm_alloc(64);
    s_shm_seen = 0;
    int sok = 0;
    if (shm) {
        shm_retain(shm);
        kthread_t sw = kthread_create("shm-yazan", shm_writer, shm);
        kthread_join(sw);
        uint8_t *q = (uint8_t *)shm;
        sok = 1;
        for (int i = 0; i < 64; i++)
            if (q[i] != (uint8_t)(i ^ 0x5A)) { sok = 0; break; }
        shm_release(shm);
        s_shm_seen = shm_count();
    }
    serial_printf("[ipc] paylasimli bellek: dogrulama=%s, bolge=%d\n",
                  sok ? "OK" : "HATA", s_shm_seen);

    int ok = pok && mok && sok;
    serial_printf("[ipc] selftest %s\n", ok ? "OK" : "HATA");
    return ok ? 0 : -1;
}
