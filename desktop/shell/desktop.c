/**
 * Arctian GUI Desktop
 * Windows 11 benzeri grafik masaüstü
 * VESA framebuffer + backbuffer + UI Kit
 *
 * Tüm çizimler backbuffer üzerine yapılır.
 * gui_main_loop() her frame'de backbuffer'ı temizler,
 * tüm bileşenleri çizer ve backbuffer_blit() ile gösterir.
 */
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

/* For polling keyboard buffer in main loop */
extern const char* keyboard_get_buffer(void);
extern void keyboard_clear_buffer(void);

/* Keyboard event API for main loop */
#include "keyboard.h"

/* ================================================================== */
/* Global State                                                       */
/* ================================================================== */

static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static int next_window_id = 1;
static int active_window_id = -1;

static startmenu_state_t startmenu_state = STARTMENU_HIDDEN;

/* Mouse state snapshot (polled each frame) */
static int mouse_x = 0;
static int mouse_y = 0;
static bool mouse_left = false;
static bool prev_mouse_left = false;
static char clock_text[6] = "??:??";

/* ================================================================== */
/* Frosted Glass Effect (Box Blur 3x3, stack-based)                   */
/* ================================================================== */

/**
 * Apply a frosted-glass (blurred + semi-transparent) overlay onto the
 * backbuffer region [rx, ry, rw, rh]. Uses a 3x3 box blur sampled
 * from the already-drawn background, then alpha-blends the tint over it.
 *
 * Max blur width is 1024 (screen width). Uses stack buffer.
 */
#define FROSTED_BLUR_R 1 /* 3x3 box blur — small but effective */
#define FROSTED_BLUR_SZ 3

