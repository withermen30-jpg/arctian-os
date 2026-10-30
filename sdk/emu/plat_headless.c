/**
 * plat_headless.c — penceresiz arka uc (CI, Docker, PNG cikti).
 */
#ifdef EMU_PLAT_HEADLESS

#include "emu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/time.h>
#endif

int plat_init(int w, int h, const char *title) {
    (void)w; (void)h;
    fprintf(stderr, "[emu] arka uc: headless");
    if (title && title[0]) fprintf(stderr, " — %s", title);
    fprintf(stderr, "\n");
    return 0;
}

void plat_shutdown(void) {}
const char *plat_name(void) { return "headless"; }
void plat_set_title(const char *t) { (void)t; }
void plat_set_size(int w, int h) { (void)w; (void)h; }
void plat_present_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }

int plat_pump(void) { return 0; }

void plat_wait_ms(int ms) {
    if (emu_det_active()) { emu_det_advance((uint32_t)ms); return; }
#ifdef _WIN32
    Sleep((DWORD)(ms > 0 ? ms : 1));
#else
    usleep((useconds_t)(ms > 0 ? ms : 1) * 1000u);
#endif
}

uint32_t plat_ticks_ms(void) {
    if (emu_det_active()) return emu_det_now();
#ifdef _WIN32
    return (uint32_t)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (uint32_t)((uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u);
#endif
}

#endif /* EMU_PLAT_HEADLESS */
