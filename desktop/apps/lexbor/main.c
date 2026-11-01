#include "app.h"
#include "lv_port.h"
#include "wolf_engine.h"
#include "adl_wolf.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "lexbor/html/html.h"
#include "lexbor/css/css.h"
#include "lexbor/selectors/selectors.h"

#define RAW_MAX    327680
#define SCRIPT_MAX 196608
#define URL_MAX    640
#define HOST_MAX   256
#define TEXT_MAX   400
#define HREF_MAX   768
#define MAX_BLOCKS 800

typedef enum { BLK_TEXT = 0, BLK_LINK } blk_t;

typedef struct {
    blk_t type;
    char text[TEXT_MAX];
    char href[HREF_MAX];
} block_t;

static asi_t *s_asi;

static lv_obj_t *g_addr;
static lv_obj_t *g_status;
static lv_obj_t *g_content;
static lv_obj_t *g_meta;

static lxb_html_document_t *g_doc;

static char g_raw[RAW_MAX];
static char g_scripts[SCRIPT_MAX];
static int  g_script_len;

static char g_nav_url[URL_MAX];
static char g_final[URL_MAX];
static char g_cur_url[URL_MAX];
static char g_cur_scheme[8];
static char g_cur_host[HOST_MAX];
static char g_pending_url[URL_MAX];
static int  g_is_https;
static int  g_nav_ok;

static char g_title[256];

static block_t g_blocks[MAX_BLOCKS];
static int g_block_count;

static char g_run[TEXT_MAX];
static int  g_run_len;

static void cpy(char *d, int cap, const char *s) {
    if (cap <= 0) return;
    int i = 0;
    if (s) { while (s[i] && i < cap - 1) { d[i] = s[i]; i++; } }
    d[i] = 0;
}

static void cat(char *d, int cap, const char *s) {
    if (cap <= 0) return;
    int i = 0; while (d[i] && i < cap - 1) i++;
    if (s) { int j = 0; while (s[j] && i < cap - 1) d[i++] = s[j++]; }
    d[i] = 0;
}

static void cat_ch(char *d, int cap, char ch) {
    int i = 0; while (d[i] && i < cap - 1) i++;
    if (i < cap - 1) { d[i++] = ch; d[i] = 0; }
}

