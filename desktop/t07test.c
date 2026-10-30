#include "dsk.h"
#include "caps.h"
#include "t07_sig_data.h"

/* ==================================================================== */
/* 0.7 oz-testleri                                                       */
/*                                                                      */
/* Sonuclar seri porta "[t07] <ad> OK/FAIL" olarak yazilir.              */
/* Yalnizca ARCTIAN_07_TEST tanimliyken derlenir ve calisir.             */
/* ==================================================================== */

static inline void t_outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p));
}
static inline uint8_t t_inb(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v;
}

static void t_puts(const char *s) {
    for (; s && *s; s++) {
        while (!(t_inb(0x3FD) & 0x20)) { }
        t_outb(0x3F8, (uint8_t)*s);
    }
}

static void t_putu(uint32_t v) {
    char b[12];
    int n = 0;
    if (!v) { t_puts("0"); return; }
    while (v && n < (int)sizeof(b)) { b[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) { char c = b[--n]; while (!(t_inb(0x3FD) & 0x20)) { } t_outb(0x3F8, (uint8_t)c); }
}

static void t_res(const char *name, int ok) {
    t_puts("[t07] ");
    t_puts(name);
    t_puts(ok ? " OK\n" : " FAIL\n");
}

/* ---- onay kancasi (oz-testte pencere cizilmez) ---- */
static int s_hook_mode;
static int s_hook_calls;

static int t_hook(int type, const char *src) {
    (void)type; (void)src;
    s_hook_calls++;
    return s_hook_mode;
}

/* ==================================================================== */
/* Kimlik ve ozel dizin                                                  */
/* ==================================================================== */
static void t_s1(void) {
    if (!g_asi || !g_asi->fs) { t_res("ozne-home", 0); return; }

    int rc = subj_open("dpk:ozne1", "Ozne Test", 0, "C:/apps/ozne1.dpk");
    int home = 0;
    if (rc == 0 && subj_home()[0]) {
        home = (g_asi->fs->find_in("C:/uygulamalar", "ozne1/") != 0);
    }
    t_res("ozne-home", rc == 0 && home);
    t_res("ozne-kutulu", subj_trusted() == 0 &&
                         s_eq(caps_subject_id(), "dpk:ozne1"));
    subj_close();

    rc = subj_open("gomulu:terminal", "Terminal", 1, "");
    t_res("ozne-guvenilen", rc == 0 && subj_trusted() == 1);
    subj_close();

    /* Ayni ad, farkli kaynak: kayitlar karismaz. */
    perm_dec_drop("dpk:x");
    perm_dec_drop("wlf:x");
    subj_open("dpk:x", "DPK X", 0, "C:/apps/x.dpk");
    perm_dec_set("dpk:x", PERM_T_PATH, "C:/ortak", PERM_ALWAYS);
    subj_close();
    subj_open("wlf:x", "WLF X", 0, "C:/apps/x.wlf");
    int farkli_kimlik = !s_eq(caps_subject_id(), "dpk:x") &&
                        s_eq(caps_subject_title(), "WLF X");
    int ayr_karar = perm_dec_get("wlf:x", PERM_T_PATH, "C:/ortak") == PERM_ASK;
    subj_close();
    t_res("ozne-ad-carpisma", farkli_kimlik && ayr_karar);
}

/* ==================================================================== */
/* Karar deposu                                                          */
/* ==================================================================== */
static void t_s2(void) {
    perm_dec_load();

    /* once: ilk okuma once, tuketilince yok */
    perm_dec_drop("dpk:t07");
    perm_dec_set("dpk:t07", PERM_T_PATH, "C:/a", PERM_ONCE);
    int ilk = perm_dec_get("dpk:t07", PERM_T_PATH, "C:/a");
    perm_dec_consume("dpk:t07", PERM_T_PATH, "C:/a");
    int ikinci = perm_dec_get("dpk:t07", PERM_T_PATH, "C:/a");
    t_res("karar-once", ilk == PERM_ONCE && ikinci == PERM_ASK);

    /* always: dosyaya yazilir, yeniden okununca durur */
    perm_dec_set("dpk:t07", PERM_T_PATH, "C:/b", PERM_ALWAYS);
    perm_dec_load();
    t_res("karar-always", perm_dec_get("dpk:t07", PERM_T_PATH, "C:/b") == PERM_ALWAYS);

    /* deny: ayni sekilde kalici */
    perm_dec_set("dpk:t07", PERM_T_EXEC, "tehlikeli", PERM_DENY);
    perm_dec_load();
    t_res("karar-deny", perm_dec_get("dpk:t07", PERM_T_EXEC, "tehlikeli") == PERM_DENY);

    /* ".." reddedilir, sorulmaz */
    char out[128];
    int nokta = perm_norm(out, sizeof(out), "C:/belgeler/../sifre") == 0;
    int normal = perm_norm(out, sizeof(out), "C:\\belgeler\\//a.txt\\") == 1 &&
                 s_eq(out, "C:/belgeler/a.txt");
    t_res("karar-nokta-nokta", nokta && normal);

    /* 129. kayit -1 doner */
    int dolu = 0;
    for (int i = 0; i < 200; i++) {
        char src[24];
        int n = 0;
        const char *p = "C:/d";
        while (*p) src[n++] = *p++;
        src[n++] = (char)('a' + (i / 26) % 26);
        src[n++] = (char)('a' + i % 26);
        src[n] = 0;
        if (perm_dec_set("dpk:t07", PERM_T_PATH, src, PERM_ONCE) != 0) { dolu = 1; break; }
    }
    t_res("karar-dolu", dolu && perm_dec_count() == 128);

    perm_dec_drop("dpk:t07");
}

/* ==================================================================== */
/* Onay penceresi                                                        */
/* ==================================================================== */
static void t_s3(void) {
    consent_set_hook(t_hook);

    s_hook_calls = 0; s_hook_mode = PERM_ONCE;
    int v = consent_ask(PERM_T_PATH, "C:/test-once");
    t_res("onay-once", v == PERM_ONCE && s_hook_calls == 1);

    /* always: karar deposuna yazilir */
    s_hook_calls = 0; s_hook_mode = PERM_ALWAYS;
    perm_dec_drop("dpk:t07s3");
    subj_open("dpk:t07s3", "S3 Test", 0, "C:/apps/s3.dpk");
    perm_dispatch_begin();
    int rc = perm_policy("C:/s3-yol", 0);
    perm_dispatch_end();
    int kayit = perm_dec_get("dpk:t07s3", PERM_T_PATH, "C:/s3-yol") == PERM_ALWAYS;
    subj_close();
    t_res("onay-always-yaz", rc == 0 && kayit);

    /* Esc = Reddet (gercek pencere + gercek olay yolu) */
    s_hook_mode = PERM_DENY;
    if (consent_test_show(PERM_T_PATH, "C:/s3-esc") == 0) {
        consent_send_esc();
        t_res("onay-esc", consent_answer_now() == PERM_DENY);
        consent_test_end();
    } else {
        t_res("onay-esc", 0);
    }

    consent_set_hook(0);
    perm_dec_drop("dpk:t07s3");
}

/* ==================================================================== */
/* Dosya kapisi                                                          */
/* ==================================================================== */
static void t_s4(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in) { t_res("kapi-home", 0); return; }
    consent_set_hook(t_hook);
    perm_dec_drop("dpk:kapi");

    subj_open("dpk:kapi", "Kapi Test", 0, "C:/apps/kapi.dpk");

    /* home: sormadan yazilir */
    s_hook_calls = 0; s_hook_mode = PERM_DENY;
    perm_dispatch_begin();
    void *h = g_asi->fs->create_in(subj_home(), "ev.txt", 0);
    int ev = 0;
    if (h) { ev = (g_asi->fs->write(h, "merhaba", 7) == 0); }
    perm_dispatch_end();
    t_res("kapi-home", ev != 0 && s_hook_calls == 0);
    if (ev) g_asi->fs->remove("C:/uygulamalar/kapi/ev.txt");

    /* deny: olusturma NULL doner ve hata SANDBOX_EPERM olur */
    s_hook_calls = 0; s_hook_mode = PERM_DENY;
    perm_dispatch_begin();
    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/kapi_denied");
    void *h2 = g_asi->fs->create_in("C:/kapi_denied", "yok.txt", 0);
    int err = caps_last_error();
    perm_dispatch_end();
    t_res("kapi-deny", h2 == 0 && err == SANDBOX_EPERM && s_hook_calls >= 1);

    /* allow: yazilir, ikinci cagri karar durdugu icin sorulmaz */
    s_hook_calls = 0; s_hook_mode = PERM_ALWAYS;
    perm_dispatch_begin();
    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/kapi_allow");
    void *h3 = g_asi->fs->create_in("C:/kapi_allow", "var.txt", 0);
    int var = 0;
    if (h3) var = (g_asi->fs->write(h3, "x", 1) == 0);
    int ilk_soru = s_hook_calls;
    s_hook_mode = PERM_DENY;               /* artik sormali, karar always */
    void *h4 = g_asi->fs->create_in("C:/kapi_allow", "ikinci.txt", 0);
    int ikinci_soru = s_hook_calls;
    perm_dispatch_end();
    t_res("kapi-allow", var != 0 && ikinci_soru == ilk_soru && h4 != 0);

    /* kullanicinin verdigi yol: ikinci soru uretmez */
    s_hook_calls = 0; s_hook_mode = PERM_DENY;
    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/kapi_vergi");
    consent_grant_user_path("dpk:kapi", "C:/kapi_vergi");
    perm_dispatch_begin();
    void *h5 = g_asi->fs->create_in("C:/kapi_vergi", "veri.txt", 0);
    perm_dispatch_end();
    t_res("kapi-kullanici-verimi", h5 != 0 && s_hook_calls == 0);

    /* guvenilen arac (Terminal) sorulmaz */
    subj_close();
    subj_open("gomulu:terminal", "Terminal", 1, "");
    s_hook_calls = 0; s_hook_mode = PERM_DENY;
    perm_dispatch_begin();
    void *h6 = g_asi->fs->create_in("C:/", "t07_terminal.txt", 0);
    perm_dispatch_end();
    t_res("kapi-terminal", h6 != 0 && s_hook_calls == 0);
    if (h6) g_asi->fs->remove("C:/t07_terminal.txt");

    subj_close();
    consent_set_hook(0);
    perm_dec_drop("dpk:kapi");
    if (g_asi->fs->remove) {
        g_asi->fs->remove("C:/kapi_allow/var.txt");
        g_asi->fs->remove("C:/kapi_allow/ikinci.txt");
        g_asi->fs->remove("C:/kapi_vergi/veri.txt");
    }
}

