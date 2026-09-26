#include "desktop.h"
#include "ui_kit.h"
#include "cursor.h"
#include "installer.h"
#include "wallpaper.h"
#include "rtc.h"
#include "backbuffer.h"
#include "mouse.h"
#include "usb_hid.h"
#include "kernel.h"
#include <stdint.h>
#include <stdbool.h>

extern const char* keyboard_get_buffer(void);
extern void keyboard_clear_buffer(void);

#include "keyboard.h"


static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static int next_window_id = 1;
static int active_window_id = -1;

static startmenu_state_t startmenu_state = STARTMENU_HIDDEN;

static int mouse_x = 0;
static int mouse_y = 0;
static bool mouse_left = false;
static bool prev_mouse_left = false;
static char clock_text[6] = "??:??";


#define FROSTED_BLUR_R 1
#define FROSTED_BLUR_SZ 3

static void apply_frosted_effect(int rx, int ry, int rw, int rh,
                                  uint32_t tint_color, int alpha) {
    int bw = backbuffer_width();
    int bh = backbuffer_height();

    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > bw) rw = bw - rx;
    if (ry + rh > bh) rh = bh - ry;
    if (rw <= 0 || rh <= 0) return;

    uint32_t row_buf[2560];
    (void)row_buf;
    if (rw > 2560) rw = 2560;

    uint8_t tint_r = (tint_color >> 16) & 0xFF;
    uint8_t tint_g = (tint_color >> 8) & 0xFF;
    uint8_t tint_b = tint_color & 0xFF;

    for (int row = 0; row < rh; row++) {
        int sy = ry + row;

        for (int col = 0; col < rw; col++) {
            int sx = rx + col;

            uint32_t r_acc = 0, g_acc = 0, b_acc = 0;
            int count = 0;
            for (int k = -FROSTED_BLUR_R; k <= FROSTED_BLUR_R; k++) {
                int px = sx + k;
                if (px < 0 || px >= bw) continue;
                uint32_t c = backbuffer_get_pixel(px, sy);
                r_acc += (c >> 16) & 0xFF;
                g_acc += (c >> 8) & 0xFF;
                b_acc += c & 0xFF;
                count++;
            }
            if (count == 0) { row_buf[col] = 0; continue; }
            uint8_t r = (uint8_t)(r_acc / count);
            uint8_t g = (uint8_t)(g_acc / count);
            uint8_t b = (uint8_t)(b_acc / count);
            row_buf[col] = (r << 16) | (g << 8) | b;
        }

        for (int col = 0; col < rw; col++) {
            int sx = rx + col;

            uint32_t r_acc = 0, g_acc = 0, b_acc = 0;
            int count = 0;
            for (int k = -FROSTED_BLUR_R; k <= FROSTED_BLUR_R; k++) {
                int py = sy + k;
                if (py < 0 || py >= bh) continue;
                uint32_t c = backbuffer_get_pixel(sx, py);
                r_acc += (c >> 16) & 0xFF;
                g_acc += (c >> 8) & 0xFF;
                b_acc += c & 0xFF;
                count++;
            }

            uint8_t blur_r, blur_g, blur_b;
            if (count == 0) { blur_r = 0; blur_g = 0; blur_b = 0; }
            else {
                blur_r = (uint8_t)(r_acc / count);
                blur_g = (uint8_t)(g_acc / count);
                blur_b = (uint8_t)(b_acc / count);
            }

            int inv_alpha = 255 - alpha;
            uint8_t fr = (uint8_t)((blur_r * inv_alpha + tint_r * alpha) / 255);
            uint8_t fg = (uint8_t)((blur_g * inv_alpha + tint_g * alpha) / 255);
            uint8_t fb = (uint8_t)((blur_b * inv_alpha + tint_b * alpha) / 255);

            backbuffer_put_pixel(sx, sy, (fr << 16) | (fg << 8) | fb);
        }
    }
}


