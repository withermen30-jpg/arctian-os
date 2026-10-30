#include "caps.h"
#include "proc.h"
#include "sched.h"
#include "serial.h"
#include "syscall.h"

static uint32_t s_over[SCHED_MAX_TASKS];
static uint8_t  s_has[SCHED_MAX_TASKS];
static uint32_t s_uid_t[SCHED_MAX_TASKS];
static uint32_t s_gid_t[SCHED_MAX_TASKS];
static int      s_err = 0;

/* ---- 0.6 uyumlulugu: gorev basina yetki maskesi ---- */
static int cur_task(void) {
    int t = sched_current();
    if (t < 0 || t >= SCHED_MAX_TASKS) return 0;
    return t;
}

static uint32_t effective(void) {
    int t = cur_task();
    if (s_has[t]) return s_over[t];
    uint32_t c;
    if (proc_owner_caps(&c)) return c;
    return CAP_ALL;
}

void caps_set(uint32_t mask) {
    int t = cur_task();
    s_over[t] = mask;
    s_has[t] = 1;
}

void caps_clear(void) {
    int t = cur_task();
    s_has[t] = 0;
    s_uid_t[t] = 0;
    s_gid_t[t] = 0;
}

void caps_enter(int pid) {
    if (pid <= 0) { caps_clear(); return; }
    caps_set(proc_caps_of(pid));
    int t = cur_task();
    s_uid_t[t] = proc_uid_of(pid);
    s_gid_t[t] = proc_gid_of(pid);
}

uint32_t caps_current(void) { return effective(); }

void caps_set_uid(int uid, int gid) {
    if (effective() != CAP_ALL) return;
    int t = cur_task();
    s_uid_t[t] = (uint32_t)uid;
    s_gid_t[t] = (uint32_t)gid;
}

uint32_t caps_uid(void) { return s_uid_t[cur_task()]; }
uint32_t caps_gid(void) { return s_gid_t[cur_task()]; }

void caps_syscall_subject(uint32_t *uid, uint32_t *gid) {
    uint32_t u = 0, g = 0;
    proc_owner_ids(&u, &g);
    if (uid) *uid = u;
    if (gid) *gid = g;
}

int caps_check(uint32_t cap) {
    if ((effective() & cap) == cap) return 0;
    s_err = SANDBOX_EPERM;
    return SANDBOX_EPERM;
}

int caps_last_error(void) { return s_err; }

int caps_syscall_check(uint32_t cap) {
    uint32_t c;
    if (!proc_owner_caps(&c)) c = CAP_ALL;
    return ((c & cap) == cap) ? 0 : SANDBOX_EPERM;
}

/* ---------------------------------------------------------------- */
/* 0.7: ozne + politika                                               */
/*                                                                    */
/* Kutulu bir uygulama acikken ozne kaydedilir. Politika yalnizca      */
/* uygulama kodu calisirken (caps_dispatch_begin/end arasinda) gecer;  */
/* ayni is parcacigindaki masaustu kodu etkilenmez.                    */
/* ---------------------------------------------------------------- */
static caps_policy_fn s_policy;
static int   s_subj_on;
static int   s_subj_task = -1;
static int   s_subj_trusted;
static int   s_dispatch;
static char  s_subj_id[48];
static char  s_subj_title[48];
static char  s_subj_home[80];

static void cp_s(char *d, int cap, const char *s) {
    int i = 0;
    if (cap <= 0) return;
    if (!s) { d[0] = 0; return; }
    for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}

void caps_set_policy(caps_policy_fn fn) { s_policy = fn; }

/* ASI arayuzu: masaustu politikayi buradan baglar (donus 0). */
static int caps_set_policy_asi(int (*fn)(const char *path, int is_write)) {
    s_policy = (caps_policy_fn)fn;
    return 0;
}

void caps_subject_begin(const char *id, const char *title,
                        const char *home, int trusted) {
    cp_s(s_subj_id, sizeof(s_subj_id), id);
    cp_s(s_subj_title, sizeof(s_subj_title), title);
    cp_s(s_subj_home, sizeof(s_subj_home), home);
    s_subj_trusted = trusted ? 1 : 0;
    s_subj_on = 1;
    s_subj_task = sched_current();
    s_dispatch = 0;
}

void caps_subject_end(void) {
    s_subj_on = 0;
    s_subj_task = -1;
    s_subj_trusted = 0;
    s_subj_id[0] = 0;
    s_subj_title[0] = 0;
    s_subj_home[0] = 0;
    s_dispatch = 0;
}

