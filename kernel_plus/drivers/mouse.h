/**
 * Arctian PS/2 Mouse Driver Header
 * Professional PS/2 mouse with IntelliMouse (scroll wheel) support
 * IRQ12 (INT 44) - VESA framebuffer cursor
 */
#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

/* Button indices for mouse_state.buttons[] */
#define MOUSE_BUTTON_LEFT   0
#define MOUSE_BUTTON_RIGHT  1
#define MOUSE_BUTTON_MIDDLE 2

/* Maximum number of buttons tracked */
#define MOUSE_MAX_BUTTONS   3

/**
 * Complete mouse state structure
 * All fields are updated atomically by the IRQ handler
 */
typedef struct {
    int x;                  /* Absolute X position (0 .. screen_width-1) */
    int y;                  /* Absolute Y position (0 .. screen_height-1) */
    int dx;                 /* Last frame X movement delta */
    int dy;                 /* Last frame Y movement delta */
    int scroll;             /* Scroll wheel delta (IntelliMouse) */
    bool buttons[MOUSE_MAX_BUTTONS];  /* Left, Right, Middle press state */
    bool present;           /* Mouse hardware detected and initialized */
    bool scroll_wheel;      /* IntelliMouse with scroll wheel detected */
} mouse_state_t;

/* Screen dimensions (set by VESA or framebuffer init) */
extern int mouse_screen_width;
extern int mouse_screen_height;

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

/** Initialize PS/2 mouse: detect, configure, enable IRQ12 */
void mouse_init(void);

/** Get pointer to the global mouse state (read-only outside driver) */
mouse_state_t* mouse_get_state(void);

/** Update screen dimensions when resolution changes */
void mouse_set_screen_size(int width, int height);

/** IRQ12 handler (exposed for ISR registration in mouse_init) */
void mouse_irq_handler(void);

/* 8042 cikis tamponunu yoklayarak fare paketlerini isler (IRQ12 gelmese de
 * girdi calissin). Masaustu her karede cagirir; ucuzdur (tampon bossa tek
 * port okumasi). */
void mouse_poll(void);

/* Tek bir PS/2 baytini paket durum makinesine verir. */
void mouse_feed_byte(uint8_t data);

/** Sol tus basma kenari sayaci (kaybolan tiklamalari yakalamak icin) */
uint32_t mouse_left_press_seq(void);

#endif /* MOUSE_H */
