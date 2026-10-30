/**
 * emu_native.c — native .wlf yukleyicisi.
 *
 * OS'taki wlf_run_native ile ayni akis:
 *   kod load_addr'e kopyalanir, bss sifirlanir, wolf_main(api) cagrilir.
 * Fark: burada adres GERCEK sanal bellege ayrilir (Linux mmap / Windows
 * VirtualAlloc). Cokme durumunda uzun atlama ile donulur (OS'teki
 * recover.c'nin masaustu karsiligi).
 */
#include "emu.h"
#include "dsk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <signal.h>
#  include <setjmp.h>
#  include <unistd.h>
#  ifndef MAP_FIXED_NOREPLACE
#    define MAP_FIXED_NOREPLACE 0x100000
#  endif
#endif

static void *s_base;          /* tahsis edilen bolge */
static size_t s_maplen;

static int map_exec(uintptr_t want, size_t len, void **out, char *err, int cap) {
#ifdef _WIN32
    void *p = VirtualAlloc((void *)want, len, MEM_COMMIT | MEM_RESERVE,
                           PAGE_EXECUTE_READWRITE);
    if (!p) {
        snprintf(err, cap, "VirtualAlloc(0x%llX) basarisiz (err=%lu)",
                 (unsigned long long)want, GetLastError());
        return -1;
    }
    *out = p;
    if ((uintptr_t)p != want)
        fprintf(stderr, "[emu] not: istenen 0x%llX yerine 0x%llX ayrildi\n",
                (unsigned long long)want, (unsigned long long)(uintptr_t)p);
    return 0;
#else
    void *p = mmap((void *)want, len, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) {
        /* MAP_FIXED_NOREPLACE eski cekirdeklerde yok: manuel kontrol */
        p = mmap((void *)want, len, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (p == MAP_FAILED) {
            snprintf(err, cap, "mmap(0x%llX) basarisiz",
                     (unsigned long long)want);
            return -1;
        }
    }
    *out = p;
    return 0;
#endif
}

static void unmap_all(void) {
    if (!s_base) return;
#ifdef _WIN32
    VirtualFree(s_base, 0, MEM_RELEASE);
#else
    munmap(s_base, s_maplen);
#endif
    s_base = 0;
    s_maplen = 0;
}

/* ---- cokme korumasi ---- */
#ifdef _WIN32
/* mingw C modunda __try garanti degil; Vectored Exception Handler + setjmp
 * kullanilir (VEH, istisnayi yutup dogrudan koruma noktasina doner). */
#include <setjmp.h>

static jmp_buf s_jmp;
static volatile int s_in_app;
static void *s_veh;
static int g_call_rc;
static const wolf_api_t *g_call_api;
static void *g_call_fn;

static LONG CALLBACK veh_filter(PEXCEPTION_POINTERS ep) {
    (void)ep;
    if (s_in_app) { s_in_app = 0; longjmp(s_jmp, 1); }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* .wlf girisi SysV ABI'dir (bkz. emu_bridge.c). */
typedef int (*fn_t)(const wolf_api_t *) __attribute__((sysv_abi));

static int guarded_call(void) {
    s_veh = AddVectoredExceptionHandler(1, veh_filter);
    if (setjmp(s_jmp) == 0) {
        s_in_app = 1;
        g_call_rc = ((fn_t)g_call_fn)(g_call_api);
        s_in_app = 0;
    } else {
        g_call_rc = -0x7FF;
    }
    if (s_veh) { RemoveVectoredExceptionHandler(s_veh); s_veh = 0; }
    return g_call_rc;
}
#else
static sigjmp_buf s_jmp;
static volatile int s_in_app;
static int g_call_rc;
static const wolf_api_t *g_call_api;
static void *g_call_fn;

static void segv_handler(int sig) {
    (void)sig;
    if (s_in_app) siglongjmp(s_jmp, 1);
    _exit(139);
}

static int guarded_call(void) {
    struct sigaction sa, old_segv, old_bus, old_fpe, old_ill;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = segv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, &old_segv);
    sigaction(SIGBUS, &sa, &old_bus);
    sigaction(SIGFPE, &sa, &old_fpe);
    sigaction(SIGILL, &sa, &old_ill);

    if (sigsetjmp(s_jmp, 1) == 0) {
        s_in_app = 1;
        typedef int (*fn_t)(const wolf_api_t *);
        g_call_rc = ((fn_t)g_call_fn)(g_call_api);
        s_in_app = 0;
    } else {
        g_call_rc = -0x7FF;
    }
    sigaction(SIGSEGV, &old_segv, 0);
    sigaction(SIGBUS, &old_bus, 0);
    sigaction(SIGFPE, &old_fpe, 0);
    sigaction(SIGILL, &old_ill, 0);
    return g_call_rc;
}
#endif

int emu_run_native(emu_pkg_t *pkg, wolf_api_t *api, char *err, int errcap) {
    const wlf_header_t *h = pkg->hdr;
    size_t imglen = h->image_size ? h->image_size : h->payload_len;
    size_t len = (size_t)h->image_size + h->bss_size;
    if (!len) len = h->payload_len + 4096;
    len = (len + 0xFFF) & ~(size_t)0xFFF;

    if (s_base) unmap_all();
    if (map_exec((uintptr_t)h->load_addr, len, &s_base, err, errcap) != 0)
        return -1;
    s_maplen = len;

    uint8_t *dst = (uint8_t *)s_base;
    memcpy(dst, pkg->payload, imglen);
    if (h->bss_size) memset(dst + h->image_size, 0, h->bss_size);

    g_call_api = emu_bridge_api(api);   /* Windows: SysV koprusu */
    g_call_fn = dst + h->entry_off;
    int rc = guarded_call();
    if (rc == -0x7FF) {
        snprintf(err, errcap, "uygulama coktu (erisim ihlali)");
        return -1;
    }
    return rc;
}