/* ==================================================================== */
/* Komut kapisi                                                          */
/* ==================================================================== */
static void t_s5(void) {
    consent_set_hook(t_hook);
    perm_dec_drop("dpk:komut");

    subj_open("dpk:komut", "Komut Test", 0, "C:/apps/komut.dpk");

    /* deny */
    s_hook_calls = 0; s_hook_mode = PERM_DENY;
    perm_dispatch_begin();
    int redd = perm_exec("ls C:/");
    perm_dispatch_end();
    t_res("komut-deny", redd != 0 && s_hook_calls == 1);

    /* allow */
    s_hook_calls = 0; s_hook_mode = PERM_ALWAYS;
    perm_dispatch_begin();
    int izin = perm_exec("sysinfo");
    int ilk = s_hook_calls;
    s_hook_mode = PERM_DENY;
    int tekrar = perm_exec("sysinfo");
    int sonrasi = s_hook_calls;
    perm_dispatch_end();
    t_res("komut-allow", izin == 0 && tekrar == 0 && sonrasi == ilk);

    /* guvenilen Terminal: soru uretmez */
    subj_close();
    subj_open("gomulu:terminal", "Terminal", 1, "");
    s_hook_calls = 0; s_hook_mode = PERM_DENY;
    perm_dispatch_begin();
    int term = perm_exec("ls");
    perm_dispatch_end();
    t_res("komut-terminal", term == 0 && s_hook_calls == 0);

    subj_close();
    consent_set_hook(0);
    perm_dec_drop("dpk:komut");
}

