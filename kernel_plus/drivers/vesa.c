#include "vesa.h"
#include "kernel.h"

static inline void vbe_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_INDEX_PORT, index);
    outw(VBE_DISPI_DATA_PORT, value);
}

static uint16_t vbe_read(uint16_t index) {
    outw(VBE_DISPI_INDEX_PORT, index);
    return inw(VBE_DISPI_DATA_PORT);
}

static framebuffer_t fb;

#define VESA_BOOT_INFO       0x7000
#define VESA_BOOT_MAGIC      0x41534556

#define PAGE_PDP    0x12000
#define PAGE_PD_FB  0x0D000

static void map_framebuffer_memory(uint32_t phys_addr) {
    int pd_index = (phys_addr >> 21) & 0x1FF;

    volatile uint64_t *pd = (volatile uint64_t *)PAGE_PD_FB;
    for (int i = 0; i < 512; i++) {
        pd[i] = 0;
    }

    for (int i = 0; i < 8; i++) {
        uint32_t page_addr = phys_addr + i * 0x200000;
        pd[pd_index + i] = (uint64_t)page_addr | 0x83;
    }

    int pdp_index = (phys_addr >> 30) & 0x1FF;
    volatile uint64_t *pdp = (volatile uint64_t *)PAGE_PDP;
    pdp[pdp_index] = (uint64_t)PAGE_PD_FB | 0x03;

    asm volatile ("mov %%cr3, %%rax; mov %%rax, %%cr3" : : : "rax");

    kernel_debug("Framebuffer 2MB sayfalar ile haritalandi");
}

int vesa_init(int width, int height, int bpp) {
    kernel_debug("VESA baslatiliyor");

    volatile uint8_t *boot_info = (volatile uint8_t *)VESA_BOOT_INFO;
    uint32_t magic = *(volatile uint32_t *)(boot_info + 0);
    if (magic == VESA_BOOT_MAGIC) {
        uint32_t phys = *(volatile uint32_t *)(boot_info + 4);
        uint16_t pitch = *(volatile uint16_t *)(boot_info + 8);
        uint16_t boot_width = *(volatile uint16_t *)(boot_info + 12);
        uint16_t boot_height = *(volatile uint16_t *)(boot_info + 14);
        uint8_t boot_bpp = *(volatile uint8_t *)(boot_info + 16);

        if (phys == 0) {
            phys = 0xFD000000;
        }

        fb.width = boot_width ? boot_width : width;
        fb.height = boot_height ? boot_height : height;
        fb.bpp = boot_bpp ? boot_bpp : bpp;
        fb.pitch = pitch ? pitch : fb.width * (fb.bpp / 8);
        fb.phys_address = phys;
        fb.enabled = true;

        map_framebuffer_memory(fb.phys_address);
        fb.address = (uint32_t *)(uintptr_t)fb.phys_address;

        kernel_debug("VESA BIOS framebuffer kullaniliyor");
        return 0;
    }

    vbe_write(VBE_DISPI_INDEX_ID, 0xB0C5);
    uint16_t id = vbe_read(VBE_DISPI_INDEX_ID);
    if (id < 0xB0C0 || id > 0xB0C5) {
        kernel_warning("Bochs VBE desteklenmiyor");
        return -1;
    }

    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);

    vbe_write(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    vbe_write(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    vbe_write(VBE_DISPI_INDEX_BPP, (uint16_t)bpp);

    vbe_write(VBE_DISPI_INDEX_VIRT_WIDTH, (uint16_t)width);

    vbe_write(VBE_DISPI_INDEX_ENABLE,
              VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    uint16_t xres = vbe_read(VBE_DISPI_INDEX_XRES);
    uint16_t yres = vbe_read(VBE_DISPI_INDEX_YRES);
    if (xres != (uint16_t)width || yres != (uint16_t)height) {
        kernel_warning("VESA modu ayarlanamadi");
        vesa_disable();
        return -1;
    }

    fb.width = width;
    fb.height = height;
    fb.bpp = bpp;
    fb.pitch = width * (bpp / 8);
    fb.phys_address = 0xFD000000;
    fb.enabled = true;

    map_framebuffer_memory(fb.phys_address);

    fb.address = (uint32_t *)(uintptr_t)fb.phys_address;

    kernel_debug("VESA basariyla baslatildi");
    return 0;
}

void vesa_disable(void) {
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    fb.enabled = false;
    kernel_debug("VESA devre disi");
}

void vesa_set_refresh_rate(int hz) {
    if (hz < 1) return;
    vbe_write(VBE_DISPI_INDEX_REFRESH, (uint16_t)hz);
    kernel_debug("VESA yenileme hizi ayarlandi");
}

bool vesa_is_enabled(void) {
    return fb.enabled;
}

framebuffer_t* vesa_get_framebuffer(void) {
    return &fb;
}

void vesa_put_pixel(int x, int y, uint32_t color) {
    if (!fb.enabled || x < 0 || x >= fb.width || y < 0 || y >= fb.height)
        return;
    fb.address[y * (fb.pitch / 4) + x] = color;
}

uint32_t vesa_get_pixel(int x, int y) {
    if (!fb.enabled || x < 0 || x >= fb.width || y < 0 || y >= fb.height)
        return 0;
    return fb.address[y * (fb.pitch / 4) + x];
}

void vesa_clear(uint32_t color) {
    if (!fb.enabled || !fb.address) return;

    uint64_t c64 = ((uint64_t)color << 32) | color;
    uint64_t *ptr = (uint64_t *)fb.address;
    int n = (fb.width * fb.height) / 2;
    for (int i = 0; i < n; i++) {
        ptr[i] = c64;
    }
    if ((fb.width * fb.height) & 1) {
        fb.address[n * 2] = color;
    }
}

void vesa_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!fb.enabled) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb.width)  w = fb.width - x;
    if (y + h > fb.height) h = fb.height - y;
    if (w <= 0 || h <= 0) return;

    int pitch_dwords = fb.pitch / 4;
    for (int row = 0; row < h; row++) {
        int base = (y + row) * pitch_dwords + x;
        for (int col = 0; col < w; col++) {
            fb.address[base + col] = color;
        }
    }
}
