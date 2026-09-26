#include "ui_kit.h"
#include "backbuffer.h"


static int ui_strlen(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int text_width(const char* s) {
    return ui_strlen(s) * 8;
}

uint32_t ui_blend_color(uint32_t base, uint32_t overlay, int alpha) {
    if (alpha <= 0)   return base;
    if (alpha >= 255) return overlay;
    int inv = 255 - alpha;
    int br = (base >> 16) & 0xFF,  bg = (base >> 8) & 0xFF,  bb = base & 0xFF;
    int or = (overlay >> 16) & 0xFF, og = (overlay >> 8) & 0xFF, ob = overlay & 0xFF;
    return (((br * inv + or * alpha) / 255) << 16) |
           (((bg * inv + og * alpha) / 255) <<  8) |
           ((bb * inv + ob * alpha) / 255);
}

static uint32_t lighten(uint32_t c, int a) { return ui_blend_color(c, 0xFFFFFF, a); }
static uint32_t darken (uint32_t c, int a) { return ui_blend_color(c, 0x000000, a); }

static int clamp(int v, int lo, int hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static void int_to_str(int v, char* buf) {
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char rev[16]; int ri = 0;
    while (v > 0) { rev[ri++] = '0' + (v % 10); v /= 10; }
    int i; for (i = 0; i < ri; i++) buf[i] = rev[ri - 1 - i];
    buf[i] = '\0';
}


static void draw_premium_shadow(int x, int y, int w, int h) {
    int bw = backbuffer_width(), bh = backbuffer_height();
    static const int alphas[] = { 6, 10, 16, 22, 30, 38, 46, 36 };
    for (int li = 0; li < 8; li++) {
        int off = li + 1;
        uint32_t col = 0x000000 | ((uint32_t)alphas[li] << 24);
        int by = y + h + off;
        if (by < bh) {
            for (int cx = x; cx < x + w; cx++)
                backbuffer_put_pixel(cx, by, col);
        }
        int rx = x + w + off;
        if (rx < bw) {
            for (int cy = y; cy < y + h; cy++)
                backbuffer_put_pixel(rx, cy, col);
        }
        if (rx < bw && by < bh)
            backbuffer_put_pixel(rx, by, col);
    }
}


static void fill_gradient_rect(int x, int y, int w, int h,
                                uint32_t top_col, uint32_t bot_col) {
    for (int row = 0; row < h; row++) {
        int alpha = (row * 255) / (h > 1 ? h - 1 : 1);
        uint32_t col = ui_blend_color(top_col, bot_col, alpha);
        backbuffer_fill_rect(x, y + row, w, 1, col);
    }
}


static void draw_inner_highlight(int x, int y, int w, int h, int radius) {
    uint32_t hi = 0x30FFFFFF;
    for (int cx = x + radius; cx < x + w - radius; cx++)
        backbuffer_put_pixel(cx, y + 1, hi);
    for (int cy = y + radius; cy < y + h / 3; cy++)
        backbuffer_put_pixel(x + 1, cy, 0x18FFFFFF);
}


void ui_draw_panel(int x, int y, int w, int h, int radius,
                   uint32_t bg_color, uint32_t border_color, bool shadow) {
    if (shadow) draw_premium_shadow(x, y, w, h);
    backbuffer_fill_rounded_rect(x, y, w, h, radius, bg_color);
    draw_inner_highlight(x, y, w, h, radius);
    if (border_color) backbuffer_draw_rounded_rect(x, y, w, h, radius, border_color);
}

void ui_draw_titled_panel(int x, int y, int w, int h, int radius,
                          const char* title, uint32_t title_bg, uint32_t body_bg) {
    int th = 40;

    draw_premium_shadow(x, y, w, h);

    backbuffer_fill_rounded_rect(x, y, w, h, radius, body_bg);

    fill_gradient_rect(x, y, w, th, lighten(title_bg, 14), title_bg);

    draw_inner_highlight(x, y, w, th, radius);

    ui_draw_arctian_logo(x + 18, y + 20, 22);

    backbuffer_draw_string_smooth(x + 36, y + 8, title, UI_COLOR_TEXT);

    backbuffer_fill_rect(x, y + th, w, 1, UI_COLOR_BORDER_LIGHT);

    backbuffer_draw_rounded_rect(x, y, w, h, radius, UI_COLOR_BORDER_MID);
}

void ui_draw_glass_card(int x, int y, int w, int h, int radius) {
    backbuffer_fill_rounded_rect(x, y, w, h, radius, UI_COLOR_BASE_03);
    draw_inner_highlight(x, y, w, h, radius);
    backbuffer_draw_rounded_rect(x, y, w, h, radius, UI_COLOR_BORDER_MID);
    for (int cx = x + radius; cx < x + w - radius; cx++)
        backbuffer_put_pixel(cx, y, UI_COLOR_ACCENT_GLOW);
    draw_premium_shadow(x, y, w, h);
}

void ui_draw_section_card(int x, int y, int w, int h,
                           const char* title, const char* subtitle) {
    ui_draw_panel(x, y, w, h, 10, UI_COLOR_BASE_03, UI_COLOR_BORDER, true);

    backbuffer_fill_rounded_rect(x, y, w, 44, 10, UI_COLOR_BASE_04);
    backbuffer_fill_rect(x, y + 34, w, 10, UI_COLOR_BASE_04);
    backbuffer_fill_rect(x, y + 44, w, 1, UI_COLOR_BORDER_LIGHT);

    backbuffer_fill_rounded_rect(x + 14, y + 12, 4, 22, 2, UI_COLOR_ACCENT);

    backbuffer_draw_string_smooth(x + 26, y + 14, title, UI_COLOR_TEXT);

    if (subtitle && subtitle[0]) {
        backbuffer_draw_string_smooth(x + 26, y + 26, subtitle, UI_COLOR_TEXT_SECOND);
    }
}


void ui_draw_text(int x, int y, const char* text, uint32_t color) {
    backbuffer_draw_string_smooth(x, y, text, color);
}

void ui_draw_text_centered(int x, int y, int w, const char* text, uint32_t color) {
    int tw = text_width(text);
    int tx = x + (w - tw) / 2;
    if (tx < x) tx = x;
    backbuffer_draw_string_smooth(tx, y, text, color);
}

void ui_draw_label(int x, int y, int w, const char* text,
                   uint32_t color, ui_text_align_t align) {
    const char* p = text;
    int cy = y;
    while (*p) {
        int len = 0;
        while (p[len] && p[len] != '\n') len++;
        int lw = len * 8;
        int tx = x;
        if (align == UI_ALIGN_CENTER) tx = x + (w - lw) / 2;
        else if (align == UI_ALIGN_RIGHT) tx = x + w - lw;
        for (int i = 0; i < len; i++) {
            char ch[2] = { p[i], '\0' };
            backbuffer_draw_string_smooth(tx + i * 8, cy, ch, color);
        }
        cy += 12;
        p += len;
        if (*p == '\n') p++;
    }
}

void ui_draw_heading(int x, int y, const char* text, uint32_t color) {
    backbuffer_draw_string_scaled(x, y, text, color, 0x00000000);
}

void ui_draw_kv_row(int x, int y, int w, int h,
                    const char* key, const char* value,
                    uint32_t key_color, uint32_t val_color) {
    backbuffer_fill_rect(x, y, w, h, UI_COLOR_BASE_02);
    backbuffer_fill_rect(x, y + h - 1, w, 1, UI_COLOR_BORDER);

    backbuffer_draw_string_smooth(x + 12, y + (h - 8) / 2, key, key_color);

    int vw = text_width(value);
    backbuffer_draw_string_smooth(x + w - vw - 12, y + (h - 8) / 2, value, val_color);
}


static void draw_btn_body(int x, int y, int w, int h, int radius,
                           uint32_t base, bool hovered, bool is_danger) {
    uint32_t top_col = lighten(base, hovered ? 30 : 18);
    uint32_t bot_col = darken (base, hovered ? 10 : 18);

    if (radius > 0) {
        backbuffer_fill_rounded_rect(x, y, w, h, radius, base);
        int mid = h * 2 / 5;
        backbuffer_fill_rounded_rect(x, y, w, mid, radius,
                                     ui_blend_color(base, top_col, 180));
    } else {
        fill_gradient_rect(x, y, w, h, top_col, bot_col);
    }

    if (hovered) {
        uint32_t glow = is_danger ? 0xFFAAAA : UI_COLOR_ACCENT_LIGHT;
        for (int cx = x + radius; cx < x + w - radius; cx++)
            backbuffer_put_pixel(cx, y + 1, ui_blend_color(base, glow, 40));
    }

    uint32_t shine = 0x40FFFFFF;
    for (int cx = x + radius; cx < x + w - radius; cx++)
        backbuffer_put_pixel(cx, y + 1, shine);
    for (int cx = x + radius; cx < x + w - radius; cx++)
        backbuffer_put_pixel(cx, y + h - 2, darken(base, 30));
}

bool ui_draw_button(int x, int y, int w, int h, int radius,
                    const char* text, ui_button_style_t style, bool enabled,
                    int mx, int my) {
    uint32_t bg, fg;
    bool is_danger = false;

    switch (style) {
        case UI_BTN_PRIMARY:
            bg = enabled ? UI_COLOR_ACCENT : UI_COLOR_DISABLED;
            fg = UI_COLOR_TEXT_INVERT;
            break;
        case UI_BTN_SECONDARY:
            bg = enabled ? UI_COLOR_BASE_04 : UI_COLOR_DISABLED;
            fg = enabled ? UI_COLOR_TEXT : UI_COLOR_TEXT_HINT;
            break;
        case UI_BTN_SUCCESS:
            bg = enabled ? UI_COLOR_SUCCESS : UI_COLOR_DISABLED;
            fg = UI_COLOR_TEXT_INVERT;
            break;
        case UI_BTN_DANGER:
            bg = enabled ? UI_COLOR_DANGER : UI_COLOR_DISABLED;
            fg = UI_COLOR_TEXT_LIGHT;
            is_danger = true;
            break;
        case UI_BTN_GHOST:
            bg = UI_COLOR_BASE_02;
            fg = enabled ? UI_COLOR_ACCENT : UI_COLOR_DISABLED;
            break;
        case UI_BTN_FLAT:
            bg = 0;
            fg = enabled ? UI_COLOR_TEXT_LINK : UI_COLOR_DISABLED;
            break;
        case UI_BTN_ICON:
        default:
            bg = enabled ? UI_COLOR_BASE_04 : UI_COLOR_DISABLED;
            fg = enabled ? UI_COLOR_TEXT : UI_COLOR_TEXT_HINT;
            break;
    }

    bool hovered = enabled && UI_HIT(mx, my, x, y, w, h);

    if (style == UI_BTN_FLAT) {
        if (hovered) backbuffer_fill_rect(x, y + h - 2, w, 1, UI_COLOR_ACCENT);
    } else if (bg) {
        draw_btn_body(x, y, w, h, radius, hovered ? lighten(bg, 12) : bg, hovered, is_danger);
        if (style == UI_BTN_GHOST || style == UI_BTN_SECONDARY) {
            backbuffer_draw_rounded_rect(x, y, w, h, radius, UI_COLOR_BORDER_LIGHT);
        } else if (enabled) {
            backbuffer_draw_rounded_rect(x, y, w, h, radius,
                                         darken(bg, 40));
        }
        if (hovered && enabled) {
            backbuffer_draw_rounded_rect(x - 1, y - 1, w + 2, h + 2,
                                         radius + 1, UI_COLOR_ACCENT_HOVER);
        }
    }

    int tw = text_width(text);
    int tx = x + (w - tw) / 2;
    int ty = y + (h - 8) / 2;
    backbuffer_draw_string_smooth(tx, ty, text, fg);

    if (!enabled) return false;
    return hovered;
}

bool ui_draw_button_icon(int x, int y, int w, int h,
                          const char* icon, const char* text,
                          ui_button_style_t style, bool enabled,
                          int mx, int my) {
    bool hit = ui_draw_button(x, y, w, h, 8, text, style, enabled, mx, my);
    int ty = y + (h - 8) / 2;
    uint32_t fg = enabled ? UI_COLOR_TEXT : UI_COLOR_DISABLED;
    backbuffer_draw_string_smooth(x + 8, ty, icon, fg);
    return hit;
}


void ui_draw_progress_bar(int x, int y, int w, int h,
                           int percent, uint32_t fg_color) {
    percent = clamp(percent, 0, 100);

    backbuffer_fill_rounded_rect(x, y, w, h, h / 2, UI_COLOR_PROGRESS_BG);
    backbuffer_draw_rounded_rect(x, y, w, h, h / 2, UI_COLOR_BORDER);

    if (percent > 0) {
        int fw = clamp((w * percent) / 100, h, w);

        backbuffer_fill_rounded_rect(x, y, fw, h, h / 2, fg_color);

        uint32_t gloss = lighten(fg_color, 50);
        for (int cx = x + 2; cx < x + fw - 2 && cx < x + w - 2; cx++) {
            backbuffer_put_pixel(cx, y + 1, ui_blend_color(fg_color, gloss, 100));
            backbuffer_put_pixel(cx, y + 2, ui_blend_color(fg_color, gloss, 40));
        }

        if (h >= 16 && fw > 24) {
            char pct[8];
            int_to_str(percent, pct);
            int pi = ui_strlen(pct);
            pct[pi] = '%'; pct[pi + 1] = '\0';
            int tw = text_width(pct);
            if (tw + 4 < fw) {
                backbuffer_draw_string_smooth(
                    x + (fw - tw) / 2, y + (h - 8) / 2,
                    pct, UI_COLOR_TEXT_INVERT);
            }
        }
    }
}

void ui_draw_progress_indeterminate(int x, int y, int w, int h,
                                     int tick, uint32_t fg_color) {
    backbuffer_fill_rounded_rect(x, y, w, h, h / 2, UI_COLOR_PROGRESS_BG);
    backbuffer_draw_rounded_rect(x, y, w, h, h / 2, UI_COLOR_BORDER);

    int block_w = w / 3;
    int pos = (tick * 3) % (w + block_w) - block_w;
    int bx = x + pos;
    int bx2 = bx + block_w;
    if (bx < x) bx = x;
    if (bx2 > x + w) bx2 = x + w;
    if (bx2 > bx)
        backbuffer_fill_rounded_rect(bx, y, bx2 - bx, h, h / 2, fg_color);
}


bool ui_draw_checkbox(int x, int y, const char* text,
                       bool checked, bool enabled, int mx, int my) {
    int s = 18;

    uint32_t box_bg  = enabled ? UI_COLOR_BASE_04 : UI_COLOR_DISABLED;
    uint32_t box_brd = enabled ? UI_COLOR_BORDER_LIGHT : UI_COLOR_DISABLED;

    backbuffer_fill_rounded_rect(x + 1, y + 1, s, s, 4, 0x30000000);
    backbuffer_fill_rounded_rect(x, y, s, s, 4, box_bg);
    backbuffer_draw_rounded_rect(x, y, s, s, 4, box_brd);

    if (checked) {
        backbuffer_fill_rounded_rect(x + 2, y + 2, s - 4, s - 4, 3, UI_COLOR_ACCENT);
        for (int i = 0; i < 4; i++) {
            backbuffer_fill_rect(x + 4 + i, y + 9 + i, 2, 2, UI_COLOR_TEXT_INVERT);
        }
        for (int i = 0; i < 6; i++) {
            backbuffer_fill_rect(x + 8 + i, y + 10 - i, 2, 2, UI_COLOR_TEXT_INVERT);
        }
    }

    bool hovered = enabled && UI_HIT(mx, my, x, y, s + 6 + text_width(text), s);
    if (hovered) {
        backbuffer_draw_rounded_rect(x - 1, y - 1, s + 2, s + 2, 5,
                                     UI_COLOR_ACCENT_HOVER);
    }

    backbuffer_draw_string_smooth(x + s + 6, y + (s - 8) / 2, text,
                                       enabled ? UI_COLOR_TEXT : UI_COLOR_DISABLED);
    if (!enabled) return false;
    return hovered;
}


bool ui_draw_radio(int x, int y, const char* text,
                    bool selected, bool enabled, int mx, int my) {
    int r = 9;
    int d = r * 2;

    uint32_t bg  = enabled ? UI_COLOR_BASE_04 : UI_COLOR_DISABLED;
    uint32_t brd = enabled ? UI_COLOR_BORDER_LIGHT : UI_COLOR_DISABLED;

    backbuffer_fill_rounded_rect(x + 1, y + 1, d, d, r, 0x30000000);
    backbuffer_fill_rounded_rect(x, y, d, d, r, bg);
    backbuffer_draw_rounded_rect(x, y, d, d, r, brd);

    if (selected) {
        backbuffer_fill_rounded_rect(x + 4, y + 4, d - 8, d - 8, r - 4, UI_COLOR_ACCENT);
        backbuffer_fill_rounded_rect(x + 6, y + 6, d - 12, d - 12, r - 6,
                                     UI_COLOR_ACCENT_LIGHT);
        backbuffer_draw_rounded_rect(x, y, d, d, r, UI_COLOR_ACCENT);
    }

    bool hovered = enabled && UI_HIT(mx, my, x, y, d + 6 + text_width(text), d);
    backbuffer_draw_string_smooth(x + d + 6, y + (d - 16) / 2, text,
                                       enabled ? UI_COLOR_TEXT : UI_COLOR_DISABLED);
    if (!enabled) return false;
    return hovered;
}


bool ui_draw_toggle(int x, int y, bool on, bool enabled, int mx, int my) {
    int sw = 44, sh = 24, r = 12, kr = 9;

    uint32_t track = on ? UI_COLOR_ACCENT : (enabled ? UI_COLOR_BASE_04 : UI_COLOR_DISABLED);
    uint32_t track_brd = on ? UI_COLOR_ACCENT_HOVER : UI_COLOR_BORDER_LIGHT;

    backbuffer_fill_rounded_rect(x, y, sw, sh, r, track);
    if (!on) {
        for (int cx = x + r; cx < x + sw - r; cx++)
            backbuffer_put_pixel(cx, y + 1, 0x20000000);
    }
    backbuffer_draw_rounded_rect(x, y, sw, sh, r, track_brd);

    int kx = on ? x + sw - kr * 2 - 3 : x + 3;
    int ky = y + (sh - kr * 2) / 2;
    backbuffer_fill_rounded_rect(kx + 1, ky + 1, kr * 2, kr * 2, kr, 0x40000000);
    backbuffer_fill_rounded_rect(kx, ky, kr * 2, kr * 2, kr, UI_COLOR_TEXT_LIGHT);
    if (on) {
        backbuffer_fill_rounded_rect(kx + 3, ky + 3, kr * 2 - 6, kr * 2 - 6,
                                     kr - 3, UI_COLOR_ACCENT_LIGHT);
    }
    backbuffer_fill_rounded_rect(kx + 3, ky + 2, kr - 1, kr / 2, kr / 2,
                                 0xE8F0F8);

    bool hovered = enabled && UI_HIT(mx, my, x, y, sw, sh);
    if (!enabled) return false;
    return hovered;
}

bool ui_draw_toggle_row(int x, int y, int w,
                         const char* label, const char* hint,
                         bool on, bool enabled, int mx, int my) {
    int h = 52;
    int sw = 44, sh = 24;

    backbuffer_fill_rect(x, y, w, h, UI_COLOR_BASE_02);
    backbuffer_fill_rect(x, y + h - 1, w, 1, UI_COLOR_BORDER);

    backbuffer_draw_string_smooth(x + 14, y + 12, label, UI_COLOR_TEXT);
    if (hint && hint[0])
        backbuffer_draw_string_smooth(x + 14, y + 26, hint, UI_COLOR_TEXT_SECOND);

    int tx = x + w - sw - 14;
    int ty = y + (h - sh) / 2;
    return ui_draw_toggle(tx, ty, on, enabled, mx, my);
}


void ui_draw_badge(int x, int y, const char* text, uint32_t bg_color) {
    int tw = text_width(text);
    int px = 8, py = 4;
    int bw = tw + px * 2, bh = 8 + py * 2;
    int r = bh / 2;
    backbuffer_fill_rounded_rect(x, y, bw, bh, r, bg_color);
    for (int cx = x + r; cx < x + bw - r; cx++)
        backbuffer_put_pixel(cx, y + 1, 0x40FFFFFF);
    backbuffer_draw_string_smooth(x + px, y + py, text, UI_COLOR_TEXT_LIGHT);
}

void ui_draw_badge_styled(int x, int y, const char* text, ui_badge_style_t style) {
    uint32_t bg;
    switch (style) {
        case UI_BADGE_SUCCESS: bg = UI_COLOR_SUCCESS;  break;
        case UI_BADGE_WARNING: bg = UI_COLOR_WARNING;  break;
        case UI_BADGE_DANGER:  bg = UI_COLOR_DANGER;   break;
        case UI_BADGE_INFO:    bg = UI_COLOR_INFO;     break;
        default:               bg = UI_COLOR_ACCENT;   break;
    }
    ui_draw_badge(x, y, text, bg);
}


bool ui_draw_slider(int x, int y, int w, int percent,
                     bool enabled, int mx, int my) {
    int th = 6, kr = 9;
    int cy = y + kr;
    percent = clamp(percent, 0, 100);

    backbuffer_fill_rounded_rect(x, cy - th/2, w, th, th/2,
                                  enabled ? UI_COLOR_BASE_04 : UI_COLOR_DISABLED);
    backbuffer_draw_rounded_rect(x, cy - th/2, w, th, th/2, UI_COLOR_BORDER);

    int fw = (w * percent) / 100;
    if (fw > 0) {
        backbuffer_fill_rounded_rect(x, cy - th/2, fw, th, th/2,
                                      enabled ? UI_COLOR_ACCENT : UI_COLOR_DISABLED);
        for (int cx = x + 1; cx < x + fw - 1; cx++)
            backbuffer_put_pixel(cx, cy - th/2 + 1, lighten(UI_COLOR_ACCENT, 60));
    }

    int kx = x + fw - kr;
    if (kx < x) kx = x;
    backbuffer_fill_rounded_rect(kx + 1, cy - kr + 1, kr * 2, kr * 2, kr, 0x40000000);
    backbuffer_fill_rounded_rect(kx, cy - kr, kr * 2, kr * 2, kr,
                                  enabled ? UI_COLOR_TEXT_LIGHT : UI_COLOR_DISABLED);
    backbuffer_fill_rounded_rect(kx + 4, cy - kr + 4, kr * 2 - 8, kr * 2 - 8,
                                  kr - 4, UI_COLOR_ACCENT);
    backbuffer_fill_rounded_rect(kx + 6, cy - kr + 5, kr - 3, kr - 4, kr - 5, 0xE0E8F0);

    if (!enabled) return false;
    return UI_HIT(mx, my, x, cy - kr, w, kr * 2);
}


void ui_draw_separator(int x, int y, int w, uint32_t color) {
    backbuffer_fill_rect(x, y, w, 1, color);
    backbuffer_fill_rect(x, y + 1, w, 1, 0x08FFFFFF);
}

void ui_draw_separator_labeled(int x, int y, int w, const char* label) {
    int tw = text_width(label);
    int lx = x + (w - tw) / 2 - 8;
    backbuffer_fill_rect(x, y + 4, lx - x - 4, 1, UI_COLOR_BORDER_MID);
    backbuffer_draw_string_smooth(lx + 4, y, label, UI_COLOR_TEXT_HINT);
    int rx = lx + 4 + tw + 4;
    backbuffer_fill_rect(rx, y + 4, x + w - rx, 1, UI_COLOR_BORDER_MID);
}


bool ui_draw_list_item(int x, int y, int w, int h,
                        const char* text, bool selected, int mx, int my) {
    bool hovered = UI_HIT(mx, my, x, y, w, h);

    if (selected) {
        backbuffer_fill_rounded_rect(x, y, w, h, 8, UI_COLOR_BASE_04);
        backbuffer_fill_rounded_rect(x, y, 4, h, 2, UI_COLOR_ACCENT);
        backbuffer_draw_string_smooth(x + 14, y + (h - 8) / 2,
                                           text, UI_COLOR_TEXT);
    } else {
        if (hovered) backbuffer_fill_rounded_rect(x, y, w, h, 8, UI_COLOR_BASE_03);
        backbuffer_draw_string_smooth(x + 10, y + (h - 8) / 2,
                                           text,
                                           hovered ? UI_COLOR_TEXT : UI_COLOR_TEXT_SECOND);
    }
    return hovered;
}

bool ui_draw_list_item_rich(int x, int y, int w, int h,
                              const char* icon, const char* title,
                              const char* subtitle,
                              bool selected, int mx, int my) {
    bool hovered = UI_HIT(mx, my, x, y, w, h);

    if (selected) {
        backbuffer_fill_rounded_rect(x, y, w, h, 8, UI_COLOR_BASE_04);
        backbuffer_fill_rounded_rect(x, y, 4, h, 2, UI_COLOR_ACCENT);
    } else if (hovered) {
        backbuffer_fill_rounded_rect(x, y, w, h, 8, UI_COLOR_BASE_03);
    }

    int icon_x = x + 12;
    int icon_y = y + (h - 28) / 2;
    backbuffer_fill_rounded_rect(icon_x, icon_y, 28, 28, 6, UI_COLOR_BASE_05);
    backbuffer_draw_rounded_rect(icon_x, icon_y, 28, 28, 6, UI_COLOR_BORDER_MID);
    backbuffer_draw_string_smooth(icon_x + (28 - text_width(icon)) / 2,
                                       icon_y + 10, icon, UI_COLOR_ACCENT);

    int tx = icon_x + 36;
    backbuffer_draw_string_smooth(tx, y + (h / 2) - 9, title,
                                       selected ? UI_COLOR_TEXT : UI_COLOR_TEXT);
    if (subtitle && subtitle[0])
        backbuffer_draw_string_smooth(tx, y + (h / 2) + 1, subtitle,
                                           UI_COLOR_TEXT_SECOND);

    return hovered;
}


void ui_draw_text_input(int x, int y, int w, int h,
                         const char* placeholder, const char* value,
                         bool focused, int cursor_pos) {
    uint32_t bg  = focused ? UI_COLOR_BASE_04 : UI_COLOR_BASE_03;
    uint32_t brd = focused ? UI_COLOR_BORDER_FOCUS : UI_COLOR_BORDER_LIGHT;

    backbuffer_fill_rounded_rect(x, y, w, h, 6, bg);
    backbuffer_draw_rounded_rect(x, y, w, h, 6, brd);

    backbuffer_fill_rect(x + 6, y + 1, w - 12, 1, 0x10FFFFFF);

    int ty = y + (h - 8) / 2;
    int vlen = ui_strlen(value);

    if (vlen > 0) {
        backbuffer_draw_string_smooth(x + 10, ty, value, UI_COLOR_TEXT);
    } else if (placeholder) {
        backbuffer_draw_string_smooth(x + 10, ty, placeholder, UI_COLOR_TEXT_HINT);
    }

    if (focused) {
        int safe_pos = clamp(cursor_pos, 0, vlen);
        int cx = x + 10 + safe_pos * 8;
        backbuffer_fill_rect(cx, ty - 1, 2, 12, UI_COLOR_ACCENT);
    }

    if (focused) {
        backbuffer_draw_rounded_rect(x - 1, y - 1, w + 2, h + 2, 7,
                                     ui_blend_color(0x000000, UI_COLOR_ACCENT, 60));
    }
}


int ui_draw_tab_bar(int x, int y, int w, int h,
                     const char** labels, int count, int active,
                     int mx, int my, bool click) {
    int tw = (count > 0) ? (w / count) : w;
    int clicked = -1;

    backbuffer_fill_rect(x, y, w, h, UI_COLOR_BASE_02);
    backbuffer_fill_rect(x, y + h - 1, w, 1, UI_COLOR_BORDER_LIGHT);

    for (int i = 0; i < count; i++) {
        int tx = x + i * tw;
        bool sel = (i == active);
        bool hov = UI_HIT(mx, my, tx, y, tw, h);

        if (sel) {
            backbuffer_fill_rect(tx, y, tw, h, UI_COLOR_BASE_03);
            backbuffer_fill_rect(tx + 4, y + h - 3, tw - 8, 3, UI_COLOR_ACCENT);
        } else if (hov) {
            backbuffer_fill_rect(tx, y, tw, h, UI_COLOR_BASE_02);
        }

        int lw = text_width(labels[i]);
        int ltx = tx + (tw - lw) / 2;
        int lty = y + (h - 8) / 2;
        backbuffer_draw_string_smooth(ltx, lty, labels[i],
                                           sel ? UI_COLOR_TEXT : UI_COLOR_TEXT_SECOND);

        if (i < count - 1)
            backbuffer_fill_rect(tx + tw - 1, y + 6, 1, h - 12, UI_COLOR_BORDER);

        if (hov && click) clicked = i;
    }
    return clicked;
}


void ui_draw_tooltip(int mx, int my, const char* text, bool show) {
    if (!show || !text || !text[0]) return;
    int tw = text_width(text);
    int pw = tw + 16, ph = 22;
    int tx = mx + 14;
    int ty = my - 30;
    int bw = backbuffer_width(), bh = backbuffer_height();
    if (tx + pw > bw) tx = bw - pw - 4;
    if (ty < 0) ty = my + 20;
    if (ty + ph > bh) ty = bh - ph - 4;

    draw_premium_shadow(tx, ty, pw, ph);
    backbuffer_fill_rounded_rect(tx, ty, pw, ph, 6, UI_COLOR_BASE_05);
    backbuffer_draw_rounded_rect(tx, ty, pw, ph, 6, UI_COLOR_BORDER_LIGHT);
    for (int cx = tx + 6; cx < tx + pw - 6; cx++)
        backbuffer_put_pixel(cx, ty + 1, 0x30FFFFFF);
    backbuffer_draw_string_smooth(tx + 8, ty + 7, text, UI_COLOR_TEXT);
}


void ui_draw_toast(int screen_w, int screen_h,
                    const char* title, const char* message,
                    ui_toast_type_t type, int alpha) {
    if (alpha <= 0) return;

    int tw = 300, th = 72;
    int tx = screen_w - tw - 16;
    int ty = screen_h - th - 16;

    uint32_t accent, icon_bg;
    const char* icon_str;
    switch (type) {
        case UI_TOAST_SUCCESS:
            accent = UI_COLOR_SUCCESS; icon_bg = UI_COLOR_SUCCESS_BG; icon_str = "OK"; break;
        case UI_TOAST_WARNING:
            accent = UI_COLOR_WARNING; icon_bg = UI_COLOR_WARNING_BG; icon_str = "!!"; break;
        case UI_TOAST_ERROR:
            accent = UI_COLOR_DANGER;  icon_bg = UI_COLOR_DANGER_BG;  icon_str = "X!"; break;
        default:
            accent = UI_COLOR_INFO;    icon_bg = UI_COLOR_INFO_BG;    icon_str = "i ";  break;
    }

    draw_premium_shadow(tx, ty, tw, th);
    backbuffer_fill_rounded_rect(tx, ty, tw, th, 10, UI_COLOR_BASE_04);
    backbuffer_fill_rounded_rect(tx, ty, 6, th, 10, accent);
    backbuffer_fill_rect(tx + 6, ty, 1, th, accent);
    backbuffer_fill_rounded_rect(tx + 16, ty + 14, 36, 36, 8, icon_bg);
    backbuffer_draw_rounded_rect(tx + 16, ty + 14, 36, 36, 8, accent);
    backbuffer_draw_string_smooth(tx + 16 + (36 - text_width(icon_str)) / 2,
                                        ty + 28, icon_str, accent);
    backbuffer_draw_string_smooth(tx + 62, ty + 10, title, UI_COLOR_TEXT);
    backbuffer_draw_string_smooth(tx + 62, ty + 30, message, UI_COLOR_TEXT_SECOND);
    backbuffer_draw_rounded_rect(tx, ty, tw, th, 10, UI_COLOR_BORDER_MID);
}


void ui_draw_clock_widget(int x, int y, int w, int h,
                           int hour, int min, int sec,
                           const char* day_str) {
    backbuffer_fill_rect(x, y, w, h, 0);

    char tbuf[10];
    tbuf[0] = '0' + (hour / 10);
    tbuf[1] = '0' + (hour % 10);
    tbuf[2] = ':';
    tbuf[3] = '0' + (min / 10);
    tbuf[4] = '0' + (min % 10);
    tbuf[5] = '\0';

    if ((sec % 2) == 0) tbuf[2] = ':';
    else                 tbuf[2] = ' ';

    int tw = ui_strlen(tbuf) * 8;
    backbuffer_draw_string_smooth(x + (w - tw) / 2, y + 2, tbuf, UI_COLOR_TEXT);

    if (day_str && day_str[0]) {
        int dw = text_width(day_str);
        backbuffer_draw_string_smooth(x + (w - dw) / 2, y + 22,
                                           day_str, UI_COLOR_TEXT_SECOND);
    }
}


void ui_draw_arctian_logo(int cx, int cy, int size) {
    int r = size / 2;
    if (r < 1) return;
    int chip_r = (size < 32) ? 1 : (size < 56) ? 2 : 3;

    backbuffer_fill_rounded_rect(cx - r + 2, cy - r + 2, size, size, r, 0x50000000);

    static const uint32_t bands[] = {
        0x9A4E28, 0xBF7038, 0xD8904C, 0xC67840, 0x8C4818
    };
    int band_h = (size + 4) / 5;
    if (band_h < 1) band_h = 1;
    for (int i = 0; i < 5; i++) {
        int by = cy - r + i * band_h;
        int bh = band_h;
        if (by + bh > cy + r) bh = cy + r - by;
        if (bh <= 0) break;
        backbuffer_fill_rounded_rect(cx - r, by, size, bh, r, bands[i]);
    }

    if (size >= 20) {
        backbuffer_fill_rounded_rect(cx - r + 4, cy - r + 4,
                                      size - 8, size - 8, r - 4, 0xD49A54);
    }
    if (size >= 32) {
        backbuffer_fill_rounded_rect(cx - r + 8, cy - r + 8,
                                      size - 16, size - 16, r - 8, 0xE8B870);
    }

    int chips[][2] = {
        { -(r * 5)/10,  -(r * 3)/10 },
        {  (r * 4)/10,  -(r * 5)/10 },
        { -(r * 3)/10,   (r * 3)/10 },
        {  (r * 5)/10,   (r * 4)/10 },
        { -(r * 4)/10,   (r * 5)/10 },
        {  (r * 5)/10,  -(r * 2)/10 },
        {  0,            (r * 3)/10 },
        { -(r * 5)/10,   (r * 2)/10 },
        {  (r * 3)/10,   0          },
        {  0,           -(r * 5)/10 },
    };
    int n = (size < 28) ? 4 : (size < 48) ? 6 : 10;
    int d = chip_r * 2;
    for (int i = 0; i < n; i++) {
        int px = cx + chips[i][0] - chip_r;
        int py = cy + chips[i][1] - chip_r;
        backbuffer_fill_rounded_rect(px + 1, py + 1, d, d, chip_r, 0x40000000);
        backbuffer_fill_rounded_rect(px, py, d, d, chip_r, 0x2A1206);
        backbuffer_fill_rounded_rect(px + 1, py + 1, chip_r, chip_r / 2 + 1,
                                      chip_r / 2, 0x4E2B14);
    }

    backbuffer_draw_rounded_rect(cx - r, cy - r, size, size, r, 0x7A3B1E);

    if (size >= 20) {
        for (int cx2 = cx - r / 2; cx2 < cx + r / 2; cx2++)
            backbuffer_put_pixel(cx2, cy - r + 2, 0x30FFFFFF);
    }
}

void ui_draw_arctian_logo_animated(int cx, int cy, int size, int tick) {
    int dx = 0, dy = 0;
    int phase = tick % 32;
    if      (phase <  8) { dx =  1; }
    else if (phase < 16) { dx =  0; dy = 1; }
    else if (phase < 24) { dx = -1; }
    else                 { dy = -1; }

    int r = size / 2 + 4;
    int glow_alpha = 30 + (tick % 60 < 30 ? tick % 30 : 30 - (tick % 30 - 30));
    (void)glow_alpha;
    backbuffer_draw_rounded_rect(cx + dx - r, cy + dy - r,
                                  r * 2, r * 2, r, UI_COLOR_ACCENT_GLOW);

    ui_draw_arctian_logo(cx + dx, cy + dy, size);
}