static int ci_starts(const char *s, const char *p) {
    while (*p) {
        char a = *s++, b = *p++;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

static inline void ser_outb(unsigned short port, unsigned char v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}
static inline unsigned char ser_inb(unsigned short port) {
    unsigned char r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static void ser(const char *s) {
    for (; s && *s; s++) {
        while (!(ser_inb(0x3FD) & 0x20)) { }
        ser_outb(0x3F8, (unsigned char)*s);
    }
}

static void ser_i(int v) {
    char b[16]; int i = 0;
    if (v == 0) { ser("0"); return; }
    if (v < 0) { ser("-"); v = -v; }
    while (v) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) { char c = b[--i]; while (!(ser_inb(0x3FD) & 0x20)) { } ser_outb(0x3F8, (unsigned char)c); }
}

static void wolf_print(const char *s, unsigned len) {
    unsigned i;
    for (i = 0; i < len; i++) {
        while (!(ser_inb(0x3FD) & 0x20)) { }
        ser_outb(0x3F8, (unsigned char)s[i]);
    }
}

static void run_add(const char *s, int n) {
    int i;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') c = ' ';
        if (c == ' ') {
            if (g_run_len == 0) continue;
            if (g_run_len < TEXT_MAX - 1 && g_run[g_run_len - 1] == ' ') continue;
        }
        if (g_run_len < TEXT_MAX - 1) g_run[g_run_len++] = c;
    }
}

static void add_block(blk_t type, const char *text, const char *href) {
    if (g_block_count >= MAX_BLOCKS) return;
    block_t *b = &g_blocks[g_block_count];
    b->type = type;
    cpy(b->text, TEXT_MAX, text ? text : "");
    cpy(b->href, HREF_MAX, href ? href : "");
    g_block_count++;
}

static void run_flush(void) {
    while (g_run_len > 0 && g_run[g_run_len - 1] == ' ') g_run_len--;
    if (g_run_len > 0) {
        g_run[g_run_len] = 0;
        add_block(BLK_TEXT, g_run, 0);
    }
    g_run_len = 0;
    g_run[0] = 0;
}

static int tag_is(const char *tag, const char *name) {
    int i;
    for (i = 0; tag[i] && name[i]; i++) {
        char a = tag[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (a != name[i]) return 0;
    }
    return tag[i] == 0 && name[i] == 0;
}

static int tag_block(const char *tag) {
    static const char *bl[] = {
        "address","article","aside","blockquote","body","caption","center",
        "dd","div","dl","dt","fieldset","figcaption","figure","footer","form",
        "h1","h2","h3","h4","h5","h6","header","hr","html","li","main","nav",
        "ol","p","pre","section","table","tbody","td","tfoot","th","thead",
        "tr","ul", 0
    };
    int i;
    for (i = 0; bl[i]; i++) if (tag_is(tag, bl[i])) return 1;
    return 0;
}

static int tag_skip(const char *tag) {
    static const char *sk[] = {
        "style","meta","link","base","noscript","template","svg","canvas",
        "iframe","object","embed","audio","video","head","script", 0
    };
    int i;
    for (i = 0; sk[i]; i++) if (tag_is(tag, sk[i])) return 1;
    return 0;
}

static void tag_lower(lxb_dom_node_t *node, char *out, int cap) {
    size_t n = 0;
    const lxb_char_t *nm = lxb_tag_name_by_id(node->local_name, &n);
    int i;
    if (!nm) { out[0] = 0; return; }
    for (i = 0; i < (int)n && i < cap - 1; i++) {
        char c = (char)nm[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        out[i] = c;
    }
    out[i] = 0;
}

static void collect_script(lxb_dom_node_t *node) {
    size_t len = 0;
    const lxb_char_t *t = lxb_dom_node_text_content(node, &len);
    if (!t || !len) return;
    if (g_script_len > 0 && g_script_len < SCRIPT_MAX - 2) {
        g_scripts[g_script_len++] = '\n';
        g_scripts[g_script_len++] = ';';
    }
    for (size_t i = 0; i < len && g_script_len < SCRIPT_MAX - 1; i++) {
        g_scripts[g_script_len++] = (char)t[i];
    }
}

static void resolve_url(const char *href, char *out, int cap) {
    out[0] = 0;
    if (!href || !href[0]) return;
    if (ci_starts(href, "http://") || ci_starts(href, "https://")) {
        cpy(out, cap, href);
        return;
    }
    char origin[300];
    origin[0] = 0;
    const char *p = strstr(g_cur_url, "://");
    if (p) {
        p += 3;
        const char *q = p;
        while (*q && *q != '/' && *q != '?' && *q != '#') q++;
        int n = (int)(q - g_cur_url);
        if (n > 0 && n < (int)sizeof(origin) - 1) {
            for (int i = 0; i < n; i++) origin[i] = g_cur_url[i];
            origin[n] = 0;
        }
    }
    if (href[0] == '/' && href[1] == '/') {
        if (ci_starts(g_cur_url, "https://")) cpy(out, cap, "https:");
        else cpy(out, cap, "http:");
        cat(out, cap, href);
        return;
    }
    if (href[0] == '/') {
        cpy(out, cap, origin);
        cat(out, cap, href);
        return;
    }
    cpy(out, cap, origin);
    cat(out, cap, "/");
    cat(out, cap, href);
}

/* Yuruyus eskiden her dugum derinliginde kendini cagiriyordu; ~65 bin ic
 * ice div uygulamanin yiginini tasiriyordu. Simdi ayrik bir yiginla
 * dongo halinde yurulur; 4096'dan derin sayfada derin kalan kirpilir. */
#define WALK_MAX 4096
static lxb_dom_node_t *g_walk_st[WALK_MAX];
static lxb_dom_node_t *const WALK_MARK = (lxb_dom_node_t *)1;

static void walk(lxb_dom_node_t *node) {
    int sp = 0;
    for (;;) {
        for (; node;) {
            if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
                lxb_dom_character_data_t *cd = lxb_dom_interface_character_data(node);
                if (cd && cd->data.data && cd->data.length)
                    run_add((const char *)cd->data.data, (int)cd->data.length);
                node = node->next;
                continue;
            }
            if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) { node = node->next; continue; }

            char tag[32];
            tag_lower(node, tag, sizeof(tag));

            if (tag_is(tag, "script")) { collect_script(node); node = node->next; continue; }
            if (tag_skip(tag)) { node = node->next; continue; }

            if (tag_is(tag, "title")) {
                size_t len = 0;
                const lxb_char_t *t = lxb_dom_node_text_content(node, &len);
                if (t && len) {
                    int k = 0;
                    for (size_t i = 0; i < len && k < (int)sizeof(g_title) - 1; i++)
                        g_title[k++] = (char)t[i];
                    g_title[k] = 0;
                }
                node = node->next;
                continue;
            }

            if (tag_is(tag, "a")) {
                lxb_dom_element_t *el = lxb_dom_interface_element(node);
                size_t hl = 0;
                const lxb_char_t *hv = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"href", 4, &hl);
                size_t tl = 0;
                const lxb_char_t *tv = lxb_dom_node_text_content(node, &tl);
                if (hl > 0 && hv && tv && tl) {
                    char href[HREF_MAX], abs[HREF_MAX], txt[TEXT_MAX];
                    int i;
                    for (i = 0; i < (int)hl && i < HREF_MAX - 1; i++) href[i] = (char)hv[i]; href[i] = 0;
                    for (i = 0; i < (int)tl && i < TEXT_MAX - 1; i++) txt[i] = (char)tv[i]; txt[i] = 0;
                    resolve_url(href, abs, HREF_MAX);
                    run_flush();
                    add_block(BLK_LINK, txt, abs);
                    node = node->next;
                    continue;
                }
            }

            if (tag_is(tag, "img")) {
                run_flush();
                add_block(BLK_TEXT, "[resim]", 0);
                node = node->next;
                continue;
            }
            if (tag_is(tag, "br")) { run_flush(); node = node->next; continue; }
            if (tag_is(tag, "hr")) {
                run_flush();
                add_block(BLK_TEXT, "-----", 0);
                node = node->next;
                continue;
            }

            if (!node->first_child) {
                if (tag_block(tag)) run_flush();
                node = node->next;
                continue;
            }
            if (tag_block(tag)) run_flush();
            if (sp < WALK_MAX) {
                if (tag_block(tag)) {
                    if (sp < WALK_MAX - 1) g_walk_st[sp++] = WALK_MARK;
                }
                g_walk_st[sp++] = node->next;
                node = node->first_child;
            } else {
                /* yigin doldu: bu daldan vazgec, kardeslere devam et */
                node = node->next;
            }
        }
        if (sp == 0) break;
        node = g_walk_st[--sp];
        if (node == WALK_MARK) { run_flush(); continue; }
    }
}

