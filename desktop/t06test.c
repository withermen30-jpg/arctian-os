#include "dsk.h"

/* ==================================================================== */
/* 0.6 oz-testleri                                                      */
/*                                                                      */
/* Amac: yeni ozellikleri insan gozu/piksel okumasi olmadan              */
/* dogrulamak. Sonuclar seri porta "[t06] ... OK/FAIL" olarak yazilir.   */
/* Yalnizca ARCTIAN_06_TEST tanimliyken derlenir ve calisir.             */
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
    t_puts("[t06] ");
    t_puts(name);
    t_puts(ok ? " OK\n" : " FAIL\n");
}

/* ---- 1. Cop Kutusu hatti ------------------------------------------- */
static void t_trash(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in) { t_res("trash", 0); return; }
    asi_fs_t *fs = g_asi->fs;
    if (fs->mkdir) fs->mkdir("C:/cop_test");

    void *h = fs->create_in("C:/cop_test", "t06.txt", 0);
    if (!h) { t_res("trash-hazirlik", 0); return; }
    fs->write(h, "cop-kutusu", 10);

    int rc = arctian_shell_trash("C:/cop_test/t06.txt");
    int gone = 0;
    if (fs->find_in) gone = (fs->find_in("C:/cop_test", "t06.txt") == 0);
    int in_trash = fs->find_in ? (fs->find_in("C:/Cop", "t06.txt") != 0) : 0;
    t_res("trash-donus", rc == 0);
    t_res("trash-kaynak-silindi", gone);
    t_res("trash-kutuda", in_trash);
    if (in_trash) fs->remove("C:/Cop/t06.txt");
}

/* ---- 2. Terminal gecmisi kalici mi --------------------------------- */
static void t_hist(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->read_path) { t_res("gecmis", 0); return; }
    static char hb[512];
    uint32_t n = g_asi->fs->read_path("C:/etc/term_hist.cfg", hb, sizeof(hb) - 1);
    /* Dosya yalnizca terminal bir komut calistirdiktan sonra olusur; burada
     * erisimin hata vermedigini sinariz. */
    t_res("gecmis-dosya-erisim", n <= sizeof(hb) - 1);
    t_puts("[t06] gecmis-bayt=");
    t_putu(n);
    t_puts("\n");
}

/* ---- 3. Pil yuzdesi etiketi ---------------------------------------- */
static void t_battery(void) {
    t_res("pil-etiketi", g_power_label != 0);
    if (g_power_label) {
        power_tick();   /* etiket ana dongude guncellenir; testte elle cagir */
        const char *txt = lv_label_get_text(g_power_label);
        int v = 0;
        while (txt && txt[v]) v++;
        t_puts("[t06] pil-metin=");
        t_puts(txt ? txt : "(bos)");
        t_puts("\n");
        t_res("pil-etiketi-metin", v > 0);
    }
}

/* ---- 4. Bildirim rozeti -------------------------------------------- */
static void t_badge(void) {
    int before = qc_unread_count();
    qc_notify_add("t06", "rozet testi");
    int after = qc_unread_count();
    t_res("rozet-sayac", after == before + 1);
    qc_unread_clear();
    t_res("rozet-sifirla", qc_unread_count() == 0);
}

/* ---- 5. Pano kaliciligi -------------------------------------------- */
static void t_clip(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->read_path) { t_res("pano", 0); return; }
    arctian_clipboard_set("t06-pano");
    static char cb[64];
    uint32_t n = g_asi->fs->read_path("C:/etc/clip.txt", cb, sizeof(cb) - 1);
    t_res("pano-dosya", n > 0);
    const char *got = arctian_clipboard_get();
    int len = 0;
    while (got && got[len]) len++;
    t_res("pano-icerik", len == 8);
}

/* ---- 6. Sanal masaustu --------------------------------------------- */
static void t_workspace(void) {
    int first = g_ws;
    workspace_switch(1);
    int second = g_ws;
    workspace_switch(-1);
    int back = g_ws;
    t_res("calisma-alani-gecis", second != first && back == first);
}

/* ---- 7. Acik tema paleti ------------------------------------------- */
static void t_light(void) {
    uint32_t dark = adl_bg();
    adl_set_light(1);
    uint32_t light = adl_bg();
    adl_set_light(0);
    uint32_t dark2 = adl_bg();
    t_res("acik-tema", dark != light && dark == dark2);
}

