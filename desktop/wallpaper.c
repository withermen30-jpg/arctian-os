#include "wallpaper.h"
#include "backbuffer.h"

#define WP_BG 0x0B1119

void wallpaper_draw_scaled(void) {
    backbuffer_clear(WP_BG);
}
