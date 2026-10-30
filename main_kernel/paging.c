#include "paging.h"
#include "serial.h"
#include "isr.h"

static inline uint64_t rd_cr3(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr3,%0" : "=r"(v));
    return v;
}

static inline void wr_cr3(uint64_t v) {
    __asm__ volatile("mov %0,%%cr3" :: "r"(v) : "memory");
}

static inline void invlpg(uint64_t va) {
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
}

static uint64_t *tbl(uint64_t phys) { return (uint64_t *)(uintptr_t)phys; }

static void zero_frame(uint64_t phys) {
    uint64_t *p = tbl(phys);
    for (int i = 0; i < 512; i++) p[i] = 0;
}

uint64_t vmm_alloc_frame(void) {
    vmm_state_t *st = VMM_STATE;
    if (!st->ready) return 0;
    if (st->free_head) {
        uint64_t p = st->free_head;
        st->free_head = *(uint64_t *)(uintptr_t)p;
        st->frames_free--;
        st->frames_used++;
        zero_frame(p);
        return p;
    }
    if (st->next_frame + 0x1000 > st->pool_end) return 0;
    uint64_t p = st->next_frame;
    st->next_frame += 0x1000;
    st->frames_used++;
    zero_frame(p);
    return p;
}

void vmm_free_frame(uint64_t phys) {
    vmm_state_t *st = VMM_STATE;
    if (!st->ready || !phys) return;
    if (phys < (uint64_t)VMM_FRAMES_BASE || phys >= st->pool_end) return;
    if (phys & 0xFFFull) return;
    *(uint64_t *)(uintptr_t)phys = st->free_head;
    st->free_head = phys;
    st->frames_free++;
    if (st->frames_used) st->frames_used--;
}

void vmm_mem_info(vmm_meminfo_t *out) {
    if (!out) return;
    vmm_state_t *st = VMM_STATE;
    uint64_t total = 0, used = 0;
    if (st->ready && st->pool_end > (uint64_t)VMM_FRAMES_BASE) {
        total = (st->pool_end - (uint64_t)VMM_FRAMES_BASE) / 0x1000;
        used = st->frames_used;
        if (used > total) used = total;
    }
    out->frames_total = total;
    out->frames_used = used;
    out->frames_free = total - used;
    out->total_bytes = total * 0x1000;
    out->used_bytes = used * 0x1000;
    out->free_bytes = out->frames_free * 0x1000;
}

void vmm_init(void) {
    vmm_state_t *st = VMM_STATE;
    uint64_t cr3 = rd_cr3() & VMM_ADDR_MASK;

    if (st->magic == VMM_MAGIC && st->ready) {
        st->kernel_space = cr3;
        return;
    }

    zero_frame(VMM_STATE_ADDR);
    st->magic = VMM_MAGIC;
    st->next_frame = (uint64_t)VMM_FRAMES_BASE;
    st->pool_end = (uint64_t)VMM_POOL_BASE + (uint64_t)VMM_POOL_SIZE;
    st->frames_used = 0;
    st->frames_free = 0;
    st->free_head = 0;
    st->killed_tasks = 0;
    st->kernel_space = cr3;
    st->kernel_pdpt = tbl(cr3)[0] & VMM_ADDR_MASK;
    st->ready = 1;

    uint64_t pdpt_hi = vmm_alloc_frame();
    uint64_t pd_hi = vmm_alloc_frame();
    if (!pdpt_hi || !pd_hi) {
        serial_puts("[vmm] frame alloc fail\n");
        for (;;) __asm__ volatile("hlt");
    }
    for (int i = 0; i < 512; i++) {
        tbl(pd_hi)[i] = ((uint64_t)i << 21) | VMM_PTE_P | VMM_PTE_W | VMM_PTE_HUGE;
    }
    tbl(pdpt_hi)[0] = pd_hi | VMM_PTE_P | VMM_PTE_W;
    tbl(cr3)[VMM_HIGHER_PML4_INDEX] = pdpt_hi | VMM_PTE_P | VMM_PTE_W;
    st->pdpt_hi = pdpt_hi;

    serial_puts("[vmm] higher-half 0xFFFF800000000000 aktif\n");
}

