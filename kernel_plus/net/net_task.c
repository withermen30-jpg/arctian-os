#include "net_task.h"
#include "sched.h"
#include "sync.h"
#include "caps.h"

#define NET_TASK_STACK_SIZE (256 * 1024)

static uint8_t s_stack[NET_TASK_STACK_SIZE] __attribute__((aligned(16)));
static sem_t s_job;
static volatile int s_state;
static void (*s_fn)(void *);
static asi_net_done_t s_done;
static void *s_user;
static uint32_t s_caps;

enum {
    NET_TASK_IDLE = 0,
    NET_TASK_RUNNING,
    NET_TASK_DONE
};

static void net_task_worker(void *arg) {
    (void)arg;
    for (;;) {
        sem_wait(&s_job);
        if (s_state != NET_TASK_RUNNING) continue;
        void (*fn)(void *) = s_fn;
        void *user = s_user;
        uint32_t caps = s_caps;
        if (fn) { caps_set(caps); fn(user); caps_clear(); }
        uint64_t f = sched_irq_save();
        s_state = NET_TASK_DONE;
        sched_irq_restore(f);
    }
}

void net_task_init(void) {
    s_state = NET_TASK_IDLE;
    s_fn = 0;
    s_done = 0;
    s_user = 0;
    s_caps = CAP_ALL;
    sem_init(&s_job, 0);
    sched_spawn_stack("netbg", net_task_worker, 0, 0, s_stack, sizeof(s_stack));
}

int net_task_available(void) { return 1; }

int net_task_busy(void) {
    return s_state == NET_TASK_RUNNING ? 1 : 0;
}

int net_task_submit(void (*fn)(void *), asi_net_done_t done, void *user) {
    if (!fn) return -1;
    uint64_t f = sched_irq_save();
    if (s_state != NET_TASK_IDLE) { sched_irq_restore(f); return -1; }
    s_fn = fn;
    s_done = done;
    s_user = user;
    s_caps = caps_current();
    s_state = NET_TASK_RUNNING;
    sched_irq_restore(f);
    sem_post(&s_job);
    return 0;
}

void net_task_yield_point(void) {
    if (s_state == NET_TASK_RUNNING) sched_yield();
}

void net_task_service(void) {
    uint64_t f = sched_irq_save();
    if (s_state != NET_TASK_DONE) { sched_irq_restore(f); return; }
    asi_net_done_t done = s_done;
    void *user = s_user;
    s_state = NET_TASK_IDLE;
    s_done = 0;
    s_fn = 0;
    s_user = 0;
    sched_irq_restore(f);
    if (done) done(0, user);
}
