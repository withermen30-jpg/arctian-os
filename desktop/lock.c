#include "app.h"
#include "asi.h"
#include "lv_port.h"
#include "lock.h"
#include <stdint.h>

extern const unsigned char font_anton[];
extern const unsigned int  font_anton_len;
extern const unsigned char font_abril[];
extern const unsigned int  font_abril_len;
extern const unsigned char font_serif[];
extern const unsigned int  font_serif_len;

#define LC_TEXT   0x000000
#define LC_HEAD   0x6D8293
#define LC_PILL   0xBFD3E0
#define LC_PLACE  0x5C707E
#define LC_ERROR  0xB0452F

#define LC_CLK_OPA 204
#define LC_PILL_OPA 171

enum {
    ST_CREATE = 0,
    ST_CREATE_PW,
    ST_SELECT,
    ST_PASSWORD
};

static asi_t      *A;
static lv_font_t  *F_CLOCK, *F_HEAD, *F_NAME, *F_FIELD, *F_BTN;
static lv_obj_t   *s_root, *s_clock;
static lv_group_t *s_group;
static int         s_step;
static int         s_done;
static int         s_rebuild;
static lv_obj_t   *s_ta_name, *s_ta_pw1, *s_ta_pw2, *s_ta_login;
static char        s_pending[32];
static char        s_target[32];
static const char *s_error;

static int streq(const char *a, const char *b) {
    int i = 0;
    if (!a || !b) return 0;
    for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}

static void read_ta(lv_obj_t *ta, char *buf, int cap) {
    buf[0] = 0;
    if (!ta) return;
    const char *t = lv_textarea_get_text(ta);
    if (!t) return;
    int i = 0;
    for (; t[i] && i < cap - 1; i++) buf[i] = t[i];
    buf[i] = 0;
}

static void buf_free_cb(lv_event_t *e) {
    void *p = lv_event_get_user_data(e);
    if (p && A && A->mem && A->mem->free) A->mem->free(p);
}

static void update_clock(void) {
    if (!s_clock) return;
    /* Saat etiketi saniyede bir yeterli; her karede RTC okumak TCG'de
     * kareyi yavaslatiyordu (kilit ekrani donuk gorunuyordu). */
    static uint64_t last_ms;
    uint64_t now = (A && A->time && A->time->ticks_ms) ? A->time->ticks_ms() : 0;
    if (last_ms && now - last_ms < 500u) return;
    last_ms = now;

    asi_datetime_t dt;
    if (!A || !A->time || !A->time->now || !A->time->now(&dt)) return;
    char b[8];
    b[0] = (char)('0' + (dt.hour / 10) % 10);
    b[1] = (char)('0' + dt.hour % 10);
    b[2] = ':';
    b[3] = (char)('0' + (dt.minute / 10) % 10);
    b[4] = (char)('0' + dt.minute % 10);
    b[5] = 0;
    static char prev[8];
    if (prev[0] == b[0] && prev[1] == b[1] && prev[2] == b[2] &&
        prev[3] == b[3] && prev[4] == b[4]) return;
    for (int i = 0; i < 8; i++) prev[i] = b[i];
    lv_label_set_text(s_clock, b);
}

static lv_obj_t *add_label(lv_obj_t *parent, const char *txt, lv_font_t *f,
                           uint32_t col, lv_opa_t opa) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    if (opa < LV_OPA_COVER) lv_obj_set_style_text_opa(l, opa, 0);
    return l;
}