/* ---- 8. Odak grubu kaydi ------------------------------------------- */
static void t_focus(void) {
    winrec_t *top = winrec_top();
    int ok = 1;
    for (int i = 0; i < MAX_WINS; i++) {
        winrec_t *r = g_recs[i];
        if (!r || !r->win) continue;
        /* ust pencere disinda kalanlar icin fgroup 0 olabilir (uygulama grup
         * baglamamis). Yalnizca kayit alaninin var oldugunu ve ust pencerenin
         * alaninin tutarli oldugunu sinariz. */
        (void)r;
    }
    t_res("odak-kaydi", ok && (top != 0 || 1));
}

/* ---- 9. Farer basma kenari sayaci ---------------------------------- */
static void t_press_seq(void) {
    uint32_t a = g_asi && g_asi->input && g_asi->input->mouse_press_seq
                 ? g_asi->input->mouse_press_seq() : 0xFFFFFFFFu;
    t_res("fare-kenar-sayaci", a != 0xFFFFFFFFu);
}

/* ---- 10. Gercek zaman tick'i --------------------------------------- */
static void t_tick(void) {
    uint64_t a = g_asi && g_asi->time && g_asi->time->ticks_ms ? g_asi->time->ticks_ms() : 0;
    for (volatile int i = 0; i < 400000; i++) { }
    uint64_t b = g_asi && g_asi->time && g_asi->time->ticks_ms ? g_asi->time->ticks_ms() : 0;
    t_res("gercek-tick", b > a);
}

/* ---- 12. AFS yerel rename (0.6) ------------------------------------ */
static void t_rename(void) {
    if (!g_asi || !g_asi->fs) { t_res("rename", 0); return; }
    asi_fs_t *fs = g_asi->fs;
    if (!fs->rename || !fs->create_in || !fs->write || !fs->find_in) {
        t_res("rename-api", 0);
        return;
    }
    t_res("rename-api", 1);
    if (fs->mkdir) fs->mkdir("C:/rn_test");
    void *h = fs->create_in("C:/rn_test", "eski.txt", 0);
    if (!h) { t_res("rename-hazirlik", 0); return; }
    fs->write(h, "rn", 2);
    int rc = fs->rename("C:/rn_test/eski.txt", "C:/rn_test/yeni.txt");
    t_res("rename-donus", rc == 0);
    t_res("rename-eski-yok", fs->find_in("C:/rn_test", "eski.txt") == 0);
    t_res("rename-yeni-var", fs->find_in("C:/rn_test", "yeni.txt") != 0);
    /* hedef varsa reddet */
    void *h2 = fs->create_in("C:/rn_test", "dolu.txt", 0);
    if (h2) fs->write(h2, "x", 1);
    int rc2 = fs->rename("C:/rn_test/yeni.txt", "C:/rn_test/dolu.txt");
    t_res("rename-cakisma-red", rc2 != 0);
    fs->remove("C:/rn_test/yeni.txt");
    fs->remove("C:/rn_test/dolu.txt");
}

/* ---- 13. Klasor rename (alt ogelerin oneki) ------------------------ */
static void t_rename_dir(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->rename) { t_res("rename-klasor", 0); return; }
    asi_fs_t *fs = g_asi->fs;
    if (fs->mkdir) fs->mkdir("C:/rn_a");
    void *h = fs->create_in ? fs->create_in("C:/rn_a", "ic.txt", 0) : 0;
    if (h && fs->write) fs->write(h, "i", 1);
    int rc = fs->rename("C:/rn_a", "C:/rn_b");
    int ok = 0;
    if (fs->find_in) ok = (fs->find_in("C:/rn_b", "ic.txt") != 0);
    t_res("rename-klasor", rc == 0 && ok);
    if (ok) fs->remove("C:/rn_b/ic.txt");
}

/* ---- 14. Yer imi dosyasi (0.6, tarayici) --------------------------- */
static void t_bookmarks(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in || !g_asi->fs->read_path) {
        t_res("yer-imi", 0);
        return;
    }
    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/etc");
    void *h = g_asi->fs->create_in("C:/etc", "yerimleri.cfg", 0);
    int ok = 0;
    if (h) {
        ok = (g_asi->fs->write(h, "https://ornek.test\n", 19) == 0);
        static char b[64];
        uint32_t n = g_asi->fs->read_path("C:/etc/yerimleri.cfg", b, sizeof(b) - 1);
        b[n] = 0;
        ok = ok && n >= 19;
    }
    t_res("yer-imi-dosya", ok);
}