static void draw_desktop_background(void) {
    wallpaper_draw_scaled();

    ui_draw_text(backbuffer_width() - 116, 24, "Arctian 64", COLOR_TEXT_SECOND);

    int cw = (int)strlen(clock_text) * 8;
    ui_draw_text(backbuffer_width() - cw - 16, backbuffer_height() - 80, clock_text, COLOR_TEXT_SECOND);
}


static void draw_taskbar(void) {
    int bw = backbuffer_width();
    int tb_w = (bw < 1400) ? TASKBAR_FLOATING_W_MIN : bw * 3 / 5;
    int tb_h = TASKBAR_HEIGHT;
    int tb_x = (backbuffer_width() - tb_w) / 2;
    int tb_y = backbuffer_height() - tb_h - TASKBAR_ELEVATION;

    for (int li = 0; li < 5; li++) {
        int off = li + 2;
        uint32_t sh_col = 0x0000000C + (li << 2);
        backbuffer_fill_rounded_rect(tb_x - off + 4, tb_y + off, tb_w - 8, tb_h,
                                      TASKBAR_RADIUS, sh_col);
    }

    apply_frosted_effect(tb_x, tb_y, tb_w, tb_h, COLOR_TASKBAR, 160);

    backbuffer_draw_rounded_rect(tb_x, tb_y, tb_w, tb_h, TASKBAR_RADIUS, 0x344255);

    for (int cx = tb_x + TASKBAR_RADIUS; cx < tb_x + tb_w - TASKBAR_RADIUS; cx++)
        backbuffer_put_pixel(cx, tb_y + 1, 0x2CFFFFFF);

    int start_btn_x = tb_x + 14;
    int start_btn_y = tb_y + 10;
    int start_btn_size = 30;

    backbuffer_fill_rounded_rect(start_btn_x, start_btn_y, start_btn_size, start_btn_size, 9, 0x5A371E);
    backbuffer_draw_rounded_rect(start_btn_x, start_btn_y, start_btn_size, start_btn_size, 9, 0x8A5630);
    ui_draw_arctian_logo(start_btn_x + start_btn_size / 2, start_btn_y + start_btn_size / 2, 20);

    int app_btn_x = tb_x + 58;
    for (int i = 0; i < window_count; i++) {
        window_t *w = &windows[i];
        if (w->state == WINDOW_CLOSED) continue;

        int btn_w = 118;
        int btn_h = tb_h - 14;
        int btn_y = tb_y + 7;
        uint32_t btn_bg = (w->active) ? 0x1C2D40 : 0x111824;
        uint32_t btn_fg = (w->active) ? COLOR_ACCENT : COLOR_TEXT_SECOND;

        backbuffer_fill_rounded_rect(app_btn_x, btn_y, btn_w, btn_h, 10, btn_bg);
        if (w->active) {
            backbuffer_fill_rounded_rect(app_btn_x + 10, btn_y + 1, btn_w - 20, 2, 1, COLOR_ACCENT);
        }
        ui_draw_text(app_btn_x + 8, btn_y + (btn_h - 8) / 2, w->title, btn_fg);

        app_btn_x += btn_w + 6;
    }

    int tray_x = tb_x + tb_w - 138;
    int tray_y = tb_y + (tb_h - 8) / 2;
    ui_draw_text(tray_x, tray_y, "64", UI_COLOR_TEXT);
    ui_draw_text(tray_x + 20, tray_y, "NET", UI_COLOR_TEXT_SECOND);
    backbuffer_draw_string_smooth(tray_x + 56, tray_y - 4, clock_text, UI_COLOR_TEXT);
}


