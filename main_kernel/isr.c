#include "isr.h"
#include "idt.h"
#include "pic.h"
#include "kernel.h"
#include "serial.h"

static void serial_hex(uint64_t v) {
    const char *d = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) serial_putc(d[(v >> i) & 0xF]);
}

static irq_handler_t irq_handlers[16] = { NULL };

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

            if (regs->err_code & (1 << 0)) kernel_debug("    Cause: page-level protection violation");
            else                           kernel_debug("    Cause: non-present page");
            if (regs->err_code & (1 << 1)) kernel_debug("    Access: write");
            else                           kernel_debug("    Access: read");
            if (regs->err_code & (1 << 2)) kernel_debug("    Origin: user mode");
            else                           kernel_debug("    Origin: kernel mode");
        }

        kernel_panic(exception_names[int_no]);
        return;
    }

    int irq = int_no - 32;

    if (irq >= 0 && irq < 16 && irq_handlers[irq] != NULL) {
        irq_handlers[irq]();
    }

    pic_send_eoi(irq);
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
