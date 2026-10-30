#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#define IDT_ENTRIES 256

// IDT gate descriptor (64-bit mode - 16 bytes)
typedef struct {
    uint16_t offset_low;    // bits 0-15 of handler
    uint16_t selector;      // code segment selector (0x08)
    uint8_t ist;            // IST (0-7), 0 = no IST
    uint8_t type_attr;      // type + DPL + present
    uint16_t offset_mid;    // bits 16-31
    uint32_t offset_high;   // bits 32-63
    uint32_t reserved;      // must be 0
} __attribute__((packed)) idt_entry_t;

// IDT pointer (for lidt)
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

// Gate type flags
#define IDT_INTERRUPT_GATE 0x8E  // present, DPL=0, 64-bit interrupt gate
#define IDT_TRAP_GATE      0x8F  // present, DPL=0, 64-bit trap gate
#define IDT_USER_GATE      0xEE  // present, DPL=3, 64-bit interrupt gate

void idt_init(void);
void idt_set_gate(int num, uint64_t base, uint16_t selector, uint8_t type_attr);

#endif
