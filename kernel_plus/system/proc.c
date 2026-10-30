#include "proc.h"
#include "caps.h"
#include "sched.h"
#include "paging.h"
#include "isr.h"
#include "serial.h"

typedef struct {
    int pid;
    int ppid;
    volatile proc_state_t state;
    int exit_code;
    int is_app;
    int waiting;
    uint32_t caps;
    uint32_t uid;
    uint32_t gid;
    char name[32];
    void (*entry)(void *);
    void *arg;
    vmm_space_t space;
    sched_tid_t task;
} pcb_t;

static pcb_t s_procs[PROC_MAX];
static int s_next_pid = 1;
static int s_init_pid = 1;

static void name_copy(char *dst, int cap, const char *src) {
    int i = 0;
    if (cap <= 0) return;
    if (!src) { dst[0] = 0; return; }
    for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

static pcb_t *proc_slot(int i) {
    if (i < 0 || i >= PROC_MAX) return 0;
    return &s_procs[i];
}

static pcb_t *proc_find(int pid) {
    if (pid <= 0) return 0;
    for (int i = 0; i < PROC_MAX; i++)
        if (s_procs[i].state != PROC_UNUSED && s_procs[i].pid == pid)
            return &s_procs[i];
    return 0;
}

static pcb_t *proc_current(void) {
    pcb_t *p = (pcb_t *)sched_owner_current();
    if (p) return p;
    return &s_procs[0];
}

static int proc_alloc_slot(void) {
    for (int i = 1; i < PROC_MAX; i++)
        if (s_procs[i].state == PROC_UNUSED) return i;
    return -1;
}

static void proc_free(pcb_t *p) {
    if (!p) return;
    if (p->task >= 0) sched_reap(p->task);
    p->state = PROC_UNUSED;
    p->task = -1;
    p->space = 0;
    p->entry = 0;
    p->arg = 0;
    p->pid = 0;
}

static void proc_wake_parent(pcb_t *p) {
    pcb_t *par = proc_find(p->ppid);
    if (par && par->waiting) {
        par->waiting = 0;
        sched_wake(par->task);
    }
}

static void proc_thread_main(void *arg);

static int proc_create(const char *name, void (*entry)(void *), void *arg, int ppid) {
    if (!entry) return -1;
    uint64_t fl = sched_irq_save();

    int slot = proc_alloc_slot();
    if (slot < 0) { sched_irq_restore(fl); return -1; }
    pcb_t *p = &s_procs[slot];

    vmm_space_t space = vmm_new_space();
    if (!space) { sched_irq_restore(fl); return -1; }

    p->pid = s_next_pid++;
    p->ppid = ppid > 0 ? ppid : s_init_pid;
    p->exit_code = 0;
    p->is_app = 0;
    p->waiting = 0;
    p->caps = CAP_ALL;
    p->uid = 0;
    p->gid = 0;
    p->entry = entry;
    p->arg = arg;
    p->space = space;
    p->task = -1;
    name_copy(p->name, sizeof(p->name), name ? name : "proc");

    sched_tid_t tid = sched_spawn_ex(p->name, proc_thread_main, p, space);
    if (tid < 0) { proc_free(p); sched_irq_restore(fl); return -1; }
    p->task = tid;
    p->state = PROC_READY;
    sched_set_owner(tid, p);

    sched_irq_restore(fl);
    serial_printf("[proc] surec olustur: %s (pid=%d, ppid=%d)\n",
                  p->name, p->pid, p->ppid);
    return p->pid;
}

static void proc_thread_main(void *arg) {
    pcb_t *p = (pcb_t *)arg;
    if (!p) sched_exit();
    p->state = PROC_RUNNING;
    if (p->entry) p->entry(p->arg);
    proc_exit(0);
}

static void proc_fault_handler(int int_no, uint64_t err, uint64_t cr2, uint64_t rip) {
    (void)err;
    pcb_t *p = (pcb_t *)sched_owner_current();
    if (!p || p->pid <= s_init_pid || p->state != PROC_RUNNING) return;
    p->state = PROC_ZOMBIE;
    p->exit_code = -11;
    serial_printf("[proc] olumcul hata (int=%d cr2=0x%X%08X rip=0x%X%08X) -> pid=%d olduruldu\n",
                  int_no,
                  (unsigned)(cr2 >> 32), (unsigned)cr2,
                  (unsigned)(rip >> 32), (unsigned)rip,
                  p->pid);
    proc_wake_parent(p);
    sched_exit();
}

void proc_init(void) {
    for (int i = 0; i < PROC_MAX; i++) {
        s_procs[i].pid = 0;
        s_procs[i].ppid = 0;
        s_procs[i].state = PROC_UNUSED;
        s_procs[i].exit_code = 0;
        s_procs[i].is_app = 0;
        s_procs[i].waiting = 0;
        s_procs[i].caps = CAP_ALL;
        s_procs[i].uid = 0;
        s_procs[i].gid = 0;
        s_procs[i].name[0] = 0;
        s_procs[i].entry = 0;
        s_procs[i].arg = 0;
        s_procs[i].space = 0;
        s_procs[i].task = -1;
    }
    s_procs[0].pid = 1;
    s_procs[0].ppid = 0;
    s_procs[0].state = PROC_RUNNING;
    name_copy(s_procs[0].name, sizeof(s_procs[0].name), "init");
    s_procs[0].space = vmm_current();
    s_procs[0].task = sched_current();
    sched_set_owner(sched_current(), &s_procs[0]);
    s_init_pid = 1;
    s_next_pid = 2;
    arctian_set_fault_handler(proc_fault_handler);
    serial_puts("[proc] surec yoneticisi aktif (PID tablosu hazir)\n");
}

int proc_spawn(const char *name, void (*entry)(void *), void *arg) {
    pcb_t *cur = proc_current();
    return proc_create(name, entry, arg, cur ? cur->pid : s_init_pid);
}

int proc_fork(const char *name, void (*entry)(void *), void *arg) {
    pcb_t *cur = proc_current();
    return proc_create(name, entry, arg, cur ? cur->pid : s_init_pid);
}

int proc_exec(void (*entry)(void *), void *arg) {
    pcb_t *p = proc_current();
    if (!p) return -1;
    p->entry = entry;
    p->arg = arg;
    if (entry) entry(arg);
    proc_exit(0);
}

void proc_exit(int code) {
    uint64_t fl = sched_irq_save();
    pcb_t *p = proc_current();
    if (p) {
        p->state = PROC_ZOMBIE;
        p->exit_code = code;
        for (int i = 0; i < PROC_MAX; i++) {
            pcb_t *c = proc_slot(i);
            if (c && c->state != PROC_UNUSED && c->ppid == p->pid)
                c->ppid = s_init_pid;
        }
        proc_wake_parent(p);
    }
    (void)fl;
    sched_exit();
}

int proc_wait(int pid, int *code) {
    pcb_t *parent = proc_current();
    for (;;) {
        uint64_t fl = sched_irq_save();
        pcb_t *c = proc_find(pid);
        if (!c) { sched_irq_restore(fl); return -1; }
        if (c->state == PROC_ZOMBIE) {
            int rp = c->pid;
            if (code) *code = c->exit_code;
            proc_free(c);
            sched_irq_restore(fl);
            return rp;
        }
        if (parent) parent->waiting = 1;
        sched_block_current();
        if (parent) parent->waiting = 0;
        sched_irq_restore(fl);
    }
}

int proc_kill(int pid) {
    uint64_t fl = sched_irq_save();
    pcb_t *p = proc_find(pid);
    if (!p) { sched_irq_restore(fl); return -1; }
    if (p->state == PROC_ZOMBIE) { sched_irq_restore(fl); return 0; }
    p->state = PROC_ZOMBIE;
    p->exit_code = -1;
    proc_wake_parent(p);
    sched_irq_restore(fl);
    return 0;
}

int proc_getpid(void) {
    pcb_t *p = proc_current();
    return p ? p->pid : -1;
}

int proc_getppid(void) {
    pcb_t *p = proc_current();
    return p ? p->ppid : -1;
}

int proc_count(void) {
    int n = 0;
    for (int i = 0; i < PROC_MAX; i++)
        if (s_procs[i].state != PROC_UNUSED) n++;
    return n;
}

int proc_info(int i, int *pid, char *name, int cap, int *state, int *ppid, int *code) {
    if (i < 0 || i >= PROC_MAX) return -1;
    pcb_t *p = &s_procs[i];
    if (p->state == PROC_UNUSED) return -1;
    if (pid) *pid = p->pid;
    if (name && cap > 0) name_copy(name, cap, p->name);
    if (state) *state = (int)p->state;
    if (ppid) *ppid = p->ppid;
    if (code) *code = p->exit_code;
    return 0;
}

int proc_info_ex(int i, asi_proc_info_t *out) {
    if (i < 0 || i >= PROC_MAX || !out) return -1;
    pcb_t *p = &s_procs[i];
    if (p->state == PROC_UNUSED) return -1;
    out->pid = p->pid;
    out->ppid = p->ppid;
    out->state = (int)p->state;
    out->exit_code = p->exit_code;
    out->caps = p->caps;
    out->uid = p->uid;
    out->gid = p->gid;
    out->cpu_ticks = sched_task_ticks(p->task);
    name_copy(out->name, sizeof(out->name), p->name);
    return 0;
}

uint32_t proc_sys_ticks(void) {
    return sched_total_ticks();
}

int proc_app_open_caps(const char *title, uint32_t caps) {
    uint64_t fl = sched_irq_save();
    int slot = proc_alloc_slot();
    if (slot < 0) { sched_irq_restore(fl); return -1; }
    pcb_t *p = &s_procs[slot];
    p->pid = s_next_pid++;
    p->ppid = s_init_pid;
    p->state = PROC_RUNNING;
    p->exit_code = 0;
    p->is_app = 1;
    p->waiting = 0;
    p->caps = caps;
    p->uid = (caps == CAP_ALL) ? 0u : 1000u;
    p->gid = (caps == CAP_ALL) ? 0u : 1000u;
    p->entry = 0;
    p->arg = 0;
    p->space = 0;
    p->task = -1;
    name_copy(p->name, sizeof(p->name), title ? title : "uygulama");
    sched_irq_restore(fl);
    serial_printf("[proc] uygulama sureci basladi: %s (pid=%d, yetki=0x%X)\n",
                  p->name, p->pid, (unsigned)caps);
    return p->pid;
}

int proc_app_open(const char *title) {
    return proc_app_open_caps(title, CAP_ALL);
}

uint32_t proc_caps_of(int pid) {
    pcb_t *p = proc_find(pid);
    return p ? p->caps : CAP_ALL;
}

int proc_owner_caps(uint32_t *out) {
    pcb_t *p = (pcb_t *)sched_owner_current();
    if (!p) return 0;
    if (out) *out = p->caps;
    return 1;
}

uint32_t proc_uid_of(int pid) {
    pcb_t *p = proc_find(pid);
    return p ? p->uid : 0u;
}

uint32_t proc_gid_of(int pid) {
    pcb_t *p = proc_find(pid);
    return p ? p->gid : 0u;
}

int proc_owner_ids(uint32_t *uid, uint32_t *gid) {
    pcb_t *p = (pcb_t *)sched_owner_current();
    if (!p) return 0;
    if (uid) *uid = p->uid;
    if (gid) *gid = p->gid;
    return 1;
}

void proc_app_close(int pid, int code) {
    if (pid <= 0) return;
    uint64_t fl = sched_irq_save();
    pcb_t *p = proc_find(pid);
    if (p && p->state != PROC_ZOMBIE) {
        p->state = PROC_ZOMBIE;
        p->exit_code = code;
        proc_wake_parent(p);
    }
    sched_irq_restore(fl);
}

int proc_reap(int *code_out) {
    uint64_t fl = sched_irq_save();
    for (int i = 0; i < PROC_MAX; i++) {
        pcb_t *p = &s_procs[i];
        if (p->state != PROC_ZOMBIE) continue;
        pcb_t *par = proc_find(p->ppid);
        if (par && par->waiting) continue;
        int pid = p->pid;
        int code = p->exit_code;
        serial_printf("[proc] zombie toplandi: %s (pid=%d, kod=%d)\n",
                      p->name, pid, code);
        proc_free(p);
        if (code_out) *code_out = code;
        sched_irq_restore(fl);
        return pid;
    }
    sched_irq_restore(fl);
    return -1;
}

static void proc_worker(void *arg) {
    int id = (int)(intptr_t)arg;
    serial_printf("[proc] isci-%d calisiyor (pid=%d, cr3=0x%X)\n",
                  id, proc_getpid(), (unsigned)(vmm_current() >> 12));
    for (volatile int i = 0; i < 300000; i++) __asm__ volatile("pause");
    serial_printf("[proc] isci-%d bitiyor (kod=%d)\n", id, id * 5);
    proc_exit(id * 5);
}

static void proc_fork_child(void *arg) {
    (void)arg;
    serial_printf("[proc] cocuk surec calisti (pid=%d, ppid=%d)\n",
                  proc_getpid(), proc_getppid());
    proc_exit(42);
}

static void proc_exec_target(void *arg) {
    (void)arg;
    serial_printf("[proc] exec hedefi calisti (pid=%d)\n", proc_getpid());
    proc_exit(9);
}

static void proc_exec_caller(void *arg) {
    (void)arg;
    serial_printf("[proc] exec cagriliyor (pid=%d)\n", proc_getpid());
    proc_exec(proc_exec_target, 0);
}

int proc_selftest(void) {
    serial_puts("[proc] ==== SUREC SELF-TEST ====\n");

    int p1 = proc_spawn("isci-A", proc_worker, (void *)(intptr_t)1);
    int p2 = proc_spawn("isci-B", proc_worker, (void *)(intptr_t)2);
    if (p1 < 0 || p2 < 0) {
        serial_puts("[proc] selftest: spawn basarisiz\n");
        return -1;
    }

    int c1 = -1, c2 = -1;
    int r1 = proc_wait(p1, &c1);
    int r2 = proc_wait(p2, &c2);
    serial_printf("[proc] wait: pid=%d kod=%d, pid=%d kod=%d\n", r1, c1, r2, c2);

    int pf = proc_fork("cocuk", proc_fork_child, 0);
    int cf = -1;
    int rf = proc_wait(pf, &cf);
    serial_printf("[proc] fork/wait: pid=%d kod=%d\n", rf, cf);

    int pe = proc_spawn("execi", proc_exec_caller, 0);
    int ce = -1;
    int re = proc_wait(pe, &ce);
    serial_printf("[proc] exec/wait: pid=%d kod=%d\n", re, ce);

    int reaped = proc_reap(0);
    serial_printf("[proc] ek zombie toplama: %d\n", reaped);

    int ok = (r1 == p1 && c1 == 5 && r2 == p2 && c2 == 10 &&
              rf == pf && cf == 42 && re == pe && ce == 9);
    serial_printf("[proc] selftest %s: spawn/fork/exec/exit/wait + zombie toplama\n",
                  ok ? "OK" : "HATA");
    return ok ? 0 : -1;
}

static void proc_fault_child(void *arg) {
    (void)arg;
    serial_printf("[proc] hatali cocuk calisiyor (pid=%d, cr3=0x%X)\n",
                  proc_getpid(), (unsigned)(vmm_current() >> 12));
    volatile uint64_t *bad = (volatile uint64_t *)(uintptr_t)VMM_USER_BASE;
    *bad = 0xDEADull;
    serial_puts("[proc] HATA: erisim page fault uretmedi\n");
    proc_exit(0);
}

int proc_fault_selftest(void) {
    int pid = proc_spawn("hatali", proc_fault_child, 0);
    if (pid < 0) {
        serial_puts("[proc] bellek koruma selftest: spawn basarisiz\n");
        return -1;
    }
    int code = 0;
    int r = proc_wait(pid, &code);
    int ok = (r == pid && code == -11);
    serial_printf("[proc] bellek koruma selftest: pid=%d kod=%d -> %s\n",
                  r, code, ok ? "OK (surec olduruldu, sistem ayakta)" : "HATA");
    return ok ? 0 : -1;
}
