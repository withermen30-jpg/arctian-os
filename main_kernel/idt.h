#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#define IDT_ENTRIES 256

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

#define IDT_INTERRUPT_GATE 0x8E
#define IDT_TRAP_GATE      0x8F
#define IDT_USER_GATE      0xEE

void idt_init(void);
void idt_set_gate(int num, uint64_t base, uint16_t selector, uint8_t type_attr);

#endif
