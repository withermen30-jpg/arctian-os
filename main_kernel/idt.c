/**
 * Arctian IDT (Interrupt Descriptor Table)
 * 64-bit AMD64 - 256 entry, 16 bytes each
 */
#include "idt.h"
#include "kernel.h"

// ISR stub table from isr.asm (48 entries: 0-31 exceptions, 32-47 IRQs)
extern uint64_t isr_stub_table[48];

static idt_entry_t idt[IDT_ENTRIES] __attribute__((aligned(16)));
static idt_ptr_t idt_ptr;

void idt_set_gate(int num, uint64_t base, uint16_t selector, uint8_t type_attr) {
    idt[num].offset_low  = base & 0xFFFF;
    idt[num].offset_mid  = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector    = selector;
    idt[num].ist         = 0;
    idt[num].type_attr   = type_attr;
    idt[num].reserved    = 0;
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
    idt_ptr.base  = (uint64_t)&idt;

    // Clear all entries
    memset(&idt, 0, sizeof(idt));

    // Exception gates 0-31 (trap gates so we can chain debug)
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, isr_stub_table[i], 0x08, IDT_TRAP_GATE);
    }

    // IRQ gates 32-47 (interrupt gates, auto-clears IF)
    for (int i = 0; i < 16; i++) {
        idt_set_gate(32 + i, isr_stub_table[32 + i], 0x08, IDT_INTERRUPT_GATE);
    }

    // Load IDT
    asm volatile ("lidt %0" : : "m"(idt_ptr));

    kernel_debug("IDT basariyla yuklendi (256 entry)");
}
