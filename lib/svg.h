#ifndef ARCTIAN_SVG_H
#define ARCTIAN_SVG_H

#include <stdint.h>

int svg_render(const char *svg, int w, int h, uint32_t *out);

#endif
