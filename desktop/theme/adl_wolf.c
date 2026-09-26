#include "adl_wolf.h"

extern const unsigned char font_ttf[];
extern const unsigned int  font_ttf_len;

static lv_font_t *s_font = 0;

void adl_font_init(void) {
    s_font = lv_tiny_ttf_create_data(font_ttf, font_ttf_len, 15);
}

lv_font_t *adl_font(void) {
    return s_font;
}

void adl_wolf_theme_init(void) {
    lv_display_t *d = lv_display_get_default();
    lv_theme_t *th = lv_theme_default_init(
        d,
        lv_color_hex(ADL_ACCENT),
        lv_color_hex(ADL_ACCENT_2),
        true,
        s_font ? s_font : LV_FONT_DEFAULT);
    lv_display_set_theme(d, th);
}
