#include "lvgl.h"
#include "lv_port.h"
#include "asi.h"
#include <stdint.h>

static asi_t    *A;
static int       W, H;
static uint32_t *FB;
static uint32_t  s_tick;
static lv_indev_t *s_indev;
static lv_indev_t *s_keypad;

static uint32_t tick_cb(void) { return s_tick; }

void lv_port_tick_advance(uint32_t ms) { s_tick += ms; }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    int w = (int)(area->x2 - area->x1 + 1);
    const uint32_t *src = (const uint32_t *)px;
    for (int y = area->y1; y <= area->y2; y++) {
        uint32_t *dst = FB + (uint32_t)y * (uint32_t)W + (uint32_t)area->x1;
        for (int x = 0; x < w; x++) dst[x] = src[x];
        src += w;
    }
    lv_display_flush_ready(disp);
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->point.x = A->input->mouse_x();
    data->point.y = A->input->mouse_y();
    data->state = (A->input->mouse_buttons() & 1) ? LV_INDEV_STATE_PRESSED
                                                  : LV_INDEV_STATE_RELEASED;
}

static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    int c = A->input->key_pop();
    if (c >= 0) {
        if (c == '\n' || c == '\r')      data->key = LV_KEY_ENTER;
        else if (c == '\b')              data->key = LV_KEY_BACKSPACE;
        else if (c == '\t')              data->key = LV_KEY_NEXT;
        else if (c == 0x1b)              data->key = LV_KEY_ESC;
        else                             data->key = (uint32_t)c;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void lv_port_init(asi_t *asi) {
    A = asi;
    W = asi->gfx->width();
    H = asi->gfx->height();
    FB = asi->gfx->framebuffer();

    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(W, H);
    uint32_t buf_bytes = (uint32_t)W * 40u * 4u;
    void *b1 = asi->mem->alloc(buf_bytes);
    lv_display_set_buffers(disp, b1, NULL, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_flush_cb(disp, flush_cb);

    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, read_cb);

    s_keypad = lv_indev_create();
    lv_indev_set_type(s_keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_keypad, keypad_read_cb);
}

lv_indev_t *lv_port_get_indev(void) {
    return s_indev;
}

lv_indev_t *lv_port_get_keypad(void) {
    return s_keypad;
}
