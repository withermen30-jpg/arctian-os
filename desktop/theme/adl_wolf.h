#ifndef ARCTIAN_ADL_WOLF_H
#define ARCTIAN_ADL_WOLF_H

#include "lvgl.h"

#define ADL_BG          0x0E0E12
#define ADL_SURFACE     0x16171C
#define ADL_SURFACE_2   0x1E2027
#define ADL_BORDER      0x2C2F38
#define ADL_TEXT        0xF5F5F7
#define ADL_TEXT_DIM    0x9AA0A6
#define ADL_ACCENT      0x3B82F6
#define ADL_ACCENT_2    0x8B5CF6
#define ADL_DANGER      0xFF5F57
#define ADL_WARN        0xFEBC2E
#define ADL_OK          0x28C840

void       adl_font_init(void);
lv_font_t *adl_font(void);

void adl_wolf_theme_init(void);

#endif
