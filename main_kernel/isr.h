#ifndef ISR_H
#define ISR_H

#include <stdint.h>

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) registers_t;

typedef void (*irq_handler_t)(void);

void isr_handler(registers_t *regs);

void isr_install_handler(int irq, irq_handler_t handler);

extern const char *exception_names[];

#endif
