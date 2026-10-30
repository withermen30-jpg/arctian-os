/**
 * Arctian GUI Cursor Header
 * 16x16 pixel arrow cursor with clean pixel art
 */
#ifndef CURSOR_H
#define CURSOR_H

#include <stdint.h>
#include <stdbool.h>

#define CURSOR_WIDTH  16
#define CURSOR_HEIGHT 16

/* Hotspot: the "tip" of the arrow (0,0) in cursor coords */
#define CURSOR_HOTSPOT_X 0
#define CURSOR_HOTSPOT_Y 0

/** Draw the cursor at (mx, my) on the backbuffer */
void cursor_draw(int mx, int my);

#endif /* CURSOR_H */