const char *caps_subject_id(void)    { return s_subj_on ? s_subj_id : ""; }
const char *caps_subject_title(void) { return s_subj_on ? s_subj_title : ""; }
const char *caps_subject_home(void)  { return s_subj_on ? s_subj_home : ""; }
int caps_subject_trusted(void)       { return s_subj_on ? s_subj_trusted : 1; }
int caps_subject_active(void)        { return s_subj_on; }

void caps_dispatch_begin(void) { s_dispatch++; }
void caps_dispatch_end(void)   { if (s_dispatch > 0) s_dispatch--; }

static int gate_open(void) {
    return s_subj_on && s_dispatch > 0 && s_policy &&
           sched_current() == s_subj_task;
}

int caps_dispatch_active(void) { return gate_open(); }

/* Politika kapaliyken eski davranis: CAP_FS denetimi. */
static int gate(const char *path, int is_write) {
    if (!gate_open()) return 0;
    if (s_policy(path, is_write)) { s_err = SANDBOX_EPERM; return SANDBOX_EPERM; }
    return 0;
}

static int gate_denied(void) { s_err = SANDBOX_EPERM; return SANDBOX_EPERM; }

/* ---------------------------------------------------------------- */
/* Tutamac -> yol baglantisi: tutamac acildigi yola baglidir.       */
/* Boylece read/write cagrilari da kapidan gecer.                   */
/* ---------------------------------------------------------------- */
#define HP_MAX 64
static struct { void *h; char path[160]; } s_hp[HP_MAX];

static void hp_put(void *h, const char *path) {
    if (!h || !path) return;
    for (int i = 0; i < HP_MAX; i++) {
        if (s_hp[i].h == h) { cp_s(s_hp[i].path, sizeof(s_hp[i].path), path); return; }
    }
    for (int i = 0; i < HP_MAX; i++) {
        if (!s_hp[i].h) {
            s_hp[i].h = h;
            cp_s(s_hp[i].path, sizeof(s_hp[i].path), path);
            return;
        }
    }
}

static const char *hp_get(void *h) {
    if (!h) return 0;
    for (int i = 0; i < HP_MAX; i++) if (s_hp[i].h == h) return s_hp[i].path;
    return 0;
}

static void join_path(char *out, int cap, const char *dir, const char *name) {
    int n = 0;
    if (dir) for (int i = 0; dir[i] && n < cap - 2; i++) out[n++] = dir[i];
    if (n > 0 && out[n - 1] != '/' && n < cap - 2) out[n++] = '/';
    if (name) for (int i = 0; name[i] && n < cap - 2; i++) out[n++] = name[i];
    out[n] = 0;
}

static asi_fs_t s_guard;
static asi_fs_t s_save;

static void guard_subject(void) {
    if (s_save.set_subject) s_save.set_subject((int)caps_uid(), (int)caps_gid());
}

static void *g_root(void) {
    if (gate_open()) {
        if (gate("C:/", 0)) return 0;
    } else if (caps_check(CAP_FS)) return 0;
    guard_subject();
    void *h = s_save.root();
    if (h) hp_put(h, "C:/");
    return h;
}

static void *g_create(void *d, const char *n, int is_dir) {
    char p[176];
    join_path(p, sizeof(p), hp_get(d), n);
    if (gate_open()) {
        if (gate(p, 1)) return 0;
    } else if (caps_check(CAP_FS)) return 0;
    guard_subject();
    void *h = s_save.create(d, n, is_dir);
    if (h) hp_put(h, p);
    return h;
}

static void *g_find(void *d, const char *n) {
    char p[176];
    join_path(p, sizeof(p), hp_get(d), n);
    if (gate_open()) {
        if (gate(p, 0)) return 0;
    } else if (caps_check(CAP_FS)) return 0;
    guard_subject();
    void *h = s_save.find(d, n);
    if (h) hp_put(h, p);
    return h;
}

static int g_write(void *f, const void *data, uint32_t size) {
    const char *p = hp_get(f);
    if (gate_open()) {
        if (gate(p, 1)) return -1;
    } else if (caps_check(CAP_FS)) return -1;
    guard_subject();
    return s_save.write(f, data, size);
}

