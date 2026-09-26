#include "idt.h"
#include "kernel.h"

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

    memset(&idt, 0, sizeof(idt));

    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, isr_stub_table[i], 0x08, IDT_TRAP_GATE);
    }

    for (int i = 0; i < 16; i++) {
        idt_set_gate(32 + i, isr_stub_table[32 + i], 0x08, IDT_INTERRUPT_GATE);
    }

    asm volatile ("lidt %0" : : "m"(idt_ptr));

    kernel_debug("IDT basariyla yuklendi (256 entry)");
}