#define QMAX 1024
static lxb_dom_node_t *g_q[QMAX];
static int g_qn;

static lxb_status_t qcb(lxb_dom_node_t *node, lxb_css_selector_specificity_t spec, void *ctx) {
    (void)spec; (void)ctx;
    if (g_qn < QMAX) g_q[g_qn++] = node;
    return LXB_STATUS_OK;
}

static int query_all(const char *sel, int len) {
    g_qn = 0;
    if (!g_doc || len <= 0) return 0;
    lxb_css_parser_t *p = lxb_css_parser_create();
    if (!p) return 0;
    lxb_css_parser_init(p, NULL);
    lxb_css_selector_list_t *list = lxb_css_selectors_parse(p, (const lxb_char_t *)sel, (size_t)len);
    if (list) {
        lxb_selectors_t *s = lxb_selectors_create();
        if (s) {
            lxb_selectors_init(s);
            lxb_selectors_find(s, lxb_dom_interface_node(g_doc), list, qcb, NULL);
            lxb_selectors_destroy(s, true);
        }
        lxb_css_selector_list_destroy_memory(list);
    }
    lxb_css_parser_destroy(p, true);
    return g_qn;
}

static long long H(lxb_dom_node_t *n) { return n ? (long long)(intptr_t)n : -1; }
static lxb_dom_node_t *N(long long h) { return h > 0 ? (lxb_dom_node_t *)(intptr_t)h : NULL; }

static long long wh_get_by_id(const char *id, unsigned len) {
    char sel[256];
    int o = 0;
    sel[o++] = '#';
    for (unsigned i = 0; i < len && o < (int)sizeof(sel) - 1; i++) sel[o++] = id[i];
    sel[o] = 0;
    query_all(sel, o);
    return g_qn ? H(g_q[0]) : -1;
}

static long long wh_get_by_tag(const char *tag, unsigned len, int index) {
    query_all(tag, (int)len);
    if (index < 0 || index >= g_qn) return -1;
    return H(g_q[index]);
}