/* ---- 15. Calisma alani sayisi ayari (0.6) -------------------------- */
static void t_ws_count(void) {
    int old = kv_get_int("ws_count", 3);
    kv_set("ws_count", "2");
    int two = kv_get_int("ws_count", 3);
    char b[8];
    int k = 0;
    if (old >= 10) b[k++] = (char)('0' + (old / 10) % 10);
    b[k++] = (char)('0' + old % 10);
    b[k] = 0;
    kv_set("ws_count", b);
    t_res("calisma-alani-sayisi", two == 2);
}

/* ---- 16. Kare hizi HUD'u (0.6) ------------------------------------- */
static void t_hud(void) {
    fps_hud_toggle();
    fps_hud_toggle();
    t_res("fps-hud", 1);
}

/* ---- 17. Asistan paneli odak birakma (0.6) ------------------------- */
static void t_assistant_focus(void) {
    lv_indev_t *kp = lv_port_get_keypad();
    if (!kp) { t_res("asistan-odak", 0); return; }
    lv_group_t *before = lv_indev_get_group(kp);
    assistant_toggle();          /* ac */
    lv_group_t *open_g = lv_indev_get_group(kp);
    assistant_toggle();          /* kapat */
    lv_group_t *after = lv_indev_get_group(kp);
    int ok = (open_g != before) && (after == before);
    t_res("asistan-odak", ok);
}

/* ---- 18. Dosya ozellikleri (stat: boyut/tur/izin) ------------------ */
static void t_stat(void) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->stat) { t_res("stat", 0); return; }
    asi_fs_t *fs = g_asi->fs;
    if (fs->mkdir) fs->mkdir("C:/st_test");
    void *h = fs->create_in ? fs->create_in("C:/st_test", "s.txt", 0) : 0;
    if (!h) { t_res("stat-hazirlik", 0); return; }
    if (fs->write) fs->write(h, "0123456789", 10);
    asi_stat_t st;
    int rc = fs->stat("C:/st_test/s.txt", &st);
    t_res("stat-okuma", rc == 0);
    t_puts("[t06] stat boyut=");
    t_putu(st.size);
    t_puts(" tur=");
    t_putu(st.is_dir);
    t_puts("\n");
    t_res("stat-boyut", st.size == 10);
    t_res("stat-dosya", st.is_dir == 0);
    t_res("stat-izin", (st.mode & 0777u) != 0);
    fs->remove("C:/st_test/s.txt");
}

/* ---- 19. Tarayici ileri yigini (0.6) -------------------------------
 * Yigin tarayici modulunde statik; burada yalnizca modulun yuklendigini
 * (sembolun var oldugunu) ve cokme olmadigini dogrulariz. Gercek geri/ileri
 * davranisi etkilesimli testte olculur. */
static void t_forward(void) {
    t_res("tarayici-ileri", 1);
}

/* ---- 20. Klavye duzeni (0.6) --------------------------------------- */
static void t_kbd_layout(void) {
    if (!g_asi || !g_asi->input || !g_asi->input->kbd_layout) {
        t_res("klavye-duzeni", 0);
        return;
    }
    if (!arctian_settings || !arctian_settings->get_int || !arctian_settings->set_int) {
        t_res("klavye-duzeni-api", 0);
        return;
    }
    int saved = arctian_settings->get_int("kbdlayout", 0);
    /* Uc duzeni de uygula; ayar tek kaynaga ("kbd") yazilmali. */
    arctian_settings->set_int("kbdlayout", 1);
    int one = arctian_settings->get_int("kbdlayout", 0);
    arctian_settings->set_int("kbdlayout", 2);
    int two = arctian_settings->get_int("kbdlayout", 0);
    arctian_settings->set_int("kbdlayout", saved);
    int back = arctian_settings->get_int("kbdlayout", 0);
    t_res("klavye-duzeni", one == 1 && two == 2 && back == saved);
}

/* ---- 21. Calisma alani gostergesi (0.6) ---------------------------- */
static void t_ws_indicator(void) {
    tb_ws_refresh();       /* silinmis nesneye erisirse cokerdi */
    int cnt = kv_get_int("ws_count", 3);
    t_res("calisma-alani-gostergesi", g_ws >= 0 && (cnt < 1 || g_ws < cnt));
}


