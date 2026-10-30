#ifndef ARCTIAN_PAGING_H
#define ARCTIAN_PAGING_H

#include <stdint.h>
#include "memory_map.h"

#define VMM_MAGIC            0x564D4D32u

#define VMM_PTE_P            0x001ull
#define VMM_PTE_W            0x002ull
#define VMM_PTE_U            0x004ull
#define VMM_PTE_PWT          0x008ull
#define VMM_PTE_PCD          0x010ull
#define VMM_PTE_HUGE         0x080ull
#define VMM_ADDR_MASK        0x000FFFFFFFFFF000ull

typedef uint64_t vmm_space_t;

typedef struct {
    uint32_t magic;
    uint32_t ready;
    uint64_t kernel_space;
    uint64_t kernel_pdpt;
    uint64_t pdpt_hi;
    uint64_t next_frame;
    uint64_t pool_end;
    uint64_t space_count;
    uint64_t fault_count;
    uint64_t last_fault_cr2;
    uint64_t last_fault_rip;
    uint32_t last_fault_err;
    uint32_t last_fault_killed;
    uint64_t frames_used;
    uint64_t frames_free;
    uint64_t free_head;
    uint64_t killed_tasks;
} vmm_state_t;

#define VMM_STATE ((vmm_state_t *)(uintptr_t)VMM_STATE_ADDR)

typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t frames_total;
    uint64_t frames_used;
    uint64_t frames_free;
} vmm_meminfo_t;

void        vmm_init(void);
vmm_space_t vmm_new_space(void);
int         vmm_map(vmm_space_t sp, uint64_t va, uint64_t pa, uint64_t flags);
int         vmm_unmap(vmm_space_t sp, uint64_t va);
void        vmm_switch(vmm_space_t sp);
vmm_space_t vmm_current(void);
uint64_t    vmm_translate(vmm_space_t sp, uint64_t va);
uint64_t    vmm_alloc_frame(void);
void        vmm_free_frame(uint64_t phys);
void        vmm_mem_info(vmm_meminfo_t *out);
int         vmm_map_kernel_range(uint64_t va, uint64_t size);
int         vmm_handle_page_fault(uint64_t cr2, uint64_t err, uint64_t rip);
int         vmm_selftest(void);
int         vmm_prot_selftest(void);
int         vmm_mem_selftest(void);

#endif