vmm_space_t vmm_new_space(void) {
    vmm_state_t *st = VMM_STATE;
    if (!st->ready) return 0;
    uint64_t pml4 = vmm_alloc_frame();
    if (!pml4) return 0;
    tbl(pml4)[0] = st->kernel_pdpt | VMM_PTE_P | VMM_PTE_W;
    tbl(pml4)[VMM_HIGHER_PML4_INDEX] = st->pdpt_hi | VMM_PTE_P | VMM_PTE_W;
    st->space_count++;
    return pml4;
}

int vmm_map(vmm_space_t sp, uint64_t va, uint64_t pa, uint64_t flags) {
    if (!sp || (va & 0xFFFull) || (pa & 0xFFFull)) return -1;
    uint64_t *pml4 = tbl(sp);
    int i4 = (int)((va >> 39) & 0x1FF);
    int i3 = (int)((va >> 30) & 0x1FF);
    int i2 = (int)((va >> 21) & 0x1FF);
    int i1 = (int)((va >> 12) & 0x1FF);

    if (!(pml4[i4] & VMM_PTE_P)) {
        uint64_t t = vmm_alloc_frame();
        if (!t) return -3;
        pml4[i4] = t | VMM_PTE_P | VMM_PTE_W | VMM_PTE_U;
    } else if (pml4[i4] & VMM_PTE_HUGE) {
        return -2;
    }
    uint64_t *pdpt = tbl(pml4[i4] & VMM_ADDR_MASK);

    if (!(pdpt[i3] & VMM_PTE_P)) {
        uint64_t t = vmm_alloc_frame();
        if (!t) return -3;
        pdpt[i3] = t | VMM_PTE_P | VMM_PTE_W | VMM_PTE_U;
    } else if (pdpt[i3] & VMM_PTE_HUGE) {
        return -2;
    }
    uint64_t *pd = tbl(pdpt[i3] & VMM_ADDR_MASK);

    if (!(pd[i2] & VMM_PTE_P)) {
        uint64_t t = vmm_alloc_frame();
        if (!t) return -3;
        pd[i2] = t | VMM_PTE_P | VMM_PTE_W | VMM_PTE_U;
    } else if (pd[i2] & VMM_PTE_HUGE) {
        return -2;
    }
    uint64_t *pt = tbl(pd[i2] & VMM_ADDR_MASK);

    pt[i1] = (pa & VMM_ADDR_MASK) | flags | VMM_PTE_P;
    invlpg(va);
    return 0;
}

int vmm_unmap(vmm_space_t sp, uint64_t va) {
    if (!sp) return -1;
    uint64_t *pml4 = tbl(sp);
    int i4 = (int)((va >> 39) & 0x1FF);
    int i3 = (int)((va >> 30) & 0x1FF);
    int i2 = (int)((va >> 21) & 0x1FF);
    int i1 = (int)((va >> 12) & 0x1FF);

    if (!(pml4[i4] & VMM_PTE_P) || (pml4[i4] & VMM_PTE_HUGE)) return -1;
    uint64_t *pdpt = tbl(pml4[i4] & VMM_ADDR_MASK);
    if (!(pdpt[i3] & VMM_PTE_P) || (pdpt[i3] & VMM_PTE_HUGE)) return -1;
    uint64_t *pd = tbl(pdpt[i3] & VMM_ADDR_MASK);
    if (!(pd[i2] & VMM_PTE_P) || (pd[i2] & VMM_PTE_HUGE)) return -1;
    uint64_t *pt = tbl(pd[i2] & VMM_ADDR_MASK);
    if (!(pt[i1] & VMM_PTE_P)) return -1;
    pt[i1] = 0;
    invlpg(va);
    return 0;
}

void vmm_switch(vmm_space_t sp) {
    if (sp) wr_cr3(sp);
}

vmm_space_t vmm_current(void) {
    return rd_cr3() & VMM_ADDR_MASK;
}

