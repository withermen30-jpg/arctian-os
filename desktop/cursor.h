#ifndef CURSOR_H
#define CURSOR_H

#include <stdint.h>
#include <stdbool.h>

#define CURSOR_WIDTH  16
#define CURSOR_HEIGHT 16

#define CURSOR_HOTSPOT_X 0
#define CURSOR_HOTSPOT_Y 0

void cursor_draw(int mx, int my);

#endif