static void apply_frosted_effect(int rx, int ry, int rw, int rh,
                                  uint32_t tint_color, int alpha) {
    int bw = backbuffer_width();
    int bh = backbuffer_height();

    /* Clamp region to screen */
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > bw) rw = bw - rx;
    if (ry + rh > bh) rh = bh - ry;
    if (rw <= 0 || rh <= 0) return;

    /* Row buffer on stack — max 2560 pixels × 4 bytes = 10KB */
    uint32_t row_buf[2560];
    (void)row_buf;
    if (rw > 2560) rw = 2560;

    uint8_t tint_r = (tint_color >> 16) & 0xFF;
    uint8_t tint_g = (tint_color >> 8) & 0xFF;
    uint8_t tint_b = tint_color & 0xFF;

    for (int row = 0; row < rh; row++) {
        int sy = ry + row;

        /* Horizontal blur into row_buf */
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

        /* Vertical blur + alpha blend */
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

/* ── Desktop Background ── */

static void draw_desktop_background(void) {
    wallpaper_draw_scaled();

    /* Minimal desktop label */
    ui_draw_text(backbuffer_width() - 116, 24, "Arctian 64", COLOR_TEXT_SECOND);

    /* Clock in bottom-right */
    int cw = (int)strlen(clock_text) * 8;
    ui_draw_text(backbuffer_width() - cw - 16, backbuffer_height() - 80, clock_text, COLOR_TEXT_SECOND);
}

/* ================================================================== */
/* Taskbar Drawing                                                     */
/* ================================================================== */

static void draw_taskbar(void) {
    int bw = backbuffer_width();
    int tb_w = (bw < 1400) ? TASKBAR_FLOATING_W_MIN : bw * 3 / 5;
    int tb_h = TASKBAR_HEIGHT;
    int tb_x = (backbuffer_width() - tb_w) / 2;
    int tb_y = backbuffer_height() - tb_h - TASKBAR_ELEVATION;

    /* Shadow glow */
    for (int li = 0; li < 5; li++) {
        int off = li + 2;
        uint32_t sh_col = 0x0000000C + (li << 2);
        backbuffer_fill_rounded_rect(tb_x - off + 4, tb_y + off, tb_w - 8, tb_h,
                                      TASKBAR_RADIUS, sh_col);
    }

    /* Frosted glass on the pill only */
    apply_frosted_effect(tb_x, tb_y, tb_w, tb_h, COLOR_TASKBAR, 160);

    /* Pill border */
    backbuffer_draw_rounded_rect(tb_x, tb_y, tb_w, tb_h, TASKBAR_RADIUS, 0x344255);

    /* Inner glow at top edge */
    for (int cx = tb_x + TASKBAR_RADIUS; cx < tb_x + tb_w - TASKBAR_RADIUS; cx++)
        backbuffer_put_pixel(cx, tb_y + 1, 0x2CFFFFFF);

    /* Start button */
    int start_btn_x = tb_x + 14;
    int start_btn_y = tb_y + 10;
    int start_btn_size = 30;

    backbuffer_fill_rounded_rect(start_btn_x, start_btn_y, start_btn_size, start_btn_size, 9, 0x5A371E);
    backbuffer_draw_rounded_rect(start_btn_x, start_btn_y, start_btn_size, start_btn_size, 9, 0x8A5630);
    ui_draw_arctian_logo(start_btn_x + start_btn_size / 2, start_btn_y + start_btn_size / 2, 20);

    /* Running app buttons */
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

    /* System tray (right side of pill) */
    int tray_x = tb_x + tb_w - 138;
    int tray_y = tb_y + (tb_h - 8) / 2;
    ui_draw_text(tray_x, tray_y, "64", UI_COLOR_TEXT);
    ui_draw_text(tray_x + 20, tray_y, "NET", UI_COLOR_TEXT_SECOND);
    backbuffer_draw_string_smooth(tray_x + 56, tray_y - 4, clock_text, UI_COLOR_TEXT);
}

/* ================================================================== */
/* Start Menu Drawing                                                  */
/* ================================================================== */

static void draw_start_menu(void) {
    if (startmenu_state == STARTMENU_HIDDEN) return;

    int sm_x = (backbuffer_width() - STARTMENU_WIDTH) / 2;
    int sm_y = backbuffer_height() - TASKBAR_HEIGHT - TASKBAR_ELEVATION - STARTMENU_HEIGHT;

    /* Frosted glass background */
    apply_frosted_effect(sm_x, sm_y, STARTMENU_WIDTH, STARTMENU_HEIGHT,
                          COLOR_START_MENU, 170);

    /* Rounded panel border */
    backbuffer_fill_rounded_rect(sm_x, sm_y, STARTMENU_WIDTH, STARTMENU_HEIGHT,
                                  WINDOW_RADIUS, 0x00000000);
    backbuffer_draw_rounded_rect(sm_x, sm_y, STARTMENU_WIDTH, STARTMENU_HEIGHT, WINDOW_RADIUS, 0x334255);

    /* User profile area */
    backbuffer_fill_rounded_rect(sm_x + 16, sm_y + 16, STARTMENU_WIDTH - 32, 64, 12, 0x121B27);
    backbuffer_fill_rounded_rect(sm_x + 28, sm_y + 24, 42, 42, 21, COLOR_ACCENT);
    backbuffer_draw_string_smooth(sm_x + 80, sm_y + 26, "Arctian User", COLOR_TEXT);
    ui_draw_text(sm_x + 80, sm_y + 46, "Masaustu hesabi", COLOR_TEXT_SECOND);

    /* Separator */
    ui_draw_separator(sm_x + 16, sm_y + 86, STARTMENU_WIDTH - 32, 0x273444);

    /* Menu items — temel sistemde kayitli uygulama yok */
    ui_draw_text(sm_x + 20, sm_y + 108, "Uygulama yok", COLOR_TEXT_SECOND);

    /* Separator */
    ui_draw_separator(sm_x + 16, sm_y + STARTMENU_HEIGHT - 74, STARTMENU_WIDTH - 32, 0x273444);

    /* Bottom buttons */
    int btn_y = sm_y + STARTMENU_HEIGHT - 58;
    ui_draw_button(sm_x + 16, btn_y, 104, 40, 10, "Kapat", UI_BTN_DANGER, true, mouse_x, mouse_y);
    ui_draw_button(sm_x + 128, btn_y, 104, 40, 10, "Yen.Baslat", UI_BTN_PRIMARY, true, mouse_x, mouse_y);
}

/* ================================================================== */
/* Window Management Helpers                                           */
/* ================================================================== */

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

    /* Find the first closed slot or last window */
    int last = window_count - 1;
    /* If already at the end, nothing to do */
    if (idx == last) return;

    window_t tmp = windows[idx];
    for (int i = idx; i < last; i++) {
        windows[i] = windows[i + 1];
    }
    windows[last] = tmp;
}

