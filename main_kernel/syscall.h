#ifndef ARCTIAN_SYSCALL_H
#define ARCTIAN_SYSCALL_H

#include <stdint.h>
#include "isr.h"

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_PUTCHAR 2
#define SYS_GETPID  3
#define SYS_TICKS   4
#define SYS_OPEN    5
#define SYS_READ    6
#define SYS_CLOSE   7

typedef int (*arctian_cap_check_fn)(uint32_t cap);
void arctian_set_cap_check(arctian_cap_check_fn fn);

typedef void (*arctian_subject_fn)(uint32_t *uid, uint32_t *gid);
void arctian_set_subject_fn(arctian_subject_fn fn);

void syscall_init(void);
void syscall_dispatch(registers_t *regs);

#endif
