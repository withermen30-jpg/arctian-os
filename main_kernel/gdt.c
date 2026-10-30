#include "gdt.h"
#include <stdint.h>

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss_t;

static gdt_entry_t g_gdt[7];
static gdt_ptr_t   g_gdtp;
static tss_t       g_tss;
static uint8_t     g_ring3_stack[16384] __attribute__((aligned(16)));

static void set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    g_gdt[i].limit_low   = (uint16_t)(limit & 0xFFFF);
    g_gdt[i].base_low    = (uint16_t)(base & 0xFFFF);
    g_gdt[i].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    g_gdt[i].access      = access;
    g_gdt[i].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    g_gdt[i].base_high   = (uint8_t)((base >> 24) & 0xFF);
}

static void set_tss_desc(int i, uint64_t base, uint32_t limit) {
    g_gdt[i].limit_low   = (uint16_t)(limit & 0xFFFF);
    g_gdt[i].base_low    = (uint16_t)(base & 0xFFFF);
    g_gdt[i].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    g_gdt[i].access      = 0x89;
    g_gdt[i].granularity = (uint8_t)((limit >> 16) & 0x0F);
    g_gdt[i].base_high   = (uint8_t)((base >> 24) & 0xFF);

    uint32_t hi = (uint32_t)(base >> 32);
    g_gdt[i + 1].limit_low   = (uint16_t)(hi & 0xFFFF);
    g_gdt[i + 1].base_low    = (uint16_t)((hi >> 16) & 0xFFFF);
    g_gdt[i + 1].base_mid    = 0;
    g_gdt[i + 1].access      = 0;
    g_gdt[i + 1].granularity = 0;
    g_gdt[i + 1].base_high   = 0;
}

void gdt_set_kernel_stack(uint64_t rsp0) {
    g_tss.rsp0 = rsp0;
}

void gdt_init(void) {
    uint8_t *raw = (uint8_t *)g_gdt;
    for (unsigned i = 0; i < sizeof(g_gdt); i++) raw[i] = 0;

    set_entry(0, 0, 0, 0x00, 0x00);
    set_entry(1, 0, 0, 0x9A, 0xA0);
    set_entry(2, 0, 0, 0x92, 0x00);
    set_entry(3, 0, 0, 0xF2, 0x00);
    set_entry(4, 0, 0, 0xFA, 0xA0);

    uint8_t *traw = (uint8_t *)&g_tss;
    for (unsigned i = 0; i < sizeof(g_tss); i++) traw[i] = 0;
    g_tss.iomap_base = (uint16_t)sizeof(g_tss);
    g_tss.rsp0 = (uint64_t)(g_ring3_stack + sizeof(g_ring3_stack));
    set_tss_desc(5, (uint64_t)&g_tss, (uint32_t)(sizeof(g_tss) - 1));

    g_gdtp.limit = (uint16_t)(sizeof(g_gdt) - 1);
    g_gdtp.base  = (uint64_t)&g_gdt;
    __asm__ volatile("lgdt %0" :: "m"(g_gdtp));

    uint16_t kd = GDT_KERNEL_DATA;
    __asm__ volatile("movw %0, %%ds; movw %0, %%es; movw %0, %%ss; movw %0, %%fs; movw %0, %%gs"
                     :: "r"(kd) : "memory");

    __asm__ volatile(
        "pushq %0\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        :: "i"((uint64_t)GDT_KERNEL_CODE) : "rax", "memory");

    uint16_t tr = GDT_TSS;
    __asm__ volatile("ltr %0" :: "r"(tr) : "memory");
}