uint64_t vmm_translate(vmm_space_t sp, uint64_t va) {
    if (!sp) return 0;
    uint64_t *pml4 = tbl(sp);
    int i4 = (int)((va >> 39) & 0x1FF);
    int i3 = (int)((va >> 30) & 0x1FF);
    int i2 = (int)((va >> 21) & 0x1FF);
    int i1 = (int)((va >> 12) & 0x1FF);

    uint64_t e = pml4[i4];
    if (!(e & VMM_PTE_P)) return 0;
    uint64_t *pdpt = tbl(e & VMM_ADDR_MASK);
    if (e & VMM_PTE_HUGE) return (e & VMM_ADDR_MASK) | (va & 0x3FFFFFFFull);

    e = pdpt[i3];
    if (!(e & VMM_PTE_P)) return 0;
    uint64_t *pd = tbl(e & VMM_ADDR_MASK);
    if (e & VMM_PTE_HUGE) return (e & VMM_ADDR_MASK) | (va & 0x3FFFFFFFull);

    e = pd[i2];
    if (!(e & VMM_PTE_P)) return 0;
    if (e & VMM_PTE_HUGE) return (e & VMM_ADDR_MASK) | (va & 0x1FFFFFull);

    uint64_t *pt = tbl(e & VMM_ADDR_MASK);
    e = pt[i1];
    if (!(e & VMM_PTE_P)) return 0;
    return (e & VMM_ADDR_MASK) | (va & 0xFFFull);
}

int vmm_map_kernel_range(uint64_t va, uint64_t size) {
    vmm_space_t sp = vmm_current();
    uint64_t start = va & ~0xFFFull;
    uint64_t end = (va + size + 0xFFF) & ~0xFFFull;
    for (uint64_t a = start; a < end; a += 0x1000) {
        if (vmm_translate(sp, a) == 0) {
            if (vmm_map(sp, a, a, VMM_PTE_P | VMM_PTE_W) != 0) return -1;
        }
    }
    return 0;
}

int vmm_handle_page_fault(uint64_t cr2, uint64_t err, uint64_t rip) {
    vmm_state_t *st = VMM_STATE;
    st->fault_count++;
    st->last_fault_cr2 = cr2;
    st->last_fault_rip = rip;
    st->last_fault_err = (uint32_t)err;

    int user_origin = (err & 4ull) ? 1 : 0;
    int prot_violation = (err & 1ull) ? 1 : 0;

    if (cr2 >= VMM_USER_BASE && cr2 < 0x0000800000000000ull) {
        st->last_fault_killed = 1;
        st->killed_tasks++;
        serial_printf("[vmm] gorev page fault cr2=0x%X%08X rip=0x%X%08X -> gorev sonlandi\n",
                      (unsigned)(cr2 >> 32), (unsigned)cr2,
                      (unsigned)(rip >> 32), (unsigned)rip);
        return 1;
    }
    if (user_origin) {
        st->last_fault_killed = 1;
        st->killed_tasks++;
        serial_printf("[vmm] koruma ihlali cr2=0x%X%08X rip=0x%X%08X (%s) -> gorev sonlandi\n",
                      (unsigned)(cr2 >> 32), (unsigned)cr2,
                      (unsigned)(rip >> 32), (unsigned)rip,
                      prot_violation ? "U/S" : "yok");
        return 1;
    }
    st->last_fault_killed = 0;
    return 0;
}

int vmm_selftest(void) {
    vmm_state_t *st = VMM_STATE;
    if (!st->ready) return -1;

    vmm_space_t ksp = st->kernel_space;
    vmm_space_t spa = vmm_new_space();
    vmm_space_t spb = vmm_new_space();
    if (!spa || !spb) return -2;

    uint64_t fr = vmm_alloc_frame();
    if (!fr) return -2;
    if (vmm_map(spb, (uint64_t)VMM_USER_BASE, fr,
                VMM_PTE_P | VMM_PTE_W | VMM_PTE_U) != 0) return -3;

    static arctian_recover_t rec;
    g_arctian_recover = &rec;
    rec.active = 0;

    int c = arctian_setjmp(&rec);
    if (c == 0) {
        rec.active = 1;
        __asm__ volatile("cli");
        vmm_switch(spa);
        *(volatile uint64_t *)(uintptr_t)VMM_USER_BASE = 0xA5A5A5A5ull;
        __asm__ volatile("cli");
        vmm_switch(ksp);
        rec.active = 0;
        g_arctian_recover = 0;
        __asm__ volatile("sti");
        return -4;
    }

    __asm__ volatile("cli");
    vmm_switch(ksp);
    rec.active = 0;
    g_arctian_recover = 0;
    __asm__ volatile("sti");
    serial_puts("[vmm] selftest OK: baska adres alanina erisim -> task killed, sistem ayakta\n");
    return 0;
}

