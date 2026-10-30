#ifndef ARCTIAN_SVG_H
#define ARCTIAN_SVG_H

#include <stdint.h>

/*
 * Minimal SVG alt kumesi -> ARGB8888 rasterizer (ikonlar icin).
 * Destek: svg(viewBox), g(translate/scale), rect, circle, ellipse, line,
 *         polygon, path (M/L/H/V/C/Q/A/Z, mutlak+relatif),
 *         fill, stroke, stroke-width, opacity/fill-opacity/stroke-opacity,
 *         #RGB/#RRGGBB ve bazi isimli renkler. Solid renkler (gradient yok).
 * out: w*h uint32, 0xAARRGGBB (alpha=0 -> seffaf).
 */
int svg_render(const char *svg, int w, int h, uint32_t *out);
void svg_intrinsic_size(const char *svg, int *w, int *h);

#endif /* ARCTIAN_SVG_H */