static void draw_start_menu(void) {
    if (startmenu_state == STARTMENU_HIDDEN) return;

    int sm_x = (backbuffer_width() - STARTMENU_WIDTH) / 2;
    int sm_y = backbuffer_height() - TASKBAR_HEIGHT - TASKBAR_ELEVATION - STARTMENU_HEIGHT;

    apply_frosted_effect(sm_x, sm_y, STARTMENU_WIDTH, STARTMENU_HEIGHT,
                          COLOR_START_MENU, 170);

    backbuffer_fill_rounded_rect(sm_x, sm_y, STARTMENU_WIDTH, STARTMENU_HEIGHT,
                                  WINDOW_RADIUS, 0x00000000);
    backbuffer_draw_rounded_rect(sm_x, sm_y, STARTMENU_WIDTH, STARTMENU_HEIGHT, WINDOW_RADIUS, 0x334255);

    backbuffer_fill_rounded_rect(sm_x + 16, sm_y + 16, STARTMENU_WIDTH - 32, 64, 12, 0x121B27);
    backbuffer_fill_rounded_rect(sm_x + 28, sm_y + 24, 42, 42, 21, COLOR_ACCENT);
    backbuffer_draw_string_smooth(sm_x + 80, sm_y + 26, "Arctian User", COLOR_TEXT);
    ui_draw_text(sm_x + 80, sm_y + 46, "Masaustu hesabi", COLOR_TEXT_SECOND);

    ui_draw_separator(sm_x + 16, sm_y + 86, STARTMENU_WIDTH - 32, 0x273444);

    ui_draw_text(sm_x + 20, sm_y + 108, "Uygulama yok", COLOR_TEXT_SECOND);

    ui_draw_separator(sm_x + 16, sm_y + STARTMENU_HEIGHT - 74, STARTMENU_WIDTH - 32, 0x273444);

    int btn_y = sm_y + STARTMENU_HEIGHT - 58;
    ui_draw_button(sm_x + 16, btn_y, 104, 40, 10, "Kapat", UI_BTN_DANGER, true, mouse_x, mouse_y);
    ui_draw_button(sm_x + 128, btn_y, 104, 40, 10, "Yen.Baslat", UI_BTN_PRIMARY, true, mouse_x, mouse_y);
}


static window_t* find_window(int id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id && windows[i].state != WINDOW_CLOSED)
            return &windows[i];
    }
    return NULL;
}

static int get_window_index(int id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id)
            return i;
    }
    return -1;
}

static void bring_to_front(int id) {
    int idx = get_window_index(id);
    if (idx < 0 || idx >= window_count) return;

    int last = window_count - 1;
    if (idx == last) return;

    window_t tmp = windows[idx];
    for (int i = idx; i < last; i++) {
        windows[i] = windows[i + 1];
    }
    windows[last] = tmp;
}


static void draw_window_content(window_t *w) {
    ui_draw_text_centered(w->x, w->y + 56, w->width, "Icerik hazirlaniyor", COLOR_TEXT_SECOND);
}

