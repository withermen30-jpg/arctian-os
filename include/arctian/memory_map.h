/**
 * Arctian - Merkezi bellek haritasi
 * Tum moduller bu sabitleri kullanir; cakisma olmamasi icin tek kaynak.
 */
#ifndef ARCTIAN_MEMORY_MAP_H
#define ARCTIAN_MEMORY_MAP_H

/* --- Yukleme / gecici --- */
#define STAGE1_LOAD_ADDR        0x00007C00u
#define STAGE2_LOAD_ADDR        0x00010000u
#define VESA_BOOT_INFO_ADDR     0x00007000u
#define PML4_ADDR               0x00008000u
#define PDPT_ADDR               0x00009000u
#define PD_LOW_ADDR             0x0000A000u
#define PD_FB_ADDR              0x0000D000u
#define BOOT_STACK_TOP          0x0008FF00u

/* --- Modul bolgeleri (sabit adresli duz .bin) --- */
#define MAIN_KERNEL_BASE        0x00100000u
#define MAIN_KERNEL_MAX         (1u * 1024u * 1024u)

#define INSTALL_BASE            0x00200000u
#define INSTALL_MAX             (1u * 1024u * 1024u)

#define KERNEL_PLUS_BASE        0x00300000u
#define KERNEL_PLUS_MAX         (7u * 1024u * 1024u)

#define DESKTOP_BASE            0x00C00000u
#define DESKTOP_MAX             (8u * 1024u * 1024u)

/* Dinamik (DuckStore) uygulama bolgesi. Desktop modulu (image+bss) 0x010B0000
 * civarina kadar uzandigi icin bu bolge 0x01800000'den baslar. */
#define APP_BASE                0x01800000u
#define APP_MAX                 (40u * 1024u * 1024u)

/* 0.6: .wlf native uygulamalari icin AYRI bolge. DPK'lar APP_BASE'den
 * baslar ve 2 MB'luk dilimler kullanir (0x01800000, 0x01A00000, ...);
 * onceki surumde .wlf de 0x01C00000 kullaniyordu ve 3. DPK ile CAKISIYORDU
 * (ikisi de ayni adrese yuklenip birbirini bozuyordu). */
#define WLF_BASE                0x03000000u
#define WLF_MAX                 (8u * 1024u * 1024u)

#define DISK_BUF_BASE           0x04000000u
#define DISK_BUF_SIZE           (16u * 1024u * 1024u)

/* Blok onbellegi + ATA DMA tamponu icin ayrilmis bolge. Modul BSS'ine
 * konursa main_kernel'in 512KB'lik yiginini modul bolgesine tasirir; bu
 * yuzden sabit, kimlik-eslemeli bir alanda tutulur. */
#define BLK_SCRATCH_BASE        0x05000000u
#define BLK_SCRATCH_SIZE        (1u * 1024u * 1024u)

#define BACKBUFFER_BASE         0x08000000u
#define BACKBUFFER_MAX          (16u * 1024u * 1024u)

#define HEAP_BASE               0x10000000u
#define HEAP_MAX                (768u * 1024u * 1024u)

/* Kimlik (identity) esleme: 0 - 1 GB, 2MB huge page */
#define IDENTITY_MAP_SIZE       (1u * 1024u * 1024u * 1024u)
#define IDENTITY_MAP_PD_ENTRIES 512u

/* --- Sanal bellek yonetimi (VMM / paging) --- */
#define VMM_HIGHER_BASE         0xFFFF800000000000ull
#define VMM_HIGHER_PML4_INDEX   256u
#define VMM_USER_BASE           0x0000008000000000ull
#define VMM_USER_PML4_INDEX     1u
#define VMM_POOL_BASE           0x0C000000u
#define VMM_POOL_SIZE           (64u * 1024u * 1024u)
#define VMM_STATE_ADDR          0x0C000000u
#define VMM_FRAMES_BASE         0x0C001000u

#endif /* ARCTIAN_MEMORY_MAP_H */
