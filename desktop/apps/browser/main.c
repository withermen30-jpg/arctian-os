#include "app.h"
#include "lv_port.h"
#include "adl_wolf.h"
#include <stdint.h>

#define MAX_BLOCKS     240
#define BLOCK_TEXT_MAX 220
#define HREF_MAX       220
#define NAME_MAX       48
#define URL_MAX        300
#define PAGE_RAW_MAX   260000
#define HIST_MAX       20
#define PARSE_GUARD    500000L
#define MAX_FORMS      6
#define MAX_SELECT_OPT 10

typedef enum {
    BLK_TEXT = 0,
    BLK_H1,
    BLK_H2,
    BLK_H3,
    BLK_LINK,
    BLK_LIST_ITEM,
    BLK_IMG,
    BLK_HR,
    BLK_FORM_TEXT,
    BLK_FORM_HIDDEN,
    BLK_FORM_SELECT,
    BLK_FORM_SUBMIT
} block_type_t;

typedef struct {
    block_type_t type;
    char text[BLOCK_TEXT_MAX];
    char href[HREF_MAX];
    char name[NAME_MAX];
    int  form_id;
    lv_obj_t *widget;
} block_t;

typedef struct {
    char action[HREF_MAX];
    int  used;
} form_t;

static block_t g_blocks[MAX_BLOCKS];
static int     g_block_count;

static form_t  g_forms[MAX_FORMS];
static int     g_form_count;

static char g_raw[PAGE_RAW_MAX];

static char g_cur_url[URL_MAX];
static char g_cur_host[160];
static char g_cur_scheme[8];

static char g_history[HIST_MAX][URL_MAX];
static int  g_hist_count;

static lv_obj_t   *g_addr;
static lv_obj_t   *g_status;
static lv_obj_t   *g_content;
static lv_group_t *g_group;
static asi_t      *g_basi;

static char          g_cur[BLOCK_TEXT_MAX];
static int            g_cur_len;
static int            g_last_space;
static block_type_t   g_cur_type;
static int             g_in_anchor;
static char           g_cur_href[HREF_MAX];
static int             g_cur_form;
static int             g_cell_in_row;

static int  g_in_select;
static char g_sel_name[NAME_MAX];
static char g_sel_labels[BLOCK_TEXT_MAX];
static char g_sel_values[HREF_MAX];
static int  g_sel_opt_count;
static int  g_in_option;
static char g_opt_value_attr[64];
static char g_opt_label[64];
static int  g_opt_label_len;

static void render_blocks(void);
static void set_status(const char *text, uint32_t color);
static void navigate(const char *url_in, int record_history);
static void submit_form(int form_id, const char *fallback_action);

static int starts_with(const char *s, const char *p) {
    while (*p) { if (*s != *p) return 0; s++; p++; }
    return 1;
}

