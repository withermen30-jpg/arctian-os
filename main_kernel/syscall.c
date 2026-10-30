#include "syscall.h"
#include "idt.h"
#include "serial.h"
#include "memory_map.h"
#include "afs.h"
#include "asi.h"
#include <stdint.h>

extern void isr_stub_128(void);

static void *g_fds[16];
static arctian_cap_check_fn s_cap_check = 0;
static arctian_subject_fn   s_subject_fn = 0;

void arctian_set_cap_check(arctian_cap_check_fn fn) { s_cap_check = fn; }
void arctian_set_subject_fn(arctian_subject_fn fn) { s_subject_fn = fn; }

static int cap_ok(uint32_t cap) {
    if (!s_cap_check) return 1;
    return s_cap_check(cap) == 0;
}

static void apply_subject(void) {
    uint32_t uid = 0, gid = 0;
    if (s_subject_fn) s_subject_fn(&uid, &gid);
    afs_set_subject((int)uid, (int)gid);
}

void syscall_init(void) {
    idt_set_gate(0x80, (uint64_t)isr_stub_128, 0x08, IDT_USER_GATE);
}

static int user_range(const void *p, uint32_t len) {
    uint64_t a = (uint64_t)(uintptr_t)p;
    uint64_t end = a + (uint64_t)len;
    if (end < a) return 0;
    return a >= (uint64_t)VMM_USER_BASE && end <= 0x0000800000000000ull;
}

static int user_cstr(const char *s) {
    uint64_t a = (uint64_t)(uintptr_t)s;
    if (a < (uint64_t)VMM_USER_BASE || a >= 0x0000800000000000ull) return 0;
    uint64_t end = (a & ~0xFFFull) + 0x1000ull;
    for (uint64_t p = a; p < end; p++) {
        if (*(const char *)(uintptr_t)p == 0) return 1;
    }
    return 0;
}

void syscall_dispatch(registers_t *regs) {
    uint64_t num = regs->rax;
    uint64_t a1 = regs->rdi;
    uint64_t a2 = regs->rsi;
    uint64_t a3 = regs->rdx;
    int64_t ret = -1;

    apply_subject();

    switch (num) {
        case SYS_WRITE: {
            const char *buf = (const char *)(uintptr_t)a2;
            uint32_t len = (uint32_t)a3;
            if (!user_range(buf, len)) { ret = -2; break; }
            for (uint32_t i = 0; i < len; i++) serial_putc(buf[i]);
            ret = (int64_t)len;
            break;
        }
        case SYS_PUTCHAR:
            serial_putc((char)a1);
            ret = 0;
            break;
        case SYS_GETPID:
            ret = 1;
            break;
        case SYS_TICKS:
            ret = 0;
            break;
        case SYS_OPEN: {
            const char *path = (const char *)(uintptr_t)a1;
            if (!cap_ok(CAP_FS)) { ret = -1; break; }
            if (!user_cstr(path)) { ret = -2; break; }
            void *h = afs_find_handle(path);
            if (!h) h = afs_create_handle(path);
            if (!h) { ret = -2; break; }
            int slot = -1;
            for (int i = 0; i < 16; i++) { if (!g_fds[i]) { slot = i; break; } }
            if (slot < 0) { ret = -3; break; }
            g_fds[slot] = h;
            ret = slot + 3;
            break;
        }
        case SYS_READ: {
            int fd = (int)a1;
            void *buf = (void *)(uintptr_t)a2;
            uint32_t len = (uint32_t)a3;
            if (!cap_ok(CAP_FS)) { ret = -1; break; }
            if (fd < 3 || fd >= 19 || !g_fds[fd - 3]) { ret = -1; break; }
            if (!user_range(buf, len)) { ret = -2; break; }
            ret = (int64_t)afs_read_handle(g_fds[fd - 3], buf, len);
            break;
        }
        case SYS_CLOSE: {
            int fd = (int)a1;
            if (!cap_ok(CAP_FS)) { ret = -1; break; }
            if (fd < 3 || fd >= 19) { ret = -1; break; }
            g_fds[fd - 3] = 0;
            ret = 0;
            break;
        }
        case SYS_EXIT:
            serial_puts("[syscall] exit\n");
            ret = 0;
            break;
        default:
            ret = -1;
            break;
    }
    regs->rax = (uint64_t)ret;
}