static long long wh_get_by_class(const char *cls, unsigned len, int index) {
    char sel[256];
    int o = 0;
    sel[o++] = '.';
    for (unsigned i = 0; i < len && o < (int)sizeof(sel) - 1; i++) sel[o++] = cls[i];
    sel[o] = 0;
    query_all(sel, o);
    if (index < 0 || index >= g_qn) return -1;
    return H(g_q[index]);
}

static long long wh_query(const char *sel, unsigned len) {
    query_all(sel, (int)len);
    return g_qn ? H(g_q[0]) : -1;
}

static long long wh_query_idx(const char *sel, unsigned len, int index) {
    query_all(sel, (int)len);
    if (index < 0 || index >= g_qn) return -1;
    return H(g_q[index]);
}

static long long wh_create(const char *tag, unsigned len) {
    if (!g_doc) return -1;
    lxb_dom_element_t *el = lxb_dom_document_create_element(&g_doc->dom_document,
                            (const lxb_char_t *)tag, (size_t)len, NULL);
    return el ? H(lxb_dom_interface_node(el)) : -1;
}

static int wh_tag_of(long long h, char *out, unsigned cap) {
    lxb_dom_node_t *n = N(h);
    if (!n || n->type != LXB_DOM_NODE_TYPE_ELEMENT) return -1;
    size_t len = 0;
    const lxb_char_t *nm = lxb_tag_name_by_id(n->local_name, &len);
    if (!nm) return -1;
    unsigned k = 0;
    for (; k < len && k < cap - 1; k++) out[k] = (char)nm[k];
    out[k] = 0;
    return (int)k;
}

static int wh_get_text(long long h, char *out, unsigned cap) {
    lxb_dom_node_t *n = N(h);
    if (!n) return -1;
    size_t len = 0;
    const lxb_char_t *t = lxb_dom_node_text_content(n, &len);
    if (!t) { if (cap) out[0] = 0; return 0; }
    unsigned k = 0;
    for (; k < len && k < cap - 1; k++) out[k] = (char)t[k];
    out[k] = 0;
    return (int)k;
}

static void wh_set_text(long long h, const char *s, unsigned len) {
    lxb_dom_node_t *n = N(h);
    if (!n) return;
    lxb_dom_node_text_content_set(n, (const lxb_char_t *)s, (size_t)len);
}

static int wh_get_attr(long long h, const char *name, unsigned nlen, char *out, unsigned cap) {
    lxb_dom_node_t *n = N(h);
    if (!n || n->type != LXB_DOM_NODE_TYPE_ELEMENT) return -1;
    size_t vl = 0;
    const lxb_char_t *v = lxb_dom_element_get_attribute(lxb_dom_interface_element(n),
                            (const lxb_char_t *)name, (size_t)nlen, &vl);
    if (!v) return -1;
    unsigned k = 0;
    for (; k < vl && k < cap - 1; k++) out[k] = (char)v[k];
    out[k] = 0;
    return (int)k;
}

static int wh_set_attr(long long h, const char *name, unsigned nlen, const char *val, unsigned vlen) {
    lxb_dom_node_t *n = N(h);
    if (!n || n->type != LXB_DOM_NODE_TYPE_ELEMENT) return -1;
    lxb_dom_element_set_attribute(lxb_dom_interface_element(n),
        (const lxb_char_t *)name, (size_t)nlen, (const lxb_char_t *)val, (size_t)vlen);
    return 0;
}

static void wh_append(long long parent, long long child) {
    lxb_dom_node_t *p = N(parent), *c = N(child);
    if (p && c) lxb_dom_node_insert_child(p, c);
}

static void wh_remove(long long h) {
    lxb_dom_node_t *n = N(h);
    if (n) lxb_dom_node_remove(n);
}

static void wh_set_style(long long h, const char *prop, unsigned plen, const char *val, unsigned vlen) {
    lxb_dom_node_t *n = N(h);
    if (!n || n->type != LXB_DOM_NODE_TYPE_ELEMENT) return;
    lxb_dom_element_t *el = lxb_dom_interface_element(n);
    size_t ol = 0;
    const lxb_char_t *old = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &ol);
    char buf[512];
    int o = 0;
    for (size_t i = 0; i < ol && o < 500; i++) buf[o++] = (char)old[i];
    if (o && o < 500) buf[o++] = ';';
    for (unsigned i = 0; i < plen && o < 500; i++) buf[o++] = prop[i];
    if (o < 500) buf[o++] = ':';
    for (unsigned i = 0; i < vlen && o < 500; i++) buf[o++] = val[i];
    buf[o] = 0;
    lxb_dom_element_set_attribute(el, (const lxb_char_t *)"style", 5, (const lxb_char_t *)buf, (size_t)o);
}

