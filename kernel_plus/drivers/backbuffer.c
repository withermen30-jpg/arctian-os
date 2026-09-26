#include "backbuffer.h"
#include "vesa.h"
#include "font8x8.h"
#include "kernel.h"
#include "memory_map.h"


#define BB_MAX_WIDTH  2560
#define BB_MAX_HEIGHT 1440

static int bb_width  = 1024;
static int bb_height = 768;

static uint32_t * const backbuffer = (uint32_t *)BACKBUFFER_BASE;
static bool active = false;


int backbuffer_init(void) {
    if (!vesa_is_enabled())
        return -1;

    framebuffer_t *fb = vesa_get_framebuffer();
    if (!fb) return -1;

    bb_width = fb->width;
    bb_height = fb->height;

    if (bb_width > BB_MAX_WIDTH)  bb_width = BB_MAX_WIDTH;
    if (bb_height > BB_MAX_HEIGHT) bb_height = BB_MAX_HEIGHT;

    uint64_t c64 = 0;
    uint64_t *ptr = (uint64_t *)backbuffer;
    int n = (bb_width * bb_height) / 2;
    for (int i = 0; i < n; i++) {
        ptr[i] = c64;
    }
    if ((bb_width * bb_height) & 1) {
        backbuffer[n * 2] = 0;
    }

    active = true;
    return 0;
}

void backbuffer_clear(uint32_t color) {
    if (!active) return;
    uint64_t c64 = ((uint64_t)color << 32) | color;
    uint64_t *ptr = (uint64_t *)backbuffer;
    int n = (bb_width * bb_height) / 2;
    for (int i = 0; i < n; i++) {
        ptr[i] = c64;
    }
    if ((bb_width * bb_height) & 1) {
        backbuffer[n * 2] = color;
    }
}

void backbuffer_put_pixel(int x, int y, uint32_t color) {
    if (!active) return;
    if (x < 0 || x >= bb_width || y < 0 || y >= bb_height)
        return;
    backbuffer[y * bb_width + x] = color;
}

uint32_t backbuffer_get_pixel(int x, int y) {
    if (!active) return 0;
    if (x < 0 || x >= bb_width || y < 0 || y >= bb_height)
        return 0;
    return backbuffer[y * bb_width + x];
}

void backbuffer_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!active) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= bb_width || y >= bb_height) return;
    if (x + w > bb_width)  w = bb_width - x;
    if (y + h > bb_height) h = bb_height - y;
    if (w <= 0 || h <= 0) return;

    uint64_t c64 = ((uint64_t)color << 32) | color;

    for (int row = 0; row < h; row++) {
        int base = (y + row) * bb_width + x;
        int col = 0;
        for (; col + 1 < w; col += 2) {
            ((uint64_t *)(backbuffer + base))[col / 2] = c64;
        }
        if (col < w) {
            backbuffer[base + col] = color;
        }
    }
}

void backbuffer_blit(void) {
    if (!active) return;

    framebuffer_t *fb = vesa_get_framebuffer();
    if (!fb || !fb->enabled || !fb->address)
        return;

    int fb_pitch_dwords = fb->pitch / 4;
    int copy_w = (bb_width < fb->width) ? bb_width : fb->width;
    int copy_h = (bb_height < fb->height) ? bb_height : fb->height;

    for (int row = 0; row < copy_h; row++) {
        int bb_off = row * bb_width;
        int fb_off = row * fb_pitch_dwords;
        int col = 0;
        for (; col + 1 < copy_w; col += 2) {
            ((uint64_t *)(fb->address + fb_off))[col / 2] =
                ((uint64_t *)(backbuffer + bb_off))[col / 2];
        }
        if (col < copy_w) {
            fb->address[fb_off + col] = backbuffer[bb_off + col];
        }
    }
}

bool backbuffer_is_active(void) {
    return active;
}

int backbuffer_width(void) {
    return bb_width;
}

int backbuffer_height(void) {
    return bb_height;
}