/* ==================================================================== */
/* Paket ve guncelleme imzasi                                            */
/* ==================================================================== */
static void t_s8(void) {
    /* Saglam paket: imza gecerli. */
    int ok = sig_verify_pkg(T07_PKG, T07_PKG_SIZE) == 0;
    t_res("imza-tamam", ok);

    /* Bir bayti degisen paket reddedilir. */
    static unsigned char bozuk[512];
    for (uint32_t i = 0; i < T07_PKG_SIZE && i < sizeof(bozuk); i++) bozuk[i] = T07_PKG[i];
    bozuk[0] ^= 0x01;
    t_res("imza-bozuk", sig_verify_pkg(bozuk, T07_PKG_SIZE) != 0);

    /* Imzali manifesto dogrulanir; imzasiz kabul edilmez. */
    update_test_reset();
    update_test_parse((const char *)T07_MANIFEST_FULL, T07_MANIFEST_FULL_SIZE);
    int imzali = update_manifest_verified();
    update_test_reset();
    update_test_parse((const char *)T07_MANIFEST_UNSIGNED, T07_MANIFEST_UNSIGNED_SIZE);
    int imzasiz = update_manifest_verified();
    t_res("imza-manifesto", imzali == 1 && imzasiz == 0);
}

/* ==================================================================== */
/* Ayarlar: otomatik guncelleme ve gecikme                               */
/* ==================================================================== */
static void t_s9(void) {
    settings_set_int("update_auto", 1);
    settings_set_int("update_delay_days", 0);
    t_res("guncelle-varsayilan", kv_get_int("update_auto", 0) == 1 &&
                                 kv_get_int("update_delay_days", -1) == 0);

    settings_set_int("update_delay_days", 31);
    int ust = kv_get_int("update_delay_days", -1);
    settings_set_int("update_delay_days", -5);
    int alt = kv_get_int("update_delay_days", -1);
    t_res("guncelle-gecikme-sinir", ust == 30 && alt == 0);

    settings_set_int("update_delay_days", 0);
    t_res("guncelle-gecikme-yaz", kv_get_int("update_delay_days", -1) == 0);
}