static char lower_c(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_unreserved(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static int str_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (lower_c(*a) != lower_c(*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static void bcopy_str(char *dst, const char *src, int dstsz) {
    int i = 0;
    if (!src) { dst[0] = 0; return; }
    while (src[i] && i < dstsz - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void bcat(char *dst, const char *src, int dstsz) {
    int i = 0;
    while (dst[i]) i++;
    int j = 0;
    while (src[j] && i < dstsz - 1) { dst[i++] = src[j++]; }
    dst[i] = 0;
}

static int b_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static const char *find_substr_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && lower_c(*h) == lower_c(*n)) { h++; n++; }
        if (!*n) return hay;
    }
    return 0;
}

static const char HEXD[] = "0123456789ABCDEF";
static void url_encode_append(char *dst, int dstsz, const char *src) {
    int i = b_len(dst);
    for (; *src && i < dstsz - 4; src++) {
        unsigned char c = (unsigned char)*src;
        if (is_unreserved((char)c)) {
            dst[i++] = (char)c;
        } else if (c == ' ') {
            dst[i++] = '+';
        } else {
            dst[i++] = '%';
            dst[i++] = HEXD[(c >> 4) & 0xF];
            dst[i++] = HEXD[c & 0xF];
        }
    }
    dst[i] = 0;
}

static void get_tag_name(const char *p, char *out, int outsz) {
    int n = 0;
    if (*p == '/') p++;
    while (*p && is_alpha(*p) && n < outsz - 1) { out[n++] = lower_c(*p); p++; }
    out[n] = 0;
}

static int find_attr(const char *tag_start, const char *tag_end,
                      const char *attr, char *out, int outsz) {
    const char *p = tag_start;
    int alen = 0;
    while (attr[alen]) alen++;
    while (p < tag_end) {
        int match = 1;
        for (int i = 0; i < alen; i++) {
            if (p + i >= tag_end || lower_c(p[i]) != attr[i]) { match = 0; break; }
        }
        if (match && p > tag_start && (is_alpha(p[-1]) || p[-1] == '-')) match = 0;
        if (match) {
            const char *q = p + alen;
            while (q < tag_end && (*q == ' ' || *q == '\t')) q++;
            if (q < tag_end && *q == '=') {
                q++;
                while (q < tag_end && (*q == ' ' || *q == '\t')) q++;
                char quote = 0;
                if (q < tag_end && (*q == '"' || *q == '\'')) { quote = *q; q++; }
                int n = 0;
                while (q < tag_end && n < outsz - 1) {
                    if (quote) { if (*q == quote) break; }
                    else { if (*q == ' ' || *q == '>' || *q == '\t') break; }
                    out[n++] = *q++;
                }
                out[n] = 0;
                return 1;
            }
            out[0] = 0;
            return 1;
        }
        p++;
    }
    out[0] = 0;
    return 0;
}

static void parse_url(const char *url, char *scheme_out, int scheme_sz,
                       char *host_out, int host_sz) {
    const char *p = url;
    int i = 0;
    while (*p && *p != ':' && i < scheme_sz - 1) { scheme_out[i++] = *p; p++; }
    scheme_out[i] = 0;
    if (starts_with(p, "://")) p += 3;
    i = 0;
    while (*p && *p != '/' && i < host_sz - 1) { host_out[i++] = *p; p++; }
    host_out[i] = 0;
}

static void resolve_url(const char *scheme, const char *host,
                         const char *href, char *out, int outsz) {
    out[0] = 0;
    if (!href || !href[0] || href[0] == '#') return;
    if (starts_with(href, "http://") || starts_with(href, "https://")) {
        bcopy_str(out, href, outsz);
        return;
    }
    if (href[0] == '/' && href[1] == '/') {
        bcopy_str(out, scheme, outsz); bcat(out, ":", outsz); bcat(out, href, outsz);
        return;
    }
    if (href[0] == '/') {
        bcopy_str(out, scheme, outsz); bcat(out, "://", outsz);
        bcat(out, host, outsz); bcat(out, href, outsz);
        return;
    }
    bcopy_str(out, scheme, outsz); bcat(out, "://", outsz);
    bcat(out, host, outsz); bcat(out, "/", outsz); bcat(out, href, outsz);
}

static block_t *push_block_full(block_type_t type, const char *text, const char *href,
                                 const char *name, int form_id) {
    if (g_block_count >= MAX_BLOCKS) return 0;
    block_t *b = &g_blocks[g_block_count];
    b->type = type;
    bcopy_str(b->text, text, BLOCK_TEXT_MAX);
    bcopy_str(b->href, href ? href : "", HREF_MAX);
    bcopy_str(b->name, name ? name : "", NAME_MAX);
    b->form_id = form_id;
    b->widget = 0;
    g_block_count++;
    return b;
}

static void push_block(block_type_t type, const char *text, const char *href) {
    push_block_full(type, text, href, "", -1);
}

static void append_char(char c) {
    if (c == ' ') {
        if (g_last_space) return;
        g_last_space = 1;
    } else {
        g_last_space = 0;
    }
    if (g_cur_len < BLOCK_TEXT_MAX - 1) g_cur[g_cur_len++] = c;
}

static void append_utf8(uint32_t cp) {
    if (cp == 0) return;
    if (cp < 0x80) { append_char((char)cp); return; }
    if (cp < 0x800) {
        append_char((char)(0xC0 | (cp >> 6)));
        append_char((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        append_char((char)(0xE0 | (cp >> 12)));
        append_char((char)(0x80 | ((cp >> 6) & 0x3F)));
        append_char((char)(0x80 | (cp & 0x3F)));
    } else {
        append_char((char)(0xF0 | (cp >> 18)));
        append_char((char)(0x80 | ((cp >> 12) & 0x3F)));
        append_char((char)(0x80 | ((cp >> 6) & 0x3F)));
        append_char((char)(0x80 | (cp & 0x3F)));
    }
}

static int try_entity(const char *p, int *consumed) {
    struct { const char *name; uint32_t cp; } table[] = {
        {"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'},
        {"apos;", '\''}, {"nbsp;", ' '}, {"mdash;", 0x2014}, {"ndash;", 0x2013},
        {"hellip;", 0x2026}, {"rsquo;", 0x2019}, {"lsquo;", 0x2018},
        {"ldquo;", 0x201C}, {"rdquo;", 0x201D}, {"copy;", 0x00A9},
        {"reg;", 0x00AE}, {"trade;", 0x2122},
    };
    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        int n = b_len(table[i].name);
        int ok = 1;
        for (int j = 0; j < n; j++) if (p[j] != table[i].name[j]) { ok = 0; break; }
        if (ok) { append_utf8(table[i].cp); *consumed = n; return 1; }
    }
    return 0;
}

static int extract_title(const char *html, char *out, int cap) {
    const char *t = find_substr_ci(html, "<title");
    if (!t) return 0;
    const char *gt = t;
    while (*gt && *gt != '>') gt++;
    if (*gt != '>') return 0;
    gt++;
    const char *end = find_substr_ci(gt, "</title>");
    if (!end || end <= gt) return 0;
    int n = 0;
    for (const char *p = gt; p < end && n < cap - 1; p++) {
        char c = *p;
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        out[n++] = c;
    }
    out[n] = 0;
    while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
    return n > 0;
}

static void flush_block(void) {
    if (g_in_anchor || g_in_option) return;
    g_cur[g_cur_len] = 0;
    while (g_cur_len > 0 && g_cur[g_cur_len - 1] == ' ') g_cur_len--;
    g_cur[g_cur_len] = 0;
    if (g_cur_len > 0) push_block(g_cur_type, g_cur, 0);
    g_cur_len = 0;
    g_last_space = 1;
}

static void html_parse(const char *html) {
    g_block_count = 0;
    g_form_count = 0;
    g_cur_len = 0; g_last_space = 1;
    g_cur_type = BLK_TEXT;
    g_in_anchor = 0; g_cur_href[0] = 0;
    g_cur_form = -1;
    g_cell_in_row = 0;
    g_in_select = 0; g_in_option = 0; g_sel_opt_count = 0;

    {
        char t[BLOCK_TEXT_MAX];
        if (extract_title(html, t, sizeof(t))) push_block(BLK_H1, t, 0);
    }

    const char *p = html;
    long guard = 0;

    while (*p) {
        if (++guard > PARSE_GUARD) break;

        if (*p == '<') {
            const char *tag_start = p;
            const char *q = p + 1;
            while (*q && *q != '>') q++;
            const char *tag_end = q;
            int closing = (tag_start[1] == '/');
            char name[16];
            get_tag_name(tag_start + 1, name, sizeof(name));

            if (!closing && (str_eq(name, "script") || str_eq(name, "style") ||
                              str_eq(name, "title")  || str_eq(name, "head") ||
                              str_eq(name, "svg"))) {
                char close_tag[16] = "</";
                bcat(close_tag, name, sizeof(close_tag));
                const char *from = (*tag_end == '>') ? tag_end + 1 : tag_end;
                const char *found = find_substr_ci(from, close_tag);
                if (found) {
                    const char *r = found;
                    while (*r && *r != '>') r++;
                    p = (*r == '>') ? r + 1 : r;
                } else {
                    p = from;
                    while (*p) p++;
                }
                continue;
            }

            if (!closing) {
                if (str_eq(name, "h1")) {
                    flush_block(); g_cur_type = BLK_H1; g_cur_len = 0; g_last_space = 1;
                } else if (str_eq(name, "h2")) {
                    flush_block(); g_cur_type = BLK_H2; g_cur_len = 0; g_last_space = 1;
                } else if (str_eq(name, "h3") || str_eq(name, "h4") ||
                           str_eq(name, "h5") || str_eq(name, "h6")) {
                    flush_block(); g_cur_type = BLK_H3; g_cur_len = 0; g_last_space = 1;
                } else if (str_eq(name, "li")) {
                    flush_block(); g_cur_type = BLK_LIST_ITEM; g_cur_len = 0; g_last_space = 1;
                } else if (str_eq(name, "a")) {
                    flush_block();
                    find_attr(tag_start, tag_end, "href", g_cur_href, sizeof(g_cur_href));
                    g_in_anchor = 1; g_cur_len = 0; g_last_space = 1;
                } else if (str_eq(name, "img")) {
                    flush_block();
                    char alt[BLOCK_TEXT_MAX];
                    if (!find_attr(tag_start, tag_end, "alt", alt, sizeof(alt)) || alt[0] == 0)
                        bcopy_str(alt, "resim", sizeof(alt));
                    push_block(BLK_IMG, alt, 0);
                } else if (str_eq(name, "form")) {
                    flush_block();
                    if (g_form_count < MAX_FORMS) {
                        char action[HREF_MAX];
                        find_attr(tag_start, tag_end, "action", action, sizeof(action));
                        bcopy_str(g_forms[g_form_count].action, action, HREF_MAX);
                        g_forms[g_form_count].used = 1;
                        g_cur_form = g_form_count;
                        g_form_count++;
                    } else {
                        g_cur_form = -1;
                    }
                } else if (str_eq(name, "select")) {
                    flush_block();
                    g_in_select = 1; g_sel_opt_count = 0;
                    g_sel_labels[0] = 0; g_sel_values[0] = 0;
                    find_attr(tag_start, tag_end, "name", g_sel_name, sizeof(g_sel_name));
                } else if (str_eq(name, "option") && g_in_select) {
                    g_in_option = 1; g_opt_label_len = 0; g_opt_label[0] = 0;
                    find_attr(tag_start, tag_end, "value", g_opt_value_attr, sizeof(g_opt_value_attr));
                } else if (str_eq(name, "input")) {
                    flush_block();
                    char type[24], nm[NAME_MAX], val[BLOCK_TEXT_MAX], ph[BLOCK_TEXT_MAX];
                    if (!find_attr(tag_start, tag_end, "type", type, sizeof(type)) || !type[0])
                        bcopy_str(type, "text", sizeof(type));
                    find_attr(tag_start, tag_end, "name", nm, sizeof(nm));
                    find_attr(tag_start, tag_end, "value", val, sizeof(val));
                    if (!find_attr(tag_start, tag_end, "placeholder", ph, sizeof(ph)) || !ph[0])
                        ph[0] = 0;

                    if (str_eq_ci(type, "hidden")) {
                        push_block_full(BLK_FORM_HIDDEN, "", val, nm, g_cur_form);
                    } else if (str_eq_ci(type, "submit") || str_eq_ci(type, "button") ||
                               str_eq_ci(type, "image")) {
                        char label[BLOCK_TEXT_MAX];
                        bcopy_str(label, val[0] ? val : "Gonder", sizeof(label));
                        push_block_full(BLK_FORM_SUBMIT, label, "", nm, g_cur_form);
                    } else if (str_eq_ci(type, "checkbox") || str_eq_ci(type, "radio")) {
                        push_block(BLK_TEXT, "[ ] (secim kutusu - bu surumde desteklenmiyor)", 0);
                    } else {
                        block_t *b = push_block_full(BLK_FORM_TEXT, ph, val, nm, g_cur_form);
                        (void)b;
                    }
                } else if (str_eq(name, "textarea")) {
                    flush_block();
                    char nm[NAME_MAX];
                    find_attr(tag_start, tag_end, "name", nm, sizeof(nm));
                    push_block_full(BLK_FORM_TEXT, "", "", nm, g_cur_form);
                } else if (str_eq(name, "hr")) {
                    flush_block();
                    push_block(BLK_HR, "", 0);
                } else if (str_eq(name, "td") || str_eq(name, "th")) {
                    if (g_cell_in_row && g_cur_len < BLOCK_TEXT_MAX - 4) {
                        append_char(' '); append_char('|'); append_char(' ');
                    }
                    g_cell_in_row = 1;
                } else if (str_eq(name, "tr")) {
                    flush_block(); g_cell_in_row = 0;
                } else if (str_eq(name, "br") || str_eq(name, "p")  || str_eq(name, "div") ||
                           str_eq(name, "table") || str_eq(name, "ul") || str_eq(name, "ol")) {
                    flush_block();
                }
            } else {
                if (str_eq(name, "a")) {
                    g_cur[g_cur_len] = 0;
                    while (g_cur_len > 0 && g_cur[g_cur_len - 1] == ' ') g_cur_len--;
                    g_cur[g_cur_len] = 0;
                    if (g_cur_len > 0) push_block(BLK_LINK, g_cur, g_cur_href);
                    g_cur_len = 0; g_in_anchor = 0; g_last_space = 1;
                } else if (str_eq(name, "option") && g_in_option) {
                    g_opt_label[g_opt_label_len] = 0;
                    while (g_opt_label_len > 0 && g_opt_label[g_opt_label_len - 1] == ' ')
                        g_opt_label[--g_opt_label_len] = 0;
                    const char *value = g_opt_value_attr[0] ? g_opt_value_attr : g_opt_label;
                    if (g_sel_opt_count < MAX_SELECT_OPT) {
                        if (g_sel_opt_count > 0) {
                            bcat(g_sel_labels, "\n", sizeof(g_sel_labels));
                            bcat(g_sel_values, "\n", sizeof(g_sel_values));
                        }
                        bcat(g_sel_labels, g_opt_label[0] ? g_opt_label : value, sizeof(g_sel_labels));
                        bcat(g_sel_values, value, sizeof(g_sel_values));
                        g_sel_opt_count++;
                    }
                    g_in_option = 0;
                } else if (str_eq(name, "select") && g_in_select) {
                    if (g_sel_opt_count > 0)
                        push_block_full(BLK_FORM_SELECT, g_sel_labels, g_sel_values,
                                        g_sel_name, g_cur_form);
                    g_in_select = 0;
                } else if (str_eq(name, "form")) {
                    flush_block(); g_cur_form = -1;
                } else if (str_eq(name, "h1") || str_eq(name, "h2") || str_eq(name, "h3") ||
                           str_eq(name, "h4") || str_eq(name, "h5") || str_eq(name, "h6")) {
                    flush_block(); g_cur_type = BLK_TEXT;
                } else if (str_eq(name, "li")) {
                    flush_block(); g_cur_type = BLK_TEXT;
                } else if (str_eq(name, "p") || str_eq(name, "div") || str_eq(name, "tr")) {
                    flush_block();
                    if (str_eq(name, "tr")) g_cell_in_row = 0;
                }
            }

            if (*tag_end == '>') { p = tag_end + 1; continue; }
            break;
        }

        if (*p == '&') {
            int consumed = 0;
            if (try_entity(p + 1, &consumed)) { p += 1 + consumed; continue; }
            if (p[1] == '#') {
                const char *q = p + 2;
                uint32_t cp = 0;
                int hex = 0;
                if (*q == 'x' || *q == 'X') { hex = 1; q++; }
                const char *q0 = q;
                while (*q) {
                    int v = -1;
                    if (*q >= '0' && *q <= '9') v = *q - '0';
                    else if (hex && *q >= 'a' && *q <= 'f') v = *q - 'a' + 10;
                    else if (hex && *q >= 'A' && *q <= 'F') v = *q - 'A' + 10;
                    else break;
                    cp = cp * (hex ? 16u : 10u) + (uint32_t)v;
                    q++;
                }
                if (q > q0 && *q == ';') { append_utf8(cp); p = q + 1; continue; }
            }
        }

        if (g_in_option) {
            char c = *p;
            if (c == '\t' || c == '\n' || c == '\r') c = ' ';
            if (c == ' ') {
                if (g_opt_label_len == 0 || g_opt_label[g_opt_label_len - 1] == ' ') { p++; continue; }
            }
            if (g_opt_label_len < (int)sizeof(g_opt_label) - 1) g_opt_label[g_opt_label_len++] = c;
            p++;
            continue;
        }

        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            append_char(' ');
            p++;
            continue;
        }

        append_char(*p);
        p++;
    }

    flush_block();
    if (g_block_count == 0) push_block(BLK_TEXT, "(Bos sayfa)", 0);
}

static void push_history(const char *url) {
    if (g_hist_count < HIST_MAX) {
        bcopy_str(g_history[g_hist_count], url, URL_MAX);
        g_hist_count++;
        return;
    }
    for (int i = 1; i < HIST_MAX; i++) bcopy_str(g_history[i - 1], g_history[i], URL_MAX);
    bcopy_str(g_history[HIST_MAX - 1], url, URL_MAX);
}

static void set_status(const char *text, uint32_t color) {
    if (!g_status) return;
    lv_label_set_text(g_status, text);
    lv_obj_set_style_text_color(g_status, lv_color_hex(color), 0);
}

static void navigate(const char *url_in, int record_history) {
    if (!url_in || !url_in[0]) {
        g_block_count = 0;
        push_block(BLK_TEXT, "Adres girin.", 0);
        render_blocks();
        set_status("Hazir", ADL_TEXT_DIM);
        return;
    }

    if (g_basi && g_basi->net && g_basi->net->link_type &&
        g_basi->net->link_type() == ASI_LINK_NONE) {
        g_block_count = 0;
        push_block(BLK_TEXT, "Ag baglantisi yok.", 0);
        render_blocks();
        set_status("Baglanti yok", ADL_DANGER);
        return;
    }

    char full[URL_MAX];
    const char *u = url_in;
    if (!starts_with(url_in, "http://") && !starts_with(url_in, "https://")) {
        bcopy_str(full, "https://", sizeof(full));
        bcat(full, url_in, sizeof(full));
        u = full;
    }

    set_status("Yukleniyor...", ADL_WARN);

    if (record_history && g_cur_url[0] && !str_eq(g_cur_url, u))
        push_history(g_cur_url);

    if (starts_with(u, "https://")) {
        uint32_t hlen = 0;
        const char *hb = (g_basi && g_basi->net && g_basi->net->https_get)
                         ? g_basi->net->https_get(u, &hlen) : 0;
        if (!hb) {
            g_block_count = 0;
            push_block(BLK_TEXT, "HTTPS getirilemedi (ag/DNS/TLS/sertifika).", 0);
            render_blocks();
            set_status("Hata", ADL_DANGER);
            return;
        }
        bcopy_str(g_raw, hb, PAGE_RAW_MAX);
    } else {
        uint32_t len = 0;
        const char *body = (g_basi && g_basi->net && g_basi->net->http_get)
                           ? g_basi->net->http_get(u, &len) : 0;
        if (!body) {
            g_block_count = 0;
            push_block(BLK_TEXT, "Sayfa getirilemedi.", 0);
            push_block(BLK_TEXT, u, 0);
            push_block(BLK_TEXT, "(Sunucu yok, DNS veya baglanti sorunu.)", 0);
            render_blocks();
            set_status("Hata", ADL_DANGER);
            return;
        }
        bcopy_str(g_raw, body, PAGE_RAW_MAX);
    }

    bcopy_str(g_cur_url, u, URL_MAX);
    parse_url(u, g_cur_scheme, sizeof(g_cur_scheme), g_cur_host, sizeof(g_cur_host));

    html_parse(g_raw);
    render_blocks();
    set_status(u, ADL_TEXT_DIM);
}

static void submit_form(int form_id, const char *fallback_action) {
    char action[HREF_MAX];
    if (form_id >= 0 && form_id < g_form_count && g_forms[form_id].action[0]) {
        resolve_url(g_cur_scheme, g_cur_host, g_forms[form_id].action, action, sizeof(action));
        if (!action[0]) bcopy_str(action, g_cur_url, sizeof(action));
    } else {
        bcopy_str(action, fallback_action && fallback_action[0] ? fallback_action : g_cur_url,
                  sizeof(action));
    }

    char query[URL_MAX];
    query[0] = 0;
    int first = 1;

    for (int i = 0; i < g_block_count; i++) {
        block_t *b = &g_blocks[i];
        if (b->form_id != form_id || !b->name[0]) continue;

        const char *value = 0;
        char selbuf[64];

        if (b->type == BLK_FORM_TEXT && b->widget) {
            value = lv_textarea_get_text(b->widget);
        } else if (b->type == BLK_FORM_HIDDEN) {
            value = b->href;
        } else if (b->type == BLK_FORM_SELECT && b->widget) {
            uint16_t sel = lv_dropdown_get_selected(b->widget);
            const char *p = b->href;
            uint16_t idx = 0;
            int n = 0;
            while (*p && idx < sel) { if (*p == '\n') idx++; p++; }
            while (*p && *p != '\n' && n < (int)sizeof(selbuf) - 1) selbuf[n++] = *p++;
            selbuf[n] = 0;
            value = selbuf;
        } else {
            continue;
        }
        if (!value) continue;

        bcat(query, first ? "?" : "&", sizeof(query));
        url_encode_append(query, sizeof(query), b->name);
        bcat(query, "=", sizeof(query));
        url_encode_append(query, sizeof(query), value);
        first = 0;
    }

    char full[URL_MAX];
    bcopy_str(full, action, sizeof(full));
    bcat(full, query, sizeof(full));

    if (g_addr) lv_textarea_set_text(g_addr, full);
    navigate(full, 1);
}

static void link_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_block_count) return;

    char href_copy[HREF_MAX];
    bcopy_str(href_copy, g_blocks[idx].href, sizeof(href_copy));

    char resolved[URL_MAX];
    resolve_url(g_cur_scheme, g_cur_host, href_copy, resolved, sizeof(resolved));
    if (!resolved[0]) return;

    if (g_addr) lv_textarea_set_text(g_addr, resolved);
    navigate(resolved, 1);
}

static void form_submit_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_block_count) return;
    submit_form(g_blocks[idx].form_id, "");
}

