#include "dsk.h"

static inline void dst_outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p));
}

static inline uint8_t dst_inb(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v;
}

void dst_puts(const char *s) {
    for (; s && *s; s++) {
        while (!(dst_inb(0x3FD) & 0x20)) { }
        dst_outb(0x3F8, (uint8_t)*s);
    }
}

void dst_putx(uint32_t v) {
    static const char h[] = "0123456789abcdef";
    char b[9]; int i = 0;
    if (v == 0) { dst_puts("0"); return; }
    while (v && i < 8) { b[i++] = h[v & 0xF]; v >>= 4; }
    while (i) { char c = b[--i]; while (!(dst_inb(0x3FD) & 0x20)) { } dst_outb(0x3F8, (uint8_t)c); }
}


void duckstore_selftest(void) {
    dst_puts("[ds] selftest start\n");
    if (!g_asi || !g_asi->net || !g_asi->net->http_get) { dst_puts("[ds] no net\n"); return; }
    if (!arctian_store || !arctian_store->install) { dst_puts("[ds] no api\n"); return; }

    /* --- Masaustu arka plani testi (buyuk dosya, akisli indirme) --- */
    {
        dst_puts("[ds] wall start\n");
        uint32_t cap = 6u * 1024u * 1024u + 8u * 1024u;
        uint8_t *wb = (uint8_t *)g_asi->mem->alloc(cap);
        uint32_t wl = 0;
        int dr = (wb && g_asi->net->download)
                 ? g_asi->net->download("http://10.0.2.2:8091/wallpapers/detay.png",
                                        wb, cap, &wl)
                 : -100;
        dst_puts("[ds] wall dl rc="); dst_putx((uint32_t)dr);
        dst_puts(" bytes="); dst_putx(wl); dst_puts("\n");
        if (dr == 0 && wl > 0) {
            void *h = g_asi->fs->create(0, "detay.wp", 0);
            int wr = h ? g_asi->fs->write(h, wb, wl) : -1;
            dst_puts("[ds] wall save rc="); dst_putx((uint32_t)wr); dst_puts("\n");
            int sr = arctian_store->set_wallpaper("detay.wp");
            dst_puts("[ds] wall set rc="); dst_putx((uint32_t)sr);
            dst_puts(" active="); dst_putx((uint32_t)arctian_store->wallpaper_active());
            dst_puts(" name="); dst_puts(arctian_store->wallpaper_name()); dst_puts("\n");
        }
    }

    uint32_t len = 0;
    const char *body = g_asi->net->http_get("http://10.0.2.2:8091/packages/hesapmakinesi.dpk", &len);
    if (!body || !len) { dst_puts("[ds] fetch FAIL\n"); return; }
    dst_puts("[ds] fetched len="); dst_putx(len); dst_puts("\n");

    char err[110];
    int r = arctian_store->install(body, len, err, sizeof(err));
    dst_puts("[ds] install rc="); dst_putx((uint32_t)r);
    dst_puts(" err="); dst_puts(err); dst_puts("\n");

    dst_puts("[ds] installed="); dst_putx((uint32_t)arctian_store->installed("hesapmakinesi"));
    dst_puts(" count="); dst_putx((uint32_t)arctian_store->count_installed()); dst_puts("\n");

    char e2[110];
    e2[0] = 0;
    int r2 = arctian_dyn_preload(0, e2, sizeof(e2));
    dst_puts("[ds] preload rc="); dst_putx((uint32_t)r2);
    dst_puts(" err="); dst_puts(e2); dst_puts("\n");

    uint8_t *code = (uint8_t *)(uintptr_t)0x01800000u;
    uint32_t sig = 0;
    for (int i = 0; i < 16; i++) sig = (sig << 1) ^ code[i];
    dst_puts("[ds] code_sig="); dst_putx(sig); dst_puts("\n");

    /* Uygulama girisini gercekten calistir (gecici bir kapsayici uzerinde). */
    arctian_dyn_begin_launch(0);
    const arctian_app_t *av = app_get(arctian_app_count);
    dst_puts("[ds] entry_off="); dst_putx(g_dyn[0].entry_off);
    dst_puts(" load="); dst_putx(g_dyn[0].load_addr);
    dst_puts(" img="); dst_putx(g_dyn[0].image_size);
    dst_puts(" bss="); dst_putx(g_dyn[0].bss_size);
    dst_puts("\n");
    if (av && av->entry) {
        lv_obj_t *tmp = lv_obj_create(lv_screen_active());
        lv_obj_set_size(tmp, 300, 400);
        dst_puts("[ds] calling entry\n");
        av->entry(tmp, g_asi);
        dst_puts("[ds] entry_ok\n");
    } else {
        dst_puts("[ds] entry MISSING\n");
    }

    /* --- Zig ile derlenmis paket (zigornek) --- */
    {
        uint32_t zl = 0;
        const char *zb = g_asi->net->http_get("http://10.0.2.2:8091/packages/zigornek.dpk", &zl);
        if (!zb || !zl) {
            dst_puts("[ds] zig fetch FAIL\n");
        } else {
            char zerr[110];
            zerr[0] = 0;
            int zr = arctian_store->install(zb, zl, zerr, sizeof(zerr));
            dst_puts("[ds] zig install rc="); dst_putx((uint32_t)zr);
            dst_puts(" err="); dst_puts(zerr); dst_puts("\n");
            int di = dyn_find("zigornek");
            dst_puts("[ds] zig idx="); dst_putx((uint32_t)di); dst_puts("\n");
            if (di >= 0) {
                char ze[110];
                ze[0] = 0;
                int pr = arctian_dyn_preload(di, ze, sizeof(ze));
                dst_puts("[ds] zig preload rc="); dst_putx((uint32_t)pr);
                dst_puts(" err="); dst_puts(ze); dst_puts("\n");
                arctian_dyn_begin_launch(di);
                const arctian_app_t *zv = app_get(arctian_app_count + di);
                if (zv && zv->entry) {
                    lv_obj_t *zt = lv_obj_create(lv_screen_active());
                    lv_obj_set_size(zt, 400, 260);
                    dst_puts("[ds] zig calling entry\n");
                    zv->entry(zt, g_asi);
                    dst_puts("[ds] zig entry_ok\n");
                } else {
                    dst_puts("[ds] zig entry MISSING\n");
                }
            }
        }
    }
    dst_puts("[ds] selftest end\n");
}