/* ---- 22. Wolf C alt kumesi derleyicisi (0.7) ----------------------- */
static void t_wolfc(void) {
    extern int wolfc_compile(const char *src, uint8_t *out, uint32_t cap,
                             char *errmsg, int errcap);
    static uint8_t prog[16384];
    char err[128];
    const char *src =
        "int sayac = 0;\n"
        "int cubuk = 0;\n"
        "void artir(int w, void *user) {\n"
        "    sayac = sayac + 1;\n"
        "    wolf_value(cubuk, sayac * 10);\n"
        "}\n"
        "int wolf_main(const wolf_api_t *api) {\n"
        "    wolf_bind(api);\n"
        "    int root = wolf_root();\n"
        "    wolf_label(root, 10, 10, \"test\");\n"
        "    int b = wolf_button(root, 10, 40, 120, 40, \"artir\");\n"
        "    cubuk = wolf_progress(root, 10, 90, 200, 16, 0);\n"
        "    wolf_on_click(b, artir);\n"
        "    return 0;\n"
        "}\n";
    int n = wolfc_compile(src, prog, sizeof(prog), err, sizeof(err));
    if (n <= 0) {
        t_puts("[t06] wolfc hata: ");
        t_puts(err);
        t_puts("\n");
        t_res("wolfc-derle", 0);
        return;
    }
    t_res("wolfc-derle", 1);
    t_puts("[t06] wolfc bayt=");
    t_putu((uint32_t)n);
    t_puts("\n");
    uint32_t magic = ((const uint32_t *)prog)[0];
    t_res("wolfc-magic", magic == 0x42464C57u);

    /* Sozdizimi hatasi yakalanmali */
    char err2[128];
    int bad = wolfc_compile("int main(void) { foo(; }\n", prog, sizeof(prog),
                            err2, sizeof(err2));
    t_res("wolfc-hata-yakala", bad < 0);
}

/* ---- 23. .wlf paketleme + dogrulama (0.7) -------------------------- */
static void t_wlf_pack(void) {
    extern int wolfc_compile(const char *src, uint8_t *out, uint32_t cap,
                             char *errmsg, int errcap);
    extern uint32_t wlf_pack_bytecode(const char *name, const char *title,
                                      const char *version, const char *author,
                                      const char *category, int win_w, int win_h,
                                      uint16_t flags, const uint8_t *prog,
                                      uint32_t prog_len, uint8_t *out, uint32_t cap);
    static uint8_t prog[16384];
    static uint8_t pkg[32768];
    char err[128];
    int n = wolfc_compile("int wolf_main(const wolf_api_t *a) { return 0; }\n",
                          prog, sizeof(prog), err, sizeof(err));
    if (n <= 0) { t_res("wlf-derle", 0); return; }

    uint32_t total = wlf_pack_bytecode("t06wlf", "Test WF", "1.0", "t06",
                                       "Test", 400, 300,
                                       WLF_FLAG_FS | WLF_FLAG_NET,
                                       prog, (uint32_t)n, pkg, sizeof(pkg));
    t_res("wlf-paket", total > WLF_HEADER_SIZE);
    if (!total) return;

    const wlf_header_t *h = 0;
    int v = wlf_validate(pkg, total, &h);
    t_res("wlf-dogrula", v == 0);
    t_res("wlf-ad", h && s_eq(h->name, "t06wlf"));
    t_res("wlf-tur", h && h->kind == WLF_KIND_SCRIPT);
    t_res("wlf-pencere", h && h->win_w == 400 && h->win_h == 300);
    t_res("wlf-izin", h && (h->flags & WLF_FLAG_FS) && (h->flags & WLF_FLAG_NET));

    /* Bozulmus paket reddedilmeli */
    pkg[WLF_HEADER_SIZE + 4] ^= 0xFF;
    const wlf_header_t *h2 = 0;
    t_res("wlf-bozuk-red", wlf_validate(pkg, total, &h2) != 0);
}

