#ifndef BACKBUFFER_H
#define BACKBUFFER_H

#include <stdint.h>
#include <stdbool.h>

int  backbuffer_init(void);
void backbuffer_resize(int w, int h);
void backbuffer_clear(uint32_t color);
void backbuffer_put_pixel(int x, int y, uint32_t color);
uint32_t backbuffer_get_pixel(int x, int y);
void backbuffer_fill_rect(int x, int y, int w, int h, uint32_t color);
void backbuffer_blit(void);
bool backbuffer_is_active(void);
int  backbuffer_width(void);
int  backbuffer_height(void);

void backbuffer_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void backbuffer_draw_string(int x, int y, const char* str, uint32_t fg, uint32_t bg);
void backbuffer_draw_string_transparent(int x, int y, const char* str, uint32_t fg);

void backbuffer_draw_char_scaled(int x, int y, char c, uint32_t fg, uint32_t bg);
void backbuffer_draw_string_scaled(int x, int y, const char* str, uint32_t fg, uint32_t bg);
void backbuffer_draw_char_smooth(int x, int y, char c, uint32_t fg);
void backbuffer_draw_string_smooth(int x, int y, const char* str, uint32_t fg);

void backbuffer_draw_rect(int x, int y, int w, int h, uint32_t color);
void backbuffer_fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);
void backbuffer_draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);

#endif
