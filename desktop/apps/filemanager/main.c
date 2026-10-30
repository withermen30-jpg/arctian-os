#include "app.h"
#include "adl_wolf.h"
#include "lv_port.h"
#include <stdint.h>

#define FM_MAX_ENT 200
#define FM_PATH    200
#define FM_NAME    68
#define FM_FLAT    512
#define FM_NAV     7
#define FM_HISTORY 16
#define FM_CRUMBS  16
#define FM_MENU_W  228

/* Geri oku: 16px SVG, 1.5px cizgi (metin karakteri degil).
 * make_icon() masaustu kabugunda (dsk_util.c) yasar; uygulamalar ayni
 * ikiliye baglandigi icin burada yalnizca bildirim gerekir. */
lv_obj_t *make_icon(lv_obj_t *parent, const char *svg, int size);
static const char FM_SVG_BACK[] =
    "<svg viewBox=\"0 0 16 16\"><path d=\"M10 3 L5.5 8 L10 13\" stroke=\"#E4E6EB\" "
    "stroke-width=\"1.5\" fill=\"none\"/></svg>";

enum {
    FM_DESK = 0x0E1014, FM_WINDOW = 0x15171C, FM_RAISED = 0x1A1D24,
    FM_SELECTED = 0x26313F, FM_MENU_BG = 0x1B1E25, FM_MENU_FIRST = 0x2A3140,
    FM_BORDER = 0x262A33, FM_DIVIDER = 0x22252D, FM_MENU_BORDER = 0x2C313B,
    FM_TEXT = 0xE4E6EB, FM_SECONDARY = 0x8A909B, FM_TERTIARY = 0x6B717C,
    FM_ACCENT = 0x3B82F6, FM_DANGER = 0xE88080, FM_HOVER = 0x191C22
};

extern const unsigned char font_plex[];
extern const unsigned int font_plex_len;
extern const unsigned char font_plex_sb[];
extern const unsigned int font_plex_sb_len;

typedef struct {
    char     name[FM_NAME];
    int      is_dir;
    uint32_t size;
    uint32_t child_count;
} fm_ent_t;

static asi_t     *A;
static char       cwd[FM_PATH];
static fm_ent_t   ents[FM_MAX_ENT];
static lv_obj_t  *rows[FM_MAX_ENT];
static int        ent_n;
static int        sel;
static int        list_w;
static int        s_sort_count;
static int        s_sort_desc;
static int        s_usb_present;
static int        s_prev_right;
static int        s_prev_left;
static int        s_selected_n;
static unsigned   s_usb_poll_ticks;
static lv_obj_t  *fm_body;
static lv_timer_t *relayout_timer;
static int        s_menu_focused;
static lv_color_t sel_bg;

static lv_obj_t *path_lbl;
static lv_obj_t *crumb_box;
static lv_obj_t *list_box;
static lv_obj_t *table_head;
static lv_obj_t *ui_tb;      /* toolbar */
static lv_obj_t *ui_side;    /* quick access */
static lv_obj_t *ui_det;     /* details panel */
static lv_obj_t *ui_st;      /* status bar */
static lv_obj_t *status_lbl;
static lv_obj_t *det_name;
static lv_obj_t *det_meta;
static lv_obj_t *det_prev;
static lv_obj_t *det_hint;
static lv_obj_t *head_name;
static lv_obj_t *head_count;
static lv_obj_t *back_btn;
static lv_obj_t *context_menu;
static lv_obj_t *context_backdrop;
static lv_obj_t *menu_items[FM_NAV + 4];
static int        menu_item_n;
static lv_group_t *menu_prev_group;
static lv_group_t *menu_group;
static lv_timer_t *input_timer;
static lv_font_t *fm_font;
static lv_font_t *fm_font_small;
static lv_font_t *fm_font_bold;
static char       s_history[FM_HISTORY][FM_PATH];
static int        s_history_n;
static char       s_crumb_path[FM_CRUMBS][FM_PATH];
static int        s_crumb_n;

static lv_obj_t *nav_btns[FM_NAV];

static lv_group_t *g_group;
static void      (*g_prompt_ok)(const char *);
static lv_obj_t  *g_prompt_ta;
static lv_obj_t  *g_prompt_ov;

/* ---- Coklu secim, pano, geri al, arama, surukle-birak --------- */
static char  s_multi[FM_MAX_ENT];   /* 1 = satir coklu secimde */
static int   s_last_sel = -1;       /* Shift aralik secimi icin son tiklanan */
static lv_obj_t *s_search;          /* klasor ici arama kutusu */
static char  s_filter[48];          /* arama filtresi (bos = kapali) */
static int   s_drag_i = -1;         /* basili tutulan satir (-1 = yok) */
static int   s_drag_on;             /* surukleme esigi asildi mi */
static int   s_drag_x, s_drag_y;    /* basinç noktasi */
static int   s_cut;                 /* pano kes modu (yapistirinca tasir) */
static lv_obj_t *s_empty_lbl;       /* Bos klasor yonlendirmesi metni */
static lv_obj_t *s_empty_btn;       /* "Olustur" dugmesi */

/* Geri al yigini: 4 seviye x 12 dosya (kopya/taşı islemleri) */
#define FM_UNDO_LV   4
#define FM_UNDO_FILES 12
typedef struct {
    int  type;    /* 1 = kopya, 2 = tasi */
    int  n;
    char src[FM_UNDO_FILES][FM_PATH];
    char dst[FM_UNDO_FILES][FM_PATH];
} fm_op_t;
static fm_op_t s_ops[FM_UNDO_LV];
static int     s_op_n;

static void apply_sel(void);
static void push_op(const fm_op_t *op);
static void clip_sel(int cut);
static void do_paste(void);
static void do_undo(void);
static void do_move_to(int i, const char *dst_dir);
static void fm_hotkey(int hk);
static int  fm_drop_cb(const char *src);
static void search_changed_cb(lv_event_t *e);
static void search_click_cb(lv_event_t *e);
static void path_click_cb(lv_event_t *e);
static void path_ok(const char *p);
static void row_press_cb(lv_event_t *e);
static void row_pressing_cb(lv_event_t *e);
static void row_release_cb(lv_event_t *e);
static void fm_menu_close(void);
static void fm_menu_show(int x, int y, int empty_space);
static void fm_navigate(const char *path, int remember);
static void fm_sync_usb(void);
static void fm_input_poll_cb(lv_timer_t *t);
static void update_breadcrumb(void);
static void update_detail(void);
static void fm_update_toolbar(void);
static void fm_update_status(void);
static void fm_update_sort_labels(void);
static int ent_vis(int i);


static const char *const NAV_PATH[FM_NAV] = {
    "C:", "C:/apps", "C:/indirilenler", "C:/belgeler", "C:/masaustu", "C:/logs", "D:"
};
static const char *const NAV_LABEL[FM_NAV] = {
    "Bu Bilgisayar", "Uygulamalar", "İndirilenler", "Belgeler", "Masaüstü", "Günlükler", "USB Bellek"
};

static void update_detail(void);
static void refresh(void);
static void row_click_cb(lv_event_t *e);
static void row_dbl_cb(lv_event_t *e);
static void nav_cb(lv_event_t *e);
static void up_cb(lv_event_t *e);
static void open_cb(lv_event_t *e);
static void refresh_cb(lv_event_t *e);
static void del_cb(lv_event_t *e);
static void newdir_ok(const char *name);
static void newfile_ok(const char *name);
static void rename_ok(const char *name);
static void rename_cb(lv_event_t *e);
static void props_cb(lv_event_t *e);
static void prompt_open(const char *title, const char *def, void (*ok)(const char *));
static void prompt_close(void);
static void prompt_newdir_cb(lv_event_t *e);
static void prompt_newfile_cb(lv_event_t *e);

static int slen(const char *s) { int n = 0; if (s) while (s[n]) n++; return n; }

static void scpy(char *d, int cap, const char *s) {
    int i = 0;
    if (cap <= 0) return;
    if (!s) { d[0] = 0; return; }
    for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}

static void scat(char *d, int cap, const char *s) {
    int i = slen(d), j = 0;
    for (; s && s[j] && i < cap - 1; i++, j++) d[i] = s[j];
    d[i] = 0;
}

