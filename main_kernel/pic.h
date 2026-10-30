#ifndef PIC_H
#define PIC_H

#include <stdint.h>
#include <stdbool.h>

// PIC portları
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

// PIC komutları
#define PIC_EOI         0x20    // End of Interrupt
#define PIC_ICW1        0x11    // Initialization Command Word 1
#define PIC_ICW4_8086   0x01    // 8086 mode

// IRQ'ları INT'lere remap (IRQ0-15 → INT 32-47)
#define PIC_OFFSET      0x20

// IRQ numaraları
#define IRQ_TIMER       0
#define IRQ_KEYBOARD    1
#define IRQ_CASCADE     2
#define IRQ_COM2        3
#define IRQ_COM1        4
#define IRQ_LPT2        5
#define IRQ_FLOPPY      6
#define IRQ_LPT1        7
#define IRQ_CMOS        8
#define IRQ_PERIPH1     9
#define IRQ_PERIPH2     10
#define IRQ_PERIPH3     11
#define IRQ_MOUSE       12
#define IRQ_FPU         13
#define IRQ_PRIMARY_ATA 14
#define IRQ_SECONDARY_ATA 15

// IRQ'ların INT numaraları
#define INT_IRQ(n)      (PIC_OFFSET + (n))

void pic_init(void);
void pic_send_eoi(int irq);
void pic_set_mask(int irq, bool masked);
void pic_disable(void);

#endif