/* ================================================================== */
/* Window Drawing                                                      */
/* ================================================================== */

static void draw_window_content(window_t *w) {
    /* Temel pencerede icerik yok — genel yer tutucu cizilir */
    ui_draw_text_centered(w->x, w->y + 56, w->width, "Icerik hazirlaniyor", COLOR_TEXT_SECOND);
}

static void draw_window_frame(window_t *w) {
    int w_x = w->x;
    int w_y = w->y;
    int w_w = w->width;
    int w_h = w->height;
    int title_h = 40;

    /* Shadow — 3 katman yumusak golge */
    backbuffer_fill_rounded_rect(w_x + 2, w_y + 3, w_w, w_h, WINDOW_RADIUS, 0x00000014);
    backbuffer_fill_rounded_rect(w_x + 4, w_y + 6, w_w, w_h, WINDOW_RADIUS, 0x0000000D);
    backbuffer_fill_rounded_rect(w_x + 6, w_y + 9, w_w, w_h, WINDOW_RADIUS, 0x00000007);

    /* Window body background */
    backbuffer_fill_rounded_rect(w_x, w_y, w_w, w_h, WINDOW_RADIUS, COLOR_WINDOW_BODY);

    /* Window border — ince cerceve */
    backbuffer_draw_rounded_rect(w_x, w_y, w_w, w_h, WINDOW_RADIUS, 0x3A2820);

    /* Title bar */
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

    /* Title bar ust ic aydinlatma cizgisi */
    for (int cx = w_x + r; cx < w_x + w_w - r; cx++)
        backbuffer_put_pixel(cx, w_y + 1, 0x28FFFFFF);

    /* Title text — smooth font */
    uint32_t title_color = w->active ? COLOR_TEXT : COLOR_TEXT_SECOND;
    backbuffer_fill_rounded_rect(w_x + 14, w_y + 14, 10, 10, 5, w->active ? COLOR_ACCENT : COLOR_TEXT_SECOND);
    backbuffer_draw_string_smooth(w_x + 32, w_y + 11, w->title, title_color);

    /* ================================================================ */
    /* Pencere Kontrol Butonlari (Kucult, Buyult/Onar, Kapat)          */
    /* ================================================================ */
    int btn_size = 20;
    int btn_gap = 8;
    int btn_y = w_y + 10;
    int close_x = w_x + w_w - 14 - btn_size;
    int max_x = close_x - btn_gap - btn_size;
    int min_x = max_x - btn_gap - btn_size;

    /* Hover tespiti */
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

    /* --- Kucult butonu (-) --- */
    backbuffer_fill_rounded_rect(min_x, btn_y, btn_size, btn_size, 6, min_bg);
    backbuffer_fill_rect(min_x + 5, btn_y + 10, 10, 2, min_symbol);

    /* --- Buyult / Onar butonu --- */
    backbuffer_fill_rounded_rect(max_x, btn_y, btn_size, btn_size, 6, max_bg);
    if (w->state == WINDOW_MAXIMIZED) {
        /* Onar ikonu: iki kare ust uste */
        backbuffer_fill_rect(max_x + 7, btn_y + 5, 8, 8, max_bg);
        backbuffer_draw_rect(max_x + 7, btn_y + 5, 8, 8, max_symbol);
        backbuffer_fill_rect(max_x + 5, btn_y + 7, 8, 8, max_bg);
        backbuffer_draw_rect(max_x + 5, btn_y + 7, 8, 8, max_symbol);
    } else {
        /* Buyult ikonu: ici bos kare */
        backbuffer_fill_rect(max_x + 5, btn_y + 5, 10, 10, max_symbol);
        backbuffer_fill_rect(max_x + 7, btn_y + 7, 6, 6, max_bg);
    }

    /* --- Kapat butonu (X) --- */
    backbuffer_fill_rounded_rect(close_x, btn_y, btn_size, btn_size, 6, close_bg);
    for (int i = 5; i <= 13; i++) {
        backbuffer_put_pixel(close_x + i,     btn_y + i,      close_symbol);
        backbuffer_put_pixel(close_x + i + 1, btn_y + i,      close_symbol);
        backbuffer_put_pixel(close_x + i,     btn_y + 19 - i, close_symbol);
        backbuffer_put_pixel(close_x + i + 1, btn_y + 19 - i, close_symbol);
    }

    /* Divider under title bar */
    backbuffer_fill_rect(w_x + 2, w_y + title_h, w_w - 4, 1, 0x253244);

    /* Content area background */
    int content_y = w_y + title_h + 1;
    int content_h = w_h - title_h - 1;
    if (content_h > 0) {
        backbuffer_fill_rect(w_x + 2, content_y, w_w - 4, content_h, COLOR_WINDOW_BODY);
    }
}

