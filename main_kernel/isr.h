#ifndef ISR_H
#define ISR_H

#include <stdint.h>
#include "recover.h"

// Register state saved by ISR stubs
typedef struct {
    // Pushed by isr_common_stub (order: last pushed = first in struct)
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    // Pushed by ISR macro
    uint64_t int_no, err_code;
    // Pushed by CPU (iretq frame)
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) registers_t;

// Handler type for IRQs
typedef void (*irq_handler_t)(void);

// Called from isr.asm stub
void isr_handler(registers_t *regs);

// Register an IRQ handler (irq: 0-15)
void isr_install_handler(int irq, irq_handler_t handler);

// Exception names
extern const char *exception_names[];

/* ---- Cokme koruma (crash recovery) ----
 * g_arctian_recover: masaustunun kaydettigi kurtarma noktasi (kernel_plus
 * ayarlar). set_recover_tick: zaman asimi denetimini PIT IRQ'dan cagirmak
 * icin kernel_plus tarafindan kurulur (main_kernel'de saat yok). */
extern arctian_recover_t *g_arctian_recover;
typedef void (*arctian_recover_tick_fn)(void);
void arctian_set_recover_tick(arctian_recover_tick_fn fn);

typedef void (*arctian_sched_tick_fn)(void);
void arctian_set_sched_tick(arctian_sched_tick_fn fn);

typedef void (*arctian_fault_fn)(int int_no, uint64_t err, uint64_t cr2, uint64_t rip);
void arctian_set_fault_handler(arctian_fault_fn fn);

#endif
