#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char ch;
    uint8_t scancode;
    bool pressed;
    bool alt;
    bool ctrl;
    bool shift;
} keyboard_event_t;

bool keyboard_pop_event(keyboard_event_t *ev);

void keyboard_process_usb_queue(void);

void keyboard_push_char(char c);

void keyboard_clear_buffer(void);

extern const char keyboard_map_normal[128];
extern const char keyboard_map_shift[128];

const char* keyboard_get_buffer(void);

#endif