static void draw_window_frame(window_t *w) {
    int w_x = w->x;
    int w_y = w->y;
    int w_w = w->width;
    int w_h = w->height;
    int title_h = 40;

    backbuffer_fill_rounded_rect(w_x + 2, w_y + 3, w_w, w_h, WINDOW_RADIUS, 0x00000014);
    backbuffer_fill_rounded_rect(w_x + 4, w_y + 6, w_w, w_h, WINDOW_RADIUS, 0x0000000D);
    backbuffer_fill_rounded_rect(w_x + 6, w_y + 9, w_w, w_h, WINDOW_RADIUS, 0x00000007);

    backbuffer_fill_rounded_rect(w_x, w_y, w_w, w_h, WINDOW_RADIUS, COLOR_WINDOW_BODY);

    backbuffer_draw_rounded_rect(w_x, w_y, w_w, w_h, WINDOW_RADIUS, 0x3A2820);

    uint32_t title_bg = w->active ? COLOR_WINDOW_TITLE : 0x0E1621;
    backbuffer_fill_rect(w_x + WINDOW_RADIUS, w_y, w_w - 2 * WINDOW_RADIUS, title_h, title_bg);
    backbuffer_fill_rect(w_x, w_y + WINDOW_RADIUS, WINDOW_RADIUS, title_h - WINDOW_RADIUS, title_bg);
    backbuffer_fill_rect(w_x + w_w - WINDOW_RADIUS, w_y + WINDOW_RADIUS, WINDOW_RADIUS, title_h - WINDOW_RADIUS, title_bg);

    int r = WINDOW_RADIUS;
    int r4 = 4 * r * r;
    for (int cx = 0; cx < r; cx++) {
        for (int cy = 0; cy < r; cy++) {
            int dx = 2 * cx - 2 * r + 1;
            int dy = 2 * cy - 2 * r + 1;
            if (dx * dx + dy * dy <= r4) {
                backbuffer_put_pixel(w_x + cx, w_y + cy, title_bg);
                backbuffer_put_pixel(w_x + w_w - 1 - cx, w_y + cy, title_bg);
            }
        }
    }

    for (int cx = w_x + r; cx < w_x + w_w - r; cx++)
        backbuffer_put_pixel(cx, w_y + 1, 0x28FFFFFF);

    uint32_t title_color = w->active ? COLOR_TEXT : COLOR_TEXT_SECOND;
    backbuffer_fill_rounded_rect(w_x + 14, w_y + 14, 10, 10, 5, w->active ? COLOR_ACCENT : COLOR_TEXT_SECOND);
    backbuffer_draw_string_smooth(w_x + 32, w_y + 11, w->title, title_color);

    int btn_size = 20;
    int btn_gap = 8;
    int btn_y = w_y + 10;
    int close_x = w_x + w_w - 14 - btn_size;
    int max_x = close_x - btn_gap - btn_size;
    int min_x = max_x - btn_gap - btn_size;

    bool hover_close = (mouse_x >= close_x && mouse_x < close_x + btn_size &&
                        mouse_y >= btn_y && mouse_y < btn_y + btn_size);
    bool hover_max   = (mouse_x >= max_x   && mouse_x < max_x + btn_size &&
                        mouse_y >= btn_y && mouse_y < btn_y + btn_size);
    bool hover_min   = (mouse_x >= min_x   && mouse_x < min_x + btn_size &&
                        mouse_y >= btn_y && mouse_y < btn_y + btn_size);

    uint32_t ctrl_bg       = 0x182432;
    uint32_t ctrl_hover_bg = 0x233040;
    uint32_t close_bg      = hover_close ? 0x4A2A32 : 0x37222A;
    uint32_t max_bg        = hover_max   ? ctrl_hover_bg : ctrl_bg;
    uint32_t min_bg        = hover_min   ? ctrl_hover_bg : ctrl_bg;

    uint32_t ctrl_symbol       = 0xD3DEEA;
    uint32_t ctrl_symbol_hover = 0xF7EEE3;
    uint32_t close_symbol       = hover_close ? 0xFFFFFF : 0xFFB4B4;
    uint32_t max_symbol        = hover_max   ? ctrl_symbol_hover : ctrl_symbol;
    uint32_t min_symbol        = hover_min   ? ctrl_symbol_hover : ctrl_symbol;

    backbuffer_fill_rounded_rect(min_x, btn_y, btn_size, btn_size, 6, min_bg);
    backbuffer_fill_rect(min_x + 5, btn_y + 10, 10, 2, min_symbol);

    backbuffer_fill_rounded_rect(max_x, btn_y, btn_size, btn_size, 6, max_bg);
    if (w->state == WINDOW_MAXIMIZED) {
        backbuffer_fill_rect(max_x + 7, btn_y + 5, 8, 8, max_bg);
        backbuffer_draw_rect(max_x + 7, btn_y + 5, 8, 8, max_symbol);
        backbuffer_fill_rect(max_x + 5, btn_y + 7, 8, 8, max_bg);
        backbuffer_draw_rect(max_x + 5, btn_y + 7, 8, 8, max_symbol);
    } else {
        backbuffer_fill_rect(max_x + 5, btn_y + 5, 10, 10, max_symbol);
        backbuffer_fill_rect(max_x + 7, btn_y + 7, 6, 6, max_bg);
    }

    backbuffer_fill_rounded_rect(close_x, btn_y, btn_size, btn_size, 6, close_bg);
    for (int i = 5; i <= 13; i++) {
        backbuffer_put_pixel(close_x + i,     btn_y + i,      close_symbol);
        backbuffer_put_pixel(close_x + i + 1, btn_y + i,      close_symbol);
        backbuffer_put_pixel(close_x + i,     btn_y + 19 - i, close_symbol);
        backbuffer_put_pixel(close_x + i + 1, btn_y + 19 - i, close_symbol);
    }

    backbuffer_fill_rect(w_x + 2, w_y + title_h, w_w - 4, 1, 0x253244);

    int content_y = w_y + title_h + 1;
    int content_h = w_h - title_h - 1;
    if (content_h > 0) {
        backbuffer_fill_rect(w_x + 2, content_y, w_w - 4, content_h, COLOR_WINDOW_BODY);
    }
}