static uint32_t g_read(void *f, void *buf, uint32_t size) {
    const char *p = hp_get(f);
    if (gate_open()) {
        if (gate(p, 0)) return 0;
    } else if (caps_check(CAP_FS)) return 0;
    guard_subject();
    return s_save.read(f, buf, size);
}

static int g_count(void) {
    if (gate_open()) { if (gate("C:/", 0)) return 0; }
    else if (caps_check(CAP_FS)) return 0;
    guard_subject();
    return s_save.count();
}

static const char *g_name(int idx) {
    if (gate_open()) { if (gate("C:/", 0)) return 0; }
    else if (caps_check(CAP_FS)) return 0;
    guard_subject();
    return s_save.name(idx);
}

static uint32_t g_size(int idx) {
    if (gate_open()) { if (gate("C:/", 0)) return 0; }
    else if (caps_check(CAP_FS)) return 0;
    guard_subject();
    return s_save.size(idx);
}

static int g_remove(const char *n) {
    if (gate_open()) { if (gate(n, 1)) return -1; }
    else if (caps_check(CAP_FS)) return -1;
    guard_subject();
    return s_save.remove(n);
}

static int g_mkdir(const char *p) {
    if (gate_open()) { if (gate(p, 1)) return -1; }
    else if (caps_check(CAP_FS)) return -1;
    guard_subject();
    return s_save.mkdir(p);
}

static void *g_create_in(const char *p, const char *n, int is_dir) {
    char full[176];
    join_path(full, sizeof(full), p, n);
    if (gate_open()) {
        if (gate(full, 1)) return 0;
    } else if (caps_check(CAP_FS)) return 0;
    guard_subject();
    void *h = s_save.create_in(p, n, is_dir);
    if (h) hp_put(h, full);
    return h;
}

static void *g_find_in(const char *p, const char *n) {
    char full[176];
    join_path(full, sizeof(full), p, n);
    if (gate_open()) {
        if (gate(full, 0)) return 0;
    } else if (caps_check(CAP_FS)) return 0;
    guard_subject();
    void *h = s_save.find_in(p, n);
    if (h) hp_put(h, full);
    return h;
}

static uint32_t g_read_path(const char *p, void *buf, uint32_t size) {
    if (gate_open()) { if (gate(p, 0)) return 0; }
    else if (caps_check(CAP_FS)) return 0;
    guard_subject();
    return s_save.read_path(p, buf, size);
}

static int g_count_dir(const char *p) {
    if (gate_open()) { if (gate(p, 0)) return 0; }
    else if (caps_check(CAP_FS)) return 0;
    guard_subject();
    return s_save.count_dir(p);
}

static int g_list_dir(const char *p, int idx, char *out, int cap, int *is_dir) {
    if (gate_open()) { if (gate(p, 0)) return -1; }
    else if (caps_check(CAP_FS)) return -1;
    guard_subject();
    return s_save.list_dir(p, idx, out, cap, is_dir);
}

static int g_stat(const char *p, asi_stat_t *out) {
    if (gate_open()) { if (gate(p, 0)) return -1; }
    else if (caps_check(CAP_FS)) return -1;
    guard_subject();
    return s_save.stat ? s_save.stat(p, out) : -1;
}

static int g_stat_handle(void *f, asi_stat_t *out) {
    const char *p = hp_get(f);
    if (gate_open()) { if (gate(p, 0)) return -1; }
    else if (caps_check(CAP_FS)) return -1;
    guard_subject();
    return s_save.stat_handle ? s_save.stat_handle(f, out) : -1;
}

static int g_chmod(const char *p, uint32_t mode) {
    if (gate_open()) { if (gate(p, 1)) return -1; }
    else if (caps_check(CAP_FS)) return -1;
    guard_subject();
    return s_save.chmod ? s_save.chmod(p, mode) : -1;
}

static int g_chown(const char *p, int uid, int gid) {
    if (gate_open()) { if (gate(p, 1)) return -1; }
    else if (caps_check(CAP_FS)) return -1;
    guard_subject();
    return s_save.chown ? s_save.chown(p, uid, gid) : -1;
}

static void g_set_subject(int uid, int gid) { (void)uid; (void)gid; }

static int g_fsck(int fix, int *problems, int *fixed) {
    if (gate_open()) { if (gate("C:/", 1)) return -1; }
    else if (caps_check(CAP_FS)) return -1;
    guard_subject();
    return s_save.fsck ? s_save.fsck(fix, problems, fixed) : -1;
}