static uint64_t leaf_pte(vmm_space_t sp, uint64_t va) {
    uint64_t *pml4 = tbl(sp);
    int i4 = (int)((va >> 39) & 0x1FF);
    int i3 = (int)((va >> 30) & 0x1FF);
    int i2 = (int)((va >> 21) & 0x1FF);
    int i1 = (int)((va >> 12) & 0x1FF);
    uint64_t e = pml4[i4];
    if (!(e & VMM_PTE_P) || (e & VMM_PTE_HUGE)) return 0;
    uint64_t *pdpt = tbl(e & VMM_ADDR_MASK);
    e = pdpt[i3];
    if (!(e & VMM_PTE_P) || (e & VMM_PTE_HUGE)) return 0;
    uint64_t *pd = tbl(e & VMM_ADDR_MASK);
    e = pd[i2];
    if (!(e & VMM_PTE_P) || (e & VMM_PTE_HUGE)) return 0;
    uint64_t *pt = tbl(e & VMM_ADDR_MASK);
    e = pt[i1];
    if (!(e & VMM_PTE_P)) return 0;
    return e;
}

int vmm_prot_selftest(void) {
    vmm_state_t *st = VMM_STATE;
    if (!st->ready) return -1;

    uint64_t *kpml4 = tbl(st->kernel_space);
    int kernel_kern = (((kpml4[0] & VMM_PTE_U) == 0) &&
                       ((kpml4[VMM_HIGHER_PML4_INDEX] & VMM_PTE_U) == 0));

    vmm_space_t sp = vmm_new_space();
    if (!sp) return -2;
    uint64_t fr = vmm_alloc_frame();
    if (!fr) return -2;
    if (vmm_map(sp, (uint64_t)VMM_USER_BASE, fr,
                VMM_PTE_P | VMM_PTE_W | VMM_PTE_U) != 0) return -3;

    uint64_t *upml4 = tbl(sp);
    int i4 = (int)(((uint64_t)VMM_USER_BASE >> 39) & 0x1FF);
    int i3 = (int)(((uint64_t)VMM_USER_BASE >> 30) & 0x1FF);
    int i2 = (int)(((uint64_t)VMM_USER_BASE >> 21) & 0x1FF);

    int user_ok = (upml4[i4] & VMM_PTE_U) != 0;
    if (user_ok) {
        uint64_t *pdpt = tbl(upml4[i4] & VMM_ADDR_MASK);
        uint64_t *pd = tbl(pdpt[i3] & VMM_ADDR_MASK);
        user_ok = ((pdpt[i3] & VMM_PTE_U) != 0) &&
                  ((pd[i2] & VMM_PTE_U) != 0) &&
                  ((leaf_pte(sp, (uint64_t)VMM_USER_BASE) & VMM_PTE_U) != 0);
    }

    int ok = kernel_kern && user_ok;
    serial_printf("[vmm] bellek koruma selftest: kernel U=0:%s kullanici U=1:%s -> %s\n",
                  kernel_kern ? "EVET" : "HAYIR",
                  user_ok ? "EVET" : "HAYIR",
                  ok ? "OK" : "HATA");
    return ok ? 0 : -4;
}

int vmm_mem_selftest(void) {
    vmm_state_t *st = VMM_STATE;
    if (!st->ready) return -1;

    uint64_t base_used = st->frames_used;
    uint64_t base_free = st->frames_free;

    uint64_t a = vmm_alloc_frame();
    uint64_t b = vmm_alloc_frame();
    uint64_t c = vmm_alloc_frame();
    if (!a || !b || !c) return -2;
    vmm_free_frame(b);
    uint64_t d = vmm_alloc_frame();

    int reuse = (d == b);
    int mid_ok = (st->frames_used == base_used + 3) && (st->frames_free == base_free);

    vmm_free_frame(a);
    vmm_free_frame(c);
    vmm_free_frame(d);
    int end_ok = (st->frames_used == base_used) && (st->frames_free == base_free + 3);

    int ok = reuse && mid_ok && end_ok;
    serial_printf("[vmm] bellek muhasebe selftest: kullanilan %u->%u cerceve, geri donusum=%s, serbest=%s -> %s\n",
                  (unsigned)base_used, (unsigned)(base_used + 3),
                  reuse ? "EVET" : "HAYIR", end_ok ? "EVET" : "HAYIR",
                  ok ? "OK" : "HATA");
    return ok ? 0 : -1;
}