void backbuffer_resize(int w, int h) {
    if (w > BB_MAX_WIDTH)  w = BB_MAX_WIDTH;
    if (h > BB_MAX_HEIGHT) h = BB_MAX_HEIGHT;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    bb_width = w;
    bb_height = h;
}


void backbuffer_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (!active) return;
    int idx = (unsigned char)c - 32;
    if (idx < 0 || idx >= 95) return;

    for (int row = 0; row < 8; row++) {
        uint8_t bits = font8x8_basic[idx][row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            backbuffer_put_pixel(x + col, y + row, color);
        }
    }
}

void backbuffer_draw_string(int x, int y, const char* str, uint32_t fg, uint32_t bg) {
    if (!active || !str) return;
    while (*str) {
        backbuffer_draw_char(x, y, *str, fg, bg);
        x += 8;
        str++;
    }
}

void backbuffer_draw_string_transparent(int x, int y, const char* str, uint32_t fg) {
    if (!active || !str) return;
    while (*str) {
        int idx = (unsigned char)(*str) - 32;
        if (idx >= 0 && idx < 95) {
            for (int row = 0; row < 8; row++) {
                uint8_t bits = font8x8_basic[idx][row];
                for (int col = 0; col < 8; col++) {
                    if (bits & (0x80 >> col)) {
                        backbuffer_put_pixel(x + col, y + row, fg);
                    }
                }
            }
        }
        x += 8;
        str++;
    }
}


void backbuffer_draw_char_scaled(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (!active) return;
    int idx = (unsigned char)c - 32;
    if (idx < 0 || idx >= 95) return;

    for (int row = 0; row < 8; row++) {
        uint8_t bits = font8x8_basic[idx][row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            backbuffer_put_pixel(x + col, y + row, color);
        }
    }
}

void backbuffer_draw_string_scaled(int x, int y, const char* str, uint32_t fg, uint32_t bg) {
    if (!active || !str) return;
    while (*str) {
        backbuffer_draw_char_scaled(x, y, *str, fg, bg);
        x += 8;
        str++;
    }
}

void backbuffer_draw_char_smooth(int x, int y, char c, uint32_t fg) {
    if (!active) return;
    int idx = (unsigned char)c - 32;
    if (idx < 0 || idx >= 95) return;

    int fr = (fg >> 16) & 0xFF;
    int fg_g = (fg >> 8) & 0xFF;
    int fb = fg & 0xFF;

    for (int row = 0; row < 8; row++) {
        uint8_t bits = font8x8_basic[idx][row];
        for (int col = 0; col < 8; col++) {
            int on = (bits & (0x80 >> col)) ? 1 : 0;
            if (!on) continue;

            int n_up    = (row > 0)   ? ((font8x8_basic[idx][row-1] & (0x80 >> col)) ? 1 : 0) : 0;
            int n_down  = (row < 7)   ? ((font8x8_basic[idx][row+1] & (0x80 >> col)) ? 1 : 0) : 0;
            int n_left  = (col > 0)   ? ((bits & (0x80 >> (col-1))) ? 1 : 0) : 0;
            int n_right = (col < 7)   ? ((bits & (0x80 >> (col+1))) ? 1 : 0) : 0;

            int is_edge = !n_up || !n_down || !n_left || !n_right;

            if (is_edge) {
                int off_count = 0;
                if (!n_up)    off_count++;
                if (!n_down)  off_count++;
                if (!n_left)  off_count++;
                if (!n_right) off_count++;

                uint32_t bg = backbuffer_get_pixel(x + col, y + row);
                int br = (bg >> 16) & 0xFF, bg_g2 = (bg >> 8) & 0xFF, bb = bg & 0xFF;

                int blend = off_count * 2;
                int total = blend + 8;
                int r = (fr * 8 + br * blend) / total;
                int g = (fg_g * 8 + bg_g2 * blend) / total;
                int b = (fb * 8 + bb * blend) / total;
                backbuffer_put_pixel(x + col, y + row, (r << 16) | (g << 8) | b);
            } else {
                backbuffer_put_pixel(x + col, y + row, fg);
            }
        }
    }
}