static lv_obj_t *make_arrow(lv_obj_t *parent, int w, int h, uint32_t col) {
    uint32_t *buf = (uint32_t *)A->mem->alloc((size_t)w * h * 4u);
    if (!buf) return 0;
    for (int i = 0; i < w * h; i++) buf[i] = 0;
    int hx0 = w * 58 / 100;
    int sh0 = h * 30 / 100, sh1 = h * 70 / 100;
    for (int y = sh0; y < sh1; y++)
        for (int x = 0; x < hx0; x++) buf[y * w + x] = col;
    for (int y = 0; y < h; y++) {
        int dist = y * 2 - h;
        if (dist < 0) dist = -dist;
        int xmax = hx0 + (int)((long)(w - hx0) * (h - dist) / h);
        if (xmax > w) xmax = w;
        for (int x = hx0; x < xmax; x++) buf[y * w + x] = col;
    }
    lv_obj_t *c = lv_canvas_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(c, buf, w, h, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_add_event_cb(c, buf_free_cb, LV_EVENT_DELETE, buf);
    return c;
}

static void on_field_click(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_user_data(e);
    if (!s_group || !ta) return;
    if (lv_obj_get_group(ta) != s_group) lv_group_add_obj(s_group, ta);
    lv_group_focus_obj(ta);
}

static void on_field_state(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    lv_opa_t opa = (lv_event_get_code(e) == LV_EVENT_FOCUSED) ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_bg_opa(ta, opa, LV_PART_CURSOR);
}

static lv_obj_t *make_field(lv_obj_t *parent, int w, int h,
                            const char *placeholder, int password, lv_obj_t **ta_out) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, w, h);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(box, lv_color_hex(LC_PILL), 0);
    lv_obj_set_style_bg_opa(box, LC_PILL_OPA, 0);
    lv_obj_set_style_radius(box, h / 2, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ta = lv_textarea_create(box);
    lv_obj_remove_style_all(ta);
    lv_obj_remove_flag(ta, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ta, w, h);
    lv_obj_set_pos(ta, 0, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_pad_left(ta, 22, 0);
    lv_obj_set_style_pad_right(ta, 22, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(LC_TEXT), 0);
    lv_obj_set_style_text_font(ta, F_FIELD, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(LC_PLACE), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_font(ta, F_FIELD, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta, lv_color_hex(LC_TEXT), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_width(ta, 0, LV_PART_CURSOR);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 31);
    if (placeholder) lv_textarea_set_placeholder_text(ta, placeholder);
    if (password) lv_textarea_set_password_mode(ta, true);
    lv_obj_remove_flag(ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ta, on_field_state, LV_EVENT_FOCUSED, 0);
    lv_obj_add_event_cb(ta, on_field_state, LV_EVENT_DEFOCUSED, 0);
    lv_obj_add_event_cb(box, on_field_click, LV_EVENT_CLICKED, ta);
    if (ta_out) *ta_out = ta;
    return box;
}

static void on_btn(lv_event_t *e);

static lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, int w, int h, int id) {
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(b, lv_color_hex(LC_PILL), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = add_label(b, txt, F_BTN, LC_TEXT, LV_OPA_COVER);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, on_btn, LV_EVENT_CLICKED, (void *)(intptr_t)id);
    return b;
}

static void screen_begin(const char *heading) {
    lv_obj_clean(s_root);
    s_ta_name = s_ta_pw1 = s_ta_pw2 = s_ta_login = 0;
    int W = A->gfx->width(), H = A->gfx->height();

    s_clock = add_label(s_root, "", F_CLOCK, LC_TEXT, LC_CLK_OPA);
    lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, H * 11 / 100);
    update_clock();

    if (heading) {
        lv_obj_t *h = add_label(s_root, heading, F_HEAD, LC_HEAD, LV_OPA_COVER);
        lv_obj_align(h, LV_ALIGN_TOP_MID, 0, H * 36 / 100);
    }

    lv_group_remove_all_objs(s_group);
}

static void focus_field(lv_obj_t *ta) {
    if (!ta) return;
    lv_group_add_obj(s_group, ta);
    lv_group_focus_obj(ta);
}

static void focus_pair(lv_obj_t *a, lv_obj_t *b) {
    if (a) lv_group_add_obj(s_group, a);
    if (b) lv_group_add_obj(s_group, b);
    if (a) lv_group_focus_obj(a);
}

