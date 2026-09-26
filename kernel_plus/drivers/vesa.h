#ifndef VESA_H
#define VESA_H

#include <stdint.h>
#include <stdbool.h>

#define VBE_DISPI_INDEX_PORT    0x01CE
#define VBE_DISPI_DATA_PORT     0x01CF

#define VBE_DISPI_INDEX_ID          0
#define VBE_DISPI_INDEX_XRES        1
#define VBE_DISPI_INDEX_YRES        2
#define VBE_DISPI_INDEX_BPP         3
#define VBE_DISPI_INDEX_ENABLE      4
#define VBE_DISPI_INDEX_BANK        5
#define VBE_DISPI_INDEX_VIRT_WIDTH  6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET    8
#define VBE_DISPI_INDEX_Y_OFFSET    9
#define VBE_DISPI_INDEX_REFRESH     0x0A

#define VBE_DISPI_DISABLED      0x00
#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_LFB_ENABLED   0x40
#define VBE_DISPI_NOCLEARMEM    0x80

#define VESA_DEFAULT_WIDTH      1024
#define VESA_DEFAULT_HEIGHT     768
#define VESA_DEFAULT_BPP        32

#define VESA_2K_WIDTH           2560
#define VESA_2K_HEIGHT          1440

#define VESA_REFRESH_60         60
#define VESA_REFRESH_75         75
#define VESA_REFRESH_120        120

typedef struct {
    uint32_t *address;
    uint32_t  phys_address;
    int width;
    int height;
    int bpp;
    int pitch;
    bool enabled;
} framebuffer_t;

int  vesa_init(int width, int height, int bpp);
void vesa_disable(void);
bool vesa_is_enabled(void);
void vesa_set_refresh_rate(int hz);

framebuffer_t* vesa_get_framebuffer(void);
void vesa_put_pixel(int x, int y, uint32_t color);
uint32_t vesa_get_pixel(int x, int y);
void vesa_clear(uint32_t color);
void vesa_fill_rect(int x, int y, int w, int h, uint32_t color);

#endif
