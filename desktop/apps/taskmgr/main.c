#include "app.h"
#include "adl_wolf.h"
#include <stdint.h>

#define TM_MAX 64
#define TM_SLOTS 16

static asi_t *A;
static lv_obj_t *g_rows;
static lv_obj_t *g_cpu, *g_ram, *g_heap, *g_nproc;
static lv_obj_t *g_status;
static lv_timer_t *g_timer;

static int      g_prev_pid[TM_MAX];
static uint32_t g_prev_tick[TM_MAX];
static uint32_t g_prev_total;
static int      g_prev_n;

static void scpy(char *d, int cap, const char *s) {
    int i = 0;
    if (cap <= 0) return;
    if (!s) { d[0] = 0; return; }
    for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}

static void u2s(uint64_t v, char *out, int cap) {
    char t[24];
    int n = 0, k = 0;
    if (v == 0) t[n++] = '0';
    while (v > 0 && n < (int)sizeof(t)) { t[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    while (n && k < cap - 1) out[k++] = t[--n];
    out[k] = 0;
}

static void bytes_h(uint64_t b, char *out, int cap) {
    char n[24];
    const char *unit;
    if (b >= 1024u * 1024u) { u2s(b / (1024u * 1024u), n, sizeof(n)); unit = " MB"; }
    else { u2s(b / 1024u, n, sizeof(n)); unit = " KB"; }
    int i = 0;
    for (int k = 0; n[k] && i < cap - 4; k++) out[i++] = n[k];
    out[i++] = unit[0]; out[i++] = unit[1]; out[i++] = unit[2]; out[i] = 0;
}

static const char *state_txt(int s) {
    switch (s) {
        case 1: return "Hazir";
        case 2: return "Calisiyor";
        case 3: return "Bekliyor";
        case 4: return "Zombi";
        default: return "-";
    }
}

static uint32_t state_col(int s) {
    switch (s) {
        case 1: return 0x6FCF97;
        case 2: return 0x56CCF2;
        case 3: return 0xF2C94C;
        case 4: return 0xEB5757;
        default: return ADL_TEXT_DIM;
    }
}

static int cpu_pct(int pid, uint32_t tick, uint32_t total) {
    if (total <= g_prev_total) return 0;
    uint32_t dt = total - g_prev_total;
    for (int i = 0; i < g_prev_n; i++) {
        if (g_prev_pid[i] != pid) continue;
        uint32_t dp = tick - g_prev_tick[i];
        return (int)((uint64_t)dp * 100u / dt);
    }
    return 0;
}

static void remember(int n, const asi_proc_info_t *inf, uint32_t total) {
    if (n > TM_MAX) n = TM_MAX;
    for (int i = 0; i < n; i++) {
        g_prev_pid[i] = inf[i].pid;
        g_prev_tick[i] = inf[i].cpu_ticks;
    }
    g_prev_n = n;
    g_prev_total = total;
}

static void kill_cb(lv_event_t *e) {
    int pid = (int)(intptr_t)lv_event_get_user_data(e);
    int rc = (A && A->proc && A->proc->kill) ? A->proc->kill(pid) : -1;
    if (g_status) {
        char b[64];
        u2s((uint64_t)pid, b, sizeof(b));
        lv_label_set_text(g_status, rc == 0 ? "Surec sonlandirildi: PID " : "Sonlandirilamadi: PID ");
        lv_label_ins_text(g_status, -1, b);
    }
}

static lv_obj_t *cell(lv_obj_t *row, int x, int w, const char *txt, uint32_t col) {
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_pos(l, x, 8);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

static void refresh(void) {
    if (!A || !g_rows) return;

    asi_meminfo_t mi;
    if (A->mem && A->mem->info) A->mem->info(&mi); else mi.total = mi.used = mi.heap_total = mi.heap_used = 0;

    uint32_t total = A->proc && A->proc->sys_ticks ? A->proc->sys_ticks() : 0;
    int n = A->proc ? A->proc->count() : 0;

    asi_proc_info_t inf[TM_MAX];
    int pct[TM_MAX];
    int got = 0, sum_cpu = 0;
    if (A->proc && A->proc->info_ex) {
        for (int i = 0; i < TM_SLOTS && got < TM_MAX; i++) {
            if (A->proc->info_ex(i, &inf[got]) == 0) got++;
        }
    }
    for (int i = 0; i < got; i++) {
        pct[i] = cpu_pct(inf[i].pid, inf[i].cpu_ticks, total);
        sum_cpu += pct[i];
    }
    if (sum_cpu > 100) sum_cpu = 100;

    char b[32];
    u2s((uint64_t)sum_cpu, b, sizeof(b));
    lv_label_set_text(g_cpu, b);
    lv_label_ins_text(g_cpu, -1, "%");

    bytes_h(mi.used, b, sizeof(b));
    lv_label_set_text(g_ram, b);
    if (mi.total) {
        lv_label_ins_text(g_ram, -1, " / ");
        bytes_h(mi.total, b, sizeof(b));
        lv_label_ins_text(g_ram, -1, b);
    }

    bytes_h(mi.heap_used, b, sizeof(b));
    lv_label_set_text(g_heap, b);

    u2s((uint64_t)n, b, sizeof(b));
    lv_label_set_text(g_nproc, b);

    lv_obj_clean(g_rows);

    int y = 0;
    for (int i = 0; i < got; i++) {
        lv_obj_t *row = lv_obj_create(g_rows);
        lv_obj_remove_style_all(row);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(row, LV_PCT(100), 34);
        lv_obj_set_pos(row, 0, y);
        lv_obj_set_style_bg_color(row, lv_color_hex(i & 1 ? ADL_SURFACE_2 : ADL_SURFACE), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        y += 35;

        char t[32];
        u2s((uint64_t)inf[i].pid, t, sizeof(t));
        cell(row, 10, 50, t, ADL_TEXT);
        u2s((uint64_t)inf[i].ppid, t, sizeof(t));
        cell(row, 66, 50, t, ADL_TEXT_DIM);
        cell(row, 124, 220, inf[i].name, ADL_TEXT);
        cell(row, 350, 90, state_txt(inf[i].state), state_col(inf[i].state));
        u2s((uint64_t)pct[i], t, sizeof(t));
        cell(row, 448, 46, t, ADL_TEXT);
        cell(row, 500, 60, inf[i].uid == 0 ? "root" : "kullanici", ADL_TEXT_DIM);

        if (inf[i].pid > 1) {
            lv_obj_t *k = lv_button_create(row);
            lv_obj_set_size(k, 84, 26);
            lv_obj_set_pos(k, 580, 4);
            lv_obj_set_style_radius(k, 8, 0);
            lv_obj_set_style_bg_color(k, lv_color_hex(ADL_DANGER), 0);
            lv_obj_t *kl = lv_label_create(k);
            lv_label_set_text(kl, "Sonlandir");
            lv_obj_center(kl);
            lv_obj_add_event_cb(k, kill_cb, LV_EVENT_CLICKED, (void *)(intptr_t)inf[i].pid);
        }
    }

    remember(got, inf, total);

    if (g_status) {
        char s[80], nb[16];
        u2s((uint64_t)n, nb, sizeof(nb));
        scpy(s, sizeof(s), "Her saniye yenilenir  -  ");
        lv_label_set_text(g_status, s);
        lv_label_ins_text(g_status, -1, nb);
        lv_label_ins_text(g_status, -1, " surec");
    }
}

static void timer_cb(lv_timer_t *t) { (void)t; refresh(); }

static void refresh_cb(lv_event_t *e) { (void)e; refresh(); }

static void app_del_cb(lv_event_t *e) {
    (void)e;
    if (g_timer) { lv_timer_delete(g_timer); g_timer = 0; }
    g_rows = 0; g_status = 0;
    g_cpu = g_ram = g_heap = g_nproc = 0;
    g_prev_n = 0;
    g_prev_total = 0;
}

static lv_obj_t *card(lv_obj_t *parent, int x, const char *title, lv_obj_t **val) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(c, 190, 68);
    lv_obj_set_pos(c, x, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(ADL_BORDER), 0);

    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_set_pos(t, 12, 8);

    lv_obj_t *v = lv_label_create(c);
    lv_label_set_text(v, "-");
    lv_obj_set_style_text_color(v, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_pos(v, 12, 30);
    *val = v;
    return c;
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    A = asi;
    int W = lv_obj_get_width(win);
    int H = lv_obj_get_height(win);
    if (W < 400) W = 820;
    if (H < 300) H = 540;

    lv_obj_set_style_bg_color(win, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, 0);

    lv_obj_t *bar = lv_obj_create(win);
    lv_obj_remove_style_all(bar);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, W, 42);
    lv_obj_set_pos(bar, 0, 0);

    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, "Gorev Yoneticisi");
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 6, 0);

    lv_obj_t *b = lv_button_create(bar);
    lv_obj_set_size(b, 90, 30);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_t *bl = lv_label_create(b);
    lv_label_set_text(bl, "Yenile");
    lv_obj_center(bl);
    lv_obj_add_event_cb(b, refresh_cb, LV_EVENT_CLICKED, 0);

    lv_obj_t *cards = lv_obj_create(win);
    lv_obj_remove_style_all(cards);
    lv_obj_remove_flag(cards, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(cards, 8, 50);
    lv_obj_set_size(cards, W - 16, 68);
    card(cards, 0, "Islemci", &g_cpu);
    card(cards, 198, "Bellek", &g_ram);
    card(cards, 396, "Cekirdek Yigini", &g_heap);
    card(cards, 594, "Surec", &g_nproc);

    lv_obj_t *hdr = lv_obj_create(win);
    lv_obj_remove_style_all(hdr);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(hdr, 8, 130);
    lv_obj_set_size(hdr, W - 16, 24);
    lv_obj_t *h1 = lv_label_create(hdr); lv_label_set_text(h1, "PID");  lv_obj_set_pos(h1, 10, 4);  lv_obj_set_style_text_color(h1, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_t *h2 = lv_label_create(hdr); lv_label_set_text(h2, "PPID"); lv_obj_set_pos(h2, 66, 4);  lv_obj_set_style_text_color(h2, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_t *h3 = lv_label_create(hdr); lv_label_set_text(h3, "Ad");   lv_obj_set_pos(h3, 124, 4); lv_obj_set_style_text_color(h3, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_t *h4 = lv_label_create(hdr); lv_label_set_text(h4, "Durum");lv_obj_set_pos(h4, 350, 4); lv_obj_set_style_text_color(h4, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_t *h5 = lv_label_create(hdr); lv_label_set_text(h5, "CPU%"); lv_obj_set_pos(h5, 448, 4); lv_obj_set_style_text_color(h5, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_t *h6 = lv_label_create(hdr); lv_label_set_text(h6, "Kullanici"); lv_obj_set_pos(h6, 500, 4); lv_obj_set_style_text_color(h6, lv_color_hex(ADL_TEXT_DIM), 0);

    g_rows = lv_obj_create(win);
    lv_obj_remove_style_all(g_rows);
    lv_obj_set_pos(g_rows, 8, 158);
    lv_obj_set_size(g_rows, W - 16, H - 158 - 32);
    lv_obj_set_style_bg_color(g_rows, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(g_rows, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_rows, 1, 0);
    lv_obj_set_style_border_color(g_rows, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_radius(g_rows, 10, 0);

    g_status = lv_label_create(win);
    lv_label_set_text(g_status, "");
    lv_obj_set_style_text_color(g_status, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_obj_align(g_status, LV_ALIGN_BOTTOM_LEFT, 8, -4);

    lv_obj_add_event_cb(win, app_del_cb, LV_EVENT_DELETE, 0);

    g_prev_n = 0;
    g_prev_total = 0;
    refresh();
    g_timer = lv_timer_create(timer_cb, 1000, 0);
}