static int seq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static int starts_with(const char *s, const char *p) {
    if (!s || !p) return 0;
    for (int i = 0; p[i]; i++) if (s[i] != p[i]) return 0;
    return 1;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int scmpi(const char *a, const char *b) {
    while (*a && *b) {
        char x = lower(*a), y = lower(*b);
        if (x != y) return (int)(unsigned char)x - (int)(unsigned char)y;
        a++; b++;
    }
    return (int)(unsigned char)lower(*a) - (int)(unsigned char)lower(*b);
}

static int ci_eq(const char *a, const char *b) {
    while (*a && *b) { if (lower(*a) != lower(*b)) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static const char *ext_of(const char *n) {
    int dot = -1;
    for (int i = 0; n[i]; i++) if (n[i] == '.') dot = i;
    if (dot < 0 || !n[dot + 1]) return "";
    return n + dot + 1;
}

static int is_text(const char *name) {
    const char *e = ext_of(name);
    return ci_eq(e, "txt") || ci_eq(e, "md") || ci_eq(e, "log") || ci_eq(e, "cfg") ||
           ci_eq(e, "json") || ci_eq(e, "c") || ci_eq(e, "h") || ci_eq(e, "py") ||
           ci_eq(e, "sh") || ci_eq(e, "ini") || ci_eq(e, "csv");
}

static uint32_t type_color(const char *name) {
    const char *e = ext_of(name);
    if (ci_eq(e, "png") || ci_eq(e, "jpg") || ci_eq(e, "jpeg") || ci_eq(e, "gif") || ci_eq(e, "bmp"))
        return 0x7DD3FC;
    if (ci_eq(e, "wlf") || ci_eq(e, "dpk") || ci_eq(e, "zip") || ci_eq(e, "iso"))
        return 0xE8B880;
    if (ci_eq(e, "py") || ci_eq(e, "sh") || ci_eq(e, "c") || ci_eq(e, "h"))
        return 0x8FD9A8;
    return FM_SECONDARY;
}

static lv_font_t *fm_text_font(void) {
    return fm_font ? fm_font : (lv_font_t *)lv_font_get_default();
}

static lv_font_t *fm_heading_font(void) {
    return fm_font_bold ? fm_font_bold : fm_text_font();
}

static void fm_style_font(lv_obj_t *obj, lv_font_t *font) {
    if (obj && font) lv_obj_set_style_text_font(obj, font, 0);
}

static int fm_has_selection(void) {
    return s_selected_n > 0;
}

static int fm_selected(int i) {
    return i == sel || s_multi[i];
}

static void fm_add_history(const char *path) {
    if (!path || seq(path, cwd)) return;
    if (s_history_n == FM_HISTORY) {
        for (int i = 1; i < FM_HISTORY; i++) scpy(s_history[i - 1], FM_PATH, s_history[i]);
        s_history_n--;
    }
    scpy(s_history[s_history_n++], FM_PATH, path);
    fm_update_toolbar();
}

static int fm_path_is_usb(const char *path) {
    return path && (path[0] == 'D' || path[0] == 'd') && path[1] == ':';
}

static void fm_sync_usb(void) {
    int present = 0;
    if (A && A->fs && A->fs->stat) {
        asi_stat_t st;
        present = A->fs->stat("D:/", &st) == 0 && st.is_dir;
    }
    s_usb_present = present;
    if (!present && fm_path_is_usb(cwd)) {
        scpy(cwd, FM_PATH, "C:");
        sel = -1;
        s_selected_n = 0;
        s_history_n = 0;
        fm_update_toolbar();
        refresh();
        return;
    }
    if (nav_btns[FM_NAV - 1]) {
        if (present) lv_obj_clear_flag(nav_btns[FM_NAV - 1], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(nav_btns[FM_NAV - 1], LV_OBJ_FLAG_HIDDEN);
    }
}

static int fm_count_children(const char *dir) {
    int count = (A && A->fs && A->fs->count_dir) ? A->fs->count_dir(dir) : 0;
    return count > 0 ? count : 0;
}

static int fm_char_rank(const char *s, int *bytes) {
    const unsigned char *p = (const unsigned char *)s;
    *bytes = 1;
    if (p[0] < 0x80) {
        unsigned char c = p[0];
        if (c == 'I') return 11; /* Türkçe büyük I, noktasız ı */
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
        if (c >= 'a' && c <= 'z') {
            switch (c) {
                case 'a': return 1; case 'b': return 2; case 'c': return 3;
                case 'd': return 5; case 'e': return 6; case 'f': return 7; case 'g': return 8;
                case 'h': return 10; case 'i': return 12; case 'j': return 13; case 'k': return 14;
                case 'l': return 15; case 'm': return 16; case 'n': return 17; case 'o': return 18;
                case 'p': return 20; case 'q': return 30; case 'r': return 21; case 's': return 22;
                case 't': return 24; case 'u': return 25; case 'v': return 27; case 'w': return 31;
                case 'x': return 32; case 'y': return 28; case 'z': return 29;
            }
        }
        return 0x100 + c;
    }
    *bytes = 2;
    if (p[0] == 0xC3 && (p[1] == 0xA7 || p[1] == 0x87)) return 4;  /* ç / Ç */
    if (p[0] == 0xC4 && (p[1] == 0x9F || p[1] == 0x9E)) return 9;  /* ğ / Ğ */
    if (p[0] == 0xC4 && p[1] == 0xB1) return 11; /* ı */
    if (p[0] == 0xC4 && p[1] == 0xB0) return 12; /* İ */
    if (p[0] == 0xC3 && (p[1] == 0xB6 || p[1] == 0x96)) return 19; /* ö / Ö */
    if (p[0] == 0xC5 && (p[1] == 0x9F || p[1] == 0x9E)) return 23; /* ş / Ş */
    if (p[0] == 0xC3 && (p[1] == 0xBC || p[1] == 0x9C)) return 26; /* ü / Ü */
    return 0x1000 + ((int)p[0] << 8) + p[1];
}

static int fm_tr_compare(const char *a, const char *b) {
    while (*a && *b) {
        int na, nb;
        int ca = fm_char_rank(a, &na), cb = fm_char_rank(b, &nb);
        if (ca != cb) return ca < cb ? -1 : 1;
        a += na;
        b += nb;
    }
    return *a ? 1 : (*b ? -1 : 0);
}

static int fm_contains_ci(const char *name, const char *filter) {
    for (const char *p = name; *p; ) {
        const char *a = p, *b = filter;
        while (*a && *b) {
            int na, nb;
            if (fm_char_rank(a, &na) != fm_char_rank(b, &nb)) break;
            a += na;
            b += nb;
        }
        if (!*b) return 1;
        int bytes;
        (void)fm_char_rank(p, &bytes);
        p += bytes;
    }
    return 0;
}

static void u2s(char *out, int cap, uint32_t v) {
    char t[12];
    int n = 0, k = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n && k < cap - 1) out[k++] = t[--n];
    out[k] = 0;
}

static void human(char *out, int cap, uint32_t v) {
    out[0] = 0;
    if (v < 1024) { u2s(out, cap, v); scat(out, cap, " B"); return; }
    uint32_t whole = v / 1024, frac = (v % 1024) * 10 / 1024;
    u2s(out, cap, whole);
    char f[3];
    f[0] = '.'; f[1] = (char)('0' + frac); f[2] = 0;
    scat(out, cap, f);
    scat(out, cap, " KB");
}

static void path_child(const char *dir, const char *name, int is_dir, char *out, int cap) {
    out[0] = 0;
    scat(out, cap, dir);
    scat(out, cap, "/");
    scat(out, cap, name);
    if (is_dir) scat(out, cap, "/");
}

static int path_up(char *p) {
    int n = slen(p);
    if (n <= 2) return 0;
    int i = n - 1;
    while (i > 0 && p[i] != '/') i--;
    if (i <= 2) { p[2] = 0; return 1; }
    p[i] = 0;
    return 1;
}

static void trim_name(char *out, int cap, const char *in) {
    int i = 0, o = 0;
    if (!in) { out[0] = 0; return; }
    while (in[i] == ' ') i++;
    for (; in[i] && in[i] != '\n' && o < cap - 1; i++) {
        char c = in[i];
        if (c == '/') c = '_';
        out[o++] = c;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
}

static int flat_exists(const char *full) {
    if (!A || !A->fs || !A->fs->name) return 0;
    for (int i = 0; i < FM_FLAT; i++) {
        const char *fn = A->fs->name(i);
        if (fn && seq(fn, full)) return 1;
    }
    return 0;
}

/* 16x16 klasor: dolu #3b82f6 govde + sekme (spesifikasyon). */
static void folder_icon(lv_obj_t *row) {
    lv_obj_t *ic = lv_obj_create(row);
    lv_obj_remove_style_all(ic);
    lv_obj_set_size(ic, 16, 16);
    lv_obj_remove_flag(ic, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *tab = lv_obj_create(ic);
    lv_obj_remove_style_all(tab);
    lv_obj_set_size(tab, 7, 4);
    lv_obj_set_style_bg_color(tab, lv_color_hex(FM_ACCENT), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tab, 1, 0);
    lv_obj_set_pos(tab, 0, 1);

    lv_obj_t *f = lv_obj_create(ic);
    lv_obj_remove_style_all(f);
    lv_obj_set_size(f, 16, 11);
    lv_obj_set_style_bg_color(f, lv_color_hex(FM_ACCENT), 0);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(f, 2, 0);
    lv_obj_set_pos(f, 0, 4);
}

/* 16x16 dosya: ince cizgili yaprak + iki metin satiri; uzantiya gore renk. */
static void file_icon(lv_obj_t *row, uint32_t col) {
    lv_obj_t *ic = lv_obj_create(row);
    lv_obj_remove_style_all(ic);
    lv_obj_set_size(ic, 16, 16);
    lv_obj_remove_flag(ic, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *p = lv_obj_create(ic);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, 12, 14);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(p, 2, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(col), 0);
    lv_obj_set_pos(p, 2, 1);

    for (int k = 0; k < 2; k++) {
        lv_obj_t *ln = lv_obj_create(ic);
        lv_obj_remove_style_all(ln);
        lv_obj_set_size(ln, k ? 5 : 7, 1);
        lv_obj_set_style_bg_color(ln, lv_color_hex(col), 0);
        lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
        lv_obj_set_pos(ln, 4, 6 + k * 3);
    }
}

static int ent_cmp(const fm_ent_t *a, const fm_ent_t *b) {
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    int cmp = (s_sort_count && a->is_dir)
              ? (a->child_count < b->child_count ? -1 : a->child_count > b->child_count ? 1 : 0)
              : fm_tr_compare(a->name, b->name);
    return s_sort_desc ? -cmp : cmp;
}

static void fm_update_status(void) {
    if (!status_lbl) return;
    int shown = 0;
    for (int i = 0; i < ent_n; i++) if (ent_vis(i)) shown++;
    char text[96];
    char num[12];
    /* Not: u2s() yazdigi tamponu bastan yazar; kuyruga eklemek icin ayri
     * tampon kullanilir (sayilar ardisik eklenir, basa yazilmaz). */
    text[0] = 0;
    if (s_filter[0]) {
        u2s(num, sizeof(num), (uint32_t)shown);
        scat(text, sizeof(text), num);
        scat(text, sizeof(text), " / ");
        u2s(num, sizeof(num), (uint32_t)ent_n);
        scat(text, sizeof(text), num);
        scat(text, sizeof(text), " öğe");
    } else {
        u2s(num, sizeof(num), (uint32_t)ent_n);
        scat(text, sizeof(text), num);
        scat(text, sizeof(text), " öğe");
    }
    if (s_selected_n) {
        scat(text, sizeof(text), " · ");
        u2s(num, sizeof(num), (uint32_t)s_selected_n);
        scat(text, sizeof(text), num);
        scat(text, sizeof(text), " seçili");
    }
    lv_label_set_text(status_lbl, text);
}


static void apply_sel(void) {
    s_selected_n = 0;
    for (int i = 0; i < ent_n; i++) {
        if (fm_selected(i)) s_selected_n++;
        if (rows[i]) {
            uint32_t sel_bg = fm_selected(i) ? FM_SELECTED : FM_WINDOW;
            lv_obj_set_style_bg_color(rows[i], lv_color_hex(sel_bg), 0);
            /* Secili satir hover'da rengini korusun (hover rengi seciliden
             * cok daha koyu; aksi halde secim kaybolmus gibi gorunuyordu). */
            lv_obj_set_style_bg_color(rows[i], lv_color_hex(sel_bg), LV_STATE_HOVERED);
        }
    }
    fm_update_status();
}

static lv_obj_t *make_row(int i) {
    lv_obj_t *b = lv_button_create(list_box);
    lv_obj_set_size(b, LV_PCT(100), 40);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FM_WINDOW), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FM_HOVER), LV_STATE_HOVERED);
    lv_obj_set_style_bg_color(b, lv_color_hex(FM_SELECTED), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_left(b, 10, 0);
    lv_obj_set_style_pad_right(b, 10, 0);
    lv_obj_set_style_pad_top(b, 0, 0);
    lv_obj_set_style_pad_bottom(b, 0, 0);
    adl_focus_ring_attach(b);

    if (ents[i].is_dir) folder_icon(b);
    else file_icon(b, type_color(ents[i].name));

    lv_obj_t *nm = lv_label_create(b);
    lv_label_set_text(nm, ents[i].name);
    lv_obj_set_style_text_color(nm, lv_color_hex(FM_TEXT), 0);
    fm_style_font(nm, fm_text_font());
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_set_width(nm, list_w - 122);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 26, 0);

    lv_obj_t *count = lv_label_create(b);
    char cnt[16];
    if (ents[i].is_dir) u2s(cnt, sizeof(cnt), ents[i].child_count);
    else human(cnt, sizeof(cnt), ents[i].size);   /* dosyada sayi yerine boyut */
    lv_label_set_text(count, cnt);
    lv_obj_set_style_text_color(count, lv_color_hex(FM_SECONDARY), 0);
    fm_style_font(count, fm_text_font());
    lv_obj_set_width(count, 80);
    lv_obj_set_style_text_align(count, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(count, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(b, row_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(b, row_dbl_cb, LV_EVENT_DOUBLE_CLICKED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(b, row_press_cb, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(b, row_pressing_cb, LV_EVENT_PRESSING, (void *)(intptr_t)i);
    lv_obj_add_event_cb(b, row_release_cb, LV_EVENT_RELEASED, (void *)(intptr_t)i);
    return b;
}

static void row_click_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= ent_n) return;
    int ctrl = arctian_shell_ctrl_down();
    int shift = arctian_shell_shift_down();
    if (shift && s_last_sel >= 0 && s_last_sel < ent_n) {
        int a = s_last_sel < i ? s_last_sel : i;
        int b = s_last_sel < i ? i : s_last_sel;
        for (int k = 0; k < ent_n; k++) s_multi[k] = 0;
        for (int k = a; k <= b; k++) s_multi[k] = 1;
        sel = i;
    } else if (ctrl) {
        s_multi[i] = s_multi[i] ? 0 : 1;
        sel = s_multi[i] ? i : -1;
        s_last_sel = i;
        if (sel < 0) {
            for (int k = ent_n - 1; k >= 0; k--) if (s_multi[k]) { sel = k; break; }
        }
    } else {
        for (int k = 0; k < ent_n; k++) s_multi[k] = 0;
        s_multi[i] = 1;
        sel = i;
        s_last_sel = i;
    }
    apply_sel();
    update_detail();
}

static void row_dbl_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= ent_n) return;
    if (!ents[i].is_dir) {
        char full[FM_PATH];
        path_child(cwd, ents[i].name, 0, full, sizeof(full));
        arctian_shell_open_file(full);
        return;
    }
    char np[FM_PATH];
    path_child(cwd, ents[i].name, 0, np, sizeof(np));
    if (slen(np) >= FM_PATH) return;
    fm_navigate(np, 1);
}

static void row_press_cb(lv_event_t *e) {
    s_drag_i = (int)(intptr_t)lv_event_get_user_data(e);
    s_drag_on = 0;
    if (A && A->input && A->input->mouse_x) {
        s_drag_x = A->input->mouse_x();
        s_drag_y = A->input->mouse_y();
    }
}

static void row_pressing_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_drag_i != i || !A || !A->input || !A->input->mouse_x) return;
    int dx = A->input->mouse_x() - s_drag_x; if (dx < 0) dx = -dx;
    int dy = A->input->mouse_y() - s_drag_y; if (dy < 0) dy = -dy;
    if (dx > 6 || dy > 6) s_drag_on = 1;
}

static void row_release_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_drag_i != i) return;
    s_drag_i = -1;
    if (!s_drag_on) return;
    s_drag_on = 0;
    if (i < 0 || i >= ent_n || ents[i].is_dir) return;
    int mx = A->input->mouse_x(), my = A->input->mouse_y();
    for (int k = 0; k < FM_NAV; k++) {
        if (!nav_btns[k] || lv_obj_has_flag(nav_btns[k], LV_OBJ_FLAG_HIDDEN)) continue;
        lv_area_t a;
        lv_obj_get_coords(nav_btns[k], &a);
        if (mx >= a.x1 && mx <= a.x2 && my >= a.y1 && my <= a.y2) {
            do_move_to(i, NAV_PATH[k]);
            return;
        }
    }
    char full[FM_PATH];
    path_child(cwd, ents[i].name, 0, full, sizeof(full));
    arctian_shell_drop_at(mx, my, full);
}