static void draw_windows(void) {
    /* Draw from first to last (last is topmost) */
    for (int i = 0; i < window_count; i++) {
        window_t *w = &windows[i];
        if (w->state == WINDOW_CLOSED || w->state == WINDOW_MINIMIZED)
            continue;

        draw_window_frame(w);
        draw_window_content(w);
    }
}

/* ================================================================== */
/* Cursor Drawing                                                       */
/* ================================================================== */

static void draw_cursor(void) {
    extern void cursor_draw(int mx, int my);
    cursor_draw(mouse_x, mouse_y);
}

/* ================================================================== */
/* Mouse Hit Testing & Interaction                                     */
/* ================================================================== */

static void handle_mouse_click(void) {
    if (!mouse_left) return;

    int tb_w = (backbuffer_width() < 1400) ? TASKBAR_FLOATING_W_MIN : backbuffer_width() * 3 / 5;
    int tb_h = TASKBAR_HEIGHT;
    int tb_x = (backbuffer_width() - tb_w) / 2;
    int tb_y = backbuffer_height() - tb_h - TASKBAR_ELEVATION;

    /* Check taskbar pill area */
    if (mouse_y >= tb_y && mouse_y < tb_y + tb_h) {
        /* Check start button */
        if (mouse_x >= tb_x + 12 && mouse_x <= tb_x + 12 + 32) {
            gui_toggle_start_menu();
            return;
        }

        /* Check taskbar app buttons */
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
        return; /* click on pill but not on a button — eat it */
    }

    /* If start menu is visible, check its buttons */
    if (startmenu_state == STARTMENU_VISIBLE) {
        int sm_x = (DESKTOP_WIDTH - STARTMENU_WIDTH) / 2;
        int sm_y = DESKTOP_HEIGHT - TASKBAR_HEIGHT - TASKBAR_ELEVATION - STARTMENU_HEIGHT;

        /* Bottom buttons */
        int btn_y = sm_y + STARTMENU_HEIGHT - 58;
        if (mouse_x >= sm_x + 16 && mouse_x < sm_x + 120 &&
            mouse_y >= btn_y && mouse_y < btn_y + 40) {
            /* Kapat button */
            extern void system_shutdown(void);
            system_shutdown();
            return;
        }
        if (mouse_x >= sm_x + 128 && mouse_x < sm_x + 232 &&
            mouse_y >= btn_y && mouse_y < btn_y + 40) {
            /* Yeniden Baslat button */
            extern void system_reboot(void);
            system_reboot();
            return;
        }

        /* Close start menu if clicking outside */
        if (mouse_x < sm_x || mouse_x > sm_x + STARTMENU_WIDTH ||
            mouse_y < sm_y || mouse_y > sm_y + STARTMENU_HEIGHT) {
            int tb_y2 = backbuffer_height() - TASKBAR_HEIGHT - TASKBAR_ELEVATION;
            if (mouse_y < tb_y2) {
                startmenu_state = STARTMENU_HIDDEN;
            }
        }
        return;
    }

    /* Check window title bars for activation/dragging */
    for (int i = window_count - 1; i >= 0; i--) {
        window_t *w = &windows[i];
        if (w->state == WINDOW_CLOSED || w->state == WINDOW_MINIMIZED) continue;

        if (mouse_x >= w->x && mouse_x < w->x + w->width &&
            mouse_y >= w->y && mouse_y < w->y + 40) {
            /* Hit on title bar */
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

            /* Start dragging */
            w->is_dragging = true;
            w->drag_off_x = mouse_x - w->x;
            w->drag_off_y = mouse_y - w->y;
            return;
        }

        /* Check if click is inside window body — activate */
        if (mouse_x >= w->x && mouse_x < w->x + w->width &&
            mouse_y >= w->y + 41 && mouse_y < w->y + w->height) {
            gui_activate_window(w->id);
            return;
        }
    }

    /* Click on desktop — hide start menu if visible */
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

            /* Constrain to screen */
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
            return; /* Only drag one window at a time */
        }
    }
}

