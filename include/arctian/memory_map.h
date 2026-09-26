#ifndef ARCTIAN_MEMORY_MAP_H
#define ARCTIAN_MEMORY_MAP_H

#define STAGE1_LOAD_ADDR        0x00007C00u
#define STAGE2_LOAD_ADDR        0x00010000u
#define VESA_BOOT_INFO_ADDR     0x00007000u
#define PML4_ADDR               0x00008000u
#define PDPT_ADDR               0x00009000u
#define PD_LOW_ADDR             0x0000A000u
#define PD_FB_ADDR              0x0000D000u
#define BOOT_STACK_TOP          0x0008FF00u

#define MAIN_KERNEL_BASE        0x00100000u
#define MAIN_KERNEL_MAX         (1u * 1024u * 1024u)

#define INSTALL_BASE            0x00200000u
#define INSTALL_MAX             (1u * 1024u * 1024u)

#define KERNEL_PLUS_BASE        0x00300000u
#define KERNEL_PLUS_MAX         (5u * 1024u * 1024u)

#define DESKTOP_BASE            0x00800000u
#define DESKTOP_MAX             (8u * 1024u * 1024u)

#define APP_BASE                0x01800000u
#define APP_MAX                 (40u * 1024u * 1024u)

#define DISK_BUF_BASE           0x04000000u
#define DISK_BUF_SIZE           (16u * 1024u * 1024u)

#define BACKBUFFER_BASE         0x08000000u
#define BACKBUFFER_MAX          (16u * 1024u * 1024u)

#define HEAP_BASE               0x10000000u
#define HEAP_MAX                (768u * 1024u * 1024u)

#define IDENTITY_MAP_SIZE       (1u * 1024u * 1024u * 1024u)
#define IDENTITY_MAP_PD_ENTRIES 512u

#endif
