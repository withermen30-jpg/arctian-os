/**
 * Arctian ISR Handler (C side)
 * Dispatches interrupts from assembly stubs to registered handlers
 */
#include "isr.h"
#include "idt.h"
#include "pic.h"
#include "kernel.h"
#include "serial.h"
#include "paging.h"
#include "syscall.h"
#include "klog.h"

static void serial_hex(uint64_t v) {
    const char *d = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) serial_putc(d[(v >> i) & 0xF]);
}

// Registered IRQ handler table (IRQ 0-15)
static irq_handler_t irq_handlers[16] = { NULL };

// Cokme koruma: masaustunun kaydettigi kurtarma noktasi ve zaman asimi denetimi
arctian_recover_t *g_arctian_recover = NULL;
static arctian_recover_tick_fn s_recover_tick = NULL;
static arctian_sched_tick_fn  s_sched_tick = NULL;
static arctian_fault_fn       s_fault_handler = NULL;

void arctian_set_recover_tick(arctian_recover_tick_fn fn) { s_recover_tick = fn; }
void arctian_set_sched_tick(arctian_sched_tick_fn fn) { s_sched_tick = fn; }
void arctian_set_fault_handler(arctian_fault_fn fn) { s_fault_handler = fn; }

static char *rc_append(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}
static char *rc_hex(char *p, uint64_t v) {
    static const char h[] = "0123456789ABCDEF";
    *p++ = '0'; *p++ = 'x';
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int nib = (int)((v >> i) & 0xF);
        if (nib || started || i == 0) { *p++ = h[nib]; started = 1; }
    }
    return p;
}
static void rc_reason(arctian_recover_t *c, int int_no, uint64_t rip, uint64_t cr2) {
    char *p = c->reason;
    p = rc_append(p, exception_names[int_no]);
    p = rc_append(p, " | komut adresi: ");
    p = rc_hex(p, rip);
    if (int_no == 14) { p = rc_append(p, " | erisilen adres: "); p = rc_hex(p, cr2); }
    *p = 0;
    c->code = 1;
}

// Exception name strings
const char *exception_names[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection",
    "VMM Communication",
    "Security Exception",
    "Reserved"
};

void isr_handler(registers_t *regs) {
    int int_no = (int)regs->int_no;

    if (int_no == 0x80) {
        syscall_dispatch(regs);
        return;
    }

    // Exception (0-31)
    if (int_no < 32) {
        serial_puts("[isr] EXC int=");
        serial_hex((uint64_t)int_no);
        serial_puts(" rip=");
        serial_hex(regs->rip);
        if (int_no == 14) {
            uint64_t cr2;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            serial_puts(" cr2=");
            serial_hex(cr2);
        }
        serial_puts("\n");
        // Page Fault: print CR2 (faulting address) and RIP before panic
        if (int_no == 14) {
            uint64_t cr2;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            kernel_debug("!!! PAGE FAULT !!!");
            kernel_debug("  Fault address (CR2): ");

            char buf[24];
            int pos = 0;
            uint64_t tmp = cr2;
            for (int i = 0; i < 16; i++) {
                int nib = (tmp >> 60) & 0xF;
                buf[pos++] = (nib < 10) ? ('0' + nib) : ('A' + nib - 10);
                tmp <<= 4;
            }
            buf[pos] = '\0';
            kernel_debug(buf);

            kernel_debug("  RIP: ");
            tmp = regs->rip;
            pos = 0;
            for (int i = 0; i < 16; i++) {
                int nib = (tmp >> 60) & 0xF;
                buf[pos++] = (nib < 10) ? ('0' + nib) : ('A' + nib - 10);
                tmp <<= 4;
            }
            buf[pos] = '\0';
            kernel_debug(buf);

            kernel_debug("  Error code:");
            tmp = regs->err_code;
            pos = 0;
            for (int i = 0; i < 16; i++) {
                int nib = (tmp >> 60) & 0xF;
                buf[pos++] = (nib < 10) ? ('0' + nib) : ('A' + nib - 10);
                tmp <<= 4;
            }
            buf[pos] = '\0';
            kernel_debug(buf);

            // Decode error code
            if (regs->err_code & (1 << 0)) kernel_debug("    Cause: page-level protection violation");
            else                           kernel_debug("    Cause: non-present page");
            if (regs->err_code & (1 << 1)) kernel_debug("    Access: write");
            else                           kernel_debug("    Access: read");
            if (regs->err_code & (1 << 2)) kernel_debug("    Origin: user mode");
            else                           kernel_debug("    Origin: kernel mode");
        }

        if (int_no == 14) {
            uint64_t pf_cr2;
            asm volatile("mov %%cr2, %0" : "=r"(pf_cr2));
            vmm_handle_page_fault(pf_cr2, regs->err_code, regs->rip);
        }

        if (s_fault_handler) {
            uint64_t fcr2 = 0;
            if (int_no == 14) asm volatile("mov %%cr2, %0" : "=r"(fcr2));
            s_fault_handler(int_no, regs->err_code, fcr2, regs->rip);
        }

        if (g_arctian_recover && g_arctian_recover->active) {
            uint64_t cr2 = 0;
            if (int_no == 14) asm volatile("mov %%cr2, %0" : "=r"(cr2));
            rc_reason(g_arctian_recover, int_no, regs->rip, cr2);
            arctian_longjmp(g_arctian_recover, 1);
        }

        klog_write(KLOG_FATAL, "panic", exception_names[int_no]);
        klog_flush();
        kernel_panic(exception_names[int_no]);
        // kernel_panic halts, but if it returns, we'd continue
        return;
    }

    // IRQ (32-47): remap to IRQ 0-15
    int irq = int_no - 32;

    // Call registered handler
    if (irq >= 0 && irq < 16 && irq_handlers[irq] != NULL) {
        irq_handlers[irq]();
    }

    // Send EOI to PIC
    pic_send_eoi(irq);

    /* Zaman asimi denetimi: PIT IRQ'si sonrasi. Uygulama yuklemede takildiysa
     * (active + deadline gecti) kurtarma noktasina don. EOI zaten gonderildi. */
    if (irq == 0 && s_recover_tick) s_recover_tick();
    if (irq == 0 && s_sched_tick) s_sched_tick();
}

void isr_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void isr_uninstall_handler(int irq) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = NULL;
    }
}