static void draw_windows(void) {
    for (int i = 0; i < window_count; i++) {
        window_t *w = &windows[i];
        if (w->state == WINDOW_CLOSED || w->state == WINDOW_MINIMIZED)
            continue;

        draw_window_frame(w);
        draw_window_content(w);
    }
}


static void draw_cursor(void) {
    extern void cursor_draw(int mx, int my);
    cursor_draw(mouse_x, mouse_y);
}


static void handle_mouse_click(void) {
    if (!mouse_left) return;

    int tb_w = (backbuffer_width() < 1400) ? TASKBAR_FLOATING_W_MIN : backbuffer_width() * 3 / 5;
    int tb_h = TASKBAR_HEIGHT;
    int tb_x = (backbuffer_width() - tb_w) / 2;
    int tb_y = backbuffer_height() - tb_h - TASKBAR_ELEVATION;

    if (mouse_y >= tb_y && mouse_y < tb_y + tb_h) {
        if (mouse_x >= tb_x + 12 && mouse_x <= tb_x + 12 + 32) {
            gui_toggle_start_menu();
            return;
        }

        int app_btn_x = tb_x + 56;
        for (int i = 0; i < window_count; i++) {
            window_t *w = &windows[i];
            if (w->state == WINDOW_CLOSED) continue;

            int btn_w = 120;
            int btn_h = tb_h - 10;
            int btn_y2 = tb_y + 5;

            if (mouse_x >= app_btn_x && mouse_x < app_btn_x + btn_w &&
                mouse_y >= btn_y2 && mouse_y < btn_y2 + btn_h) {
                gui_activate_window(w->id);
                return;
            }
            app_btn_x += btn_w + 4;
        }
        return;
    }

    if (startmenu_state == STARTMENU_VISIBLE) {
        int sm_x = (DESKTOP_WIDTH - STARTMENU_WIDTH) / 2;
        int sm_y = DESKTOP_HEIGHT - TASKBAR_HEIGHT - TASKBAR_ELEVATION - STARTMENU_HEIGHT;

        int btn_y = sm_y + STARTMENU_HEIGHT - 58;
        if (mouse_x >= sm_x + 16 && mouse_x < sm_x + 120 &&
            mouse_y >= btn_y && mouse_y < btn_y + 40) {
            extern void system_shutdown(void);
            system_shutdown();
            return;
        }
        if (mouse_x >= sm_x + 128 && mouse_x < sm_x + 232 &&
            mouse_y >= btn_y && mouse_y < btn_y + 40) {
            extern void system_reboot(void);
            system_reboot();
            return;
        }

        if (mouse_x < sm_x || mouse_x > sm_x + STARTMENU_WIDTH ||
            mouse_y < sm_y || mouse_y > sm_y + STARTMENU_HEIGHT) {
            int tb_y2 = backbuffer_height() - TASKBAR_HEIGHT - TASKBAR_ELEVATION;
            if (mouse_y < tb_y2) {
                startmenu_state = STARTMENU_HIDDEN;
            }
        }
        return;
    }

    for (int i = window_count - 1; i >= 0; i--) {
        window_t *w = &windows[i];
        if (w->state == WINDOW_CLOSED || w->state == WINDOW_MINIMIZED) continue;

        if (mouse_x >= w->x && mouse_x < w->x + w->width &&
            mouse_y >= w->y && mouse_y < w->y + 40) {
            gui_activate_window(w->id);

            int btn_size = 20;
            int btn_gap = 8;
            int btn_y = w->y + 10;
            int close_x = w->x + w->width - 14 - btn_size;
            int max_x = close_x - btn_gap - btn_size;
            int min_x = max_x - btn_gap - btn_size;

            if (mouse_x >= close_x && mouse_x < close_x + btn_size &&
                mouse_y >= btn_y && mouse_y < btn_y + btn_size) {
                gui_close_window(w->id);
                return;
            }

            if (mouse_x >= max_x && mouse_x < max_x + btn_size &&
                mouse_y >= btn_y && mouse_y < btn_y + btn_size) {
                gui_toggle_maximize(w->id);
                return;
            }

            if (mouse_x >= min_x && mouse_x < min_x + btn_size &&
                mouse_y >= btn_y && mouse_y < btn_y + btn_size) {
                gui_toggle_minimize(w->id);
                return;
            }

            w->is_dragging = true;
            w->drag_off_x = mouse_x - w->x;
            w->drag_off_y = mouse_y - w->y;
            return;
        }

        if (mouse_x >= w->x && mouse_x < w->x + w->width &&
            mouse_y >= w->y + 41 && mouse_y < w->y + w->height) {
            gui_activate_window(w->id);
            return;
        }
    }

    if (startmenu_state == STARTMENU_VISIBLE) {
        startmenu_state = STARTMENU_HIDDEN;
    }
}

