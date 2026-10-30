/* ============================================================
 * emu_time.c — deterministic virtual clock (--deterministic).
 *
 * When enabled, ALL time in the emulator comes from one virtual
 * counter (no real clock, no real sleeping):
 *   - plat_ticks_ms()  -> virtual now
 *   - plat_wait_ms(n)  -> advance virtual now by n ms, return at once
 *   - main loop        -> +1 ms heartbeat per iteration (monotonic drip)
 *   - asi t_now/t_unix -> fixed epoch (2026-01-01T00:00Z) + virtual now
 *
 * Consequence: the framebuffer after N frames is a PURE FUNCTION of
 * the .wlf and the script (--click times are virtual too), so
 * screenshots are byte-identical across machines/OSes. The cost:
 * frames run as fast as the CPU allows (--fps pacing is skipped) and
 * wall-clock pacing no longer matches a real device.
 * ============================================================ */
#include "emu.h"

#include <time.h>

static int      s_on;
static uint64_t s_vt;                                   /* virtual ms      */
static const time_t s_epoch = 1767225600;               /* 2026-01-01T00:00Z */

void emu_det_set(int on) { s_on = on; s_vt = 0; }

int emu_det_active(void) { return s_on; }

uint32_t emu_det_now(void) { return (uint32_t)s_vt; }

void emu_det_advance(uint32_t ms) { if (s_on) s_vt += (ms ? ms : 1u); }

time_t emu_det_unix(void) { return s_epoch + (time_t)(s_vt / 1000u); }