static void update_detail(void) {
    if (!det_name) return;
    int n = s_selected_n;
    int single = sel >= 0 && sel < ent_n && n == 1;
    if (!single) {
        lv_obj_set_style_text_align(det_name, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_align(det_meta, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_align(det_hint, LV_TEXT_ALIGN_LEFT, 0);
        if (n > 1) {
            char text[48];
            u2s(text, sizeof(text), (uint32_t)n);
            scat(text, sizeof(text), " öğe seçili");
            lv_label_set_text(det_name, text);
            lv_obj_set_style_text_color(det_name, lv_color_hex(FM_TEXT), 0);
            fm_style_font(det_name, fm_heading_font());
            lv_label_set_text(det_meta, "");
            lv_label_set_text(det_prev, "");
            lv_label_set_text(det_hint, "");
        } else {
            lv_label_set_text(det_name, "Bir öğe seçin");
            lv_obj_set_style_text_color(det_name, lv_color_hex(FM_SECONDARY), 0);
            fm_style_font(det_name, fm_text_font());
            lv_label_set_text(det_meta, "");
            lv_label_set_text(det_prev, "");
            lv_label_set_text(det_hint, "Sağ tıklayarak işlemlere ulaşın");
        }
        return;
    }

    fm_ent_t *en = &ents[sel];
    lv_obj_set_style_text_color(det_name, lv_color_hex(FM_TEXT), 0);
    lv_obj_set_style_text_align(det_name, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(det_meta, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(det_hint, LV_TEXT_ALIGN_LEFT, 0);
    fm_style_font(det_name, fm_heading_font());
    lv_label_set_text(det_name, en->name);
    if (en->is_dir) {
        char meta[56];
        u2s(meta, sizeof(meta), en->child_count);
        char suffix[32];
        scpy(suffix, sizeof(suffix), " öğe");
        scat(meta, sizeof(meta), suffix);
        char fullmeta[72];
        scpy(fullmeta, sizeof(fullmeta), "Klasör · ");
        scat(fullmeta, sizeof(fullmeta), meta);
        lv_label_set_text(det_meta, fullmeta);
        lv_label_set_text(det_prev, "");
        lv_label_set_text(det_hint, "");
        return;
    }
    char meta[48];
    scpy(meta, sizeof(meta), "Dosya · ");
    char hs[24];
    human(hs, sizeof(hs), en->size);
    scat(meta, sizeof(meta), hs);
    lv_label_set_text(det_meta, meta);
    lv_label_set_text(det_hint, "");
    if (!is_text(en->name)) { lv_label_set_text(det_prev, "Önizleme yok"); return; }
    if (!A || !A->fs || !A->fs->read_path) { lv_label_set_text(det_prev, "Önizleme yok"); return; }
    static char pv[1024];
    char full[FM_PATH];
    path_child(cwd, en->name, 0, full, sizeof(full));
    uint32_t bytes = A->fs->read_path(full, pv, sizeof(pv) - 1);
    if (!bytes) { lv_label_set_text(det_prev, "Boş dosya"); return; }
    pv[bytes] = 0;
    for (uint32_t i = 0; i < bytes; i++) {
        unsigned char c = (unsigned char)pv[i];
        if (c == '\r' || c == '\t') pv[i] = ' ';
        else if (c < 32 && c != '\n') pv[i] = '.';
    }
    lv_label_set_text(det_prev, pv);
}

static int ent_vis(int i) {
    return !s_filter[0] || fm_contains_ci(ents[i].name, s_filter);
}

static void update_breadcrumb(void) {
    if (!crumb_box) return;
    lv_obj_clean(crumb_box);
    lv_obj_set_style_pad_all(crumb_box, 0, 0);
    lv_obj_set_style_pad_column(crumb_box, 6, 0);
    lv_obj_set_style_pad_all(crumb_box, 0, 0);
    lv_obj_set_style_pad_column(crumb_box, 6, 0);
    s_crumb_n = 0;

    lv_obj_t *home = lv_button_create(crumb_box);
    lv_obj_set_size(home, LV_SIZE_CONTENT, 30);
    adl_focus_ring_attach(home);
    adl_focus_ring_attach(home);
    lv_obj_set_style_bg_opa(home, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(home, 0, 0);
    lv_obj_set_style_pad_all(home, 4, 0);
    lv_obj_set_style_radius(home, 4, 0);
    lv_obj_set_style_bg_color(home, lv_color_hex(FM_HOVER), LV_STATE_HOVERED);
    fm_style_font(home, fm_text_font());
    lv_obj_t *hl = lv_label_create(home);
    lv_label_set_text(hl, "Bu Bilgisayar");
    lv_obj_set_style_text_color(hl, lv_color_hex(FM_SECONDARY), 0);
    fm_style_font(hl, fm_text_font());
    lv_obj_center(hl);
    lv_obj_add_event_cb(home, nav_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)"C:");

    lv_obj_t *slash = lv_label_create(crumb_box);
    lv_label_set_text(slash, "/");
    lv_obj_set_style_text_color(slash, lv_color_hex(FM_TERTIARY), 0);
    fm_style_font(slash, fm_text_font());

    char path[FM_PATH];
    char drive[3] = { cwd[0] ? cwd[0] : 'C', ':', 0 };
    scpy(path, sizeof(path), drive);
    scpy(s_crumb_path[s_crumb_n], FM_PATH, path);
    int active = seq(cwd, drive);
    lv_obj_t *root = lv_button_create(crumb_box);
    lv_obj_set_size(root, LV_SIZE_CONTENT, 30);
    adl_focus_ring_attach(root);
    adl_focus_ring_attach(root);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 4, 0);
    lv_obj_set_style_radius(root, 4, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(FM_HOVER), LV_STATE_HOVERED);
    fm_style_font(root, fm_text_font());
    lv_obj_t *rl = lv_label_create(root);
    lv_label_set_text(rl, (drive[0] == 'D' || drive[0] == 'd') ? "USB Bellek" : "C:");
    lv_obj_set_style_text_color(rl, lv_color_hex(active ? FM_TEXT : FM_SECONDARY), 0);
    fm_style_font(rl, fm_text_font());
    lv_obj_center(rl);
    lv_obj_add_event_cb(root, nav_cb, LV_EVENT_CLICKED, s_crumb_path[s_crumb_n++]);

    const char *part = cwd;
    if (part[0] && part[1] == ':') part += 2;
    while (*part == '/') part++;
    while (*part && s_crumb_n < FM_CRUMBS) {
        char name[FM_NAME];
        int n = 0;
        while (part[n] && part[n] != '/' && n < FM_NAME - 1) { name[n] = part[n]; n++; }
        name[n] = 0;
        part += n;
        while (*part == '/') part++;
        scat(path, sizeof(path), "/");
        scat(path, sizeof(path), name);
        scpy(s_crumb_path[s_crumb_n], FM_PATH, path);
        active = !*part;

        lv_obj_t *sep = lv_label_create(crumb_box);
        lv_label_set_text(sep, "/");
        lv_obj_set_style_text_color(sep, lv_color_hex(FM_TERTIARY), 0);
        fm_style_font(sep, fm_text_font());
        lv_obj_t *seg = lv_button_create(crumb_box);
        lv_obj_set_size(seg, LV_SIZE_CONTENT, 30);
        adl_focus_ring_attach(seg);
        adl_focus_ring_attach(seg);
        lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_pad_all(seg, 4, 0);
        lv_obj_set_style_radius(seg, 4, 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(FM_HOVER), LV_STATE_HOVERED);
        fm_style_font(seg, fm_text_font());
        lv_obj_t *label = lv_label_create(seg);
        lv_label_set_text(label, name);
        lv_obj_set_style_text_color(label, lv_color_hex(active ? FM_TEXT : FM_SECONDARY), 0);
        fm_style_font(label, fm_text_font());
        lv_obj_center(label);
        lv_obj_add_event_cb(seg, nav_cb, LV_EVENT_CLICKED, s_crumb_path[s_crumb_n++]);
    }
}

static void refresh(void) {
    if (!list_box) return;
    for (int i = 0; i < FM_MAX_ENT; i++) rows[i] = 0;
    lv_obj_clean(list_box);
    ent_n = 0;

    int n = fm_count_children(cwd);
    for (int i = 0; i < n && ent_n < FM_MAX_ENT; i++) {
        char name[FM_NAME];
        int is_dir = 0;
        if (!A || !A->fs || !A->fs->list_dir || A->fs->list_dir(cwd, i, name, sizeof(name), &is_dir) != 0) continue;
        fm_ent_t *en = &ents[ent_n++];
        scpy(en->name, sizeof(en->name), name);
        en->is_dir = is_dir;
        en->size = 0;
        en->child_count = 0;
        char full[FM_PATH];
        path_child(cwd, name, is_dir, full, sizeof(full));
        if (is_dir) en->child_count = (uint32_t)fm_count_children(full);
        else if (A->fs->stat) {
            asi_stat_t st;
            if (A->fs->stat(full, &st) == 0) en->size = st.size;
        }
    }

    for (int i = 1; i < ent_n; i++) {
        fm_ent_t item = ents[i];
        int j = i - 1;
        while (j >= 0 && ent_cmp(&ents[j], &item) > 0) { ents[j + 1] = ents[j]; j--; }
        ents[j + 1] = item;
    }

    for (int i = 0; i < ent_n; i++) s_multi[i] = 0;
    sel = -1;
    s_last_sel = -1;
    s_selected_n = 0;

    int shown = 0;
    for (int i = 0; i < ent_n; i++) {
        if (!ent_vis(i)) continue;
        rows[i] = make_row(i);
        shown++;
    }

    update_breadcrumb();
    fm_update_sort_labels();
    for (int i = 0; i < FM_NAV; i++) {
        if (!nav_btns[i]) continue;
        if (i == FM_NAV - 1 && !s_usb_present) { lv_obj_add_flag(nav_btns[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(nav_btns[i], LV_OBJ_FLAG_HIDDEN);
        int current = seq(cwd, NAV_PATH[i]);
        lv_obj_set_style_bg_color(nav_btns[i], lv_color_hex(current ? FM_SELECTED : FM_WINDOW), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(nav_btns[i], 0),
                                    lv_color_hex(current ? FM_TEXT : FM_SECONDARY), 0);
    }

    if (s_empty_lbl) {
        if (ent_n == 0) lv_label_set_text(s_empty_lbl, "Bu klasör boş");
        else if (shown == 0) lv_label_set_text(s_empty_lbl, "Eşleşen öğe yok");
        lv_obj_set_style_text_color(s_empty_lbl, lv_color_hex(FM_SECONDARY), 0);
        lv_obj_set_style_text_align(s_empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
        if (!shown) lv_obj_clear_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    fm_update_status();
    fm_update_toolbar();
    update_detail();
}

static void fm_navigate(const char *path, int remember) {
    if (!path || !path[0]) return;
    if (remember) fm_add_history(cwd);
    scpy(cwd, FM_PATH, path);
    sel = -1;
    refresh();
}

static void nav_cb(lv_event_t *e) {
    const char *path = (const char *)lv_event_get_user_data(e);
    fm_navigate(path, 1);
}

static void up_cb(lv_event_t *e) {
    (void)e;
    char parent[FM_PATH];
    scpy(parent, sizeof(parent), cwd);
    if (path_up(parent)) fm_navigate(parent, 1);
}

static void open_cb(lv_event_t *e) {
    (void)e;
    if (sel < 0 || sel >= ent_n) return;
    if (!ents[sel].is_dir) {
        char full[FM_PATH];
        path_child(cwd, ents[sel].name, 0, full, sizeof(full));
        arctian_shell_open_file(full);
        return;
    }
    char next[FM_PATH];
    path_child(cwd, ents[sel].name, 0, next, sizeof(next));
    fm_navigate(next, 1);
}

static void refresh_cb(lv_event_t *e) { (void)e; refresh(); }

static void del_cb(lv_event_t *e) {
    (void)e;
    if (!A || !A->fs || !A->fs->remove || !A->fs->name || !fm_has_selection()) return;
    for (int i = 0; i < ent_n; i++) {
        if (!fm_selected(i)) continue;
        char full[FM_PATH];
        path_child(cwd, ents[i].name, ents[i].is_dir, full, sizeof(full));
        if (ents[i].is_dir) {
            for (int k = 0; k < FM_FLAT; k++) {
                const char *name = A->fs->name(k);
                if (name && starts_with(name, full)) A->fs->remove(name);
            }
        } else if (arctian_shell_trash(full) != 0) A->fs->remove(full);
    }
    sel = -1;
    s_selected_n = 0;
    refresh();
}

static void newdir_ok(const char *name) {
    char nm[FM_NAME];
    trim_name(nm, sizeof(nm), name);
    if (!nm[0]) return;
    char full[FM_PATH];
    path_child(cwd, nm, 1, full, sizeof(full));
    if (flat_exists(full)) return;
    if (A && A->fs && A->fs->create_in) A->fs->create_in(cwd, nm, 1);
    refresh();
}

static void newfile_ok(const char *name) {
    char nm[FM_NAME];
    trim_name(nm, sizeof(nm), name);
    if (!nm[0]) return;
    char full[FM_PATH];
    path_child(cwd, nm, 0, full, sizeof(full));
    if (flat_exists(full)) return;
    if (A && A->fs && A->fs->create_in) {
        void *h = A->fs->create_in(cwd, nm, 0);
        if (h && A->fs->write) A->fs->write(h, "", 0);
    }
    refresh();
}

static lv_obj_t *mk_btn(lv_obj_t *parent, const char *txt, int w, int h,
                        uint32_t bg, lv_event_cb_t cb, void *ud) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FM_HOVER), LV_STATE_HOVERED);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    adl_focus_ring_attach(b);
    lv_obj_t *label = lv_label_create(b);
    lv_label_set_text(label, txt);
    lv_obj_set_style_text_color(label, lv_color_hex(FM_TEXT), 0);
    fm_style_font(label, fm_text_font());
    lv_obj_center(label);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

static lv_obj_t *mk_nav(lv_obj_t *parent, const char *text, const char *path) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, LV_PCT(100), 32);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FM_WINDOW), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FM_HOVER), LV_STATE_HOVERED);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_left(b, 8, 0);
    lv_obj_set_style_pad_right(b, 8, 0);
    adl_focus_ring_attach(b);
    lv_obj_t *label = lv_label_create(b);
    lv_label_set_text(label, text);
    fm_style_font(label, fm_text_font());
    lv_obj_set_style_text_color(label, lv_color_hex(FM_SECONDARY), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)path);
    return b;
}

