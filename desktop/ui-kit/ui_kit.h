#ifndef UI_KIT_H
#define UI_KIT_H

#include <stdint.h>
#include <stdbool.h>


#define UI_COLOR_BASE_00      0x0E0A08
#define UI_COLOR_BASE_01      0x160F0C
#define UI_COLOR_BASE_02      0x1E1511
#define UI_COLOR_BASE_03      0x271A15
#define UI_COLOR_BASE_04      0x31211A
#define UI_COLOR_BASE_05      0x3C2820

#define UI_COLOR_PRIMARY      UI_COLOR_BASE_00
#define UI_COLOR_BG           UI_COLOR_BASE_01
#define UI_COLOR_SURFACE      UI_COLOR_BASE_02
#define UI_COLOR_SURFACE2     UI_COLOR_BASE_03

#define UI_COLOR_ACCENT_DEEP  0xB8813A
#define UI_COLOR_ACCENT       0xD8A15A
#define UI_COLOR_ACCENT_HOVER 0xEABC72
#define UI_COLOR_ACCENT_LIGHT 0xF5D99A
#define UI_COLOR_ACCENT_GLOW  0x8B5E2A

#define UI_COLOR_SUCCESS      0x4DBD8A
#define UI_COLOR_SUCCESS_BG   0x112A1E
#define UI_COLOR_WARNING      0xF0C040
#define UI_COLOR_WARNING_BG   0x2A2108
#define UI_COLOR_DANGER       0xE05555
#define UI_COLOR_DANGER_BG    0x2A1010
#define UI_COLOR_INFO         0x5AABDB
#define UI_COLOR_INFO_BG      0x0D2030

#define UI_COLOR_BORDER       0x3A2820
#define UI_COLOR_BORDER_MID   0x4E362A
#define UI_COLOR_BORDER_LIGHT 0x6A4A38
#define UI_COLOR_BORDER_FOCUS 0xD8A15A

#define UI_COLOR_TEXT         0xF0E8DE
#define UI_COLOR_TEXT_SECOND  0xC8B09A
#define UI_COLOR_TEXT_HINT    0x9A7E6A
#define UI_COLOR_TEXT_LIGHT   0xFFFFFF
#define UI_COLOR_TEXT_LINK    0xD8A15A
#define UI_COLOR_TEXT_INVERT  0x1A100C

#define UI_COLOR_DISABLED     0x5A4035
#define UI_COLOR_PROGRESS_BG  0x2A1B16
#define UI_COLOR_GLOW         0xD8A15A


typedef enum {
    UI_BTN_PRIMARY,
    UI_BTN_SECONDARY,
    UI_BTN_SUCCESS,
    UI_BTN_DANGER,
    UI_BTN_GHOST,
    UI_BTN_FLAT,
    UI_BTN_ICON
} ui_button_style_t;

typedef enum {
    UI_ALIGN_LEFT,
    UI_ALIGN_CENTER,
    UI_ALIGN_RIGHT
} ui_text_align_t;

#define UI_CENTER UI_ALIGN_CENTER

typedef enum {
    UI_BADGE_DEFAULT,
    UI_BADGE_SUCCESS,
    UI_BADGE_WARNING,
    UI_BADGE_DANGER,
    UI_BADGE_INFO
} ui_badge_style_t;

typedef enum {
    UI_TOAST_INFO,
    UI_TOAST_SUCCESS,
    UI_TOAST_WARNING,
    UI_TOAST_ERROR
} ui_toast_type_t;

uint32_t ui_blend_color(uint32_t base, uint32_t overlay, int alpha);

bool ui_draw_button(int x, int y, int w, int h, int radius,
                    const char* text, ui_button_style_t style, bool enabled,
                    int mx, int my);

bool ui_draw_button_icon(int x, int y, int w, int h,
                         const char* icon, const char* text,
                         ui_button_style_t style, bool enabled,
                         int mx, int my);

void ui_draw_panel(int x, int y, int w, int h, int radius,
                   uint32_t bg_color, uint32_t border_color, bool shadow);

void ui_draw_titled_panel(int x, int y, int w, int h, int radius,
                          const char* title, uint32_t title_bg, uint32_t body_bg);

void ui_draw_glass_card(int x, int y, int w, int h, int radius);

void ui_draw_section_card(int x, int y, int w, int h,
                          const char* title, const char* subtitle);

void ui_draw_text(int x, int y, const char* text, uint32_t color);
void ui_draw_text_centered(int x, int y, int w, const char* text, uint32_t color);
void ui_draw_label(int x, int y, int w, const char* text,
                   uint32_t color, ui_text_align_t align);
void ui_draw_heading(int x, int y, const char* text, uint32_t color);
void ui_draw_kv_row(int x, int y, int w, int h,
                    const char* key, const char* value,
                    uint32_t key_color, uint32_t val_color);

void ui_draw_progress_bar(int x, int y, int w, int h,
                          int percent, uint32_t fg_color);
void ui_draw_progress_indeterminate(int x, int y, int w, int h,
                                    int tick, uint32_t fg_color);

bool ui_draw_checkbox(int x, int y, const char* text,
                      bool checked, bool enabled, int mx, int my);
bool ui_draw_radio(int x, int y, const char* text,
                   bool selected, bool enabled, int mx, int my);

bool ui_draw_toggle(int x, int y, bool on, bool enabled, int mx, int my);
bool ui_draw_toggle_row(int x, int y, int w,
                        const char* label, const char* hint,
                        bool on, bool enabled, int mx, int my);

void ui_draw_badge(int x, int y, const char* text, uint32_t bg_color);
void ui_draw_badge_styled(int x, int y, const char* text, ui_badge_style_t style);

bool ui_draw_slider(int x, int y, int w, int percent,
                    bool enabled, int mx, int my);

void ui_draw_separator(int x, int y, int w, uint32_t color);
void ui_draw_separator_labeled(int x, int y, int w, const char* label);

bool ui_draw_list_item(int x, int y, int w, int h,
                       const char* text, bool selected, int mx, int my);
bool ui_draw_list_item_rich(int x, int y, int w, int h,
                             const char* icon, const char* title,
                             const char* subtitle,
                             bool selected, int mx, int my);

void ui_draw_text_input(int x, int y, int w, int h,
                        const char* placeholder, const char* value,
                        bool focused, int cursor_pos);

int ui_draw_tab_bar(int x, int y, int w, int h,
                    const char** labels, int count, int active,
                    int mx, int my, bool click);

void ui_draw_tooltip(int mx, int my, const char* text, bool show);

void ui_draw_toast(int screen_w, int screen_h,
                   const char* title, const char* message,
                   ui_toast_type_t type, int alpha);

void ui_draw_clock_widget(int x, int y, int w, int h,
                          int hour, int min, int sec,
                          const char* day_str);

void ui_draw_arctian_logo(int cx, int cy, int size);
void ui_draw_arctian_logo_animated(int cx, int cy, int size, int tick);

#define UI_HIT(mx, my, rx, ry, rw, rh) \
    ((mx) >= (rx) && (mx) < (rx)+(rw) && (my) >= (ry) && (my) < (ry)+(rh))

#endif