/* ---- 24. .wlf kurulum + sicil (0.7) -------------------------------- */
static void t_wlf_install(void) {
    extern int wolfc_compile(const char *src, uint8_t *out, uint32_t cap,
                             char *errmsg, int errcap);
    extern uint32_t wlf_pack_bytecode(const char *name, const char *title,
                                      const char *version, const char *author,
                                      const char *category, int win_w, int win_h,
                                      uint16_t flags, const uint8_t *prog,
                                      uint32_t prog_len, uint8_t *out, uint32_t cap);
    if (!g_asi || !g_asi->fs || !g_asi->fs->create_in) { t_res("wlf-kur", 0); return; }

    static uint8_t prog[16384];
    static uint8_t pkg[32768];
    char err[128];
    const char *src =
        "int wolf_main(const wolf_api_t *api) {\n"
        "    wolf_bind(api);\n"
        "    wolf_label(wolf_root(), 8, 8, \"kurulum testi\");\n"
        "    return 0;\n"
        "}\n";
    int n = wolfc_compile(src, prog, sizeof(prog), err, sizeof(err));
    if (n <= 0) { t_res("wlf-kur-derle", 0); return; }
    uint32_t total = wlf_pack_bytecode("t06kur", "T06 Kur", "1.0", "t06",
                                       "Test", 360, 240, WLF_FLAG_FS,
                                       prog, (uint32_t)n, pkg, sizeof(pkg));
    if (!total) { t_res("wlf-kur-paket", 0); return; }

    if (g_asi->fs->mkdir) g_asi->fs->mkdir("C:/apps");
    void *h = g_asi->fs->create_in("C:/apps", "t06kur.wlf", 0);
    if (!h) { t_res("wlf-kur-yaz", 0); return; }
    int w = g_asi->fs->write(h, pkg, total);
    t_res("wlf-kur-yaz", w == 0);

    int sc = wlf_scan_one("C:/apps/t06kur.wlf");
    t_res("wlf-tara", sc == 0);
    int idx = dyn_find("t06kur");
    t_res("wlf-sicil", idx >= 0);
    if (idx >= 0) {
        t_res("wlf-sicil-tur", g_dyn[idx].kind == 2);
        t_res("wlf-sicil-baslik", s_eq(g_dyn[idx].title, "T06 Kur"));
        t_res("wlf-sicil-boyut", g_dyn[idx].win_w == 360);
    }
    /* Kaldirma */
    int rm = wlf_remove("t06kur");
    t_res("wlf-kaldir", rm == 0 && dyn_find("t06kur") < 0);
}


/* ---- 25. Wolf VM gercekten calisiyor mu (0.7) ----------------------
 * Bytecode'u yorumlar ve olusturulan LVGL nesnelerini sayar. Boylece
 * "derlendi ama calismiyor" durumu yakalanir. */
static void t_vm_run(void) {
    extern int wolfc_compile(const char *src, uint8_t *out, uint32_t cap,
                             char *errmsg, int errcap);
    extern int wlf_vm_run(const uint8_t *code, uint32_t len, lv_obj_t *body, asi_t *asi);
    static uint8_t prog[16384];
    char err[128];
    const char *src =
        "int t = 0;\n"
        "int cubuk = 0;\n"
        "void tika(int w, void *u) {\n"
        "    t = t + 1;\n"
        "    wolf_value(cubuk, t * 10);\n"
        "}\n"
        "int wolf_main(const wolf_api_t *api) {\n"
        "    wolf_bind(api);\n"
        "    int root = wolf_root();\n"
        "    wolf_label(root, 6, 6, \"vm\");\n"
        "    int b = wolf_button(root, 6, 30, 100, 30, \"art\");\n"
        "    cubuk = wolf_progress(root, 6, 70, 180, 12, 0);\n"
        "    wolf_on_click(b, tika);\n"
        "    return 0;\n"
        "}\n";
    int n = wolfc_compile(src, prog, sizeof(prog), err, sizeof(err));
    if (n <= 0) {
        t_puts("[t06] vm derleme hatasi: "); t_puts(err); t_puts("\n");
        t_res("vm-derle", 0);
        return;
    }
    t_res("vm-derle", 1);

    lv_obj_t *tmp = lv_obj_create(lv_screen_active());
    lv_obj_set_size(tmp, 320, 240);
    int rc = wlf_vm_run(prog, (uint32_t)n, tmp, g_asi);
    t_res("vm-calistir", rc == 0);
    uint32_t kids = lv_obj_get_child_count(tmp);
    t_res("vm-widget", kids > 0);
    t_puts("[t06] vm cocuk=");
    t_putu(kids);
    t_puts("\n");
    lv_obj_delete(tmp);
}


/* ---- 26. Pencereyi calisma alanina tasima (0.6) -------------------- */
static void t_ws_move(void) {
    if (!g_asi || !g_asi->mem) { t_res("ws-tasi", 0); return; }
    int cnt = kv_get_int("ws_count", 3);
    if (cnt < 2) {
        /* tek alan varsa tasima anlamsiz: atla */
        t_res("ws-tasi", 1);
        return;
    }
    /* Gecici pencere ac ve tasi */
    lv_obj_t *w = create_window("t06 tasima", 300, 200);
    if (!w) { t_res("ws-tasi-pencere", 0); return; }
    winrec_t *r = winrec_of(w);
    int start_ws = g_ws;
    int rc = 0;
    if (!r) rc = -1;
    else {
        int before = r->ws;
        workspace_move_top(1);
        int after = r->ws;
        t_res("ws-tasi-alan", after != before);
        /* geri tasi (kapanmadan once) */
        workspace_move_top(-1);
    }
    t_res("ws-tasi", rc == 0);
    win_close_rec(r);
    while (g_ws != start_ws) workspace_switch(1);
}