static void wh_navigate(const char *url, unsigned len) {
    int i;
    for (i = 0; i < (int)len && i < URL_MAX - 1; i++) g_pending_url[i] = url[i];
    g_pending_url[i] = 0;
}

static long long wh_body(void) {
    if (!g_doc || !g_doc->body) return -1;
    return H(lxb_dom_interface_node(g_doc->body));
}

static long long wh_parent(long long h) {
    lxb_dom_node_t *n = N(h);
    if (!n || !n->parent) return -1;
    if (n->parent->type == LXB_DOM_NODE_TYPE_DOCUMENT) return -1;
    return H(n->parent);
}

static long long wh_child_count(long long h) {
    lxb_dom_node_t *n = N(h);
    if (!n) return -1;
    long long c = 0;
    for (lxb_dom_node_t *x = n->first_child; x; x = x->next) c++;
    return c;
}

static long long wh_child_at(long long h, int index) {
    lxb_dom_node_t *n = N(h);
    if (!n || index < 0) return -1;
    int c = 0;
    for (lxb_dom_node_t *x = n->first_child; x; x = x->next) {
        if (c == index) return H(x);
        c++;
    }
    return -1;
}

static int wh_set_cookie(const char *name, unsigned nlen, const char *val, unsigned vlen) {
    if (!s_asi || !s_asi->net || !s_asi->net->cookie_set || !g_cur_host[0]) return -1;
    char n[64], v[1024];
    unsigned i;
    for (i = 0; i < nlen && i < sizeof(n) - 1; i++) n[i] = name[i];
    n[i] = 0;
    for (i = 0; i < vlen && i < sizeof(v) - 1; i++) v[i] = val[i];
    v[i] = 0;
    s_asi->net->cookie_set(g_cur_host, n, v);
    return 0;
}

static int wh_current_url(char *out, unsigned cap) {
    if (!out || cap == 0) return 0;
    unsigned i = 0;
    for (; g_cur_url[i] && i < cap - 1; i++) out[i] = g_cur_url[i];
    out[i] = 0;
    return (int)i;
}

static const wolf_host_t g_host = {
    wh_get_by_id, wh_get_by_tag, wh_get_by_class, wh_query, wh_query_idx, wh_create,
    wh_tag_of, wh_get_text, wh_set_text, wh_get_attr, wh_set_attr,
    wh_append, wh_remove, wh_set_style, wh_navigate,
    wh_body, wh_parent, wh_child_count, wh_child_at,
    wh_set_cookie, wh_current_url
};

static lv_obj_t *mk_label(lv_obj_t *parent, const char *txt) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    return l;
}

static int navigate(const char *url);
static void nav_work(void *u);
static void nav_done(int status, void *u);

static void link_click_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    const char *href = (const char *)lv_obj_get_user_data(obj);
    if (href && href[0]) navigate(href);
}

static int navigate(const char *url) {
    cpy(g_nav_url, URL_MAX, url);
    g_is_https = ci_starts(url, "https://");
    if (s_asi && s_asi->net && s_asi->net->link_type &&
        s_asi->net->link_type() == ASI_LINK_NONE) {
        lv_label_set_text(g_status, "Ag baglantisi yok.");
        return -1;
    }
    lv_label_set_text(g_status, "Yukleniyor...");
    if (s_asi && s_asi->net && s_asi->net->submit) {
        if (s_asi->net->submit(nav_work, nav_done, 0) == 0) return 0;
    }
    nav_work(0);
    nav_done(0, 0);
    return 0;
}