/* ================================================================== */
/* Public API                                                         */
/* ================================================================== */

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

    /* Copy title safely */
    int ti = 0;
    while (title[ti] && ti < 63) {
        win->title[ti] = title[ti];
        ti++;
    }
    win->title[ti] = '\0';

    /* Save previous position */
    win->prev_x = x;
    win->prev_y = y;
    win->prev_w = w;
    win->prev_h = h;

    /* Deactivate all others */
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

    /* If this was active, activate next available */
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

    /* If window is minimized, restore it */
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

/* ================================================================== */
/* GUI Lifecycle                                                       */
/* ================================================================== */

void gui_init(void) {
    kernel_debug("GUI baslatiliyor (backbuffer + UI Kit)...");

    /* Set mouse screen size */
    mouse_set_screen_size(DESKTOP_WIDTH, DESKTOP_HEIGHT);

    kernel_debug("GUI basariyla baslatildi");
}

void gui_run_installer(void) {
    kernel_debug("Kurulum sihirbazi baslatiliyor...");

    /* Run the installer - returns INSTALL_MODE_TEST if user chose "Test Et"
     * or INSTALL_MODE_PERMANENT for full install.
     * Keyboard navigation is supported, and there's an auto-timeout to Test mode. */
    installer_mode_t mode = installer_run();

    if (mode == INSTALL_MODE_PERMANENT) {
        /* installer_run() handles the full flow including reboot */
        return;
    }

    kernel_debug("Test modu - masaustu baslatiliyor");
}

void gui_main_loop(void) {
    mouse_state_t *mouse = mouse_get_state();

    while (1) {
        rtc_format_time(clock_text, sizeof(clock_text));
        usb_hid_poll();

    /* Process keyboard events */
    keyboard_event_t ev;
    while (keyboard_pop_event(&ev)) {
        if (!ev.pressed) continue;

        /* Alt+F4 = close active window */
        if (ev.alt && ev.scancode == 0x3E) { /* F4 scancode */
            if (active_window_id >= 0) {
                gui_close_window(active_window_id);
            }
            continue;
        }
    }

        /* Poll mouse state */
        mouse_x = mouse->x;
        mouse_y = mouse->y;
        mouse_left = mouse->buttons[MOUSE_BUTTON_LEFT] ||
                     mouse->buttons[MOUSE_BUTTON_RIGHT] ||
                     mouse->buttons[MOUSE_BUTTON_MIDDLE];
        bool mouse_pressed = mouse_left && !prev_mouse_left;

        /* Handle mouse interactions */
        if (mouse_pressed) {
            handle_mouse_click();
        }
        if (mouse_left) {
            handle_dragging();
        } else {
            /* Clear dragging flags when button is released */
            for (int i = 0; i < window_count; i++) {
                if (windows[i].is_dragging) {
                    windows[i].is_dragging = false;
                }
            }
        }

        /* Draw everything */
        draw_desktop_background();

        if (startmenu_state == STARTMENU_VISIBLE) {
            draw_start_menu();
        }

        draw_windows();
        draw_taskbar();
        draw_cursor();

        /* Present to screen */
        backbuffer_blit();

        /* Brief pause to avoid burning CPU */
        for (volatile int i = 0; i < 500000; i++) {
            asm volatile ("nop");
        }
        prev_mouse_left = mouse_left;
    }
}