void caps_fs_guard_install(asi_fs_t **slot) {
    if (!slot || !*slot) return;
    s_save = **slot;
    s_guard = **slot;
    s_guard.root = g_root;
    s_guard.create = g_create;
    s_guard.find = g_find;
    s_guard.write = g_write;
    s_guard.read = g_read;
    s_guard.count = g_count;
    s_guard.name = g_name;
    s_guard.size = g_size;
    s_guard.remove = g_remove;
    s_guard.mkdir = g_mkdir;
    s_guard.create_in = g_create_in;
    s_guard.find_in = g_find_in;
    s_guard.read_path = g_read_path;
    s_guard.count_dir = g_count_dir;
    s_guard.list_dir = g_list_dir;
    s_guard.stat = g_stat;
    s_guard.stat_handle = g_stat_handle;
    s_guard.chmod = g_chmod;
    s_guard.chown = g_chown;
    s_guard.set_subject = g_set_subject;
    s_guard.fsck = g_fsck;
    *slot = &s_guard;
}

static asi_sandbox_t s_sandbox;

void caps_init(asi_t *asi) {
    for (int i = 0; i < SCHED_MAX_TASKS; i++) {
        s_over[i] = CAP_ALL;
        s_has[i] = 0;
        s_uid_t[i] = 0;
        s_gid_t[i] = 0;
    }
    s_err = 0;
    s_policy = 0;
    s_subj_on = 0;
    s_subj_task = -1;
    s_subj_trusted = 0;
    s_dispatch = 0;
    s_subj_id[0] = 0;
    s_subj_title[0] = 0;
    s_subj_home[0] = 0;
    for (int i = 0; i < HP_MAX; i++) { s_hp[i].h = 0; s_hp[i].path[0] = 0; }
    s_sandbox.enter = caps_enter;
    s_sandbox.leave = caps_clear;
    s_sandbox.current = caps_current;
    s_sandbox.check = caps_check;
    s_sandbox.last_error = caps_last_error;
    s_sandbox.selftest = caps_selftest;
    s_sandbox.set_uid = caps_set_uid;
    s_sandbox.uid = caps_uid;
    s_sandbox.gid = caps_gid;
    /* 0.7: masaustunun ozne/kapi arayuzu */
    s_sandbox.subject_begin = caps_subject_begin;
    s_sandbox.subject_end = caps_subject_end;
    s_sandbox.subject_id = caps_subject_id;
    s_sandbox.subject_title = caps_subject_title;
    s_sandbox.subject_home = caps_subject_home;
    s_sandbox.subject_trusted = caps_subject_trusted;
    s_sandbox.subject_active = caps_subject_active;
    s_sandbox.dispatch_begin = caps_dispatch_begin;
    s_sandbox.dispatch_end = caps_dispatch_end;
    s_sandbox.dispatch_active = caps_dispatch_active;
    s_sandbox.set_policy = caps_set_policy_asi;
    if (asi) asi->sandbox = &s_sandbox;
    arctian_set_cap_check(caps_syscall_check);
    arctian_set_subject_fn(caps_syscall_subject);
}

int caps_selftest(void) {
    if (!g_asi || !g_asi->net || !g_asi->fs) return -1;
    int net_ok = 0, fs_ok = 0, allow_ok = 0;

    caps_set(CAP_FS);
    uint32_t len = 0;
    const char *b = g_asi->net->http_get("http://arctian.invalid/", &len);
    net_ok = (b == 0 && caps_last_error() == SANDBOX_EPERM);

    caps_set(CAP_NET);
    void *h = g_asi->fs->create(0, "caps_denied.tmp", 0);
    fs_ok = (h == 0 && caps_last_error() == SANDBOX_EPERM);

    caps_set(CAP_ALL);
    void *h2 = g_asi->fs->create(0, "caps_allow.tmp", 0);
    allow_ok = (h2 != 0);
    if (h2) g_asi->fs->remove("caps_allow.tmp");

    caps_clear();
    serial_printf("[caps] sandbox selftest: net_eprem=%s fs_eprem=%s izinli_fs=%s -> %s\n",
                  net_ok ? "EVET" : "HAYIR",
                  fs_ok ? "EVET" : "HAYIR",
                  allow_ok ? "EVET" : "HAYIR",
                  (net_ok && fs_ok && allow_ok) ? "OK" : "HATA");
    return (net_ok && fs_ok && allow_ok) ? 0 : -1;
}