static void do_action(int id) {
    if (id >= 10) {
        int i = id - 10;
        if (i < 0 || i >= arctian_auth_user_count()) return;
        const char *nm = arctian_auth_user_name(i);
        int k = 0; for (; nm[k] && k < 31; k++) s_target[k] = nm[k];
        s_target[k] = 0;
        if (arctian_auth_user_has_password(i)) {
            s_step = ST_PASSWORD;
            s_error = 0;
            s_rebuild = 1;
        } else {
            arctian_auth_set_current(s_target);
            s_done = 1;
        }
        return;
    }
    switch (id) {
        case 1:
            read_ta(s_ta_name, s_pending, sizeof(s_pending));
            if (!s_pending[0]) { s_error = "Kullanıcı adı gerekli"; s_rebuild = 1; break; }
            s_error = 0;
            s_step = ST_CREATE_PW;
            s_rebuild = 1;
            break;
        case 2:
        case 3: {
            int first = (arctian_auth_user_count() == 0);
            const char *pw = 0;
            char p1[40];
            if (id == 2) {
                read_ta(s_ta_name, s_pending, sizeof(s_pending));
            } else {
                read_ta(s_ta_pw1, p1, sizeof(p1));
                char p2[40];
                read_ta(s_ta_pw2, p2, sizeof(p2));
                if (!p1[0]) { s_error = "Parola gerekli"; s_rebuild = 1; break; }
                if (!streq(p1, p2)) { s_error = "Parolalar eşleşmiyor"; s_rebuild = 1; break; }
                pw = p1;
            }
            if (!s_pending[0]) { s_error = "Kullanıcı adı gerekli"; s_rebuild = 1; break; }
            int r = arctian_auth_create_user(s_pending, pw);
            if (r == 0) {
                s_error = 0;
                if (first) { arctian_auth_set_current(s_pending); s_done = 1; }
                else { s_step = ST_SELECT; s_rebuild = 1; }
            } else if (r == -6) { s_error = "Bu ad zaten var"; s_rebuild = 1; }
            else { s_error = "Hesap oluşturulamadı"; s_rebuild = 1; }
            break;
        }
        case 5: {
            char p[40];
            read_ta(s_ta_login, p, sizeof(p));
            if (arctian_auth_verify_user(s_target, p)) {
                arctian_auth_set_current(s_target);
                s_done = 1;
            } else {
                s_error = "Parola hatalı";
                s_rebuild = 1;
            }
            break;
        }
        case 6:
            s_pending[0] = 0;
            s_target[0] = 0;
            s_error = 0;
            s_step = ST_CREATE;
            s_rebuild = 1;
            break;
        default: break;
    }
}

static void on_btn(lv_event_t *e) {
    do_action((int)(intptr_t)lv_event_get_user_data(e));
}

static void on_field_ready(lv_event_t *e) {
    do_action((int)(intptr_t)lv_event_get_user_data(e));
}

static void error_label(int y) {
    if (!s_error) return;
    lv_obj_t *l = add_label(s_root, s_error, F_FIELD, LC_ERROR, LV_OPA_COVER);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
}

static void build_create(void) {
    screen_begin("Hesap oluşturun");
    int H = A->gfx->height();
    s_ta_name = 0;
    lv_obj_t *fb = make_field(s_root, 360, 64, "Kullanıcı adı", 0, &s_ta_name);
    lv_obj_align(fb, LV_ALIGN_TOP_MID, 0, H * 48 / 100);

    lv_obj_t *q = add_label(s_root, "Şifre oluşturulsun mu?", F_HEAD, LC_HEAD, LV_OPA_COVER);
    lv_obj_align(q, LV_ALIGN_TOP_MID, 0, H * 59 / 100);

    lv_obj_t *row = lv_obj_create(s_root);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 20, 0);
    lv_obj_set_size(row, 360, 60);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, H * 66 / 100);
    make_btn(row, "Evet", 140, 56, 1);
    make_btn(row, "Hayır", 140, 56, 2);

    error_label(H * 75 / 100);
    focus_field(s_ta_name);
}

static void build_create_pw(void) {
    screen_begin("Şifre oluşturun");
    int H = A->gfx->height();
    s_ta_pw1 = 0; s_ta_pw2 = 0;
    lv_obj_t *fb1 = make_field(s_root, 360, 64, "Parola", 1, &s_ta_pw1);
    lv_obj_align(fb1, LV_ALIGN_TOP_MID, 0, H * 48 / 100);
    lv_obj_t *fb2 = make_field(s_root, 360, 64, "Parola tekrar", 1, &s_ta_pw2);
    lv_obj_align(fb2, LV_ALIGN_TOP_MID, 0, H * 58 / 100);

    lv_obj_t *b = make_btn(s_root, "Oluştur", 200, 60, 3);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, H * 70 / 100);

    error_label(H * 80 / 100);
    focus_pair(s_ta_pw1, s_ta_pw2);
}

static lv_obj_t *make_pill(lv_obj_t *parent, const char *nm, int id, int hgt) {
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, hgt);
    lv_obj_set_style_bg_color(pill, lv_color_hex(LC_PILL), 0);
    lv_obj_set_style_bg_opa(pill, LC_PILL_OPA, 0);
    lv_obj_set_style_radius(pill, hgt / 2, 0);
    lv_obj_set_style_pad_left(pill, 40, 0);
    lv_obj_set_style_pad_right(pill, 26, 0);
    lv_obj_set_style_pad_column(pill, 20, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    add_label(pill, nm && nm[0] ? nm : "kullanici", F_NAME, LC_TEXT, LV_OPA_COVER);
    make_arrow(pill, 52, 34, 0xFF000000u);
    lv_obj_add_event_cb(pill, on_btn, LV_EVENT_CLICKED, (void *)(intptr_t)id);
    return pill;
}

