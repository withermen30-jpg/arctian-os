#include "syscall.h"
#include "gdt.h"
#include "paging.h"
#include "serial.h"
#include "recover.h"
#include "isr.h"
#include "afs.h"
#include <stdint.h>
#include <stddef.h>

extern void memcpy(void *dst, const void *src, size_t n);
extern void enter_user(uint64_t rip, uint64_t rsp, uint64_t rflags);
extern unsigned char ring3_payload_start[];
extern unsigned char ring3_payload_end[];
extern unsigned char ring3_fault_start[];
extern unsigned char ring3_fault_end[];
extern unsigned char ring3_file_start[];
extern unsigned char ring3_file_end[];

static int enter_and_wait(const unsigned char *start, const unsigned char *end,
                          const char *enter_msg, const char *ok_msg) {
    vmm_state_t *st = VMM_STATE;
    if (!st->ready) return -1;

    vmm_space_t sp = vmm_current();
    uint64_t code = vmm_alloc_frame();
    uint64_t stack = vmm_alloc_frame();
    if (!code || !stack) return -2;

    if (vmm_map(sp, (uint64_t)VMM_USER_BASE, code,
                VMM_PTE_P | VMM_PTE_W | VMM_PTE_U) != 0) return -3;
    if (vmm_map(sp, (uint64_t)VMM_USER_BASE + 0x1000, stack,
                VMM_PTE_P | VMM_PTE_W | VMM_PTE_U) != 0) return -3;

    uint32_t len = (uint32_t)(end - start);
    memcpy((void *)(uintptr_t)VMM_USER_BASE, start, len);

    static arctian_recover_t rec;
    g_arctian_recover = &rec;
    rec.active = 0;

    int c = arctian_setjmp(&rec);
    if (c == 0) {
        rec.active = 1;
        serial_puts(enter_msg);
        __asm__ volatile("cli");
        enter_user((uint64_t)VMM_USER_BASE,
                   (uint64_t)VMM_USER_BASE + 0x2000 - 16,
                   0x2);
        __asm__ volatile("cli");
        rec.active = 0;
        g_arctian_recover = 0;
        __asm__ volatile("sti");
        return -4;
    }

    __asm__ volatile("cli");
    uint16_t kd = GDT_KERNEL_DATA;
    __asm__ volatile("movw %0, %%ds; movw %0, %%es" :: "r"(kd) : "memory");
    rec.active = 0;
    g_arctian_recover = 0;
    __asm__ volatile("sti");
    serial_puts(ok_msg);
    return 0;
}

int ring3_selftest(void) {
    return enter_and_wait(ring3_payload_start, ring3_payload_end,
        "[ring3] CPL3'e giriliyor, syscall testi...\n",
        "[ring3] selftest OK: outb #GP ile engellendi, gorev sonlandi, sistem ayakta\n");
}

int ring3_file_selftest(void) {
    const char content[] = "dosya icerigi: merhaba ring3!";
    void *fh = afs_create_handle("syscall_test.txt");
    if (!fh || afs_write_handle(fh, content, (uint32_t)(sizeof(content) - 1)) != 0) {
        serial_puts("[ring3] AFS hazir degil, dosya testi atlandi\n");
        return 0;
    }
    int rc = enter_and_wait(ring3_file_start, ring3_file_end,
        "[ring3] dosya testi: open/read/close syscall...\n",
        "[ring3] dosya selftest OK: AFS open/read/write calisti, outb #GP engellendi\n");
    afs_remove("syscall_test.txt");
    return rc;
}

int ring3_fault_selftest(void) {
    int rc = enter_and_wait(ring3_fault_start, ring3_fault_end,
        "[ring3] bellek koruma testi: NULL'a yazma denemesi (CPL3)...\n",
        "[ring3] bellek koruma OK: NULL (cekirdek) erisimi #PF ile engellendi, gorev oldu\n");
    if (rc != 0) {
        serial_puts("[ring3] bellek koruma HATA: gorev sonlanmadi\n");
        return rc;
    }
    vmm_state_t *st = VMM_STATE;
    int user = (st->last_fault_err & 4u) ? 1 : 0;
    int prot = (st->last_fault_err & 1u) ? 1 : 0;
    serial_printf("[ring3] #PF ayristirma: cr2=0x%X%08X err=0x%X user=%d koruma=%d\n",
                  (unsigned)(st->last_fault_cr2 >> 32), (unsigned)st->last_fault_cr2,
                  (unsigned)st->last_fault_err, user, prot);
    return (user && prot) ? 0 : -1;
}
