#include "pic.h"
#include "kernel.h"

void pic_init(void) {
    kernel_debug("PIC baslatiliyor...");

    outb(PIC1_COMMAND, PIC_ICW1);
    io_wait();
    outb(PIC2_COMMAND, PIC_ICW1);
    io_wait();

    outb(PIC1_DATA, PIC_OFFSET);
    io_wait();
    outb(PIC2_DATA, PIC_OFFSET + 8);
    io_wait();

    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    outb(PIC1_DATA, PIC_ICW4_8086);
    io_wait();
    outb(PIC2_DATA, PIC_ICW4_8086);
    io_wait();

    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    kernel_debug("PIC basariyla baslatildi (IRQ0-15 -> INT 32-47)");
}

void pic_send_eoi(int irq) {
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
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    kernel_debug("PIC devre disi birakildi");
}