static void form_text_ready_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_block_count) return;
    submit_form(g_blocks[idx].form_id, "");
}

static void render_blocks(void) {
    if (!g_content) return;
    lv_obj_clean(g_content);

    for (int i = 0; i < g_block_count; i++) {
        block_t *b = &g_blocks[i];
        switch (b->type) {
        case BLK_H1:
        case BLK_H2:
        case BLK_H3: {
            lv_obj_t *l = lv_label_create(g_content);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, LV_PCT(100));
            lv_label_set_text(l, b->text);
            uint32_t col = (b->type == BLK_H1) ? ADL_ACCENT :
                           (b->type == BLK_H2) ? ADL_ACCENT_2 : ADL_TEXT;
            lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
            lv_obj_set_style_pad_top(l, (b->type == BLK_H1) ? 14 : 8, 0);
            lv_obj_set_style_pad_bottom(l, 4, 0);
            break;
        }
        case BLK_TEXT: {
            lv_obj_t *l = lv_label_create(g_content);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, LV_PCT(100));
            lv_label_set_text(l, b->text);
            lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
            lv_obj_set_style_pad_bottom(l, 4, 0);
            break;
        }
        case BLK_LIST_ITEM: {
            lv_obj_t *row = lv_obj_create(g_content);
            lv_obj_remove_style_all(row);
            lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_left(row, 14, 0);
            lv_obj_set_style_pad_bottom(row, 2, 0);
            lv_obj_t *l = lv_label_create(row);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, LV_PCT(96));
            lv_label_set_text_fmt(l, "- %s", b->text);
            lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
            break;
        }
        case BLK_LINK: {
            lv_obj_t *l = lv_label_create(g_content);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, LV_PCT(100));
            lv_label_set_text(l, b->text);
            lv_obj_set_style_text_color(l, lv_color_hex(ADL_ACCENT), 0);
            lv_obj_set_style_text_decor(l, LV_TEXT_DECOR_UNDERLINE, 0);
            lv_obj_set_style_pad_bottom(l, 4, 0);
            lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(l, link_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            break;
        }
        case BLK_IMG: {
            lv_obj_t *box = lv_obj_create(g_content);
            lv_obj_set_size(box, LV_PCT(100), 56);
            lv_obj_set_style_bg_color(box, lv_color_hex(ADL_SURFACE_2), 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(box, lv_color_hex(ADL_BORDER), 0);
            lv_obj_set_style_border_width(box, 1, 0);
            lv_obj_set_style_radius(box, 6, 0);
            lv_obj_set_style_pad_bottom(box, 8, 0);
            lv_obj_t *l = lv_label_create(box);
            lv_label_set_text_fmt(l, "[Resim: %s]", b->text);
            lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT_DIM), 0);
            lv_obj_center(l);
            break;
        }
        case BLK_HR: {
            lv_obj_t *line = lv_obj_create(g_content);
            lv_obj_remove_style_all(line);
            lv_obj_set_size(line, LV_PCT(100), 1);
            lv_obj_set_style_bg_color(line, lv_color_hex(ADL_BORDER), 0);
            lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
            lv_obj_set_style_margin_top(line, 6, 0);
            lv_obj_set_style_margin_bottom(line, 6, 0);
            break;
        }
        case BLK_FORM_TEXT: {
            lv_obj_t *ta = lv_textarea_create(g_content);
            lv_textarea_set_one_line(ta, 1);
            lv_obj_set_width(ta, LV_PCT(94));
            if (b->text[0]) lv_textarea_set_placeholder_text(ta, b->text);
            if (b->href[0]) lv_textarea_set_text(ta, b->href);
            lv_obj_set_style_pad_bottom(ta, 6, 0);
            lv_obj_add_event_cb(ta, form_text_ready_cb, LV_EVENT_READY, (void *)(intptr_t)i);
            b->widget = ta;
            break;
        }
        case BLK_FORM_HIDDEN:
            break;
        case BLK_FORM_SELECT: {
            lv_obj_t *dd = lv_dropdown_create(g_content);
            lv_obj_set_width(dd, LV_PCT(94));
            lv_dropdown_set_options(dd, b->text);
            lv_obj_set_style_pad_bottom(dd, 6, 0);
            b->widget = dd;
            break;
        }
        case BLK_FORM_SUBMIT: {
            lv_obj_t *btn = lv_button_create(g_content);
            lv_obj_set_style_bg_color(btn, lv_color_hex(ADL_ACCENT), 0);
            lv_obj_set_style_radius(btn, 8, 0);
            lv_obj_set_style_pad_bottom(btn, 6, 0);
            lv_obj_add_event_cb(btn, form_submit_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_t *l = lv_label_create(btn);
            lv_label_set_text(l, b->text);
            lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
            break;
        }
        default:
            break;
        }
    }
}