static void handle_dragging(void) {
    for (int i = 0; i < window_count; i++) {
        window_t *w = &windows[i];
        if (w->state == WINDOW_CLOSED || w->state == WINDOW_MINIMIZED) continue;

        if (w->is_dragging) {
            int new_x = mouse_x - w->drag_off_x;
            int new_y = mouse_y - w->drag_off_y;

            if (new_x < 0) new_x = 0;
            if (new_y < 0) new_y = 0;
            if (new_x + w->width > backbuffer_width()) new_x = backbuffer_width() - w->width;
            if (new_y + w->height > backbuffer_height() - TASKBAR_HEIGHT - TASKBAR_ELEVATION)
                new_y = backbuffer_height() - TASKBAR_HEIGHT - TASKBAR_ELEVATION - w->height;

            w->x = new_x;
            w->y = new_y;

            if (!mouse_left) {
                w->is_dragging = false;
            }
            return;
        }
    }
}


int gui_create_window(const char *title, int x, int y, int w, int h) {
    if (window_count >= MAX_WINDOWS) return -1;

    window_t *win = &windows[window_count];
    win->id = next_window_id++;
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->state = WINDOW_NORMAL;
    win->active = true;
    win->draggable = true;
    win->is_dragging = false;

    int ti = 0;
    while (title[ti] && ti < 63) {
        win->title[ti] = title[ti];
        ti++;
    }
    win->title[ti] = '\0';

    win->prev_x = x;
    win->prev_y = y;
    win->prev_w = w;
    win->prev_h = h;

    for (int i = 0; i < window_count; i++) {
        windows[i].active = false;
    }
    win->active = true;
    active_window_id = win->id;

    window_count++;
    return win->id;
}

void gui_close_window(int id) {
    window_t *w = find_window(id);
    if (!w) return;
    w->state = WINDOW_CLOSED;

    if (id == active_window_id) {
        active_window_id = -1;
        for (int i = window_count - 1; i >= 0; i--) {
            if (windows[i].state != WINDOW_CLOSED) {
                windows[i].active = true;
                active_window_id = windows[i].id;
                break;
            }
        }
    }
}

void gui_activate_window(int id) {
    window_t *w = find_window(id);
    if (!w) return;

    for (int i = 0; i < window_count; i++) {
        windows[i].active = (windows[i].id == id);
    }
    active_window_id = id;

    if (w->state == WINDOW_MINIMIZED) {
        w->state = WINDOW_NORMAL;
    }

    bring_to_front(id);
}