void backbuffer_draw_string_smooth(int x, int y, const char* str, uint32_t fg) {
    if (!active || !str) return;
    while (*str) {
        backbuffer_draw_char_smooth(x, y, *str, fg);
        x += 8;
        str++;
    }
}


void backbuffer_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (!active || w <= 0 || h <= 0) return;
    backbuffer_fill_rect(x, y, w, 1, color);
    backbuffer_fill_rect(x, y + h - 1, w, 1, color);
    backbuffer_fill_rect(x, y + 1, 1, h - 2, color);
    backbuffer_fill_rect(x + w - 1, y + 1, 1, h - 2, color);
}

void backbuffer_fill_rounded_rect(int x, int y, int w, int h,
                                  int r, uint32_t color) {
    if (!active || w <= 0 || h <= 0) return;

    int max_r = (w < h ? w : h) / 2;
    if (r > max_r) r = max_r;
    if (r < 1) { backbuffer_fill_rect(x, y, w, h, color); return; }

    int cx1 = x, cy1 = y, cx2 = x + w, cy2 = y + h;
    if (cx2 <= 0 || cy2 <= 0 || cx1 >= bb_width || cy1 >= bb_height) return;

    int base_r = (color >> 16) & 0xFF;
    int base_g = (color >> 8)  & 0xFF;
    int base_b =  color        & 0xFF;

    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            int px = x + dx;
            int py = y + dy;
            int inside = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    int fx = (dx * 4 + sx) - (r * 4) + 2;
                    int fy = (dy * 4 + sy) - (r * 4) + 2;
                    if (fx * fx + fy * fy <= r * r * 16)
                        inside++;
                }
            }
            if (inside < 16) {
                if (inside > 0) {
                    uint32_t bg = backbuffer_get_pixel(px, py);
                    int bg_a = 16 - inside;
                    int fg_a = inside;
                    int blend_r = (base_r * fg_a + ((bg >> 16) & 0xFF) * bg_a) / 16;
                    int blend_g = (base_g * fg_a + ((bg >> 8) & 0xFF) * bg_a) / 16;
                    int blend_b = (base_b * fg_a + (bg & 0xFF) * bg_a) / 16;
                    backbuffer_put_pixel(px, py, (blend_r << 16) | (blend_g << 8) | blend_b);
                }
            } else {
                backbuffer_put_pixel(px, py, color);
            }

            px = x + w - 1 - dx;
            py = y + dy;
            inside = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    int fx = (dx * 4 + sx) - (r * 4) + 2;
                    int fy = (dy * 4 + sy) - (r * 4) + 2;
                    if (fx * fx + fy * fy <= r * r * 16)
                        inside++;
                }
            }
            if (inside < 16) {
                if (inside > 0) {
                    uint32_t bg = backbuffer_get_pixel(px, py);
                    int bg_a = 16 - inside;
                    int fg_a = inside;
                    int blend_r = (base_r * fg_a + ((bg >> 16) & 0xFF) * bg_a) / 16;
                    int blend_g = (base_g * fg_a + ((bg >> 8) & 0xFF) * bg_a) / 16;
                    int blend_b = (base_b * fg_a + (bg & 0xFF) * bg_a) / 16;
                    backbuffer_put_pixel(px, py, (blend_r << 16) | (blend_g << 8) | blend_b);
                }
            } else {
                backbuffer_put_pixel(px, py, color);
            }

            px = x + dx;
            py = y + h - 1 - dy;
            inside = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    int fx = (dx * 4 + sx) - (r * 4) + 2;
                    int fy = (dy * 4 + sy) - (r * 4) + 2;
                    if (fx * fx + fy * fy <= r * r * 16)
                        inside++;
                }
            }
            if (inside < 16) {
                if (inside > 0) {
                    uint32_t bg = backbuffer_get_pixel(px, py);
                    int bg_a = 16 - inside;
                    int fg_a = inside;
                    int blend_r = (base_r * fg_a + ((bg >> 16) & 0xFF) * bg_a) / 16;
                    int blend_g = (base_g * fg_a + ((bg >> 8) & 0xFF) * bg_a) / 16;
                    int blend_b = (base_b * fg_a + (bg & 0xFF) * bg_a) / 16;
                    backbuffer_put_pixel(px, py, (blend_r << 16) | (blend_g << 8) | blend_b);
                }
            } else {
                backbuffer_put_pixel(px, py, color);
            }

            px = x + w - 1 - dx;
            py = y + h - 1 - dy;
            inside = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    int fx = (dx * 4 + sx) - (r * 4) + 2;
                    int fy = (dy * 4 + sy) - (r * 4) + 2;
                    if (fx * fx + fy * fy <= r * r * 16)
                        inside++;
                }
            }
            if (inside < 16) {
                if (inside > 0) {
                    uint32_t bg = backbuffer_get_pixel(px, py);
                    int bg_a = 16 - inside;
                    int fg_a = inside;
                    int blend_r = (base_r * fg_a + ((bg >> 16) & 0xFF) * bg_a) / 16;
                    int blend_g = (base_g * fg_a + ((bg >> 8) & 0xFF) * bg_a) / 16;
                    int blend_b = (base_b * fg_a + (bg & 0xFF) * bg_a) / 16;
                    backbuffer_put_pixel(px, py, (blend_r << 16) | (blend_g << 8) | blend_b);
                }
            } else {
                backbuffer_put_pixel(px, py, color);
            }
        }
    }

    int body_top = y + r;
    int body_bot = y + h - r;
    int body_left = x + r;
    int body_right = x + w - r;

    if (body_bot > body_top) {
        backbuffer_fill_rect(x, body_top, w, body_bot - body_top, color);
    }
    if (body_left > x) {
        backbuffer_fill_rect(x, y, body_left - x, r, color);
        backbuffer_fill_rect(x, body_bot, body_left - x, r, color);
    }
    if (body_right < x + w - 1) {
        backbuffer_fill_rect(body_right + 1, y, (x + w - 1) - body_right, r, color);
        backbuffer_fill_rect(body_right + 1, body_bot, (x + w - 1) - body_right, r, color);
    }
}