static void go_cb(lv_event_t *e) {
    (void)e;
    const char *t = lv_textarea_get_text(g_addr);
    char url[URL_MAX];
    bcopy_str(url, t, sizeof(url));
    navigate(url, 1);
}

static void addr_ready_cb(lv_event_t *e) { go_cb(e); }

static void back_cb(lv_event_t *e) {
    (void)e;
    if (g_hist_count <= 0) return;
    g_hist_count--;
    char url[URL_MAX];
    bcopy_str(url, g_history[g_hist_count], sizeof(url));
    if (g_addr) lv_textarea_set_text(g_addr, url);
    navigate(url, 0);
}

static void browser_del_cb(lv_event_t *e) {
    (void)e;
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, NULL);
    g_addr = 0; g_status = 0; g_content = 0;
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    g_basi = asi;
    g_block_count = 0;
    g_form_count = 0;
    g_hist_count = 0;
    g_cur_url[0] = 0;
    g_cur_host[0] = 0;
    bcopy_str(g_cur_scheme, "https", sizeof(g_cur_scheme));

    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(win, 0, 0);
    lv_obj_set_style_pad_row(win, 0, 0);

    lv_obj_t *topbar = lv_obj_create(win);
    lv_obj_remove_style_all(topbar);
    lv_obj_set_size(topbar, LV_PCT(100), 44);
    lv_obj_set_style_pad_all(topbar, 4, 0);
    lv_obj_set_flex_flow(topbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(topbar, 6, 0);

    lv_obj_t *back = lv_button_create(topbar);
    lv_obj_set_size(back, 40, 36);
    lv_obj_set_style_bg_color(back, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bkl = lv_label_create(back);
    lv_label_set_text(bkl, "<");
    lv_obj_set_style_text_color(bkl, lv_color_hex(ADL_TEXT), 0);
    lv_obj_center(bkl);

    g_addr = lv_textarea_create(topbar);
    lv_textarea_set_one_line(g_addr, 1);
    lv_textarea_set_placeholder_text(g_addr, "ornek.com veya https://...");
    lv_obj_set_size(g_addr, 100, 36);
    lv_obj_set_flex_grow(g_addr, 1);
    lv_obj_add_event_cb(g_addr, addr_ready_cb, LV_EVENT_READY, NULL);

    lv_obj_t *go = lv_button_create(topbar);
    lv_obj_set_size(go, 60, 36);
    lv_obj_set_style_bg_color(go, lv_color_hex(ADL_ACCENT), 0);
    lv_obj_set_style_radius(go, 8, 0);
    lv_obj_add_event_cb(go, go_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = lv_label_create(go);
    lv_label_set_text(gl, "Git");
    lv_obj_set_style_text_color(gl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(gl);

    g_status = lv_label_create(win);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_status, LV_PCT(96));
    lv_obj_set_style_pad_left(g_status, 6, 0);
    lv_label_set_text(g_status, "Hazir");
    lv_obj_set_style_text_color(g_status, lv_color_hex(ADL_TEXT_DIM), 0);

    g_content = lv_obj_create(win);
    lv_obj_set_size(g_content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(g_content, 1);
    lv_obj_set_style_bg_color(g_content, lv_color_hex(ADL_BG), 0);
    lv_obj_set_style_bg_opa(g_content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_content, 0, 0);
    lv_obj_set_style_pad_all(g_content, 10, 0);
    lv_obj_set_style_pad_row(g_content, 2, 0);
    lv_obj_set_flex_flow(g_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_content, LV_DIR_VER);

    lv_obj_add_event_cb(win, browser_del_cb, LV_EVENT_DELETE, NULL);

    if (!g_group) g_group = lv_group_create();
    lv_group_remove_all_objs(g_group);
    lv_group_add_obj(g_group, g_addr);
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, g_group);
    lv_group_focus_obj(g_addr);

    push_block(BLK_H1, "Arctian Tarayici", 0);
    push_block(BLK_TEXT, "Adres yazip Enter'a basin (ornek: google.com, github.com)", 0);
    push_block(BLK_TEXT, "Linklere tiklayarak gezinebilir, \"<\" ile geri donebilirsiniz.", 0);
    push_block(BLK_TEXT, "Arama/form kutularina yazip Enter'a basarak veya \"Gonder\" butonuna tiklayarak formlari gonderebilirsiniz.", 0);
    render_blocks();
}