void gui_move_window(int id, int x, int y) {
    window_t *w = find_window(id);
    if (!w) return;
    w->x = x;
    w->y = y;
}

void gui_toggle_maximize(int id) {
    window_t *w = find_window(id);
    if (!w) return;

    if (w->state == WINDOW_MAXIMIZED) {
        w->x = w->prev_x;
        w->y = w->prev_y;
        w->width = w->prev_w;
        w->height = w->prev_h;
        w->state = WINDOW_NORMAL;
    } else {
        w->prev_x = w->x;
        w->prev_y = w->y;
        w->prev_w = w->width;
        w->prev_h = w->height;
        w->x = 0;
        w->y = 0;
        w->width = DESKTOP_WIDTH;
        w->height = DESKTOP_HEIGHT - TASKBAR_HEIGHT - TASKBAR_ELEVATION;
        w->state = WINDOW_MAXIMIZED;
    }
}

void gui_toggle_minimize(int id) {
    window_t *w = find_window(id);
    if (!w) return;

    if (w->state == WINDOW_MINIMIZED) {
        w->state = WINDOW_NORMAL;
    } else {
        w->state = WINDOW_MINIMIZED;
    }
}

window_t *gui_get_window(int id) {
    return find_window(id);
}

void gui_toggle_start_menu(void) {
    if (startmenu_state == STARTMENU_VISIBLE)
        startmenu_state = STARTMENU_HIDDEN;
    else
        startmenu_state = STARTMENU_VISIBLE;
}

bool gui_is_startmenu_visible(void) {
    return (startmenu_state == STARTMENU_VISIBLE);
}

void gui_update_mouse(int mx, int my, bool left_click) {
    mouse_x = mx;
    mouse_y = my;
    mouse_left = left_click;
}


void gui_init(void) {
    kernel_debug("GUI baslatiliyor (backbuffer + UI Kit)...");

    mouse_set_screen_size(DESKTOP_WIDTH, DESKTOP_HEIGHT);

    kernel_debug("GUI basariyla baslatildi");
}

void gui_run_installer(void) {
    kernel_debug("Kurulum sihirbazi baslatiliyor...");

    installer_mode_t mode = installer_run();

    if (mode == INSTALL_MODE_PERMANENT) {
        return;
    }

    kernel_debug("Test modu - masaustu baslatiliyor");
}

void gui_main_loop(void) {
    mouse_state_t *mouse = mouse_get_state();

    while (1) {
        rtc_format_time(clock_text, sizeof(clock_text));
        usb_hid_poll();

    keyboard_event_t ev;
    while (keyboard_pop_event(&ev)) {
        if (!ev.pressed) continue;

        if (ev.alt && ev.scancode == 0x3E) {
            if (active_window_id >= 0) {
                gui_close_window(active_window_id);
            }
            continue;
        }
    }

        mouse_x = mouse->x;
        mouse_y = mouse->y;
        mouse_left = mouse->buttons[MOUSE_BUTTON_LEFT] ||
                     mouse->buttons[MOUSE_BUTTON_RIGHT] ||
                     mouse->buttons[MOUSE_BUTTON_MIDDLE];
        bool mouse_pressed = mouse_left && !prev_mouse_left;

        if (mouse_pressed) {
            handle_mouse_click();
        }
        if (mouse_left) {
            handle_dragging();
        } else {
            for (int i = 0; i < window_count; i++) {
                if (windows[i].is_dragging) {
                    windows[i].is_dragging = false;
                }
            }
        }

        draw_desktop_background();

        if (startmenu_state == STARTMENU_VISIBLE) {
            draw_start_menu();
        }

        draw_windows();
        draw_taskbar();
        draw_cursor();

        backbuffer_blit();

        for (volatile int i = 0; i < 500000; i++) {
            asm volatile ("nop");
        }
        prev_mouse_left = mouse_left;
    }
}