void update_selftest(void) {
    dst_puts("[upd] selftest start\n");
    dst_puts("[upd] base="); dst_puts(update_base_url()); dst_puts("\n");
    dst_puts("[upd] current="); dst_puts(update_current_version()); dst_puts("\n");

    int rc = update_check();
    const update_status_t *st = update_status();
    dst_puts("[upd] check rc="); dst_putx((uint32_t)rc);
    dst_puts(" state="); dst_putx((uint32_t)st->state);
    dst_puts(" target="); dst_puts(st->version);
    dst_puts(" msg="); dst_puts(st->message); dst_puts("\n");

    if (rc > 0) {
        int ra = update_apply();
        dst_puts("[upd] apply rc="); dst_putx((uint32_t)ra);
        dst_puts(" state="); dst_putx((uint32_t)update_status()->state);
        dst_puts(" msg="); dst_puts(update_status()->message); dst_puts("\n");
        dst_puts("[upd] applied="); dst_puts(update_applied_version());
        dst_puts(" installed="); dst_putx((uint32_t)arctian_store->count_installed());
        dst_puts("\n");
        /* Dogrulama: kurulan modul gercekten yuklenebiliyor mu? */
        int di = dyn_find("hesapmakinesi");
        dst_puts("[upd] module idx="); dst_putx((uint32_t)di); dst_puts("\n");
    }
    dst_puts("[upd] selftest end\n");

    /* Test: Ayarlar > Sistem sayfasini ac (guncelleme karti gorunur). */
    arctian_shell_launch("Ayarlar");
}

