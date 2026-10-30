/**
 * Arctian GUI Cursor Implementation
 * 16x16 pixel-art arrow cursor, clean and sharp.
 */
#include "cursor.h"
#include "backbuffer.h"

/*
 * 16x16 arrow cursor pixel map:
 *   . = transparent (no pixel)
 *   # = black border
 *   W = white fill
 *
 * Legend:
 *   # 0x0B0F14 (dark border)
 *   W 0xFFFFFF (white fill)
 */
static const uint8_t cursor_data[CURSOR_HEIGHT][CURSOR_WIDTH] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
    {0,1,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
    {0,1,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
    {0,1,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {0,1,2,2,2,2,2,2,2,1,0,0,0,0,0,0},
    {0,1,2,2,2,2,2,2,2,2,1,0,0,0,0,0},
    {0,1,2,2,2,2,1,1,1,1,1,1,0,0,0,0},
    {0,1,2,2,1,2,2,1,0,0,0,0,0,0,0,0},
    {0,1,2,1,0,1,2,2,1,0,0,0,0,0,0,0},
    {0,1,1,0,0,1,2,2,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0},
};

#define CURSOR_BORDER  0x0B0F14
#define CURSOR_FILL    0xFFFFFF

static void put_px(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= backbuffer_width() || y >= backbuffer_height())
        return;
    backbuffer_put_pixel(x, y, color);
}

void cursor_draw(int mx, int my) {
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            uint8_t p = cursor_data[row][col];
            if (p == 0) continue;

            /* Gölge (2px sağ, 2px aşağı, yarı saydam) */
            put_px(mx + col + 2, my + row + 2, 0x60000000);

            /* Ana pixel */
            put_px(mx + col, my + row, (p == 1) ? CURSOR_BORDER : CURSOR_FILL);
        }
    }
}