static void fm_back_cb(lv_event_t *e) {
    (void)e;
    if (s_history_n <= 0) return;
    s_history_n--;
    scpy(cwd, FM_PATH, s_history[s_history_n]);
    sel = -1;
    refresh();
}

static void fm_escape_cb(lv_event_t *e) {
    if (lv_event_get_key(e) == LV_KEY_ESC && s_search) {
        lv_textarea_set_text(s_search, "");
        scpy(s_filter, sizeof(s_filter), "");
        lv_indev_t *kp = lv_port_get_keypad();
        if (kp) lv_indev_set_group(kp, 0);
        refresh();
    }
}

static void fm_search_changed_cb(lv_event_t *e) {
    (void)e;
    if (!s_search) return;
    scpy(s_filter, sizeof(s_filter), lv_textarea_get_text(s_search));
    if (!s_filter[0]) {
        lv_indev_t *kp = lv_port_get_keypad();
        if (kp) lv_indev_set_group(kp, 0);
    }
    refresh();
}

static void fm_header_sort_cb(lv_event_t *e) {
    int by_count = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_sort_count == by_count) s_sort_desc = !s_sort_desc;
    else { s_sort_count = by_count; s_sort_desc = 0; }
    refresh();
}

static void fm_update_sort_labels(void) {
    if (head_name) lv_label_set_text(head_name, s_sort_count ? "Ad" : (s_sort_desc ? "Ad ↓" : "Ad ↑"));
    if (head_count) lv_label_set_text(head_count, s_sort_count ? (s_sort_desc ? "Öğe ↓" : "Öğe ↑") : "Öğe");
}

static void fm_update_toolbar(void) {
    if (!back_btn) return;
    int enabled = s_history_n > 0;
    if (enabled) {
        lv_obj_clear_state(back_btn, LV_STATE_DISABLED);
        lv_obj_set_style_opa(back_btn, LV_OPA_COVER, 0);
    } else {
        lv_obj_add_state(back_btn, LV_STATE_DISABLED);
        lv_obj_set_style_opa(back_btn, LV_OPA_40, 0);
    }
}

static void fm_menu_close(void) {
    int was_open = context_backdrop || context_menu || menu_group || s_menu_focused;
    if (context_backdrop) { lv_obj_delete(context_backdrop); context_backdrop = 0; }
    context_menu = 0;
    menu_item_n = 0;
    s_menu_focused = 0;
    if (was_open) {
        lv_indev_t *kp = lv_port_get_keypad();
        if (kp) lv_indev_set_group(kp, menu_prev_group);
        menu_prev_group = 0;
        if (menu_group) { lv_group_delete(menu_group); menu_group = 0; }
    }
}

static void fm_menu_overlay_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) fm_menu_close();
}

static void fm_menu_action_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_READY && !lv_obj_has_state(lv_event_get_target(e), LV_STATE_FOCUSED)) return;
    int action = (int)(intptr_t)lv_event_get_user_data(e);
    fm_menu_close();
    switch (action) {
        case 0: open_cb(0); break;
        case 1: prompt_newdir_cb(0); break;
        case 2: prompt_newfile_cb(0); break;
        case 3: rename_cb(0); break;
        case 4: clip_sel(0); break;
        case 5: clip_sel(1); break;
        case 6: del_cb(0); break;
        case 7: refresh_cb(0); break;
    }
}

static void fm_menu_key_cb(lv_event_t *e) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC) fm_menu_close();
    else if (key == LV_KEY_UP && menu_group) lv_group_focus_prev(menu_group);
    else if ((key == LV_KEY_DOWN || key == LV_KEY_NEXT) && menu_group) lv_group_focus_next(menu_group);
}

static lv_obj_t *fm_menu_add_item(lv_obj_t *menu, const char *label, const char *shortcut,
                                  int action, int enabled, int primary) {
    lv_obj_t *b = lv_button_create(menu);
    lv_obj_set_size(b, LV_PCT(100), 32);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? FM_MENU_FIRST : FM_MENU_BG), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FM_SELECTED), LV_STATE_HOVERED);
    lv_obj_set_style_text_color(b, lv_color_hex(enabled ? FM_TEXT : FM_TERTIARY), LV_STATE_DISABLED);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_left(b, 10, 0);
    lv_obj_set_style_pad_right(b, 10, 0);
    if (!enabled) lv_obj_add_state(b, LV_STATE_DISABLED);
    adl_focus_ring_attach(b);
    lv_obj_t *left = lv_label_create(b);
    lv_label_set_text(left, label);
    lv_obj_set_style_text_color(left, lv_color_hex(action == 6 ? FM_DANGER : (enabled ? FM_TEXT : FM_TERTIARY)), 0);
    fm_style_font(left, fm_text_font());
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *right = lv_label_create(b);
    lv_label_set_text(right, shortcut);
    lv_obj_set_style_text_color(right, lv_color_hex(primary ? FM_SECONDARY : FM_TERTIARY), 0);
    fm_style_font(right, fm_font_small ? fm_font_small : fm_text_font());
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(b, fm_menu_action_cb, LV_EVENT_CLICKED, (void *)(intptr_t)action);
    lv_obj_add_event_cb(b, fm_menu_key_cb, LV_EVENT_KEY, 0);
    lv_obj_add_event_cb(b, fm_menu_action_cb, LV_EVENT_READY, (void *)(intptr_t)action);
    if (menu_item_n < (int)(sizeof(menu_items) / sizeof(menu_items[0]))) menu_items[menu_item_n++] = b;
    return b;
}

static void fm_menu_separator(lv_obj_t *menu) {
    lv_obj_t *sep = lv_obj_create(menu);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, LV_PCT(100), 9);
    lv_obj_remove_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *line = lv_obj_create(sep);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(FM_MENU_BORDER), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_align(line, LV_ALIGN_CENTER, 0, 0);
}

