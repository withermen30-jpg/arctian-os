#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

#define MOUSE_BUTTON_LEFT   0
#define MOUSE_BUTTON_RIGHT  1
#define MOUSE_BUTTON_MIDDLE 2

#define MOUSE_MAX_BUTTONS   3

typedef struct {
    int x;
    int y;
    int dx;
    int dy;
    int scroll;
    bool buttons[MOUSE_MAX_BUTTONS];
    bool present;
    bool scroll_wheel;
} mouse_state_t;

extern int mouse_screen_width;
extern int mouse_screen_height;


void mouse_init(void);

mouse_state_t* mouse_get_state(void);

void mouse_set_screen_size(int width, int height);

void mouse_irq_handler(void);

#endif