static void build_select(void) {
    screen_begin("Lütfen hesap seçiniz.");
    int H = A->gfx->height();
    int n = arctian_auth_user_count();

    lv_obj_t *col = lv_obj_create(s_root);
    lv_obj_remove_style_all(col);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 12, 0);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(col, LV_ALIGN_TOP_MID, 0, H * 43 / 100);

    if (n <= 0) {
        add_label(col, "Hesap yok", F_NAME, LC_TEXT, LV_OPA_COVER);
    } else {
        int ph = (n > 3) ? 60 : 84;
        for (int i = 0; i < n; i++)
            make_pill(col, arctian_auth_user_name(i), 10 + i, ph);
    }

    lv_obj_t *nb = make_btn(s_root, "Yeni hesap", 200, 54, 6);
    lv_obj_align(nb, LV_ALIGN_BOTTOM_MID, 0, -H * 11 / 100);
}

static void build_password(void) {
    screen_begin("Lütfen giriş yapın.");
    int H = A->gfx->height();

    s_ta_login = 0;
    lv_obj_t *fb = make_field(s_root, 360, 64, "Parola", 1, &s_ta_login);
    lv_obj_align(fb, LV_ALIGN_TOP_MID, 0, H * 50 / 100);
    lv_obj_add_event_cb(s_ta_login, on_field_ready, LV_EVENT_READY, (void *)(intptr_t)5);

    lv_obj_t *b = make_btn(s_root, "Giriş", 200, 60, 5);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, H * 62 / 100);

    error_label(H * 72 / 100);
    focus_field(s_ta_login);
}

void lock_screen_run(asi_t *asi) {
    if (!asi || !asi->gfx || !asi->mem) return;
#ifdef ARCTIAN_AUTOUSER
    if (arctian_auth_has_user() && arctian_auth_current()[0]) return;
#endif
    A = asi;

    F_CLOCK = lv_tiny_ttf_create_data(font_anton, font_anton_len, 120);
    F_HEAD  = lv_tiny_ttf_create_data(font_abril, font_abril_len, 30);
    F_BTN   = lv_tiny_ttf_create_data(font_abril, font_abril_len, 24);
    F_NAME  = lv_tiny_ttf_create_data(font_serif, font_serif_len, 44);
    F_FIELD = lv_tiny_ttf_create_data(font_serif, font_serif_len, 26);

    int W = asi->gfx->width(), H = asi->gfx->height();
    s_root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, W, H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_group = lv_group_create();
    lv_indev_t *kp = lv_port_get_keypad();
    lv_indev_t *ptr = lv_port_get_indev();
    if (kp) lv_indev_set_group(kp, s_group);
    if (ptr) lv_indev_set_group(ptr, s_group);

    s_done = 0;
    s_error = 0;
    s_step = arctian_auth_has_user() ? ST_SELECT : ST_CREATE;

    while (!s_done) {
        s_rebuild = 0;
        if (s_step == ST_CREATE)         build_create();
        else if (s_step == ST_CREATE_PW) build_create_pw();
        else if (s_step == ST_PASSWORD)  build_password();
        else                             build_select();

        while (!s_done && !s_rebuild) {
            update_clock();
            lv_timer_handler();
            {
                int dx, dy, dw, dh;
                if (lv_port_take_dirty(&dx, &dy, &dw, &dh) && dw > 0 && dh > 0)
                    asi->gfx->present_rect(dx, dy, dw, dh);
            }
            asm volatile("hlt");
        }
    }

    if (kp) lv_indev_set_group(kp, 0);
    if (ptr) lv_indev_set_group(ptr, 0);
    lv_group_remove_all_objs(s_group);
    lv_obj_delete(s_root);
    s_root = 0;
    s_clock = 0;

    if (F_CLOCK) lv_tiny_ttf_destroy(F_CLOCK);
    if (F_HEAD)  lv_tiny_ttf_destroy(F_HEAD);
    if (F_BTN)   lv_tiny_ttf_destroy(F_BTN);
    if (F_NAME)  lv_tiny_ttf_destroy(F_NAME);
    if (F_FIELD) lv_tiny_ttf_destroy(F_FIELD);
    F_CLOCK = F_HEAD = F_BTN = F_NAME = F_FIELD = 0;
}

