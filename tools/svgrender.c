#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "svg.h"

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "kullanim: svgrender <girdi.svg> <G> <Y> <cikti.raw>\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 3;
    if (fseek(f, 0, SEEK_END) != 0) return 3;
    long n = ftell(f);
    if (n <= 0) return 3;
    if (fseek(f, 0, SEEK_SET) != 0) return 3;
    char *svg = (char *)malloc((size_t)n + 1);
    if (!svg) return 4;
    if (fread(svg, 1, (size_t)n, f) != (size_t)n) return 4;
    svg[n] = 0;
    fclose(f);

    int W = atoi(argv[2]), H = atoi(argv[3]);
    if (W <= 0 || H <= 0) return 5;
    uint32_t *out = (uint32_t *)calloc((size_t)W * H, 4);
    if (!out) return 6;

    int r = svg_render(svg, W, H, out);
    FILE *o = fopen(argv[4], "wb");
    if (!o) return 7;
    if (fwrite(out, 4, (size_t)W * H, o) != (size_t)W * H) return 8;
    fclose(o);

    int nz = 0;
    for (int i = 0; i < W * H; i++) if ((out[i] >> 24) & 0xFF) nz++;
    fprintf(stderr, "svgrender %dx%d ret=%d dolu=%.1f%%\n", W, H, r,
            100.0 * nz / (double)(W * H));
    free(svg);
    free(out);
    return r != 0;
}
