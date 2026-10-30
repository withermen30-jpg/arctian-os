/**
 * Arctian PIC (Programmable Interrupt Controller)
 * 8259A - IRQ 0-15 remap to INT 32-47
 */
#include "pic.h"
#include "kernel.h"

void pic_init(void) {
    kernel_debug("PIC baslatiliyor...");

    // ICW1: Initialize PIC1 and PIC2
    outb(PIC1_COMMAND, PIC_ICW1);
    io_wait();
    outb(PIC2_COMMAND, PIC_ICW1);
    io_wait();

    // ICW2: Remap IRQs - PIC1 offset = 0x20 (INT 32), PIC2 offset = 0x28 (INT 40)
    outb(PIC1_DATA, PIC_OFFSET);
    io_wait();
    outb(PIC2_DATA, PIC_OFFSET + 8);
    io_wait();

    // ICW3: Tell PIC1 that PIC2 is at IRQ2, tell PIC2 its cascade identity
    outb(PIC1_DATA, 0x04);   // Slave at IRQ2 (bit 2 = 0x04)
    io_wait();
    outb(PIC2_DATA, 0x02);   // Slave identity = 2
    io_wait();

    // ICW4: 8086 mode, auto EOI off
    outb(PIC1_DATA, PIC_ICW4_8086);
    io_wait();
    outb(PIC2_DATA, PIC_ICW4_8086);
    io_wait();

    // Mask all IRQs by default
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    kernel_debug("PIC basariyla baslatildi (IRQ0-15 -> INT 32-47)");
}

void pic_send_eoi(int irq) {
    // If IRQ >= 8, send EOI to slave too
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_set_mask(int irq, bool masked) {
    uint16_t port;
    uint8_t mask_bit = 1 << (irq % 8);

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
    }

    uint8_t current = inb(port);
    if (masked) {
        current |= mask_bit;
    } else {
        current &= ~mask_bit;
    }
    outb(port, current);
}

void pic_disable(void) {
    // Mask all IRQs
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    kernel_debug("PIC devre disi birakildi");
}