static void fm_menu_show(int x, int y, int empty_space) {
    fm_menu_close();
    context_backdrop = lv_obj_create(lv_obj_get_parent(list_box));
    lv_obj_remove_style_all(context_backdrop);
    lv_obj_set_size(context_backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(context_backdrop, 0, 0);
    lv_obj_set_style_bg_opa(context_backdrop, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(context_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(context_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(context_backdrop, fm_menu_overlay_cb, LV_EVENT_CLICKED, 0);

    context_menu = lv_obj_create(context_backdrop);
    lv_obj_remove_style_all(context_menu);
    lv_obj_set_size(context_menu, FM_MENU_W, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(context_menu, lv_color_hex(FM_MENU_BG), 0);
    lv_obj_set_style_bg_opa(context_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(context_menu, 1, 0);
    lv_obj_set_style_border_color(context_menu, lv_color_hex(FM_MENU_BORDER), 0);
    lv_obj_set_style_radius(context_menu, 6, 0);
    lv_obj_set_style_pad_all(context_menu, 4, 0);
    lv_obj_set_style_pad_row(context_menu, 0, 0);
    lv_obj_set_style_pad_column(context_menu, 0, 0);
    lv_obj_set_style_shadow_width(context_menu, 20, 0);
    lv_obj_set_style_shadow_color(context_menu, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(context_menu, 89, 0);
    lv_obj_set_style_shadow_offset_y(context_menu, 8, 0);
    lv_obj_set_flex_flow(context_menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(context_menu, LV_OBJ_FLAG_SCROLLABLE);

    int has_sel = fm_has_selection();
    if (!empty_space) {
        fm_menu_add_item(context_menu, "Aç", "Enter", 0, has_sel, 1);
        fm_menu_separator(context_menu);
    }
    fm_menu_add_item(context_menu, "Yeni klasör", "Ctrl+Shift+N", 1, 1, 0);
    fm_menu_add_item(context_menu, "Yeni dosya", "Ctrl+N", 2, 1, 0);
    fm_menu_separator(context_menu);
    if (!empty_space) {
        fm_menu_add_item(context_menu, "Yeniden adlandır", "F2", 3, has_sel, 0);
        fm_menu_add_item(context_menu, "Kopyala", "Ctrl+C", 4, has_sel, 0);
        fm_menu_add_item(context_menu, "Taşı", "Ctrl+X", 5, has_sel, 0);
        fm_menu_separator(context_menu);
        fm_menu_add_item(context_menu, "Sil", "Del", 6, has_sel, 0);
        fm_menu_separator(context_menu);
    }
    fm_menu_add_item(context_menu, "Yenile", "F5", 7, 1, 0);

    lv_obj_update_layout(context_menu);
    lv_obj_t *body = lv_obj_get_parent(list_box);
    int bw = lv_obj_get_width(body), bh = lv_obj_get_height(body);
    lv_area_t body_area;
    lv_obj_get_coords(body, &body_area);
    int mx = x - body_area.x1, my = y - body_area.y1;
    int mw = lv_obj_get_width(context_menu), mh = lv_obj_get_height(context_menu);
    int max_x = bw - mw;
    int max_y = bh - mh;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (mx > max_x) mx = max_x;
    if (my > max_y) my = max_y;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    lv_obj_set_pos(context_menu, mx, my);

    lv_indev_t *kp = lv_port_get_keypad();
    menu_prev_group = kp ? lv_indev_get_group(kp) : 0;
    menu_group = lv_group_create();
    for (int i = 0; i < menu_item_n; i++) lv_group_add_obj(menu_group, menu_items[i]);
    if (kp) lv_indev_set_group(kp, menu_group);
    if (menu_item_n) lv_group_focus_obj(menu_items[0]);
    s_menu_focused = 1;
}

static void fm_input_poll_cb(lv_timer_t *t) {
    (void)t;
    if (!A || !fm_body || !A->input || !A->input->mouse_buttons || !A->input->mouse_x || !A->input->mouse_y) return;
    lv_obj_t *shell = lv_obj_get_parent(fm_body);
    if (shell && lv_obj_has_flag(shell, LV_OBJ_FLAG_HIDDEN)) {
        s_prev_right = (A->input->mouse_buttons() >> 1) & 1;
        s_prev_left = A->input->mouse_buttons() & 1;
        return;
    }
    int buttons = A->input->mouse_buttons();
    int right = (buttons >> 1) & 1;
    int left = buttons & 1;
    int x = A->input->mouse_x(), y = A->input->mouse_y();
    lv_area_t shell_area;
    if (shell) {
        lv_obj_get_coords(shell, &shell_area);
        if (x < shell_area.x1 || x > shell_area.x2 || y < shell_area.y1 || y > shell_area.y2) {
            if (context_menu && left && !s_prev_left) fm_menu_close();
            s_prev_right = right;
            s_prev_left = left;
            return;
        }
    }
    if (context_menu && left && !s_prev_left) {
        lv_area_t menu_area;
        lv_obj_get_coords(context_menu, &menu_area);
        if (x < menu_area.x1 || x > menu_area.x2 || y < menu_area.y1 || y > menu_area.y2)
            fm_menu_close();
    }
    if (++s_usb_poll_ticks >= 20) {
        s_usb_poll_ticks = 0;
        fm_sync_usb();
    }
    if (right && !s_prev_right) {
        if (context_menu) fm_menu_close();
        else {
            int found = -1;
            for (int i = 0; i < ent_n; i++) {
                if (!rows[i]) continue;
                lv_area_t a;
                lv_obj_get_coords(rows[i], &a);
                if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) { found = i; break; }
            }
            if (found >= 0) {
                if (!fm_selected(found)) {
                    for (int i = 0; i < ent_n; i++) s_multi[i] = 0;
                    s_multi[found] = 1;
                    sel = found;
                    s_last_sel = found;
                    apply_sel();
                    update_detail();
                }
                fm_menu_show(x, y, 0);
            } else {
                lv_area_t a;
                lv_obj_get_coords(list_box, &a);
                lv_area_t h;
                lv_obj_get_coords(table_head, &h);
                if ((x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) ||
                    (x >= h.x1 && x <= h.x2 && y >= h.y1 && y <= h.y2))
                    fm_menu_show(x, y, 1);
            }
        }
    }
    s_prev_right = right;
    s_prev_left = left;
}

static void build_ui(lv_obj_t *win) {
    /* Govde yeni olusturulmus olabilir: olcumden once LVGL koordinatlarini
     * tazele, yoksa W/H eski/varsayilan deger kalir ve duzen fallback'e duser
     * (ilk acilista 820x500 olarak cizilme hatasi). */
    lv_obj_update_layout(win);
    int W = lv_obj_get_width(win), H = lv_obj_get_height(win);
    lv_obj_set_style_bg_color(win, lv_color_hex(FM_WINDOW), 0);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(win, 0, 0);
    lv_obj_set_style_radius(win, 0, 0);
    lv_obj_set_style_pad_all(win, 0, 0);
    lv_obj_remove_flag(win, LV_OBJ_FLAG_SCROLLABLE);
    if (W < 460) W = 820;
    if (H < 280) H = 500;
    const int toolbar_h = 52, status_h = 28, side_w = 184, detail_w = 260, head_h = 30;
    int body_h = H - toolbar_h - status_h;
    if (body_h < 100) body_h = 100;
    list_w = W - side_w - detail_w;
    if (list_w < 100) list_w = 100;

    lv_obj_t *toolbar = lv_obj_create(win);
    lv_obj_remove_style_all(toolbar);
    lv_obj_remove_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(toolbar, W, toolbar_h);
    lv_obj_set_pos(toolbar, 0, 0);
    lv_obj_set_style_bg_color(toolbar, lv_color_hex(FM_WINDOW), 0);
    lv_obj_set_style_bg_opa(toolbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(toolbar, 1, 0);
    lv_obj_set_style_border_side(toolbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(toolbar, lv_color_hex(FM_DIVIDER), 0);
    ui_tb = toolbar;

    back_btn = mk_btn(toolbar, "", 32, 32, FM_WINDOW, fm_back_cb, 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(FM_BORDER), 0);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 14, 0);
    {
        lv_obj_t *bic = make_icon(back_btn, FM_SVG_BACK, 16);
        if (bic) lv_obj_center(bic);
    }
    path_lbl = 0;

    crumb_box = lv_obj_create(toolbar);
    lv_obj_remove_style_all(crumb_box);
    lv_obj_set_size(crumb_box, W - 320, 32);
    lv_obj_set_pos(crumb_box, 56, 10);
    lv_obj_set_flex_flow(crumb_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(crumb_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(crumb_box, 6, 0);
    lv_obj_remove_flag(crumb_box, LV_OBJ_FLAG_SCROLLABLE);

    s_search = lv_textarea_create(toolbar);
    lv_textarea_set_one_line(s_search, true);
    lv_obj_set_scrollbar_mode(s_search, LV_SCROLLBAR_MODE_OFF);  /* sagdaki dikey cizgi */
    lv_textarea_set_cursor_click_pos(s_search, true);
    lv_obj_set_size(s_search, 240, 32);
    lv_obj_align(s_search, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_textarea_set_placeholder_text(s_search, "Ara");
    lv_obj_set_style_bg_color(s_search, lv_color_hex(FM_RAISED), 0);
    lv_obj_set_style_bg_opa(s_search, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_search, 1, 0);
    lv_obj_set_style_border_color(s_search, lv_color_hex(FM_BORDER), 0);
    lv_obj_set_style_radius(s_search, 4, 0);
    lv_obj_set_style_pad_left(s_search, 10, 0);
    lv_obj_set_style_pad_right(s_search, 10, 0);
    fm_style_font(s_search, fm_text_font());
    lv_obj_set_style_text_color(s_search, lv_color_hex(FM_TEXT), 0);
    lv_obj_add_event_cb(s_search, fm_search_changed_cb, LV_EVENT_VALUE_CHANGED, 0);
    lv_obj_add_event_cb(s_search, search_click_cb, LV_EVENT_CLICKED, 0);
    lv_obj_add_event_cb(s_search, fm_escape_cb, LV_EVENT_KEY, 0);
    adl_focus_ring_attach(s_search);

    ui_side = lv_obj_create(win);
    lv_obj_remove_style_all(ui_side);
    lv_obj_remove_flag(ui_side, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ui_side, 0, toolbar_h);
    lv_obj_set_size(ui_side, side_w, body_h);
    lv_obj_set_style_bg_color(ui_side, lv_color_hex(FM_WINDOW), 0);
    lv_obj_set_style_bg_opa(ui_side, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_side, 1, 0);
    lv_obj_set_style_border_side(ui_side, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(ui_side, lv_color_hex(FM_DIVIDER), 0);
    lv_obj_set_style_pad_top(ui_side, 12, 0);
    lv_obj_set_style_pad_bottom(ui_side, 12, 0);
    lv_obj_set_style_pad_left(ui_side, 8, 0);
    lv_obj_set_style_pad_right(ui_side, 8, 0);
    lv_obj_set_style_pad_row(ui_side, 2, 0);
    lv_obj_set_flex_flow(ui_side, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *side_heading = lv_label_create(ui_side);
    lv_label_set_text(side_heading, "Hızlı erişim");
    lv_obj_set_style_text_color(side_heading, lv_color_hex(FM_TERTIARY), 0);
    fm_style_font(side_heading, fm_font_small ? fm_font_small : fm_text_font());
    lv_obj_set_style_pad_left(side_heading, 8, 0);
    lv_obj_set_style_pad_top(side_heading, 4, 0);
    lv_obj_set_style_pad_bottom(side_heading, 8, 0);
    for (int i = 0; i < FM_NAV; i++) nav_btns[i] = mk_nav(ui_side, NAV_LABEL[i], NAV_PATH[i]);

    table_head = lv_obj_create(win);
    lv_obj_remove_style_all(table_head);
    lv_obj_set_size(table_head, list_w, head_h);
    lv_obj_set_pos(table_head, side_w, toolbar_h);
    lv_obj_set_flex_flow(table_head, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(table_head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_left(table_head, 10, 0);
    lv_obj_set_style_pad_right(table_head, 10, 0);
    lv_obj_set_style_bg_color(table_head, lv_color_hex(FM_WINDOW), 0);
    lv_obj_set_style_bg_opa(table_head, LV_OPA_COVER, 0);

    lv_obj_t *name_head_btn = lv_button_create(table_head);
    lv_obj_set_size(name_head_btn, list_w - 100, head_h);
    lv_obj_set_style_bg_opa(name_head_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(name_head_btn, lv_color_hex(FM_HOVER), LV_STATE_HOVERED);
    lv_obj_set_style_radius(name_head_btn, 4, 0);
    lv_obj_set_style_border_width(name_head_btn, 0, 0);
    lv_obj_set_style_shadow_width(name_head_btn, 0, 0);
    lv_obj_set_style_pad_all(name_head_btn, 0, 0);
    adl_focus_ring_attach(name_head_btn);
    head_name = lv_label_create(name_head_btn);
    lv_label_set_text(head_name, "Ad ↑");
    lv_obj_set_style_text_color(head_name, lv_color_hex(FM_TERTIARY), 0);
    fm_style_font(head_name, fm_font_small ? fm_font_small : fm_text_font());
    lv_obj_align(head_name, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(name_head_btn, fm_header_sort_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);

    lv_obj_t *count_head_btn = lv_button_create(table_head);
    lv_obj_set_size(count_head_btn, 80, head_h);
    lv_obj_set_style_bg_opa(count_head_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(count_head_btn, lv_color_hex(FM_HOVER), LV_STATE_HOVERED);
    lv_obj_set_style_radius(count_head_btn, 4, 0);
    lv_obj_set_style_border_width(count_head_btn, 0, 0);
    lv_obj_set_style_shadow_width(count_head_btn, 0, 0);
    lv_obj_set_style_pad_all(count_head_btn, 0, 0);
    adl_focus_ring_attach(count_head_btn);
    head_count = lv_label_create(count_head_btn);
    lv_label_set_text(head_count, "Öğe");
    lv_obj_set_style_text_color(head_count, lv_color_hex(FM_TERTIARY), 0);
    fm_style_font(head_count, fm_font_small ? fm_font_small : fm_text_font());
    lv_obj_set_width(head_count, 80);
    lv_obj_set_style_text_align(head_count, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(head_count, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(count_head_btn, fm_header_sort_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    list_box = lv_obj_create(win);
    lv_obj_remove_style_all(list_box);
    lv_obj_set_size(list_box, list_w, body_h - head_h);
    lv_obj_set_pos(list_box, side_w, toolbar_h + head_h);
    lv_obj_set_style_bg_color(list_box, lv_color_hex(FM_WINDOW), 0);
    lv_obj_set_style_bg_opa(list_box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(list_box, 12, 0);
    lv_obj_set_style_pad_right(list_box, 12, 0);
    lv_obj_set_style_pad_top(list_box, 0, 0);
    lv_obj_set_style_pad_bottom(list_box, 0, 0);
    lv_obj_set_style_pad_row(list_box, 0, 0);
    lv_obj_set_flex_flow(list_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_box, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(list_box, fm_menu_overlay_cb, LV_EVENT_CLICKED, 0);

    s_empty_lbl = lv_label_create(win);
    lv_label_set_text(s_empty_lbl, "");
    lv_obj_set_style_text_color(s_empty_lbl, lv_color_hex(FM_SECONDARY), 0);
    fm_style_font(s_empty_lbl, fm_text_font());
    lv_obj_set_style_text_align(s_empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_empty_lbl, list_w - 24);
    lv_obj_align(s_empty_lbl, LV_ALIGN_TOP_LEFT, side_w + 12,
                 toolbar_h + head_h + (body_h - head_h) / 2 - 10);
    lv_obj_add_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);
    s_empty_btn = 0;

    ui_det = lv_obj_create(win);
    lv_obj_remove_style_all(ui_det);
    lv_obj_set_pos(ui_det, W - detail_w, toolbar_h);
    lv_obj_set_size(ui_det, detail_w, body_h);
    lv_obj_set_style_bg_color(ui_det, lv_color_hex(FM_WINDOW), 0);
    lv_obj_set_style_bg_opa(ui_det, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_det, 1, 0);
    lv_obj_set_style_border_side(ui_det, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(ui_det, lv_color_hex(FM_DIVIDER), 0);
    lv_obj_set_style_pad_all(ui_det, 20, 0);
    lv_obj_set_style_pad_row(ui_det, 6, 0);
    lv_obj_set_flex_flow(ui_det, LV_FLEX_FLOW_COLUMN);
    /* Icerik ustten hizalanir; dikey ortalama secimden sonra basligi
     * panelin ortasina kaydiriyordu. */
    lv_obj_set_flex_align(ui_det, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(ui_det, LV_OBJ_FLAG_SCROLLABLE);

    det_name = lv_label_create(ui_det);
    lv_label_set_text(det_name, "Bir öğe seçin");
    lv_obj_set_style_text_color(det_name, lv_color_hex(FM_TEXT), 0);
    fm_style_font(det_name, fm_heading_font());
    lv_label_set_long_mode(det_name, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(det_name, LV_PCT(100));
    lv_obj_set_style_text_align(det_name, LV_TEXT_ALIGN_CENTER, 0);
    det_meta = lv_label_create(ui_det);
    lv_label_set_text(det_meta, "");
    lv_obj_set_style_text_color(det_meta, lv_color_hex(FM_SECONDARY), 0);
    fm_style_font(det_meta, fm_font_small ? fm_font_small : fm_text_font());
    lv_obj_set_width(det_meta, LV_PCT(100));
    lv_obj_set_style_text_align(det_meta, LV_TEXT_ALIGN_CENTER, 0);
    det_hint = lv_label_create(ui_det);
    lv_label_set_text(det_hint, "Sağ tıklayarak işlemlere ulaşın");
    lv_obj_set_style_text_color(det_hint, lv_color_hex(FM_TERTIARY), 0);
    fm_style_font(det_hint, fm_font_small ? fm_font_small : fm_text_font());
    lv_obj_set_width(det_hint, LV_PCT(100));
    lv_obj_set_style_text_align(det_hint, LV_TEXT_ALIGN_CENTER, 0);
    det_prev = lv_label_create(ui_det);
    lv_label_set_text(det_prev, "");
    lv_obj_set_style_text_color(det_prev, lv_color_hex(FM_SECONDARY), 0);
    fm_style_font(det_prev, fm_font_small ? fm_font_small : fm_text_font());
    lv_label_set_long_mode(det_prev, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(det_prev, LV_PCT(100));

    ui_st = lv_obj_create(win);
    lv_obj_remove_style_all(ui_st);
    lv_obj_set_size(ui_st, W, status_h);
    lv_obj_set_pos(ui_st, 0, H - status_h);
    lv_obj_set_style_bg_color(ui_st, lv_color_hex(FM_WINDOW), 0);
    lv_obj_set_style_bg_opa(ui_st, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_st, 1, 0);
    lv_obj_set_style_border_side(ui_st, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(ui_st, lv_color_hex(FM_DIVIDER), 0);
    lv_obj_set_style_pad_left(ui_st, 14, 0);
    lv_obj_set_style_pad_right(ui_st, 14, 0);

    status_lbl = lv_label_create(ui_st);
    lv_label_set_text(status_lbl, "0 öğe");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(FM_TERTIARY), 0);
    fm_style_font(status_lbl, fm_font_small ? fm_font_small : fm_text_font());
    lv_obj_align(status_lbl, LV_ALIGN_LEFT_MID, 0, 0);
}

static void fm_relayout(lv_obj_t *win) {
    if (!win) return;
    lv_obj_t *shell = lv_obj_get_parent(win);
    int outer_w = shell ? lv_obj_get_width(shell) : lv_obj_get_width(win) + 4;
    int outer_h = shell ? lv_obj_get_height(shell) : lv_obj_get_height(win) + 40;
    int W = outer_w - 4, H = outer_h - 40;
    if (W < 460 || H < 280) return;
    const int toolbar_h = 52, status_h = 28, side_w = 184, detail_w = 260, head_h = 30;
    int body_h = H - toolbar_h - status_h;
    if (body_h < 100) body_h = 100;
    list_w = W - side_w - detail_w;
    if (list_w < 100) list_w = 100;
    if (shell) {
        lv_obj_t *titlebar = lv_obj_get_child(shell, 0);
        if (titlebar) { lv_obj_set_size(titlebar, outer_w - 2, 40); lv_obj_align(titlebar, LV_ALIGN_TOP_MID, 0, 1); }
        lv_obj_set_pos(win, 2, 40);
        lv_obj_set_size(win, outer_w - 4, outer_h - 40);
    }
    if (crumb_box) { lv_obj_set_pos(crumb_box, 56, 10); lv_obj_set_size(crumb_box, W - 320, 32); }
    if (ui_tb) lv_obj_set_size(ui_tb, W, toolbar_h);
    if (back_btn) lv_obj_set_pos(back_btn, 14, (toolbar_h - 32) / 2);
    if (s_search) lv_obj_align(s_search, LV_ALIGN_RIGHT_MID, -14, 0);
    if (ui_side) lv_obj_set_size(ui_side, side_w, body_h);
    if (table_head) { lv_obj_set_pos(table_head, side_w, toolbar_h); lv_obj_set_size(table_head, list_w, head_h); }
    if (head_name) lv_obj_set_width(lv_obj_get_parent(head_name), list_w - 100);
    if (list_box) { lv_obj_set_pos(list_box, side_w, toolbar_h + head_h); lv_obj_set_size(list_box, list_w, body_h - head_h); }
    if (s_empty_lbl) {
        lv_obj_set_width(s_empty_lbl, list_w - 24);
        lv_obj_set_pos(s_empty_lbl, side_w + 12,
                       toolbar_h + head_h + (body_h - head_h) / 2 - 10);
    }
    if (ui_det) { lv_obj_set_pos(ui_det, W - detail_w, toolbar_h); lv_obj_set_size(ui_det, detail_w, body_h); }
    if (ui_st) { lv_obj_set_pos(ui_st, 0, H - status_h); lv_obj_set_size(ui_st, W, status_h); }
    if (context_menu) fm_menu_close();
    if (ent_n) refresh();
}

/* Pencere govdesi boyut degistiginde (snap/buyut/kucult) yeniden akis */
static void fm_resize_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_SIZE_CHANGED)
        fm_relayout(lv_event_get_target(e));
}

/* Ilk acilista koordinatlar tazelenmeden cizim yapilmisti; 300 ms sonra
 * bir kez daha akis (timer tek seferlik, cb'de kendini temizler). */
static void fm_relayout_timer_cb(lv_timer_t *t) {
    (void)t;
    relayout_timer = 0;   /* repeat 1 oldugu icin LVGL sonra siler */
    if (fm_body) fm_relayout(fm_body);
}



/* ------------------------------------------------------------------ */
/* Dosya islemleri: kopya/taşı, pano, geri al                         */
/* ------------------------------------------------------------------ */

/* "C:/belgeler/a.txt" -> dir="C:/belgeler", nm="a.txt" (kokta dir="") */
static void fm_split(const char *full, char *dir, int dcap, char *nm, int ncap) {
    int last = -1;
    for (int i = 0; full[i]; i++) if (full[i] == '/') last = i;
    scpy(nm, ncap, full + last + 1);
    if (last < 0) { dir[0] = 0; return; }
    int k = 0;
    for (int i = 0; i < last && k < dcap - 1; i++) dir[k++] = full[i];
    dir[k] = 0;
}

static void fm_full_in(const char *dir, const char *nm, char *out, int cap) {
    out[0] = 0;
    if (dir[0]) { scat(out, cap, dir); scat(out, cap, "/"); }
    scat(out, cap, nm);
}

/* Dosyanin tam yolu var mi? (stat yoksa duz ad AFS listesinde aranir) */
static int fm_path_exists(const char *full) {
    if (!A || !A->fs) return 0;
    if (A->fs->stat) {
        asi_stat_t st;
        if (A->fs->stat(full, &st) == 0) return 1;
    }
    return flat_exists(full);
}

/* Dosyayi handle olarak ac (kok dosya: find(0, ad); klasor: find_in) */
static void *fm_src_open(const char *full, char *nm_out, int cap) {
    if (!A || !A->fs) return 0;
    char dir[FM_PATH], nm[FM_NAME];
    fm_split(full, dir, sizeof(dir), nm, sizeof(nm));
    scpy(nm_out, cap, nm);
    if (!dir[0]) return A->fs->find ? A->fs->find(0, nm) : 0;
    return A->fs->find_in ? A->fs->find_in(dir, nm) : 0;
}

/* Handle'lar arasi kopya (4 KB parcalar halinde) */
static int fm_copy_h(void *sh, void *dh) {
    if (!sh || !dh || !A->fs->read || !A->fs->write) return 0;
    char buf[4096];
    uint32_t n;
    while ((n = A->fs->read(sh, buf, sizeof(buf))) > 0) {
        if (A->fs->write(dh, buf, n) != 0) return 0;
        if (n < sizeof(buf)) break;
    }
    return 1;
}

/* Dosyayi hedef klasore kopyala. Cakismada " (kopya)" ekler.
 * dst_nm_out: olusan ad. 1 = basarili. */
static int fm_copy_one(const char *src_full, const char *dst_dir,
                       char *dst_nm_out, int cap) {
    if (!A || !A->fs) return 0;
    char snm[FM_NAME];
    void *sh = fm_src_open(src_full, snm, sizeof(snm));
    if (!sh) return 0;
    char cand[FM_NAME];
    scpy(cand, FM_NAME, snm);
    for (int t = 0; t < 4; t++) {
        char full[FM_PATH];
        fm_full_in(dst_dir, cand, full, sizeof(full));
        if (!fm_path_exists(full)) {
            void *dh = dst_dir[0]
                       ? (A->fs->create_in ? A->fs->create_in(dst_dir, cand, 0) : 0)
                       : (A->fs->create ? A->fs->create(0, cand, 0) : 0);
            if (dh) {
                if (dst_nm_out) scpy(dst_nm_out, cap, cand);
                return fm_copy_h(sh, dh);
            }
        }
        char alt[FM_NAME];
        scpy(alt, FM_NAME, snm);
        scat(alt, FM_NAME, t == 0 ? " (kopya)" : (t == 1 ? " (kopya 2)" : " (kopya 3)"));
        scpy(cand, FM_NAME, alt);
    }
    return 0;
}

static void push_op(const fm_op_t *op) {
    if (s_op_n >= FM_UNDO_LV) {            /* yer ac: en eskisini at */
        for (int i = 1; i < FM_UNDO_LV; i++) s_ops[i - 1] = s_ops[i];
        s_op_n--;
    }
    s_ops[s_op_n++] = *op;
}

/* Secili dosyalari ('\n' ile ayrilmis yollar) panoya yaz; cut=1: kes modu */
static void clip_sel(int cut) {
    static char acc[2048];
    acc[0] = 0;
    int n = 0;
    for (int i = 0; i < ent_n; i++) {
        if (!s_multi[i] && i != sel) continue;
        if (ents[i].is_dir) continue;       /* klasorler pano disi */
        char full[FM_PATH];
        path_child(cwd, ents[i].name, 0, full, sizeof(full));
        if (n) scat(acc, sizeof(acc), "\n");
        scat(acc, sizeof(acc), full);
        n++;
    }
    if (!n) {
        if (status_lbl) lv_label_set_text(status_lbl, "Seçili dosya yok");
        return;
    }
    arctian_clipboard_set(acc);
    s_cut = cut;
    if (status_lbl) {
        char st[64];
        u2s(st, sizeof(st), (uint32_t)n);
        scat(st, sizeof(st), cut ? " öğe kesildi" : " öğe kopyalandı");
        lv_label_set_text(status_lbl, st);
    }
}

/* Panodaki dosya yollarini bulundugun klasore yapistir (kes modunda tasi) */
static void do_paste(void) {
    const char *clip = arctian_clipboard_get();
    if (!clip || !clip[0]) {
        if (status_lbl) lv_label_set_text(status_lbl, "Panoda dosya yok");
        return;
    }
    char srcs[FM_UNDO_FILES][FM_PATH];
    int n = 0;
    const char *p = clip;
    while (*p && n < FM_UNDO_FILES) {
        char line[FM_PATH];
        int k = 0;
        while (*p && *p != '\n' && k < FM_PATH - 1) line[k++] = *p++;
        line[k] = 0;
        if (*p == '\n') p++;
        if (!line[0] || !fm_path_exists(line)) continue;   /* metin satiri: atla */
        scpy(srcs[n], FM_PATH, line);
        n++;
    }
    if (!n) {
        if (status_lbl) lv_label_set_text(status_lbl, "Panoda dosya yok");
        return;
    }
    fm_op_t op;
    op.type = s_cut ? 2 : 1;
    op.n = 0;
    for (int i = 0; i < n; i++) {
        char dname[FM_NAME];
        if (!fm_copy_one(srcs[i], cwd, dname, sizeof(dname))) continue;
        char dfull[FM_PATH];
        fm_full_in(cwd, dname, dfull, sizeof(dfull));
        scpy(op.src[op.n], FM_PATH, srcs[i]);
        scpy(op.dst[op.n], FM_PATH, dfull);
        op.n++;
        if (s_cut && A->fs->remove) A->fs->remove(srcs[i]);
    }
    if (s_cut) s_cut = 0;
    if (op.n > 0) push_op(&op);
    if (status_lbl) {
        char st[64];
        u2s(st, sizeof(st), (uint32_t)op.n);
        scat(st, sizeof(st), " öğe yapıştırıldı");
        lv_label_set_text(status_lbl, st);
    }
    refresh();
}

/* ---- 0.6: Ozellikler penceresi ----
 * Secili ogenin yolu, turu, boyutu ve izinlerini gosterir. Izinler `fs->stat`
 * uzerinden gelir (AFS mode alani; 0644 / 0755 gibi). */
static void props_close_cb(lv_event_t *e) {
    lv_obj_t *ov = (lv_obj_t *)lv_event_get_user_data(e);
    if (ov) lv_obj_delete(ov);
}

static void props_now_cb(lv_event_t *e) {
    (void)e;
}

static void props_show(void) {
    if (sel < 0 || sel >= ent_n) {
        if (status_lbl) lv_label_set_text(status_lbl, "Once bir oge secin");
        return;
    }
    fm_ent_t *en = &ents[sel];
    char full[FM_PATH];
    path_child(cwd, en->name, en->is_dir, full, sizeof(full));

    asi_stat_t st;
    int have = 0;
    if (A && A->fs && A->fs->stat) have = (A->fs->stat(full, &st) == 0);

    lv_obj_t *ov = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(ov);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0), 0);
    lv_obj_set_style_bg_opa(ov, 150, 0);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_align(ov, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *p = lv_obj_create(ov);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, 520, 320);
    lv_obj_center(p);
    lv_obj_set_style_bg_color(p, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, 14, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_pad_all(p, 18, 0);
    lv_obj_set_style_pad_row(p, 8, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(p);
    lv_label_set_text(t, "Ozellikler");
    lv_obj_set_style_text_color(t, lv_color_hex(ADL_TEXT), 0);

    const char *rows[4];
    char v0[FM_PATH + 8], v1[40], v2[64], v3[96];
    /* Ad */
    scpy(v1, sizeof(v1), en->name);
    /* Tur */
    if (en->is_dir) scpy(v2, sizeof(v2), "Klasor");
    else {
        const char *ex = ext_of(en->name);
        if (ex[0]) { scpy(v2, sizeof(v2), "Dosya ("); scat(v2, sizeof(v2), ex); scat(v2, sizeof(v2), ")"); }
        else scpy(v2, sizeof(v2), "Dosya");
    }
    /* Boyut */
    if (have && !st.is_dir) human(v3, sizeof(v3), st.size);
    else if (en->is_dir) scpy(v3, sizeof(v3), "-");
    else scpy(v3, sizeof(v3), "bilinmiyor");
    /* Izinler */
    char perm[32];
    if (have) {
        uint32_t m = st.mode & 0777u;
        perm[0] = '0';
        perm[1] = (char)('0' + ((m >> 6) & 7));
        perm[2] = (char)('0' + ((m >> 3) & 7));
        perm[3] = (char)('0' + (m & 7));
        perm[4] = 0;
    } else scpy(perm, sizeof(perm), "-");
    /* Yol */
    scpy(v0, sizeof(v0), full);

    const char *labels[5] = {"Ad", "Tur", "Boyut", "Izinler", "Yol"};
    const char *vals[5] = {v1, v2, v3, perm, v0};
    rows[0] = 0; (void)rows;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *r = lv_obj_create(p);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, LV_PCT(100), 26);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *l = lv_label_create(r);
        lv_label_set_text(l, labels[i]);
        lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT_DIM), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t *vl = lv_label_create(r);
        lv_label_set_text(vl, vals[i]);
        lv_label_set_long_mode(vl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(vl, 360);
        lv_obj_set_style_text_color(vl, lv_color_hex(ADL_TEXT), 0);
        lv_obj_align(vl, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, 110, 36);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(b, props_close_cb, LV_EVENT_CLICKED, ov);
    lv_obj_add_event_cb(ov, props_close_cb, LV_EVENT_CLICKED, ov);
    lv_obj_t *bl = lv_label_create(b);
    lv_label_set_text(bl, "Kapat");
    lv_obj_set_style_text_color(bl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(bl);
    (void)props_now_cb;
}

static void props_cb(lv_event_t *e) { (void)e; props_show(); }

/* 0.6: secili ogeyi yeniden adlandir. Yerel AFS rename kullanilir; dosya
 * icerigi kopyalanmaz, yalnizca ad degisir (buyuk dosyada aninda biter). */
static void rename_ok(const char *name) {
    char nm[FM_NAME];
    trim_name(nm, sizeof(nm), name);
    if (!nm[0] || sel < 0 || sel >= ent_n) return;
    if (seq(nm, ents[sel].name)) return;

    char from[FM_PATH], to[FM_PATH];
    path_child(cwd, ents[sel].name, ents[sel].is_dir, from, sizeof(from));
    path_child(cwd, nm, ents[sel].is_dir, to, sizeof(to));
    if (flat_exists(to)) {
        if (status_lbl) lv_label_set_text(status_lbl, "Bu adda bir oge zaten var");
        return;
    }

    int rc = -1;
    if (A && A->fs && A->fs->rename) rc = A->fs->rename(from, to);
    if (rc != 0) {
        if (status_lbl) lv_label_set_text(status_lbl, "Yeniden adlandirilamadi");
        return;
    }
    if (status_lbl) lv_label_set_text(status_lbl, "Yeniden adlandirildi");
    refresh();
}

static void rename_cb(lv_event_t *e) {
    (void)e;
    if (sel < 0 || sel >= ent_n) {
        if (status_lbl) lv_label_set_text(status_lbl, "Once bir oge secin");
        return;
    }
    char title[80];
    scpy(title, sizeof(title), "Yeniden adlandir: ");
    scat(title, sizeof(title), ents[sel].name);
    prompt_open(title, ents[sel].name, rename_ok);
}

/* Son islemi geri al: kopya = hedefi sil; tasi = geri kopyala + hedefi sil */
static void do_undo(void) {
    if (s_op_n <= 0) {
        if (status_lbl) lv_label_set_text(status_lbl, "Geri alınacak işlem yok");
        return;
    }
    if (!A || !A->fs) return;
    fm_op_t op = s_ops[--s_op_n];
    for (int i = 0; i < op.n; i++) {
        if (op.type == 1) {
            if (A->fs->remove) A->fs->remove(op.dst[i]);
        } else {
            /* tasi geri al: dst -> src yolu */
            char sdir[FM_PATH], snm[FM_NAME];
            fm_split(op.src[i], sdir, sizeof(sdir), snm, sizeof(snm));
            void *sh = fm_src_open(op.dst[i], snm, sizeof(snm));
            void *dh = sdir[0]
                       ? (A->fs->create_in ? A->fs->create_in(sdir, snm, 0) : 0)
                       : (A->fs->create ? A->fs->create(0, snm, 0) : 0);
            fm_copy_h(sh, dh);
            if (A->fs->remove) A->fs->remove(op.dst[i]);
        }
    }
    if (status_lbl) lv_label_set_text(status_lbl, "İşlem geri alındı");
    refresh();
}

/* Secili satiri hedef klasore tasi (surukle-birak) */
static void do_move_to(int i, const char *dst_dir) {
    if (!A || !A->fs || !A->fs->remove) return;
    if (seq(cwd, dst_dir)) return;
    char full[FM_PATH];
    path_child(cwd, ents[i].name, 0, full, sizeof(full));
    char dname[FM_NAME];
    if (!fm_copy_one(full, dst_dir, dname, sizeof(dname))) {
        if (status_lbl) lv_label_set_text(status_lbl, "Taşınamadı");
        return;
    }
    A->fs->remove(full);
    fm_op_t op;
    op.type = 2;
    op.n = 1;
    scpy(op.src[0], FM_PATH, full);
    fm_full_in(dst_dir, dname, op.dst[0], FM_PATH);
    push_op(&op);
    if (status_lbl) lv_label_set_text(status_lbl, "Taşındı");
    refresh();
}

/* Masaustu/CLI kisayollari: pencereye yonlendirilir      */
static void fm_hotkey(int hk) {
    if (hk == 3) clip_sel(0);        /* Ctrl+C kopyala */
    else if (hk == 4) clip_sel(1);   /* Ctrl+X kes */
    else if (hk == 5) do_paste();    /* Ctrl+V yapistir */
    else if (hk == 7) do_undo();     /* Ctrl+Z geri al */
}

/* Baska bir yerden bu pencereye birakilan dosya -> kopyala      */
static int fm_drop_cb(const char *src) {
    if (!src || !src[0] || !fm_path_exists(src)) return 0;
    char dname[FM_NAME];
    if (!fm_copy_one(src, cwd, dname, sizeof(dname))) return 0;
    char dfull[FM_PATH];
    fm_full_in(cwd, dname, dfull, sizeof(dfull));
    fm_op_t op;
    op.type = 1;
    op.n = 1;
    scpy(op.src[0], FM_PATH, src);
    scpy(op.dst[0], FM_PATH, dfull);
    push_op(&op);
    refresh();
    return 1;
}

/* ---- Arama kutusu + duzenlenebilir yol --------- */

static void search_changed_cb(lv_event_t *e) {
    (void)e;
    if (!s_search) return;
    scpy(s_filter, sizeof(s_filter), lv_textarea_get_text(s_search));
    if (!s_filter[0]) {
        /* filtre kalkti: keypad grubunu masaustuna geri birak */
        lv_indev_t *kp = lv_port_get_keypad();
        if (kp) lv_indev_set_group(kp, 0);
    }
    refresh();
}

static void search_click_cb(lv_event_t *e) {
    (void)e;
    if (!s_search) return;
    if (!g_group) g_group = lv_group_create();
    lv_group_remove_all_objs(g_group);
    lv_group_add_obj(g_group, s_search);
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, g_group);
    lv_group_focus_obj(s_search);
}

static void path_ok(const char *p) {
    char t[FM_PATH];
    int i = 0;
    if (p) while (p[i] == ' ') i++;
    scpy(t, FM_PATH, p ? p + i : "");
    int n = slen(t);
    while (n > 0 && t[n - 1] == ' ') t[--n] = 0;
    if (!t[0]) return;
    scpy(cwd, FM_PATH, t);
    sel = -1;
    refresh();
}

static void path_click_cb(lv_event_t *e) {
    (void)e;
    prompt_open("Yola Git", cwd, path_ok);
}


static void prompt_close(void) {
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, 0);
    if (g_prompt_ov) { lv_obj_delete(g_prompt_ov); g_prompt_ov = 0; }
    g_prompt_ta = 0;
    g_prompt_ok = 0;
}

static void prompt_ok_cb(lv_event_t *e) {
    (void)e;
    char buf[FM_PATH];      /* yol girişi uzun olabilir */
    const char *t = g_prompt_ta ? lv_textarea_get_text(g_prompt_ta) : "";
    if (!t || !t[0]) t = g_prompt_ta ? lv_textarea_get_placeholder_text(g_prompt_ta) : "";
    scpy(buf, FM_PATH, t ? t : "");
    void (*fn)(const char *) = g_prompt_ok;
    prompt_close();
    if (fn) fn(buf);
}

static void prompt_cancel_cb(lv_event_t *e) {
    (void)e;
    prompt_close();
}

static void prompt_open(const char *title, const char *def, void (*ok)(const char *)) {
    prompt_close();
    g_prompt_ok = ok;

    lv_obj_t *ov = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(ov);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ov, 150, 0);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_align(ov, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(ov);
    lv_obj_remove_style_all(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 14, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(panel, 460, 214);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *ti = lv_label_create(panel);
    lv_label_set_text(ti, title);
    lv_obj_set_style_text_color(ti, lv_color_hex(ADL_TEXT), 0);

    g_prompt_ta = lv_textarea_create(panel);
    lv_textarea_set_one_line(g_prompt_ta, true);
    lv_obj_set_width(g_prompt_ta, LV_PCT(100));
    lv_obj_set_height(g_prompt_ta, 42);
    lv_textarea_set_placeholder_text(g_prompt_ta, def ? def : "");

    lv_obj_t *hint = lv_label_create(panel);
    lv_label_set_text(hint, "Adı klavyeden yazın, sonra Tamam'a basın.");
    lv_obj_set_style_text_color(hint, lv_color_hex(ADL_TEXT_DIM), 0);

    lv_obj_t *rowc = lv_obj_create(panel);
    lv_obj_remove_style_all(rowc);
    lv_obj_set_width(rowc, LV_PCT(100));
    lv_obj_set_height(rowc, 40);
    lv_obj_set_flex_flow(rowc, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rowc, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(rowc, 10, 0);
    lv_obj_remove_flag(rowc, LV_OBJ_FLAG_SCROLLABLE);
    mk_btn(rowc, "İptal", 90, 36, ADL_SURFACE, prompt_cancel_cb, 0);
    mk_btn(rowc, "Tamam", 90, 36, ADL_ACCENT, prompt_ok_cb, 0);

    g_prompt_ov = ov;

    if (!g_group) g_group = lv_group_create();
    lv_group_remove_all_objs(g_group);
    lv_group_add_obj(g_group, g_prompt_ta);
    /* Enter da Tamam gibi onaylar (tek satir kutusunda READY olayi)     */
    lv_obj_add_event_cb(g_prompt_ta, prompt_ok_cb, LV_EVENT_READY, 0);
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, g_group);
    lv_group_focus_obj(g_prompt_ta);
}

static void prompt_newdir_cb(lv_event_t *e) {
    (void)e;
    prompt_open("Yeni Klasör", "yeni_klasor", newdir_ok);
}

static void prompt_newfile_cb(lv_event_t *e) {
    (void)e;
    prompt_open("Yeni Dosya", "yeni_dosya.txt", newfile_ok);
}

static void fm_del_cb(lv_event_t *e) {
    (void)e;
    prompt_close();
    fm_menu_close();
    if (input_timer) { lv_timer_delete(input_timer); input_timer = 0; }
    if (relayout_timer) { lv_timer_delete(relayout_timer); relayout_timer = 0; }
    if (fm_font) { lv_tiny_ttf_destroy(fm_font); fm_font = 0; }
    if (fm_font_small) { lv_tiny_ttf_destroy(fm_font_small); fm_font_small = 0; }
    if (fm_font_bold) { lv_tiny_ttf_destroy(fm_font_bold); fm_font_bold = 0; }
    path_lbl = 0;
    crumb_box = 0;
    table_head = 0;
    list_box = 0;
    ui_tb = 0;
    back_btn = 0;
    context_menu = 0;
    context_backdrop = 0;
    fm_body = 0;
    ui_side = 0;
    ui_det = 0;
    ui_st = 0;
    status_lbl = 0;
    det_name = 0;
    det_meta = 0;
    det_prev = 0;
    det_hint = 0;
    head_name = 0;
    head_count = 0;
    for (int i = 0; i < FM_NAV; i++) nav_btns[i] = 0;
    s_search = 0;
    s_filter[0] = 0;
    s_drag_i = -1;
    s_drag_on = 0;
    s_cut = 0;
    s_empty_lbl = 0;
    s_empty_btn = 0;
    s_history_n = 0;
    s_selected_n = 0;
    s_usb_poll_ticks = 0;
    s_prev_right = 0;
    s_prev_left = 0;
    sel = -1;
    s_last_sel = -1;
    ent_n = 0;
    A = 0;
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    A = asi;
    fm_body = win;
    fm_font = lv_tiny_ttf_create_data(font_plex, font_plex_len, 14);
    fm_font_small = lv_tiny_ttf_create_data(font_plex, font_plex_len, 12);
    fm_font_bold = lv_tiny_ttf_create_data(font_plex_sb, font_plex_sb_len, 16);
    lv_obj_t *shell = lv_obj_get_parent(win);
    /* Pencere yeni olusturuldu: koordinatlar tazelenmeden olculurse govde
     * eski/varsayilan boyutla yerlesir (ilk acilista 820x500 hatasi). */
    if (shell) lv_obj_update_layout(shell);
    if (shell) {
        lv_obj_set_style_bg_color(shell, lv_color_hex(FM_WINDOW), 0);
        lv_obj_set_style_bg_opa(shell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(shell, 1, 0);
        lv_obj_set_style_border_color(shell, lv_color_hex(FM_BORDER), 0);
        lv_obj_set_style_radius(shell, 6, 0);
        lv_obj_t *titlebar = lv_obj_get_child(shell, 0);
        if (titlebar) {
            lv_obj_set_size(titlebar, lv_obj_get_width(shell) - 2, 40);
            lv_obj_align(titlebar, LV_ALIGN_TOP_MID, 0, 1);
            lv_obj_set_style_bg_color(titlebar, lv_color_hex(FM_WINDOW), 0);
            lv_obj_set_style_border_width(titlebar, 1, 0);
            lv_obj_set_style_border_side(titlebar, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(titlebar, lv_color_hex(FM_DIVIDER), 0);
            lv_obj_t *title = lv_obj_get_child(titlebar, 0);
            if (title) {
                lv_obj_align(title, LV_ALIGN_LEFT_MID, 14, 0);
                lv_obj_set_style_text_color(title, lv_color_hex(FM_TEXT), 0);
                fm_style_font(title, fm_font);
            }
            /* Kontrol butonlari WM tarafindan SVG ikonlarla cizilir
             * (windows.c add_ctrl). Burada yalnizca boyut/hizalari
             * duzenlenir; metin sembolu EKLENMEZ. */
            for (int i = 1; i <= 3; i++) {
                lv_obj_t *control = lv_obj_get_child(titlebar, i);
                if (!control) continue;
                lv_obj_set_size(control, 28, 26);
                lv_obj_align(control, LV_ALIGN_RIGHT_MID, -8 - (i - 1) * 32, 0);
                lv_obj_t *icon = lv_obj_get_child(control, 0);
                if (icon) lv_obj_center(icon);
            }
        }
        lv_obj_set_pos(win, 2, 40);
        lv_obj_set_size(win, lv_obj_get_width(shell) - 4, lv_obj_get_height(shell) - 40);
    }
    if (shell) {
        lv_obj_t *titlebar = lv_obj_get_child(shell, 0);
        lv_obj_t *title = titlebar ? lv_obj_get_child(titlebar, 0) : 0;
        if (title) fm_style_font(title, fm_font);
    }
    sel = -1;
    s_last_sel = -1;
    s_selected_n = 0;
    s_history_n = 0;
    s_sort_count = 0;
    s_sort_desc = 0;
    s_usb_poll_ticks = 0;
    s_prev_right = 0;
    s_prev_left = 0;
    s_op_n = 0;
    s_cut = 0;
    s_filter[0] = 0;
    ent_n = 0;
    scpy(cwd, FM_PATH, "C:");

    build_ui(win);
    arctian_shell_set_relayout(win, fm_relayout);   /* Yeniden akis */
    /* Ilk acilista govde boyutu tazelenmeden olculmustu (820x500 hatasi):
     * boyut olayi + 300 ms sonraki tek seferlik timer ile duzeltilir. */
    lv_obj_add_event_cb(win, fm_resize_cb, LV_EVENT_SIZE_CHANGED, 0);
    if (relayout_timer) lv_timer_delete(relayout_timer);
    relayout_timer = lv_timer_create(fm_relayout_timer_cb, 300, 0);
    lv_timer_set_repeat_count(relayout_timer, 1);
    /* Klavye kisayollari + surukle-birak hedefi     */
    arctian_shell_set_hotkey(win, fm_hotkey);
    arctian_shell_set_drop(win, fm_drop_cb);

    lv_obj_add_event_cb(win, fm_del_cb, LV_EVENT_DELETE, 0);
    fm_update_toolbar();
    fm_sync_usb();
    refresh();
    input_timer = lv_timer_create(fm_input_poll_cb, 25, 0);
}