static void url_enc(const char *s, char *out, int cap) {
    static const char *hex = "0123456789ABCDEF";
    int o = 0;
    for (; s && *s && o < cap - 4; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else if (c == ' ') {
            out[o++] = '+';
        } else {
            out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
}

static void parse_scheme_host(const char *url) {
    g_cur_scheme[0] = 0;
    g_cur_host[0] = 0;
    const char *p = strstr(url, "://");
    const char *host;
    if (p) {
        int n = (int)(p - url);
        if (n > 7) n = 7;
        int i;
        for (i = 0; i < n; i++) g_cur_scheme[i] = url[i];
        g_cur_scheme[i] = 0;
        host = p + 3;
    } else {
        cpy(g_cur_scheme, sizeof(g_cur_scheme), "https");
        host = url;
    }
    int i = 0;
    while (host[i] && host[i] != '/' && host[i] != '?' && host[i] != '#' &&
           i < HOST_MAX - 1) {
        g_cur_host[i] = host[i];
        i++;
    }
    g_cur_host[i] = 0;
}

static void render(void) {
    if (g_content) lv_obj_clean(g_content);
    int i;
    for (i = 0; i < g_block_count; i++) {
        block_t *b = &g_blocks[i];
        if (b->type == BLK_LINK && b->text[0]) {
            lv_obj_t *l = mk_label(g_content, b->text);
            lv_obj_set_style_text_color(l, lv_color_hex(0x4C8DFF), 0);
            lv_obj_set_style_text_decor(l, LV_TEXT_DECOR_UNDERLINE, 0);
            lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(l, b->href);
            lv_obj_add_event_cb(l, link_click_cb, LV_EVENT_CLICKED, 0);
        } else if (b->text[0]) {
            mk_label(g_content, b->text);
        }
    }
    if (g_block_count == 0) {
        mk_label(g_content, "(Sayfada gosterilecek metin bulunamadi.)");
    }
}

static void run_scripts(void) {
    if (g_script_len <= 0) {
        lv_label_set_text(g_meta, "Lexbor 3.1.0  |  JS: script yok");
        return;
    }
    g_scripts[g_script_len] = 0;
    wolf_engine_set_print(wolf_print);
    wolf_engine_set_host(&g_host);
    wolf_engine_set_limits(20000000, 48);
    wolf_engine_reset();
    char out[256];
    out[0] = 0;
    int rc = wolf_engine_eval(g_scripts, (unsigned)g_script_len, out, sizeof(out));
    char buf[384];
    cpy(buf, sizeof(buf), "Lexbor 3.1.0  |  JS rc=");
    char num[16]; int n = rc, k = 0;
    if (n == 0) num[k++] = '0';
    if (n < 0) { cat(buf, sizeof(buf), "-"); n = -n; }
    while (n > 0 && k < 15) { num[k++] = (char)('0' + n % 10); n /= 10; }
    while (k > 0) cat_ch(buf, sizeof(buf), num[--k]);
    if (out[0]) { cat(buf, sizeof(buf), " "); cat(buf, sizeof(buf), out); }
    lv_label_set_text(g_meta, buf);
}

static void parse_and_show(void) {
    if (g_doc) { lxb_html_document_destroy(g_doc); g_doc = NULL; }
    g_block_count = 0;
    g_run_len = 0;
    g_run[0] = 0;
    g_title[0] = 0;
    g_script_len = 0;

    g_doc = lxb_html_document_create();
    if (g_doc) {
        lxb_html_document_parse(g_doc, (const lxb_char_t *)g_raw, strlen(g_raw));
        lxb_dom_node_t *root = lxb_dom_interface_node(g_doc);
        walk(root->first_child);
        run_flush();
    }
    render();
    if (g_title[0]) lv_label_set_text(g_status, g_title);
    else lv_label_set_text(g_status, g_cur_url);
    run_scripts();
    if (g_doc) { lxb_html_document_destroy(g_doc); g_doc = NULL; }
    if (g_pending_url[0]) {
        char u[URL_MAX];
        cpy(u, sizeof(u), g_pending_url);
        g_pending_url[0] = 0;
        navigate(u);
    }
}

static void nav_work(void *u) {
    (void)u;
    uint32_t len = 0;
    const char *body = 0;
    g_nav_ok = 0;
    g_raw[0] = 0;
    if (s_asi && s_asi->net) {
        body = g_is_https ? s_asi->net->https_get(g_nav_url, &len)
                          : s_asi->net->http_get(g_nav_url, &len);
    }
    if (body && len) {
        uint32_t n = len;
        if (n > RAW_MAX - 1) n = RAW_MAX - 1;
        for (uint32_t i = 0; i < n; i++) g_raw[i] = body[i];
        g_raw[n] = 0;
        g_nav_ok = 1;
    }
    const char *fin = (s_asi && s_asi->net && s_asi->net->last_url) ? s_asi->net->last_url() : 0;
    if (fin && fin[0]) cpy(g_final, URL_MAX, fin);
    else cpy(g_final, URL_MAX, g_nav_url);
    ser("[lexbor] fetch ok=");
    ser_i(g_nav_ok);
    ser(" bytes=");
    ser_i((int)strlen(g_raw));
    ser("\n");
}

static void nav_done(int status, void *u) {
    (void)status; (void)u;
    if (!g_nav_ok) {
        if (g_content) lv_obj_clean(g_content);
        g_block_count = 0;
        add_block(BLK_TEXT, "Sayfa getirilemedi (ag/DNS/TLS).", 0);
        add_block(BLK_TEXT, g_nav_url, 0);
        render();
        lv_label_set_text(g_status, "Hata");
        lv_label_set_text(g_meta, "Lexbor 3.1.0  |  veri yok");
        return;
    }
    cpy(g_cur_url, URL_MAX, g_final);
    parse_scheme_host(g_cur_url);
    parse_and_show();
}

static void do_search_cb(lv_event_t *e) {
    (void)e;
    const char *txt = lv_textarea_get_text(g_addr);
    if (!txt || !txt[0]) return;
    char url[URL_MAX];
    if (ci_starts(txt, "http://") || ci_starts(txt, "https://")) {
        cpy(url, sizeof(url), txt);
    } else {
        int has_space = 0, has_dot = 0;
        for (int i = 0; txt[i]; i++) { if (txt[i] == ' ') has_space = 1; if (txt[i] == '.') has_dot = 1; }
        if (!has_space && has_dot) {
            cpy(url, sizeof(url), "https://");
            cat(url, sizeof(url), txt);
        } else {
            cpy(url, sizeof(url), "https://www.google.com/search?q=");
            int base = (int)strlen(url);
            url_enc(txt, url + base, URL_MAX - base);
        }
    }
    navigate(url);
}

void app_entry(lv_obj_t *win, asi_t *asi) {
    s_asi = asi;
    g_block_count = 0;
    g_script_len = 0;
    g_pending_url[0] = 0;
    g_cur_url[0] = 0;
    g_title[0] = 0;

    lv_obj_t *root = lv_obj_create(win);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 8, 0);
    lv_obj_set_style_pad_row(root, 6, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = lv_obj_create(root);
    lv_obj_remove_style_all(top);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_height(top, 40);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 6, 0);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    g_addr = lv_textarea_create(top);
    lv_textarea_set_one_line(g_addr, true);
    lv_obj_set_flex_grow(g_addr, 1);
    lv_obj_set_height(g_addr, 36);
    lv_textarea_set_placeholder_text(g_addr, "Ara veya URL yaz (orn. google.com)");

    lv_obj_t *go = lv_button_create(top);
    lv_obj_set_size(go, 70, 36);
    lv_obj_t *gol = lv_label_create(go);
    lv_label_set_text(gol, "Git");
    lv_obj_center(gol);
    lv_obj_add_event_cb(go, do_search_cb, LV_EVENT_CLICKED, 0);
    lv_obj_add_event_cb(g_addr, do_search_cb, LV_EVENT_READY, 0);

    g_status = lv_label_create(root);
    lv_label_set_text(g_status, "Hazir - Lexbor motoru");
    lv_obj_set_width(g_status, LV_PCT(100));

    g_content = lv_obj_create(root);
    lv_obj_remove_style_all(g_content);
    lv_obj_set_width(g_content, LV_PCT(100));
    lv_obj_set_flex_grow(g_content, 1);
    lv_obj_set_flex_flow(g_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(g_content, 4, 0);
    lv_obj_set_style_pad_all(g_content, 4, 0);
    lv_obj_add_flag(g_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_content, LV_SCROLLBAR_MODE_AUTO);

    g_meta = lv_label_create(root);
    lv_obj_set_width(g_meta, LV_PCT(100));
    lv_obj_set_style_text_color(g_meta, lv_color_hex(ADL_TEXT_DIM), 0);
    lv_label_set_text(g_meta, "Lexbor 3.1.0");

    mk_label(g_content, "Adres cubuguna bir adres yaz, Enter'a bas veya Git'e tikla.");
    mk_label(g_content, "Adres degilse Google'da arama yapilir.");
}