static lv_obj_t *s_ta_disk;
static lv_obj_t *s_disk_errlbl;
static int       s_disk_done;

static void disk_set_err(const char *s) {
    if (s_disk_errlbl) lv_label_set_text(s_disk_errlbl, s ? s : "");
}

static void disk_submit(void) {
    char p[64];
    read_ta(s_ta_disk, p, sizeof(p));
    if (!p[0]) { disk_set_err("Parola gerekli"); return; }
    if (!A->crypt || !A->crypt->unlock) { disk_set_err("Sifreleme servisi yok"); return; }
    if (A->crypt->unlock(p) == 0) {
        s_disk_done = 1;
    } else {
        disk_set_err("Parola hatalı, tekrar deneyin");
        if (s_ta_disk) lv_textarea_set_text(s_ta_disk, "");
    }
}

static void disk_click_cb(lv_event_t *e) { (void)e; disk_submit(); }
static void disk_ready_cb(lv_event_t *e) { (void)e; disk_submit(); }

static void build_disk(void) {
    screen_begin("Disk parolası");
    int H = A->gfx->height();
    s_ta_disk = 0;
    lv_obj_t *fb = make_field(s_root, 360, 64, "Disk parolası", 1, &s_ta_disk);
    lv_obj_align(fb, LV_ALIGN_TOP_MID, 0, H * 50 / 100);
    lv_obj_add_event_cb(s_ta_disk, disk_ready_cb, LV_EVENT_READY, 0);

    lv_obj_t *b = make_btn(s_root, "Aç", 200, 60, 777);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, H * 62 / 100);
    lv_obj_add_event_cb(b, disk_click_cb, LV_EVENT_CLICKED, 0);

    s_disk_errlbl = add_label(s_root, "", F_FIELD, LC_ERROR, LV_OPA_COVER);
    lv_obj_align(s_disk_errlbl, LV_ALIGN_TOP_MID, 0, H * 72 / 100);

    focus_field(s_ta_disk);
}

void disk_unlock_run(asi_t *asi) {
    if (!asi || !asi->gfx || !asi->mem || !asi->crypt) return;
    if (!asi->crypt->present() || asi->crypt->unlocked()) return;
    A = asi;

    F_CLOCK = lv_tiny_ttf_create_data(font_anton, font_anton_len, 120);
    F_HEAD  = lv_tiny_ttf_create_data(font_abril, font_abril_len, 30);
    F_BTN   = lv_tiny_ttf_create_data(font_abril, font_abril_len, 24);
    F_NAME  = lv_tiny_ttf_create_data(font_serif, font_serif_len, 44);
    F_FIELD = lv_tiny_ttf_create_data(font_serif, font_serif_len, 26);

    int W = asi->gfx->width(), H = asi->gfx->height();
    s_root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, W, H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_group = lv_group_create();
    lv_indev_t *kp = lv_port_get_keypad();
    lv_indev_t *ptr = lv_port_get_indev();
    if (kp) lv_indev_set_group(kp, s_group);
    if (ptr) {
        lv_indev_set_group(ptr, s_group);
        lv_obj_t *cur = lv_indev_get_cursor(ptr);
        if (cur) lv_obj_move_foreground(cur);
    }

    s_disk_done = 0;
    s_ta_disk = 0;
    s_disk_errlbl = 0;
    build_disk();

    while (!s_disk_done) {
        update_clock();
        lv_timer_handler();
        {
            int dx, dy, dw, dh;
            if (lv_port_take_dirty(&dx, &dy, &dw, &dh) && dw > 0 && dh > 0)
                asi->gfx->present_rect(dx, dy, dw, dh);
        }
    }

    if (kp) lv_indev_set_group(kp, 0);
    if (ptr) lv_indev_set_group(ptr, 0);
    lv_group_remove_all_objs(s_group);
    lv_obj_delete(s_root);
    s_root = 0;
    s_clock = 0;
    s_ta_disk = 0;
    s_disk_errlbl = 0;

    if (F_CLOCK) lv_tiny_ttf_destroy(F_CLOCK);
    if (F_HEAD)  lv_tiny_ttf_destroy(F_HEAD);
    if (F_BTN)   lv_tiny_ttf_destroy(F_BTN);
    if (F_NAME)  lv_tiny_ttf_destroy(F_NAME);
    if (F_FIELD) lv_tiny_ttf_destroy(F_FIELD);
    F_CLOCK = F_HEAD = F_BTN = F_NAME = F_FIELD = 0;
}

