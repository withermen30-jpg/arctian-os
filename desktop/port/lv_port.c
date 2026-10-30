/**
 * LVGL -> Arctian port katmani
 * display flush -> backbuffer, indev <- mouse, tick <- yerel sayac.
 */
#include "lvgl.h"
#include "lv_port.h"
#include "asi.h"
#include <stdint.h>

static asi_t    *A;
static int       W, H;
static uint32_t *FB;
static uint32_t  s_tick;
static uint32_t  s_last_seq;   /* son gorulen sol tus basma kenari */
static int       s_synth;      /* 0=yok, 1/2=sentetik bas/birak */
/* Kirli dikdortgen (ekran koordinati); baslangicta gecersiz. */
static int       s_dx1 = 1 << 20, s_dy1 = 1 << 20, s_dx2 = -1, s_dy2 = -1;
static lv_indev_t *s_indev;
static lv_indev_t *s_keypad;

#define LV_PORT_BUF_ROWS 40
#define LV_PORT_MAX_W    2560
static void    *s_buf;
static uint32_t s_buf_cap;

static uint32_t tick_cb(void) {
    /* Gercek zaman: LVGL zamani kare hizindan bagimsiz ilerler. Cekirdek
     * PIT 1 ms sayaci ASI uzerinden okunur; yoksa yerel sayaca dusulur. */
    if (A && A->time && A->time->ticks_ms) return (uint32_t)A->time->ticks_ms();
    return s_tick;
}

void lv_port_tick_advance(uint32_t ms) { s_tick += ms; }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    int w = (int)(area->x2 - area->x1 + 1);
    const uint32_t *src = (const uint32_t *)px;
    for (int y = area->y1; y <= area->y2; y++) {
        uint32_t *dst = FB + (uint32_t)y * (uint32_t)W + (uint32_t)area->x1;
        for (int x = 0; x < w; x++) dst[x] = src[x];
        src += w;
    }
    /* Kirli bolge birlesimi (performans): sunum yalnizca bu alani kopyalar. */
    if (area->x1 < s_dx1) s_dx1 = area->x1;
    if (area->y1 < s_dy1) s_dy1 = area->y1;
    if (area->x2 > s_dx2) s_dx2 = area->x2;
    if (area->y2 > s_dy2) s_dy2 = area->y2;
    lv_display_flush_ready(disp);
}

/* Biriken kirli dikdortgeni alir ve sifirlar. 1 = gecerli alan var. */
int lv_port_take_dirty(int *x, int *y, int *w, int *h) {
    if (s_dx1 > s_dx2 || s_dy1 > s_dy2) return 0;
    if (x) *x = s_dx1;
    if (y) *y = s_dy1;
    if (w) *w = s_dx2 - s_dx1 + 1;
    if (h) *h = s_dy2 - s_dy1 + 1;
    s_dx1 = s_dy1 = 1 << 20;
    s_dx2 = s_dy2 = -1;
    return 1;
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->point.x = A->input->mouse_x();
    data->point.y = A->input->mouse_y();

    /* Basma kenari telafisi: yavas kare hizinda (TCG) bas+birak ayni
     * yoklama arasina dusup LVGL'e hic PRESSED ulasmiyordu. Cekirdek sol tus
     * kenarlarini sayar; kacirilan her kenar icin burada sentetik bir
     * PRESSED + RELEASED cifti uretiriz. */
    uint32_t seq = (A->input->mouse_press_seq) ? A->input->mouse_press_seq() : 0;
    int down = (A->input->mouse_buttons() & 1) ? 1 : 0;

    if (s_synth == 1) {              /* sentetik basili tut (1 yoklama) */
        s_synth = 2;
        data->state = LV_INDEV_STATE_PRESSED;
        return;
    }
    if (s_synth == 2) {              /* sentetik birak */
        s_synth = 0;
        s_last_seq = seq;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    if (!down && seq != s_last_seq) {
        s_last_seq = seq;
        s_synth = 2;                 /* bu yoklamada bas, sonrakinde birak */
        data->state = LV_INDEV_STATE_PRESSED;
        return;
    }
    s_last_seq = seq;
    data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    static int s_held;
    (void)indev;
    if (s_held) {
        s_held = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    int c = A->input->key_pop();
    if (c >= 0) {
        s_held = 1;
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
    s_buf_cap = (uint32_t)LV_PORT_MAX_W * LV_PORT_BUF_ROWS * 4u;
    s_buf = asi->mem->alloc(s_buf_cap);
    uint32_t buf_bytes = (uint32_t)W * LV_PORT_BUF_ROWS * 4u;
    if (buf_bytes > s_buf_cap) buf_bytes = s_buf_cap;
    lv_display_set_buffers(disp, s_buf, NULL, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_flush_cb(disp, flush_cb);

    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, read_cb);
    /* LVGL zamani artik gercek PIT sayacindan geliyor; cift tik
     * penceresi gercek degerine cekildi. */
    lv_indev_set_double_click_time(s_indev, 400);

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

void lv_port_set_resolution(int w, int h) {
    if (w < 1 || h < 1) return;
    lv_display_t *disp = lv_display_get_default();
    if (!disp) return;

    W = w;
    H = h;
    if (A && A->gfx && A->gfx->framebuffer) FB = A->gfx->framebuffer();

    lv_display_set_resolution(disp, w, h);

    uint32_t buf_bytes = (uint32_t)w * LV_PORT_BUF_ROWS * 4u;
    if (buf_bytes > s_buf_cap) buf_bytes = s_buf_cap;
    if (s_buf)
        lv_display_set_buffers(disp, s_buf, NULL, buf_bytes,
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
}