/* ==================================================================== */
/* Sistem manifestosu ve gecikme kurali                                  */
/* ==================================================================== */
#define T07_REL 1793000000ull
static void t_s10(void) {
    uint64_t gun = 86400ull;
    t_res("guncelle-gun-dolmadi",
          update_eligible((uint32_t)T07_REL, 3, T07_REL + 2 * gun) == 0);
    t_res("guncelle-gun-doldu",
          update_eligible((uint32_t)T07_REL, 3, T07_REL + 3 * gun) == 1);
    t_res("guncelle-saat-yok",
          update_eligible((uint32_t)T07_REL, 3, 0) == 0 &&
          update_eligible((uint32_t)T07_REL, 0, 0) == 1);

    /* Imzasiz manifesto surum yukseltmez.                    */
    update_test_reset();
    update_test_parse((const char *)T07_MANIFEST_UNSIGNED, T07_MANIFEST_UNSIGNED_SIZE);
    int surum = update_manifest()->version[0] != 0;
    t_res("guncelle-imzasiz", !update_manifest_verified() && surum);
}

/* ==================================================================== */
/* Kapanis ve acilista kurulum                                           */
/* ==================================================================== */
static int s_t07_auto = 1;
static int s_t07_delay = 0;
static int t07_auto_fn(void) { return s_t07_auto; }
static int t07_delay_fn(void) { return s_t07_delay; }
static int t07_real_auto(void) { return kv_get_int("update_auto", 1); }
static int t07_real_delay(void) { return kv_get_int("update_delay_days", 0); }