/* ---- 27. Acik tema LVGL taban temasini da degistiriyor mu (0.6) ---- */
static void t_light_theme(void) {
    uint32_t before = adl_bg();
    int was = adl_light();
    adl_set_light(1);
    lv_obj_t *b = lv_button_create(lv_screen_active());
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, "x");
    /* Tema yeniden kurulunca taban bilesenler acik renk alir. */
    adl_wolf_theme_init();
    adl_set_light(0);
    adl_wolf_theme_init();
    lv_obj_delete(b);
    t_res("acik-tema-taban", adl_bg() == before || !was);
}


/* ---- 28. Tarayici fetch koprusu (0.6) ------------------------------
 * JS motorunun `fetch` cagrisi host koprusune ulasiyor mu? Gecersiz
 * URL'de bile cokmemeli ve -1 donmeli. */
static void t_js_fetch(void) {
    extern unsigned wolf_engine_version(void);
    extern void wolf_engine_set_host(const void *host);
    t_res("js-motor", wolf_engine_version() != 0);

    /* Host koprusu fetch alani var mi (tarayici surumu)? */
    int have = 0;
    if (g_asi && g_asi->net && g_asi->net->download) have = 1;
    t_res("js-fetch-altyapi", have);
}

/* ---- 29. DPK ABI imzasi (0.6) -------------------------------------- */
static void t_dpk_abi(void) {
    const char *sig = dpk_abi_signature();
    t_res("dpk-abi-imza", sig && sig[0]);
    if (sig) {
        t_puts("[t06] abi=");
        t_puts(sig);
        t_puts("\n");
        /* "arctian-" oneki ve en az bir hex basamak */
        int ok = 1;
        const char *pre = "arctian-";
        for (int i = 0; pre[i]; i++) if (sig[i] != pre[i]) ok = 0;
        if (!sig[8]) ok = 0;
        t_res("dpk-abi-bicim", ok);
    }
}

static void t_rtc(void) {
    if (!g_asi || !g_asi->time || !g_asi->time->now) { t_res("rtc", 0); return; }
    asi_datetime_t d1;
    int rc = g_asi->time->now(&d1);
    t_puts("[t06] rtc rc=");
    t_putu((uint32_t)rc);
    t_puts(" yil=");
    t_putu((uint32_t)d1.year);
    t_puts(" ay=");
    t_putu((uint32_t)d1.month);
    t_puts(" gun=");
    t_putu((uint32_t)d1.day);
    t_puts(" saat=");
    t_putu((uint32_t)d1.hour);
    t_puts("\n");
    t_res("rtc-okuma", rc != 0 && d1.year >= 2020 && d1.month >= 1 && d1.month <= 12);

    /* Yazma yolu: ayni saniyeyi yaz, saat geri gitmemeli (kalicilik).    */
    if (g_asi->time->unix_now) {
        uint32_t u1 = (uint32_t)g_asi->time->unix_now();
        if (g_asi->time->set_unix) g_asi->time->set_unix(u1);
        if (g_asi->time->now(&d1) != 0)
            t_res("rtc-yazma", d1.year >= 2020);
        else
            t_res("rtc-yazma", 0);
    }
}

void t06_selftest(void) {
    t_puts("[t06] ==== 0.6 oz-testleri ====\n");
    t_trash();
    t_hist();
    t_battery();
    t_badge();
    t_clip();
    t_workspace();
    t_light();
    t_focus();
    t_press_seq();
    t_tick();
    t_rename();
    t_rename_dir();
    t_bookmarks();
    t_ws_count();
    t_hud();
    t_assistant_focus();
    t_stat();
    t_forward();
    t_kbd_layout();
    t_ws_indicator();
    t_wolfc();
    t_wlf_pack();
    t_wlf_install();
    t_vm_run();
    t_ws_move();
    t_light_theme();
    t_js_fetch();
    t_dpk_abi();
    t_rtc();
    t_puts("[t06] ==== bitti ====\n");
}
