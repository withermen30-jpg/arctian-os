#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdint.h>
#include <stdbool.h>


#define COLOR_BG            0x1A120E
#define COLOR_TASKBAR       0x241816
#define COLOR_WINDOW_TITLE  0x2A1E18
#define COLOR_WINDOW_BODY   0x241816
#define COLOR_ACCENT        0xD8A15A
#define COLOR_ACCENT_HOVER  0xE4BD7B
#define COLOR_TEXT          0xF7EEE3
#define COLOR_TEXT_SECOND   0xD1BAA7
#define COLOR_BTN_PRIMARY   0xD8A15A
#define COLOR_BTN_CLOSE     0xD96B5F
#define COLOR_START_MENU    0x2A1B16


#define DESKTOP_WIDTH       1920
#define DESKTOP_HEIGHT      1080
#define TASKBAR_FLOATING_W_MIN 760
#define TASKBAR_HEIGHT      60
#define TASKBAR_FLOATING_W  760
#define TASKBAR_ELEVATION   16
#define TASKBAR_RADIUS      30
#define STARTMENU_WIDTH     420
#define STARTMENU_HEIGHT    500
#define WINDOW_RADIUS       16
#define TASKBAR_ALPHA       102


typedef enum {
    WINDOW_NORMAL,
    WINDOW_MINIMIZED,
    WINDOW_MAXIMIZED,
    WINDOW_CLOSED
} window_state_t;

typedef struct {
    int id;
    int x, y;
    int width, height;
    int prev_x, prev_y;
    int prev_w, prev_h;
    char title[64];
    window_state_t state;
    bool active;
    bool draggable;
    int drag_off_x, drag_off_y;
    bool is_dragging;
} window_t;

#define MAX_WINDOWS 16


typedef enum {
    STARTMENU_HIDDEN,
    STARTMENU_VISIBLE
} startmenu_state_t;


int  gui_create_window(const char* title, int x, int y, int w, int h);
void gui_close_window(int id);
void gui_activate_window(int id);
void gui_move_window(int id, int x, int y);
void gui_toggle_maximize(int id);
void gui_toggle_minimize(int id);
window_t* gui_get_window(int id);

void gui_toggle_start_menu(void);
bool gui_is_startmenu_visible(void);

void gui_init(void);
void gui_run_installer(void);
void gui_main_loop(void);

void gui_update_mouse(int mx, int my, bool left_click);

#endif