static void t_s11(void) {
    update_set_settings(t07_auto_fn, t07_delay_fn);
    slot_test_begin();

    /* 1) otomatik kapali: yuva degismez */
    update_test_reset();
    update_test_parse((const char *)T07_MANIFEST_FULL, T07_MANIFEST_FULL_SIZE);
    update_test_set_now(T07_REL + 10 * 86400ull);
    s_t07_auto = 0; s_t07_delay = 0;
    update_set_ready(1);
    int a1 = update_shutdown_step();
    int s1 = slot_active();
    t_res("guncelle-kapali", a1 == 0 && s1 == 0);

    /* 2) gecikme dolmadi */
    s_t07_auto = 1; s_t07_delay = 3;
    update_test_set_now(T07_REL + 86400ull);
    update_set_ready(1);
    int a2 = update_shutdown_step();
    t_res("guncelle-erken", a2 == 0 && slot_active() == 0);

    /* 3) gecikme doldu + paket hazir: yuva cevrilir */
    update_test_set_now(T07_REL + 4 * 86400ull);
    update_set_ready(1);
    int a3 = update_shutdown_step();
    int s3 = slot_active();
    t_res("guncelle-kur", a3 == 1 && s3 == 1);

    /* 4) ag yok / paket hazir degil: kapanis durmaz (0 veya hata, cokme yok) */
    update_set_ready(0);
    int a4 = update_shutdown_step();
    t_res("guncelle-agsiz", a4 == 0);

    slot_test_end();
    update_set_ready(0);
    update_test_reset();
    update_set_settings(t07_real_auto, t07_real_delay);
}

/* ==================================================================== */
/* A/B yuva ve geri donus                                                */
/* ==================================================================== */
static void t_s12(void) {
    slot_test_begin();

    /* Yazma kesilirse etkin yuva degismez. */
    slot_test_cut(1);
    int r1 = slot_switch_pending();
    int a1 = slot_active();
    t_res("yuva-kesik", r1 != 0 && slot_test_cut_hit() == 1 && a1 == 0);

    /* boot_ok vermeyen yeni yuva sonraki acilista terkedilir. */
    slot_test_cut(0);
    int sw = slot_switch_pending();
    int bad = slot_active();
    int from = -1, to = -1;
    int guard = slot_boot_guard(&from, &to);
    t_res("yuva-geri", sw == 0 && bad == 1 && guard == 1 &&
                       from == 1 && to == 0 && slot_active() == 0);

    /* Kullanici diski iki yuvada da ayni: yuva islemi AFS'e dokunmaz. */
    int kd = 1;
    if (g_asi && g_asi->fs && g_asi->fs->create_in && g_asi->fs->write &&
        g_asi->fs->read_path) {
        g_asi->fs->mkdir("C:/belgeler");
        void *h = g_asi->fs->create_in("C:/belgeler", "ab.txt", 0);
        if (h) g_asi->fs->write(h, "arsiv", 5);
        slot_switch_pending();
        slot_boot_guard(0, 0);
        char b[16];
        uint32_t n = g_asi->fs->read_path("C:/belgeler/ab.txt", b, sizeof(b) - 1);
        b[n] = 0;
        kd = (n == 5 && s_eq(b, "arsiv"));
    }
    t_res("yuva-kullanici-disk", kd);

    slot_test_end();
}

void t07_selftest(void) {
    t_puts("[t07] ==== 0.7 oz-testleri ====\n");
    t_s1();
    t_s2();
    t_s3();
    t_s4();
    t_s5();
    t_s8();
    t_s9();
    t_s10();
    t_s11();
    t_s12();
    t_puts("[t07] ==== bitti ====\n");
}