void backbuffer_draw_rounded_rect(int x, int y, int w, int h,
                                  int r, uint32_t color) {
    if (!active || w <= 0 || h <= 0) return;

    int max_r = (w < h ? w : h) / 2;
    if (r > max_r) r = max_r;
    if (r < 1) { backbuffer_draw_rect(x, y, w, h, color); return; }

    for (int dy = 0; dy <= r; dy++) {
        for (int dx = 0; dx <= r; dx++) {
            int dist2 = dx * dx + dy * dy;
            int r2 = r * r;
            int r1 = (r - 1) * (r - 1);
            if (r1 < 1) r1 = 0;

            if (dist2 >= r1 && dist2 <= r2) {
                backbuffer_put_pixel(x + r - dx, y + r - dy, color);
                backbuffer_put_pixel(x + w - 1 - r + dx, y + r - dy, color);
                backbuffer_put_pixel(x + r - dx, y + h - 1 - r + dy, color);
                backbuffer_put_pixel(x + w - 1 - r + dx, y + h - 1 - r + dy, color);
            }
        }
    }

    int yt = y + r;
    int yb = y + h - r - 1;
    int xl = x + r;
    int xr = x + w - r - 1;

    if (xr >= xl) {
        backbuffer_fill_rect(xl, y, xr - xl + 1, 1, color);
        backbuffer_fill_rect(xl, y + h - 1, xr - xl + 1, 1, color);
    }
    if (yb >= yt) {
        backbuffer_fill_rect(x, yt, 1, yb - yt + 1, color);
        backbuffer_fill_rect(x + w - 1, yt, 1, yb - yt + 1, color);
    }
}
