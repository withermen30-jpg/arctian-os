/* Arctian gelismis tarayici.
 * Adres cubuguna bir URL yazip "Git"e bas (veya Enter). HTTP/HTTPS uzerinden
 * sayfayi ceker, HTML'i basit bloklara ayristirir (baslik, paragraf, link,
 * liste ogesi, resim yer tutucusu, yatay cizgi, form alanlari, secim kutusu)
 * ve bunlari LVGL nesneleri olarak stilize edip gosterir. Linklere tiklayinca
 * sayfa icinde gezinilir, "<" butonu ile gecmise donulur. Formlar (orn. Google
 * arama kutusu) gercekten calisir: metin girilebilir, "Gonder"e basilinca ya
 * da Enter'a basilinca form action'ina GET sorgu dizesi olarak eklenip
 * gezinilir.
 *
 * Icerik iki katmanda cizilir: (1) agdan gelen HTML, <style> ve harici
 * stylesheet'lerden kurulan gercek bir CSS alt kumesi motoruyla,
 * (2) <img> kaynaklari agdan cekilip gercek PNG cozucuyle cozulerek. Motor
 * satir ici/klas/kimlik/soy selectors, kutu modeli, flex, tablo hucreleri,
 * form alanlari ve basit yerlesim kurallarini destekler.
 *
 * Kisitlar: libc yok, tum bellek statik. JS calistirilmaz; CSS'in yalnizca
 * bir alt kumesi ve PNG resim formati desteklenir. */
#include "app.h"
#include "lv_port.h"
#include "adl_wolf.h"
#include "png.h"
#include "svg.h"
#include "wolf_engine.h"
#include <stdint.h>

extern const unsigned char font_ttf[];
extern const unsigned int  font_ttf_len;

/* ---------------- Sabitler ---------------- */
#define MAX_BLOCKS     240
#define BLOCK_TEXT_MAX 220
#define HREF_MAX       220     /* linklerde URL, formlarda deger listesi tutar */
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
    BLK_FORM_TEXT,     /* gercek, duzenlenebilir metin kutusu */
    BLK_FORM_HIDDEN,   /* gorunmez, sadece gonderimde deger tasir */
    BLK_FORM_SELECT,   /* acilir kutu (dropdown) */
    BLK_FORM_SUBMIT    /* form gonderme butonu */
} block_type_t;

typedef struct {
    block_type_t type;
    char text[BLOCK_TEXT_MAX];  /* goruntu metni / dropdown icin "etiket1\netiket2..." */
    char href[HREF_MAX];        /* link URL'si / hidden-input degeri / dropdown icin "deger1\ndeger2..." */
    char name[NAME_MAX];        /* form alani adi (name="...") */
    int  form_id;                /* -1 = forma ait degil */
    lv_obj_t *widget;             /* render sirasinda olusturulan canli nesne (deger okumak icin) */
} block_t;

typedef struct {
    char action[HREF_MAX];
    int  used;
} form_t;

/* ---------------- Global durum ---------------- */
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
/* Ileri yigini (0.6): geri alinan sayfa buraya itilir; yeni gezinme temizler. */
static char g_fwd[HIST_MAX][URL_MAX];
static int  g_fwd_count;

static lv_obj_t   *g_addr;
static lv_obj_t   *g_status;
static lv_obj_t   *g_content;
static lv_group_t *g_group;
static asi_t      *g_basi;
static lv_timer_t *g_wheel_timer;

/* Ayristirici durumu */
static char          g_cur[BLOCK_TEXT_MAX];
static int            g_cur_len;
static int            g_last_space;
static block_type_t   g_cur_type;
static int             g_in_anchor;
static char           g_cur_href[HREF_MAX];
static int             g_cur_form;      /* parse sirasinda icinde bulunulan form id, yoksa -1 */
static int             g_cell_in_row;   /* tablo hucresi ayirici icin */

/* <select> ayristirma durumu */
static int  g_in_select;
static char g_sel_name[NAME_MAX];
static char g_sel_labels[BLOCK_TEXT_MAX];
static char g_sel_values[HREF_MAX];
static int  g_sel_opt_count;
static int  g_in_option;
static char g_opt_value_attr[64];
static char g_opt_label[64];
static int  g_opt_label_len;

/* Ileri bildirimler */
static void render_blocks(void);
static void set_status(const char *text, uint32_t color);
static void navigate(const char *url_in, int record_history);
static void submit_form(int form_id, const char *fallback_action);
static void render_page(const char *html);
static int page_is_blocked(const char *h);
static void collect_resources(const char *html, const char *scheme, const char *host);
static void res_clear(void);
static void images_clear(void);

/* ---------------- Kucuk yardimci fonksiyonlar (libc yok) ---------------- */
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

/* Case-insensitive alt dizi arama (strstr yerine) */
static const char *find_substr_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && lower_c(*h) == lower_c(*n)) { h++; n++; }
        if (!*n) return hay;
    }
    return 0;
}

/* application/x-www-form-urlencoded uretir: bosluk -> '+', ayrilmis
 * karakterler -> %XX. Form gonderiminde dogru sorgu dizesi icin sarttir. */
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

/* <etiket ...  veya </etiket ... icinden etiket adini kucuk harfle cikarir */
static void get_tag_name(const char *p, char *out, int outsz) {
    int n = 0;
    if (*p == '/') p++;
    while (*p && is_alpha(*p) && n < outsz - 1) { out[n++] = lower_c(*p); p++; }
    out[n] = 0;
}

/* tag_start '<' karakterini, tag_end kapanis '>' karakterini gosterir.
 * attr, kucuk harfli oznitelik adi (orn. "href"). Bulunursa 1 doner. */
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
        /* onceki karakter alfanumerik olmamali (orn. "href" ararken
         * "xhref" gibi bir seye yanlislikla eslesmesin) */
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
            /* "checked" gibi degersiz oznitelik: varlik olarak isaretle */
            out[0] = 0;
            return 1;
        }
        p++;
    }
    out[0] = 0;
    return 0;
}

/* ---------------- URL yardimcilari ---------------- */
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

/* ---------------- Blok listesi ---------------- */
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

/* Unicode kod noktasini UTF-8 olarak akisa ekler (sayisal varliklar icin). */
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

/* Bilinen HTML varliklarini (entity) coker; ortak Google/GitHub sayfalarinda
 * sikca gecen birkac ek varlik da eklendi. */
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

/* <title>...</title> metnini cikarir. */
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
    if (g_in_anchor || g_in_option) return; /* metin sadece kapanista yayinlanir */
    g_cur[g_cur_len] = 0;
    while (g_cur_len > 0 && g_cur[g_cur_len - 1] == ' ') g_cur_len--;
    g_cur[g_cur_len] = 0;
    if (g_cur_len > 0) push_block(g_cur_type, g_cur, 0);
    g_cur_len = 0;
    g_last_space = 1;
}

/* ---------------- HTML -> blok ayristirici ---------------- */
static void html_parse(const char *html) {
    g_block_count = 0;
    g_form_count = 0;
    g_cur_len = 0; g_last_space = 1;
    g_cur_type = BLK_TEXT;
    g_in_anchor = 0; g_cur_href[0] = 0;
    g_cur_form = -1;
    g_cell_in_row = 0;
    g_in_select = 0; g_in_option = 0; g_sel_opt_count = 0;

    /* Sayfa basligini en uste koy. */
    {
        char t[BLOCK_TEXT_MAX];
        if (extract_title(html, t, sizeof(t))) push_block(BLK_H1, t, 0);
    }

    const char *p = html;
    long guard = 0;

    while (*p) {
        if (++guard > PARSE_GUARD) break; /* bozuk/asiri buyuk sayfaya karsi guvenlik */

        if (*p == '<') {
            const char *tag_start = p;
            const char *q = p + 1;
            while (*q && *q != '>') q++;
            const char *tag_end = q;
            int closing = (tag_start[1] == '/');
            char name[16];
            get_tag_name(tag_start + 1, name, sizeof(name));

            /* Gorunmemesi gereken icerik: script/style/title/head govdesi.
             * NOT: <noscript> BILINCLI olarak buraya eklenmedi - JS calismadigi
             * icin sitelerin "JS kapaliyken goster" icin koydugu yedek icerik
             * tam da bize lazim olan sey; normal metin gibi islenmesine izin
             * verilir. svg de ozel islem gerektirmez: ic ice etiketlerin metin
             * tasimayan ozniteligi zaten atlanir. */
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
                        /* basit tutuluyor: gorsel kutu, form gonderiminde
                         * (bu surumde) dahil edilmez. */
                        push_block(BLK_TEXT, "[ ] (secim kutusu - bu surumde desteklenmiyor)", 0);
                    } else {
                        /* text, search, email, password, url, number, tel, vb. */
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
                /* Diger etiketler (span, b, strong, vb.) yoksayilir;
                 * icindeki metin akisina dokunulmaz. */
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
            break; /* '>' bulunamadi: bozuk HTML, ayristirmayi bitir */
        }

        if (*p == '&') {
            int consumed = 0;
            if (try_entity(p + 1, &consumed)) { p += 1 + consumed; continue; }
            if (p[1] == '#') {                       /* sayisal varlik: &#305; / &#x131; */
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
            /* option etiketi icindeki metni ayri bir tampona topla */
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

/* ---------------- Gecmis ---------------- */
static void fwd_clear(void) { g_fwd_count = 0; }

static void push_history(const char *url) {
    /* Yeni bir gezinme ileri yiginini gecersiz kilar (tarayici kurali). */
    fwd_clear();
    if (g_hist_count < HIST_MAX) {
        bcopy_str(g_history[g_hist_count], url, URL_MAX);
        g_hist_count++;
        return;
    }
    for (int i = 1; i < HIST_MAX; i++) bcopy_str(g_history[i - 1], g_history[i], URL_MAX);
    bcopy_str(g_history[HIST_MAX - 1], url, URL_MAX);
}

/* ---------------- Durum satiri ---------------- */
static void set_status(const char *text, uint32_t color) {
    if (!g_status) return;
    lv_label_set_text(g_status, text);
    lv_obj_set_style_text_color(g_status, lv_color_hex(color), 0);
}

/* ---------------- Gezinme (fetch + parse + render) ---------------- */

/* Arka plan cekme: ag isi fiber'de; ayristirma + cizim (UI) done'da. */
static char g_nav_url[URL_MAX];
static char g_nav_final[URL_MAX];
static char g_nav_pending[URL_MAX];
static int  g_nav_is_https = 0;
static int  g_nav_ok = 0;
static int  g_nav_have_pending = 0;
static int  g_search_fb = 0;
static int  g_google_tries = 0;

static int search_query_of(const char *url, char *out, int cap) {
    const char *q = 0;
    const char *p = url;
    while (*p) {
        int bound = (p == url || p[-1] == '?' || p[-1] == '&');
        if (bound && p[0] == 'q' && p[1] == '=') { q = p + 2; break; }
        if (bound && (p[0] == 'q' || p[0] == 'Q') && (p[1] == 'u' || p[1] == 'U') &&
            (p[2] == 'e' || p[2] == 'E') && (p[3] == 'r' || p[3] == 'R') &&
            (p[4] == 'y' || p[4] == 'Y') && p[5] == '=') { q = p + 6; break; }
        p++;
    }
    if (!q) return 0;
    int o = 0;
    while (*q && *q != '&' && *q != '#' && o < cap - 1) {
        if (*q == '+') { out[o++] = ' '; q++; }
        else if (*q == '%' && q[1] && q[2]) {
            int hi = -1, lo = -1;
            if (q[1] >= '0' && q[1] <= '9') hi = q[1] - '0';
            else if (q[1] >= 'a' && q[1] <= 'f') hi = q[1] - 'a' + 10;
            else if (q[1] >= 'A' && q[1] <= 'F') hi = q[1] - 'A' + 10;
            if (q[2] >= '0' && q[2] <= '9') lo = q[2] - '0';
            else if (q[2] >= 'a' && q[2] <= 'f') lo = q[2] - 'a' + 10;
            else if (q[2] >= 'A' && q[2] <= 'F') lo = q[2] - 'A' + 10;
            if (hi >= 0 && lo >= 0) { out[o++] = (char)(hi * 16 + lo); q += 3; }
            else { out[o++] = *q++; }
        }
        else out[o++] = *q++;
    }
    out[o] = 0;
    return o > 0;
}

static void search_url_encode(const char *s, char *out, int cap) {
    int o = 0;
    for (int i = 0; s[i] && o < cap - 1; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else if (c == ' ') {
            out[o++] = '+';
        } else if (o < cap - 4) {
            out[o++] = '%';
            out[o++] = "0123456789ABCDEF"[c >> 4];
            out[o++] = "0123456789ABCDEF"[c & 15];
        }
    }
    out[o] = 0;
}

static void trigger_search_fallback(void) {
    char q[200];
    if ((search_query_of(g_nav_url, q, sizeof(q)) ||
         search_query_of(g_nav_final, q, sizeof(q))) && q[0]) {
        g_search_fb = 1;
        char nurl[URL_MAX];
        bcopy_str(nurl, "https://html.duckduckgo.com/html/?q=", sizeof(nurl));
        int base = b_len(nurl);
        search_url_encode(q, nurl + base, URL_MAX - base);
        bcopy_str(g_nav_pending, nurl, URL_MAX);
        g_nav_have_pending = 1;
    }
}

static void browser_nav_work(void *u) {
    (void)u;
    uint32_t len = 0;
    const char *body = g_nav_is_https ? g_basi->net->https_get(g_nav_url, &len)
                                      : g_basi->net->http_get(g_nav_url, &len);
    g_nav_ok = body ? 1 : 0;
    if (body) bcopy_str(g_raw, body, PAGE_RAW_MAX);
    else g_raw[0] = 0;
    const char *fin = (g_basi->net && g_basi->net->last_url) ? g_basi->net->last_url() : 0;
    if (fin && fin[0]) bcopy_str(g_nav_final, fin, URL_MAX);
    else bcopy_str(g_nav_final, g_nav_url, URL_MAX);
    res_clear();
    if (g_nav_ok) {
        char sch[8], host[160];
        parse_url(g_nav_final, sch, sizeof(sch), host, sizeof(host));
        collect_resources(g_raw, sch, host);
    }
    if (g_nav_ok && !g_search_fb) {
        int is_google = find_substr_ci(g_nav_final, "google.") != 0;
        int is_sorry = find_substr_ci(g_nav_final, "/sorry/") != 0;
        if (page_is_blocked(g_raw) || is_sorry) {
            if (is_google && !is_sorry && g_google_tries < 3) {
                g_google_tries++;
            } else {
                trigger_search_fallback();
            }
        }
    }
}

static void browser_nav_done(int status, void *u) {
    (void)status; (void)u;

    if (!g_nav_ok) {
        g_block_count = 0;
        if (g_nav_is_https) {
            push_block(BLK_TEXT, "HTTPS getirilemedi (ag/DNS/TLS/sertifika).", 0);
        } else {
            push_block(BLK_TEXT, "Sayfa getirilemedi.", 0);
            push_block(BLK_TEXT, g_nav_url, 0);
            push_block(BLK_TEXT, "(Sunucu yok, DNS veya baglanti sorunu.)", 0);
        }
        render_blocks();
        set_status("Hata", ADL_DANGER);
    } else {
        bcopy_str(g_cur_url, g_nav_final, URL_MAX);
        parse_url(g_nav_final, g_cur_scheme, sizeof(g_cur_scheme), g_cur_host, sizeof(g_cur_host));
        render_page(g_raw);
        if (!g_nav_have_pending && g_cur_url[0] &&
            find_substr_ci(g_cur_url, "google.") && page_is_blocked(g_raw)) {
            trigger_search_fallback();
        }
        set_status(g_nav_final, ADL_TEXT_DIM);
    }

    if (g_nav_have_pending) {
        char next[URL_MAX];
        bcopy_str(next, g_nav_pending, sizeof(next));
        g_nav_have_pending = 0;
        navigate(next, 1);
    }
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

    if (!find_substr_ci(u, "duckduckgo.com")) g_search_fb = 0;
    if (!find_substr_ci(u, "google.")) g_google_tries = 0;

    if (record_history && g_cur_url[0] && !str_eq(g_cur_url, u))
        push_history(g_cur_url);

    bcopy_str(g_nav_url, u, URL_MAX);
    g_nav_is_https = starts_with(u, "https://");

    if (g_basi && g_basi->net && g_basi->net->submit) {
        if (g_basi->net->busy && g_basi->net->busy()) {
            bcopy_str(g_nav_pending, u, URL_MAX);
            g_nav_have_pending = 1;
            return;
        }
        if (g_basi->net->submit(browser_nav_work, browser_nav_done, 0) == 0) return;
    }

    browser_nav_work(0);
    browser_nav_done(0, 0);
}

/* ---------------- Form gonderimi ---------------- */
/* form_id'ye ait tum alanlari toplar, x-www-form-urlencoded sorgu dizesi
 * kurar ve action'a (yoksa mevcut sayfaya) GET olarak gezinir.
 * NOT: ASI ag katmaninda yalnizca GET vardir; method="post" olan formlar da
 * GET'e dusurulur. Gercek POST govdesi gerekiyorsa once asi->net'e bir
 * "https_post" ilkeli eklenmelidir - bu dosyanin degistiremeyecegi bir sey. */
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
            /* secilen index'e karsilik gelen degeri "deger1\ndeger2..." icinden cek */
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

/* ---------------- Linke tiklama ---------------- */
static void link_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_block_count) return;

    char href_copy[HREF_MAX];
    bcopy_str(href_copy, g_blocks[idx].href, sizeof(href_copy));

    char resolved[URL_MAX];
    resolve_url(g_cur_scheme, g_cur_host, href_copy, resolved, sizeof(resolved));
    if (!resolved[0]) return; /* sayfa ici capa (#...) - yoksay */

    if (g_addr) lv_textarea_set_text(g_addr, resolved);
    navigate(resolved, 1);
}

static void form_submit_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_block_count) return;
    submit_form(g_blocks[idx].form_id, "");
}

/* Metin kutusunda Enter'a basmak, o forma ait bir gonder butonu olmasa bile
 * formu gonderir (Google arama kutusu tam olarak boyle calisir). */
static void form_text_ready_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_block_count) return;
    submit_form(g_blocks[idx].form_id, "");
}

/* ---------------- Bloklari LVGL nesnelerine cizme ---------------- */
static void render_blocks(void) {
    if (!g_content) return;
    lv_obj_clean(g_content);
    lv_obj_set_style_bg_color(g_content, lv_color_hex(ADL_BG), 0);
    lv_obj_set_style_bg_opa(g_content, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_content, 10, 0);
    lv_obj_set_style_pad_row(g_content, 2, 0);

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
            if (b->href[0]) lv_textarea_set_text(ta, b->href); /* value= ile gelen baslangic degeri */
            lv_obj_set_style_pad_bottom(ta, 6, 0);
            lv_obj_add_event_cb(ta, form_text_ready_cb, LV_EVENT_READY, (void *)(intptr_t)i);
            b->widget = ta;
            break;
        }
        case BLK_FORM_HIDDEN:
            /* gorunmez alan, cizilmez */
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

/* ================= CSS motoru, DOM ve gomulu Google sayfasi ================= */

#define CSS_NODES 2600
#define CSS_RULES 4200
#define CSS_SEL   72
#define CSS_DECL  512
#define CSS_NTEXT 400

#define RES_MAX   40
#define IMG_MAX   40
#define SVG_BLOB_MAX 220000

typedef struct {
    char tag[12];
    char id[16];
    char cls[56];
    char istyle[180];
    char text[CSS_NTEXT];
    char src[160];
    char href[160];
    char name[28];
    char type[16];
    char value[80];
    char ph[80];
    char alt[48];
    int  wa, wa_pct, ha, ha_pct;
    int  align_attr;
    int  svg_off, svg_len;
    int  has_bgc; uint32_t bgc;
    int  form_id;
    int parent, first, last, next;
    lv_obj_t *widget;
} dnode_t;

typedef struct {
    char sel[CSS_SEL];
    int spec;
    int imp;
    char decl[CSS_DECL];
} css_rule_t;

typedef struct {
    uint32_t color; int has_color;
    uint32_t bg; int has_bg;
    int bg_transparent;
    int font_size;
    int has_font;
    int bold, italic, underline, strike;
    int align;
    int pad[4];
    int mar[4];
    int has_mar, has_pad;
    int has_w, width, w_pct;
    int has_h, height, h_pct;
    int has_minw, minw, minw_pct; int has_maxw, maxw, maxw_pct;
    int has_minh, minh, minh_pct; int has_maxh, maxh, maxh_pct;
    int radius;
    int border_w; uint32_t border_c;
    int shadow;
    int hide;
    int is_flex;
    int row;
    int wrap;
    int has_flexdir;
    int justify;
    int align_items;
    int align_self;
    int gap;
    int flex_grow;
    int position;
    int top, left, right, bottom;
    int has_top, has_left, has_right, has_bottom;
    int overflow_hidden;
    int line_height;
    int letter_space;
    int nowrap;
    int opacity;
    int has_grad; uint32_t grad_c; int grad_dir;
    int transform;
    int grid_cols;
    int object_fit;
    int aspect;
    int has_aspect;
    int color_a, bg_a, border_a;
    int has_outline, outline_w, outline_a; uint32_t outline_c;
    int shadow_x, shadow_y, shadow_spread, shadow_a, shadow_inset;
    int has_shadow; uint32_t shadow_c;
    int radius_tl, radius_tr, radius_br, radius_bl, has_corner_radius;
    int align_content;
    int flex_basis, has_flex_basis;
    int order, has_order;
    int list_type, has_list;
    int text_indent;
    int ellipsis;
    char bg_url[220];
    int has_bg_url;
    int bg_size, bg_size_w, bg_size_h, bg_size_wp, bg_size_hp;
    int bg_repeat;
    int has_bg_pos, bg_pos_x, bg_pos_y, bg_pos_xp, bg_pos_yp;
    int has_content; char content[160];
    int content_url; char content_src[160];
    int tx, ty, has_translate;
    int tscale, tscalex, tscaley, has_scale;
    int trot, has_rot;
    int grid_rows;
    int col_span, has_col_span;
    int row_span, has_row_span;
} css_style_t;

static dnode_t g_dn[CSS_NODES];
static int g_dn_count;
static css_rule_t g_css[CSS_RULES];
static int g_css_count;

static int g_pe;

static void css_trim(char *s);

#define CSS_VARS 96
static char g_vname[CSS_VARS][40];
static char g_vval[CSS_VARS][96];
static int g_vcount;

static void css_var_set(const char *name, const char *val) {
    int n = b_len(name);
    if (n > 39) n = 39;
    for (int i = 0; i < g_vcount; i++) {
        if (b_len(g_vname[i]) == n) {
            int k = 0;
            while (k < n && g_vname[i][k] == name[k]) k++;
            if (k == n) { bcopy_str(g_vval[i], val, 96); return; }
        }
    }
    if (g_vcount >= CSS_VARS) return;
    bcopy_str(g_vname[g_vcount], name, 40);
    bcopy_str(g_vval[g_vcount], val, 96);
    g_vcount++;
}

static const char *css_var_get(const char *name) {
    for (int i = 0; i < g_vcount; i++) {
        int ln = b_len(g_vname[i]);
        if (ln == b_len(name)) {
            int k = 0;
            while (k < ln && g_vname[i][k] == name[k]) k++;
            if (k == ln) return g_vval[i];
        }
    }
    return 0;
}

static void css_var_subst(const char *in, char *out, int cap) {
    int o = 0;
    const char *p = in;
    while (*p && o < cap - 1) {
        if (p[0] == 'v' && p[1] == 'a' && p[2] == 'r' && p[3] == '(') {
            const char *q = p + 4;
            while (*q == ' ') q++;
            char name[48]; int n = 0;
            while (*q && *q != ')' && *q != ',' && n < 47) name[n++] = *q++;
            name[n] = 0;
            css_trim(name);
            char fallback[96]; fallback[0] = 0;
            if (*q == ',') {
                q++;
                while (*q == ' ') q++;
                int fn = 0;
                while (*q && *q != ')' && fn < 95) fallback[fn++] = *q++;
                fallback[fn] = 0;
                css_trim(fallback);
            }
            const char *val = css_var_get(name);
            if (!val) val = fallback;
            if (val) { int i = 0; while (val[i] && o < cap - 1) out[o++] = val[i++]; }
            while (*p && *p != ')') p++;
            if (*p == ')') p++;
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = 0;
}

/* Sayfadaki <script> icerikleri (Wolf Engine ile calistirilir). */
static char g_script_src[300000];
static int  g_script_len;

static char g_svg_blob[SVG_BLOB_MAX];
static int  g_svg_blob_len;

/* CSS/HTML icinde tanimli formlar (blok katmanindaki g_forms'tan bagimsiz). */
typedef struct { char action[200]; int node; } pform_t;
static pform_t g_pforms[24];
static int g_pform_count;

/* Agdan cekilen ham kaynaklar (harici CSS dosyalari, resim baytlari). */
static char     g_res_url[RES_MAX][440];
static uint8_t *g_res_data[RES_MAX];
static uint32_t g_res_len[RES_MAX];
static int      g_res_css[RES_MAX];
static int      g_res_count;

/* Cozulmus resim arabellegi (ARGB8888). */
typedef struct { char url[440]; uint32_t *px; int w, h; } img_ent_t;
static img_ent_t g_imgs[IMG_MAX];
static int g_img_count;

static int strn_ci_eq(const char *a, const char *b, int n);

static void css_trim(char *s) {
    int n = b_len(s);
    while (n > 0 && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\n'||s[n-1]=='\r')) s[--n] = 0;
    int i = 0;
    while (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r') i++;
    int j = 0;
    while (s[i]) s[j++] = s[i++];
    s[j] = 0;
}

static int css_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

typedef struct { const char *name; uint32_t rgb; } css_named_t;
static int css_named(const char *s, uint32_t *out) {
    static const css_named_t t[] = {
        {"aliceblue",0xF0F8FF},{"antiquewhite",0xFAEBD7},{"aqua",0x00FFFF},
        {"aquamarine",0x7FFFD4},{"azure",0xF0FFFF},{"beige",0xF5F5DC},
        {"bisque",0xFFE4C4},{"black",0x000000},{"blanchedalmond",0xFFEBCD},
        {"blue",0x0000FF},{"blueviolet",0x8A2BE2},{"brown",0xA52A2A},
        {"burlywood",0xDEB887},{"cadetblue",0x5F9EA0},{"chartreuse",0x7FFF00},
        {"chocolate",0xD2691E},{"coral",0xFF7F50},{"cornflowerblue",0x6495ED},
        {"cornsilk",0xFFF8DC},{"crimson",0xDC143C},{"cyan",0x00FFFF},
        {"darkblue",0x00008B},{"darkcyan",0x008B8B},{"darkgoldenrod",0xB8860B},
        {"darkgray",0xA9A9A9},{"darkgrey",0xA9A9A9},{"darkgreen",0x006400},
        {"darkkhaki",0xBDB76B},{"darkmagenta",0x8B008B},{"darkolivegreen",0x556B2F},
        {"darkorange",0xFF8C00},{"darkorchid",0x9932CC},{"darkred",0x8B0000},
        {"darksalmon",0xE9967A},{"darkseagreen",0x8FBC8F},{"darkslateblue",0x483D8B},
        {"darkslategray",0x2F4F4F},{"darkslategrey",0x2F4F4F},{"darkturquoise",0x00CED1},
        {"darkviolet",0x9400D3},{"deeppink",0xFF1493},{"deepskyblue",0x00BFFF},
        {"dimgray",0x696969},{"dimgrey",0x696969},{"dodgerblue",0x1E90FF},
        {"firebrick",0xB22222},{"floralwhite",0xFFFAF0},{"forestgreen",0x228B22},
        {"fuchsia",0xFF00FF},{"gainsboro",0xDCDCDC},{"ghostwhite",0xF8F8FF},
        {"gold",0xFFD700},{"goldenrod",0xDAA520},{"gray",0x808080},
        {"grey",0x808080},{"green",0x008000},{"greenyellow",0xADFF2F},
        {"honeydew",0xF0FFF0},{"hotpink",0xFF69B4},{"indianred",0xCD5C5C},
        {"indigo",0x4B0082},{"ivory",0xFFFFF0},{"khaki",0xF0E68C},
        {"lavender",0xE6E6FA},{"lavenderblush",0xFFF0F5},{"lawngreen",0x7CFC00},
        {"lemonchiffon",0xFFFACD},{"lightblue",0xADD8E6},{"lightcoral",0xF08080},
        {"lightcyan",0xE0FFFF},{"lightgoldenrodyellow",0xFAFAD2},{"lightgray",0xD3D3D3},
        {"lightgrey",0xD3D3D3},{"lightgreen",0x90EE90},{"lightpink",0xFFB6C1},
        {"lightsalmon",0xFFA07A},{"lightseagreen",0x20B2AA},{"lightskyblue",0x87CEFA},
        {"lightslategray",0x778899},{"lightslategrey",0x778899},{"lightsteelblue",0xB0C4DE},
        {"lightyellow",0xFFFFE0},{"lime",0x00FF00},{"limegreen",0x32CD32},
        {"linen",0xFAF0E6},{"magenta",0xFF00FF},{"maroon",0x800000},
        {"mediumaquamarine",0x66CDAA},{"mediumblue",0x0000CD},{"mediumorchid",0xBA55D3},
        {"mediumpurple",0x9370DB},{"mediumseagreen",0x3CB371},{"mediumslateblue",0x7B68EE},
        {"mediumspringgreen",0x00FA9A},{"mediumturquoise",0x48D1CC},{"mediumvioletred",0xC71585},
        {"midnightblue",0x191970},{"mintcream",0xF5FFFA},{"mistyrose",0xFFE4E1},
        {"moccasin",0xFFE4B5},{"navajowhite",0xFFDEAD},{"navy",0x000080},
        {"oldlace",0xFDF5E6},{"olive",0x808000},{"olivedrab",0x6B8E23},
        {"orange",0xFFA500},{"orangered",0xFF4500},{"orchid",0xDA70D6},
        {"palegoldenrod",0xEEE8AA},{"palegreen",0x98FB98},{"paleturquoise",0xAFEEEE},
        {"palevioletred",0xDB7093},{"papayawhip",0xFFEFD5},{"peachpuff",0xFFDAB9},
        {"peru",0xCD853F},{"pink",0xFFC0CB},{"plum",0xDDA0DD},
        {"powderblue",0xB0E0E6},{"purple",0x800080},{"rebeccapurple",0x663399},
        {"red",0xFF0000},{"rosybrown",0xBC8F8F},{"royalblue",0x4169E1},
        {"saddlebrown",0x8B4513},{"salmon",0xFA8072},{"sandybrown",0xF4A460},
        {"seagreen",0x2E8B57},{"seashell",0xFFF5EE},{"sienna",0xA0522D},
        {"silver",0xC0C0C0},{"skyblue",0x87CEEB},{"slateblue",0x6A5ACD},
        {"slategray",0x708090},{"slategrey",0x708090},{"snow",0xFFFAFA},
        {"springgreen",0x00FF7F},{"steelblue",0x4682B4},{"tan",0xD2B48C},
        {"teal",0x008080},{"thistle",0xD8BFD8},{"tomato",0xFF6347},
        {"turquoise",0x40E0D0},{"violet",0xEE82EE},{"wheat",0xF5DEB3},
        {"white",0xFFFFFF},{"whitesmoke",0xF5F5F5},{"yellow",0xFFFF00},
        {"yellowgreen",0x9ACD32},
    };
    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++)
        if (str_eq_ci(s, t[i].name)) { *out = t[i].rgb; return 1; }
    return 0;
}

static void hsv_to_rgb(int h, int s, int v, uint32_t *out) {
    if (h < 0) h += 360;
    if (h >= 360) h %= 360;
    int c = v * s / 255;
    int hp = h / 60;
    int x = c * (h % 60) / 60;
    if (hp % 2 == 1) x = c - x;
    int m = v - c;
    int r, g, b;
    switch (hp) {
        case 0: r = c; g = x; b = 0; break;
        case 1: r = x; g = c; b = 0; break;
        case 2: r = 0; g = c; b = x; break;
        case 3: r = 0; g = x; b = c; break;
        case 4: r = x; g = 0; b = c; break;
        default: r = c; g = 0; b = x; break;
    }
    r += m; g += m; b += m;
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    *out = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static int css_comp(const char **pp, int *pct) {
    const char *p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    int n = 0, any = 0;
    if (*p == '-') p++;
    while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; any = 1; }
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') { p++; any = 1; } }
    while (*p == ' ') p++;
    *pct = 0;
    if (*p == '%') { *pct = 1; p++; }
    *pp = p;
    return any ? n : -1;
}

static int css_color(const char *s, uint32_t *out);

static int color_and_pct(const char *t, uint32_t *c, int *pct) {
    *pct = -1;
    char buf[96];
    bcopy_str(buf, t, sizeof(buf));
    css_trim(buf);
    char *pc = 0;
    for (char *p = buf; *p; p++) if (*p == '%') pc = p;
    if (pc) {
        char *q = pc;
        while (q > buf && (q[-1] == ' ' || q[-1] == '\t')) q--;
        char *num_end = q;
        while (q > buf && q[-1] >= '0' && q[-1] <= '9') q--;
        if (q < num_end) {
            int v = 0;
            for (char *r = q; r < num_end; r++) v = v * 10 + (*r - '0');
            *pct = v;
            char save = *q;
            *q = 0;
            css_trim(buf);
            int ok = css_color(buf, c);
            *q = save;
            return ok;
        }
        *pc = 0;
        css_trim(buf);
    }
    return css_color(buf, c);
}

static int g_css_alpha = 255;

static int css_alpha_val(const char **pp) {
    const char *p = *pp;
    while (*p == ' ' || *p == ',' || *p == '/') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    int ip = 0, any = 0;
    while (*p >= '0' && *p <= '9') { ip = ip * 10 + (*p - '0'); p++; any = 1; }
    double frac = 0;
    if (*p == '.') { p++; double f = 0.1; while (*p >= '0' && *p <= '9') { frac += (*p - '0') * f; f /= 10; p++; any = 1; } }
    if (!any) { *pp = p; return -1; }
    double v = ip + frac;
    if (*p == '%') { p++; v = v * 255.0 / 100.0; }
    else v = v <= 1.0 ? v * 255.0 : v;
    *pp = p;
    if (neg) v = -v;
    int r = (int)(v + 0.5);
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    return r;
}

static double d_log(double x) {
    if (x <= 0) return -1e30;
    int k = 0;
    while (x >= 2.0) { x /= 2.0; k++; }
    while (x < 1.0) { x *= 2.0; k--; }
    double z = (x - 1.0) / (x + 1.0);
    double z2 = z * z, sum = 0, term = z;
    for (int i = 1; i <= 21; i += 2) { sum += term / i; term *= z2; }
    return 2.0 * sum + k * 0.6931471805599453;
}

static double d_exp(double x) {
    int k = (int)(x / 0.6931471805599453 + (x >= 0 ? 0.5 : -0.5));
    double r = x - k * 0.6931471805599453;
    double sum = 1.0, term = 1.0;
    for (int i = 1; i <= 18; i++) { term *= r / i; sum += term; }
    while (k > 0) { sum *= 2.0; k--; }
    while (k < 0) { sum /= 2.0; k++; }
    return sum;
}

static double d_pow(double b, double e) {
    if (b < 0) return -d_exp(e * d_log(-b));
    if (b == 0) return 0;
    return d_exp(e * d_log(b));
}

static double d_sin(double x) {
    while (x > 3.141592653589793) x -= 6.283185307179586;
    while (x < -3.141592653589793) x += 6.283185307179586;
    double x2 = x * x, term = x, sum = x;
    for (int i = 3; i < 22; i += 2) { term *= -x2 / (double)(i * (i - 1)); sum += term; }
    return sum;
}

static double d_cos(double x) { return d_sin(x + 1.5707963267948966); }

static double css_compf(const char **pp, int *pct) {
    const char *p = *pp;
    while (*p == ' ' || *p == '\t' || *p == ',') p++;
    int neg = 0, any = 0;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    double v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; any = 1; }
    if (*p == '.') { p++; double f = 0.1; while (*p >= '0' && *p <= '9') { v += (*p - '0') * f; f /= 10; p++; any = 1; } }
    *pct = 0;
    if (*p == '%') { *pct = 1; p++; }
    else if ((p[0] == 'd' || p[0] == 'D') && (p[1] == 'e' || p[1] == 'E') && (p[2] == 'g' || p[2] == 'G')) p += 3;
    *pp = p;
    if (!any) return 0;
    return neg ? -v : v;
}

static void srgb_pack(double r, double g, double b, uint32_t *out, int alpha) {
    double v[3]; v[0] = r; v[1] = g; v[2] = b;
    int o[3];
    for (int i = 0; i < 3; i++) {
        double c = v[i];
        if (c < 0) c = 0;
        if (c > 1) c = 1;
        double s = (c <= 0.0031308) ? c * 12.92 : 1.055 * d_pow(c, 1.0 / 2.4) - 0.055;
        int iv = (int)(s * 255.0 + 0.5);
        if (iv < 0) iv = 0;
        if (iv > 255) iv = 255;
        o[i] = iv;
    }
    *out = ((uint32_t)o[0] << 16) | ((uint32_t)o[1] << 8) | (uint32_t)o[2];
    g_css_alpha = alpha;
}

static int css_lab_color(const char *s, uint32_t *out) {
    const char *p = s;
    int kind = 0;
    if (strn_ci_eq(p, "oklab(", 6)) { kind = 1; p += 6; }
    else if (strn_ci_eq(p, "oklch(", 6)) { kind = 2; p += 6; }
    else if (strn_ci_eq(p, "lab(", 4)) { kind = 3; p += 4; }
    else if (strn_ci_eq(p, "lch(", 4)) { kind = 4; p += 4; }
    else return -1;
    int p1 = 0, p2 = 0, p3 = 0;
    double c1 = css_compf(&p, &p1);
    double c2 = css_compf(&p, &p2);
    double c3 = css_compf(&p, &p3);
    while (*p == ' ' || *p == ',') p++;
    int alpha = 255;
    if (*p == '/') {
        p++;
        int ap = 0;
        const char *save = p;
        double a = css_compf(&p, &ap);
        if (p != save) {
            if (ap) alpha = (int)(a * 2.55 + 0.5);
            else alpha = (a <= 1.0) ? (int)(a * 255.0 + 0.5) : (int)(a + 0.5);
            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;
        }
    }
    if (kind == 1 || kind == 2) {
        double L = p1 ? c1 / 100.0 : c1;
        double a, b;
        if (kind == 2) { double C = p2 ? c2 / 100.0 : c2; double H = c3; a = C * d_cos(H * 3.141592653589793 / 180.0); b = C * d_sin(H * 3.141592653589793 / 180.0); }
        else { a = p2 ? c2 / 100.0 : c2; b = p3 ? c3 / 100.0 : c3; }
        double l_ = L + 0.3963377774 * a + 0.2158037573 * b;
        double m_ = L - 0.1055613458 * a - 0.0638541728 * b;
        double s_ = L - 0.0894841775 * a - 1.2914855480 * b;
        double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
        srgb_pack(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                  -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                  -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s, out, alpha);
    } else {
        double L = c1;
        double a, b;
        if (kind == 4) { double C = p2 ? c2 * 125.0 / 100.0 : c2; double H = c3; a = C * d_cos(H * 3.141592653589793 / 180.0); b = C * d_sin(H * 3.141592653589793 / 180.0); }
        else { a = c2; b = c3; }
        double fy = (L + 16.0) / 116.0;
        double fx = fy + a / 500.0;
        double fz = fy - b / 200.0;
        double e = 216.0 / 24389.0;
        double kk = 24389.0 / 27.0;
        double tx = fx * fx * fx, ty = fy * fy * fy, tz = fz * fz * fz;
        double X = 0.95047 * (tx > e ? tx : (116.0 * fx - 16.0) / kk);
        double Y = (ty > e ? ty : (116.0 * fy - 16.0) / kk);
        double Z = 1.08883 * (tz > e ? tz : (116.0 * fz - 16.0) / kk);
        srgb_pack(3.2406 * X - 1.5372 * Y - 0.4986 * Z,
                  -0.9689 * X + 1.8758 * Y + 0.0415 * Z,
                  0.0557 * X - 0.2040 * Y + 1.0570 * Z, out, alpha);
    }
    return 1;
}

static int css_color(const char *s, uint32_t *out) {
    if (!s || !s[0]) return 0;
    g_css_alpha = 255;
    if (str_eq_ci(s, "transparent")) { *out = 0; g_css_alpha = 0; return 1; }
    if (s[0] == '#') {
        int n = b_len(s + 1);
        if (n == 3 || n == 4) {
            int r = css_hexval(s[1]), g = css_hexval(s[2]), b = css_hexval(s[3]);
            if (r < 0 || g < 0 || b < 0) return 0;
            *out = (uint32_t)((r * 17) << 16 | (g * 17) << 8 | (b * 17));
            if (n == 4) { int a = css_hexval(s[4]); if (a >= 0) g_css_alpha = a * 17; }
            return 1;
        }
        if (n == 6 || n == 8) {
            int v[6];
            for (int i = 0; i < 6; i++) { v[i] = css_hexval(s[1 + i]); if (v[i] < 0) return 0; }
            *out = (uint32_t)((v[0]<<20)|(v[1]<<16)|(v[2]<<12)|(v[3]<<8)|(v[4]<<4)|v[5]);
            if (n == 8) {
                int a1 = css_hexval(s[7]), a2 = css_hexval(s[8]);
                if (a1 >= 0 && a2 >= 0) g_css_alpha = a1 * 16 + a2;
            }
            return 1;
        }
        return 0;
    }
    if ((s[0] == 'r' || s[0] == 'R') && (s[1] == 'g' || s[1] == 'G') &&
        (s[2] == 'b' || s[2] == 'B')) {
        const char *p = s + 3;
        if (*p == 'a' || *p == 'A') p++;
        while (*p && *p != '(') { if (*p == ',') return 0; p++; }
        if (*p != '(') return 0;
        p++;
        int pc;
        int r = css_comp(&p, &pc);
        while (*p == ' ' || *p == ',' || *p == '/') p++;
        int g = css_comp(&p, &pc);
        while (*p == ' ' || *p == ',' || *p == '/') p++;
        int b = css_comp(&p, &pc);
        if (r < 0) return 0;
        if (r > 255) r = 255;
        if (g < 0) g = 0; if (g > 255) g = 255;
        if (b < 0) b = 0; if (b > 255) b = 255;
        int av = css_alpha_val(&p);
        if (av >= 0) g_css_alpha = av;
        *out = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        return 1;
    }
    if ((s[0] == 'h' || s[0] == 'H') && (s[1] == 's' || s[1] == 'S') &&
        (s[2] == 'l' || s[2] == 'L')) {
        const char *p = s + 3;
        if (*p == 'a' || *p == 'A') p++;
        const char *q = p;
        while (*q && *q != '(') q++;
        if (*q != '(') return 0;
        p = q + 1;
        int pc;
        int h = css_comp(&p, &pc);
        while (*p == ' ' || *p == ',' || *p == '/') p++;
        int sat = css_comp(&p, &pc);
        int satpct = pc;
        while (*p == ' ' || *p == ',' || *p == '/') p++;
        int lum = css_comp(&p, &pc);
        int lumpct = pc;
        int av = css_alpha_val(&p);
        if (av >= 0) g_css_alpha = av;
        if (h < 0 || sat < 0 || lum < 0) return 0;
        if (!satpct) sat = sat * 100 / 255;
        if (!lumpct) lum = lum * 100 / 255;
        if (sat > 100) sat = 100;
        if (lum > 100) lum = 100;
        int v = lum * 255 / 100;
        if (lum > 50) v = 255 - (100 - lum) * 255 / 100;
        int ss = 0;
        if (lum > 0 && lum < 100) ss = sat * 255 / 100;
        hsv_to_rgb(h, ss, v, out);
        return 1;
    }
    if ((s[0] == 'h' || s[0] == 'H') && (s[1] == 'w' || s[1] == 'W') &&
        (s[2] == 'b' || s[2] == 'B')) {
        const char *p = s + 3;
        while (*p && *p != '(') p++;
        if (*p != '(') return 0;
        p++;
        int pc;
        int h = css_comp(&p, &pc);
        while (*p == ' ' || *p == ',' || *p == '/') p++;
        int w = css_comp(&p, &pc); int wpct = pc;
        while (*p == ' ' || *p == ',' || *p == '/') p++;
        int b = css_comp(&p, &pc); int bpct = pc;
        if (h < 0 || w < 0 || b < 0) return 0;
        int av = css_alpha_val(&p);
        if (av >= 0) g_css_alpha = av;
        if (!wpct) w = w * 100 / 255;
        if (!bpct) b = b * 100 / 255;
        if (w < 0) w = 0;
        if (b < 0) b = 0;
        if (w + b >= 100) {
            int g = (w + b) > 0 ? w * 255 / (w + b) : 0;
            *out = ((uint32_t)g << 16) | ((uint32_t)g << 8) | (uint32_t)g;
            return 1;
        }
        uint32_t pure = 0;
        hsv_to_rgb(h, 255, 255, &pure);
        int scale = 100 - w - b;
        int rr = (int)((pure >> 16 & 0xFF) * scale / 100 + w * 255 / 100);
        int gg = (int)((pure >> 8 & 0xFF) * scale / 100 + w * 255 / 100);
        int bb = (int)((pure & 0xFF) * scale / 100 + w * 255 / 100);
        if (rr < 0) rr = 0; if (rr > 255) rr = 255;
        if (gg < 0) gg = 0; if (gg > 255) gg = 255;
        if (bb < 0) bb = 0; if (bb > 255) bb = 255;
        *out = ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
        return 1;
    }
    {
        char pre[11];
        int k = 0;
        for (; k < 10 && s[k]; k++) pre[k] = lower_c(s[k]);
        pre[k] = 0;
        if (str_eq(pre, "color-mix(")) {
            const char *p = s + 10;
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
            char t1[96]; int n1 = 0;
            while (*p && *p != ',' && n1 < 95) t1[n1++] = *p++;
            t1[n1] = 0;
            if (*p == ',') p++;
            char t2[96]; int n2 = 0;
            while (*p && *p != ')' && n2 < 95) t2[n2++] = *p++;
            t2[n2] = 0;
            uint32_t c1 = 0, c2 = 0;
            int p1 = -1, p2 = -1;
            if (!color_and_pct(t1, &c1, &p1)) return 0;
            if (!color_and_pct(t2, &c2, &p2)) return 0;
            if (p1 < 0 && p2 < 0) { p1 = 50; p2 = 50; }
            else if (p1 < 0) p1 = 100 - p2;
            else if (p2 < 0) p2 = 100 - p1;
            if (p1 < 0) p1 = 0;
            if (p2 < 0) p2 = 0;
            int rr = (int)(((c1 >> 16 & 0xFF) * p1 + (c2 >> 16 & 0xFF) * p2) / 100);
            int gg = (int)(((c1 >> 8 & 0xFF) * p1 + (c2 >> 8 & 0xFF) * p2) / 100);
            int bb = (int)(((c1 & 0xFF) * p1 + (c2 & 0xFF) * p2) / 100);
            if (rr < 0) rr = 0; if (rr > 255) rr = 255;
            if (gg < 0) gg = 0; if (gg > 255) gg = 255;
            if (bb < 0) bb = 0; if (bb > 255) bb = 255;
            *out = ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
            return 1;
        }
    }
    {
        int lr = css_lab_color(s, out);
        if (lr >= 0) return lr;
    }
    return css_named(s, out);
}

static int css_num(const char *v, int *pct) {
    int i = 0, n = 0;
    while (v[i] == ' ') i++;
    if (v[i] == '-') i++;
    while (v[i] >= '0' && v[i] <= '9') { n = n * 10 + (v[i] - '0'); i++; }
    while (v[i] == ' ') i++;
    *pct = (v[i] == '%');
    return n;
}

static int g_view_w = 900, g_view_h = 600;

static int css_unit_px(int n, int u) {
    switch (u) {
        case 2: case 3: return n * 16;
        case 4: return n * 4 / 3;
        case 5: return g_view_w * n / 100;
        case 6: return g_view_h * n / 100;
        case 7: { int m = g_view_w < g_view_h ? g_view_w : g_view_h; return m * n / 100; }
        case 8: { int m = g_view_w > g_view_h ? g_view_w : g_view_h; return m * n / 100; }
        case 9: return n * 378 / 10;
        case 10: return n * 378 / 100;
        case 11: return n * 96;
        default: return n;
    }
}

typedef struct { double px; int pct, ok; } cexpr_t;

static double css_unit_factor(int u) {
    switch (u) {
        case 0: return 1.0;
        case 2: case 3: return 16.0;
        case 4: return 4.0 / 3.0;
        case 5: return (g_view_w > 0 ? g_view_w : 900) / 100.0;
        case 6: return (g_view_h > 0 ? g_view_h : 600) / 100.0;
        case 7: { int m = g_view_w < g_view_h ? g_view_w : g_view_h; return (m > 0 ? m : 600) / 100.0; }
        case 8: { int m = g_view_w > g_view_h ? g_view_w : g_view_h; return (m > 0 ? m : 900) / 100.0; }
        case 9: return 37.8;
        case 10: return 3.78;
        case 11: return 96.0;
        default: return 1.0;
    }
}

static cexpr_t cexpr_parse(const char **pp);
static cexpr_t cexpr_term(const char **pp);

static cexpr_t cexpr_op(cexpr_t a, cexpr_t b, char op) {
    cexpr_t r; r.ok = a.ok & b.ok;
    if (op == '+') { r.px = a.px + b.px; r.pct = a.pct | b.pct; }
    else if (op == '-') { r.px = a.px - b.px; r.pct = a.pct | b.pct; }
    else if (op == '*') { r.px = a.px * b.px; r.pct = a.pct | b.pct; }
    else { r.px = (b.px != 0.0) ? a.px / b.px : 0.0; r.pct = a.pct | b.pct; }
    return r;
}

static cexpr_t cexpr_primary(const char **pp) {
    const char *p = *pp;
    cexpr_t r; r.px = 0; r.pct = 0; r.ok = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '(') {
        p++;
        r = cexpr_parse(&p);
        while (*p == ' ') p++;
        if (*p == ')') p++;
        *pp = p;
        return r;
    }
    if (is_alpha(*p)) {
        char fn[16]; int n = 0;
        while (is_alpha(*p) && n < 15) { fn[n++] = lower_c(*p); p++; }
        fn[n] = 0;
        while (*p == ' ') p++;
        if (*p != '(') { *pp = p; return r; }
        p++;
        int iscalc = (n == 4 && fn[0] == 'c' && fn[1] == 'a' && fn[2] == 'l' && fn[3] == 'c');
        int ismin = (n == 3 && fn[0] == 'm' && fn[1] == 'i' && fn[2] == 'n');
        int ismax = (n == 3 && fn[0] == 'm' && fn[1] == 'a' && fn[2] == 'x');
        int isclamp = (n == 5 && fn[0] == 'c' && fn[1] == 'l' && fn[2] == 'a' && fn[3] == 'm' && fn[4] == 'p');
        if (iscalc) {
            r = cexpr_parse(&p);
            while (*p == ' ') p++;
            if (*p == ')') p++;
            *pp = p; return r;
        }
        if (ismin || ismax || isclamp) {
            cexpr_t a[8]; int na = 0;
            while (na < 8) {
                a[na++] = cexpr_parse(&p);
                while (*p == ' ') p++;
                if (*p == ',') { p++; continue; }
                break;
            }
            while (*p && *p != ')') p++;
            if (*p == ')') p++;
            r = a[0];
            if (ismin) { for (int i = 1; i < na; i++) if (a[i].px < r.px) r = a[i]; }
            else if (ismax) { for (int i = 1; i < na; i++) if (a[i].px > r.px) r = a[i]; }
            else if (isclamp && na == 3) { r = a[1]; if (r.px < a[0].px) r = a[0]; if (r.px > a[2].px) r = a[2]; }
            else r.ok = 0;
            *pp = p; return r;
        }
        while (*p && *p != ')') p++;
        if (*p == ')') p++;
        *pp = p; return r;
    }
    int neg = 0;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    double num = 0; int any = 0;
    while (*p >= '0' && *p <= '9') { num = num * 10 + (*p - '0'); p++; any = 1; }
    if (*p == '.') { p++; double f = 0.1; while (*p >= '0' && *p <= '9') { num += (*p - '0') * f; f /= 10; p++; any = 1; } }
    if (!any) { *pp = p; return r; }
    if (neg) num = -num;
    if (*p == '%') { p++; r.px = num * (g_view_w > 0 ? g_view_w : 900) / 100.0; r.pct = 1; r.ok = 1; *pp = p; return r; }
    int u = -1, len = 0;
    if (p[0] == 'p' && p[1] == 'x') { u = 0; len = 2; }
    else if (p[0] == 'e' && p[1] == 'm') { u = 2; len = 2; }
    else if (p[0] == 'r' && p[1] == 'e' && p[2] == 'm') { u = 3; len = 3; }
    else if (p[0] == 'p' && p[1] == 't') { u = 4; len = 2; }
    else if (p[0] == 'v' && p[1] == 'w') { u = 5; len = 2; }
    else if (p[0] == 'v' && p[1] == 'h') { u = 6; len = 2; }
    else if (p[0] == 'v' && p[1] == 'm' && p[2] == 'i' && p[3] == 'n') { u = 7; len = 4; }
    else if (p[0] == 'v' && p[1] == 'm' && p[2] == 'a' && p[3] == 'x') { u = 8; len = 4; }
    else if (p[0] == 'c' && p[1] == 'm') { u = 9; len = 2; }
    else if (p[0] == 'm' && p[1] == 'm') { u = 10; len = 2; }
    else if (p[0] == 'i' && p[1] == 'n') { u = 11; len = 2; }
    if (u >= 0) { p += len; r.px = num * css_unit_factor(u); r.pct = 0; r.ok = 1; }
    else { r.px = num; r.pct = 0; r.ok = 1; }
    *pp = p;
    return r;
}

static cexpr_t cexpr_term(const char **pp) {
    cexpr_t r = cexpr_primary(pp);
    for (;;) {
        const char *p = *pp;
        while (*p == ' ') p++;
        if (*p == '*' || *p == '/') { char op = *p++; cexpr_t b = cexpr_primary(&p); r = cexpr_op(r, b, op); *pp = p; }
        else { *pp = p; break; }
    }
    return r;
}

static cexpr_t cexpr_parse(const char **pp) {
    cexpr_t r = cexpr_term(pp);
    for (;;) {
        const char *p = *pp;
        while (*p == ' ') p++;
        if (*p == '+' || *p == '-') { char op = *p++; cexpr_t b = cexpr_term(&p); r = cexpr_op(r, b, op); *pp = p; }
        else { *pp = p; break; }
    }
    return r;
}

static int css_len(const char *v, int *pct, int *unit) {
    *pct = 0; *unit = 0;
    {
        const char *q = v;
        while (*q == ' ') q++;
        if (is_alpha(*q)) {
            char fn[16]; int n = 0;
            while (is_alpha(*q) && n < 15) { fn[n++] = lower_c(*q); q++; }
            fn[n] = 0;
            if ((n == 4 && fn[0] == 'c' && fn[1] == 'a' && fn[2] == 'l' && fn[3] == 'c') ||
                (n == 3 && fn[0] == 'm' && fn[1] == 'i' && fn[2] == 'n') ||
                (n == 3 && fn[0] == 'm' && fn[1] == 'a' && fn[2] == 'x') ||
                (n == 5 && fn[0] == 'c' && fn[1] == 'l' && fn[2] == 'a' && fn[3] == 'm' && fn[4] == 'p')) {
                const char *rp = v;
                cexpr_t e = cexpr_primary(&rp);
                if (e.ok) {
                    int view = g_view_w > 0 ? g_view_w : 900;
                    if (e.pct) { *pct = 1; *unit = 1; int r = (int)(e.px * 100.0 / view + 0.5); return r < 0 ? 0 : r; }
                    *pct = 0; *unit = 0;
                    return (int)(e.px + (e.px < 0 ? -0.5 : 0.5));
                }
                return -1;
            }
        }
    }
    int i = 0;
    while (v[i] == ' ') i++;
    int neg = 0;
    if (v[i] == '-') { neg = 1; i++; }
    else if (v[i] == '+') i++;
    if (!((v[i] >= '0' && v[i] <= '9') || v[i] == '.')) return -1;
    int n = 0;
    while (v[i] >= '0' && v[i] <= '9') { n = n * 10 + (v[i] - '0'); i++; }
    if (v[i] == '.') { i++; while (v[i] >= '0' && v[i] <= '9') i++; }
    if (v[i] == '%') { *pct = 1; *unit = 1; i++; }
    else if (v[i] == 'p' && v[i + 1] == 'x') { i += 2; *unit = 0; }
    else if (v[i] == 'e' && v[i + 1] == 'm') { i += 2; *unit = 2; }
    else if (v[i] == 'r' && v[i + 1] == 'e' && v[i + 2] == 'm') { i += 3; *unit = 3; }
    else if (v[i] == 'p' && v[i + 1] == 't') { i += 2; *unit = 4; }
    else if (v[i] == 'v' && v[i + 1] == 'w') { i += 2; *unit = 5; }
    else if (v[i] == 'v' && v[i + 1] == 'h') { i += 2; *unit = 6; }
    else if (v[i] == 'v' && v[i + 1] == 'm' && v[i + 2] == 'i' && v[i + 3] == 'n') { i += 4; *unit = 7; }
    else if (v[i] == 'v' && v[i + 1] == 'm' && v[i + 2] == 'a' && v[i + 3] == 'x') { i += 4; *unit = 8; }
    else if (v[i] == 'c' && v[i + 1] == 'm') { i += 2; *unit = 9; }
    else if (v[i] == 'm' && v[i + 1] == 'm') { i += 2; *unit = 10; }
    else if (v[i] == 'i' && v[i + 1] == 'n') { i += 2; *unit = 11; }
    while (v[i] == ' ') i++;
    if (v[i]) return -1;
    return neg ? -n : n;
}

static int css_len_px(const char *v, int *pct) {
    int u;
    int n = css_len(v, pct, &u);
    if (n < 0) return -1;
    if (*pct) return n;
    return css_unit_px(n, u);
}

static int css_auto(const char *v) { return str_eq_ci(v, "auto"); }

static void css_sides(const char *val, int *out) {
    int v[4], n = 0, pc;
    const char *q = val;
    while (*q && n < 4) {
        while (*q == ' ') q++;
        if (!*q) break;
        int x = css_len_px(q, &pc);
        if (x < 0) x = 0;
        v[n++] = x;
        while (*q && *q != ' ') q++;
    }
    if (n == 1) { for (int k = 0; k < 4; k++) out[k] = v[0]; }
    else if (n == 2) { out[0] = out[2] = v[0]; out[1] = out[3] = v[1]; }
    else if (n == 3) { out[0] = v[0]; out[1] = out[3] = v[1]; out[2] = v[2]; }
    else if (n == 4) { for (int k = 0; k < 4; k++) out[k] = v[k]; }
}

static int g_inh_font = 16;

static void css_bg(css_style_t *s, const char *val) {
    uint32_t c;
    if (str_eq_ci(val, "none")) { s->has_bg = 0; return; }
    if (str_eq_ci(val, "transparent")) { s->has_bg = 1; s->bg = 0; s->bg_transparent = 1; return; }
    if (css_color(val, &c)) { s->bg = c; s->bg_a = g_css_alpha; s->has_bg = 1; s->bg_transparent = 0; return; }
    char tok[64];
    const char *p = val;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        int n = 0;
        while (*p && *p != ' ' && *p != '\t' && n < 63) tok[n++] = *p++;
        tok[n] = 0;
        if (!tok[0]) break;
        if (str_eq_ci(tok, "transparent")) { s->has_bg = 1; s->bg = 0; s->bg_transparent = 1; }
        else if (css_color(tok, &c)) { s->bg = c; s->bg_a = g_css_alpha; s->has_bg = 1; s->bg_transparent = 0; }
    }
}

static void css_gradient(css_style_t *s, const char *val) {
    if (!find_substr_ci(val, "gradient")) return;
    int dir = 1;
    if (find_substr_ci(val, "to right") || find_substr_ci(val, "to left") ||
        find_substr_ci(val, "90deg") || find_substr_ci(val, "270deg") ||
        find_substr_ci(val, "to bottom right") || find_substr_ci(val, "to top right"))
        dir = 2;
    uint32_t cols[4];
    int nc = 0;
    const char *p = val;
    while (*p && nc < 4) {
        if (*p == '#') {
            char tok[10];
            int n = 1;
            tok[0] = '#';
            while (n < 9 && css_hexval(p[n]) >= 0) { tok[n] = p[n]; n++; }
            tok[n] = 0;
            uint32_t c;
            if ((n == 4 || n == 5 || n == 7 || n == 9) && css_color(tok, &c)) cols[nc++] = c;
            p += n;
            continue;
        }
        p++;
    }
    if (nc >= 2) {
        s->bg = cols[0];
        s->grad_c = cols[nc - 1];
        s->has_bg = 1;
        s->bg_transparent = 0;
        s->has_grad = 1;
        s->grad_dir = dir;
    }
}

static void css_url(css_style_t *s, const char *val) {
    const char *u = find_substr_ci(val, "url(");
    if (!u) return;
    u += 4;
    while (*u == ' ' || *u == '\'' || *u == '"') u++;
    int n = 0;
    while (u[n] && u[n] != ')' && u[n] != '\'' && u[n] != '"' && n < 219) n++;
    if (n <= 0) return;
    for (int i = 0; i < n; i++) s->bg_url[i] = u[i];
    s->bg_url[n] = 0;
    s->has_bg_url = 1;
}

static double css_f(const char *s) {
    while (*s == ' ') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    double v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    if (*s == '.') { s++; double f = 0.1; while (*s >= '0' && *s <= '9') { v += (*s - '0') * f; f /= 10; s++; } }
    return neg ? -v : v;
}

static void css_set(css_style_t *s, const char *prop, const char *val) {
    uint32_t c;
    int pc, unit;
    if (str_eq(prop, "color")) { if (css_color(val, &c)) { s->color = c; s->color_a = g_css_alpha; s->has_color = 1; } }
    else if (str_eq(prop, "background-color")) css_bg(s, val);
    else if (str_eq(prop, "background")) {
        if (str_eq_ci(val, "none")) s->has_bg = 0;
        else if (find_substr_ci(val, "gradient")) css_gradient(s, val);
        else css_bg(s, val);
        css_url(s, val);
    }
    else if (str_eq(prop, "background-image")) { css_gradient(s, val); css_url(s, val); }
    else if (str_eq(prop, "font-size")) {
        s->has_font = 1;
        if (str_eq_ci(val, "xx-small")) s->font_size = 9;
        else if (str_eq_ci(val, "x-small")) s->font_size = 10;
        else if (str_eq_ci(val, "small")) s->font_size = 12;
        else if (str_eq_ci(val, "medium")) s->font_size = 16;
        else if (str_eq_ci(val, "large")) s->font_size = 18;
        else if (str_eq_ci(val, "x-large")) s->font_size = 24;
        else if (str_eq_ci(val, "xx-large")) s->font_size = 32;
        else if (str_eq_ci(val, "smaller")) s->font_size = g_inh_font * 5 / 6;
        else if (str_eq_ci(val, "larger")) s->font_size = g_inh_font * 6 / 5;
        else {
            int n = css_len(val, &pc, &unit);
            if (n > 0) {
                if (pc) s->font_size = g_inh_font * n / 100;
                else if (unit == 2 || unit == 3) s->font_size = g_inh_font * n;
                else s->font_size = css_unit_px(n, unit);
            }
        }
    }
    else if (str_eq(prop, "font-weight")) {
        if (str_eq_ci(val, "bold") || str_eq_ci(val, "bolder")) s->bold = 1;
        else if (str_eq_ci(val, "normal") || str_eq_ci(val, "lighter")) s->bold = 0;
        else { int n = css_num(val, &pc); s->bold = (n >= 600); }
    }
    else if (str_eq(prop, "font-style")) {
        if (str_eq_ci(val, "italic") || str_eq_ci(val, "oblique")) s->italic = 1;
        else if (str_eq_ci(val, "normal")) s->italic = 0;
    }
    else if (str_eq(prop, "font")) {
        if (find_substr_ci(val, "italic") || find_substr_ci(val, "oblique")) s->italic = 1;
        if (find_substr_ci(val, "bold")) s->bold = 1;
        char tok[64];
        const char *p = val;
        while (*p) {
            while (*p == ' ') p++;
            int n = 0;
            while (*p && *p != ' ' && n < 63) tok[n++] = *p++;
            tok[n] = 0;
            if (!tok[0]) break;
            int p2, u2;
            int v = css_len(tok, &p2, &u2);
            if (v > 0 && (p2 || u2 == 2 || u2 == 3 || find_substr_ci(tok, "px") ||
                          find_substr_ci(tok, "pt"))) {
                if (p2) s->font_size = g_inh_font * v / 100;
                else if (u2 == 2 || u2 == 3) s->font_size = g_inh_font * v;
                else s->font_size = css_unit_px(v, u2);
                s->has_font = 1;
                break;
            }
        }
    }
    else if (str_eq(prop, "text-decoration") || str_eq(prop, "text-decoration-line")) {
        if (str_eq_ci(val, "none")) { s->underline = 0; s->strike = 0; }
        else {
            if (find_substr_ci(val, "underline")) s->underline = 1;
            if (find_substr_ci(val, "line-through")) s->strike = 1;
        }
    }
    else if (str_eq(prop, "text-align")) {
        if (str_eq_ci(val, "center")) s->align = 1;
        else if (str_eq_ci(val, "right") || str_eq_ci(val, "end")) s->align = 2;
        else if (str_eq_ci(val, "justify")) s->align = 3;
        else s->align = 0;
    }
    else if (str_eq(prop, "line-height")) {
        if (!str_eq_ci(val, "normal")) {
            int n = css_len(val, &pc, &unit);
            if (n > 0) {
                if (pc) s->line_height = s->font_size * n / 100;
                else if (unit == 0 && n < 10) s->line_height = s->font_size * n;
                else s->line_height = css_unit_px(n, unit);
            }
        }
    }
    else if (str_eq(prop, "letter-spacing")) { int n = css_len_px(val, &pc); if (n != -1) s->letter_space = n; }
    else if (str_eq(prop, "border-radius")) {
        int t[4]; int k = 0; const char *p = val;
        while (*p && k < 4) {
            while (*p == ' ') p++;
            char tok[32]; int n = 0;
            while (*p && *p != ' ' && *p != '/' && n < 31) tok[n++] = *p++;
            tok[n] = 0;
            if (!tok[0]) break;
            int q; int v = css_len_px(tok, &q);
            if (v >= 0) t[k++] = v;
            else break;
        }
        if (k == 1) s->radius = t[0];
        else if (k >= 2) {
            s->has_corner_radius = 1;
            s->radius_tl = t[0];
            s->radius_tr = t[1];
            if (k == 2) { s->radius_br = t[0]; s->radius_bl = t[1]; }
            else if (k == 3) { s->radius_br = t[2]; s->radius_bl = t[1]; }
            else { s->radius_br = t[2]; s->radius_bl = t[3]; }
        }
    }
    else if (str_eq(prop, "border-top-left-radius")) { int n = css_len_px(val, &pc); if (n >= 0) { s->has_corner_radius = 1; s->radius_tl = n; } }
    else if (str_eq(prop, "border-top-right-radius")) { int n = css_len_px(val, &pc); if (n >= 0) { s->has_corner_radius = 1; s->radius_tr = n; } }
    else if (str_eq(prop, "border-bottom-right-radius")) { int n = css_len_px(val, &pc); if (n >= 0) { s->has_corner_radius = 1; s->radius_br = n; } }
    else if (str_eq(prop, "border-bottom-left-radius")) { int n = css_len_px(val, &pc); if (n >= 0) { s->has_corner_radius = 1; s->radius_bl = n; } }
    else if (str_eq(prop, "border") || str_eq(prop, "border-top") ||
             str_eq(prop, "border-right") || str_eq(prop, "border-bottom") ||
             str_eq(prop, "border-left")) {
        if (str_eq_ci(val, "none") || find_substr_ci(val, "hidden")) s->border_w = 0;
        else {
            int saw = 0;
            char tok[64];
            const char *p = val;
            while (*p) {
                while (*p == ' ' || *p == '\t') p++;
                int n = 0;
                while (*p && *p != ' ' && *p != '\t' && n < 63) tok[n++] = *p++;
                tok[n] = 0;
                if (!tok[0]) break;
                int p2, u2;
                int w = css_len(tok, &p2, &u2);
                if (w >= 0 && (tok[0] >= '0' && tok[0] <= '9')) { s->border_w = w; saw = 1; }
                else if (css_color(tok, &c)) s->border_c = c, s->border_a = g_css_alpha;
            }
            if (!saw && s->border_w == 0) s->border_w = 1;
        }
    }
    else if (str_eq(prop, "border-width")) { int n = css_len_px(val, &pc); if (n >= 0) s->border_w = n; }
    else if (str_eq(prop, "border-color")) { if (css_color(val, &c)) { s->border_c = c; s->border_a = g_css_alpha; } }
    else if (str_eq(prop, "border-style")) {
        if (str_eq_ci(val, "none") || str_eq_ci(val, "hidden")) s->border_w = 0;
        else if (s->border_w == 0) s->border_w = 1;
    }
    else if (str_eq(prop, "box-shadow")) {
        if (!str_eq_ci(val, "none")) {
            int nums[5]; int nn = 0; uint32_t sc = 0; int hasc = 0; int inset = 0;
            const char *p = val;
            char tok[64];
            while (*p) {
                while (*p == ' ' || *p == ',') p++;
                int n = 0;
                while (*p && *p != ' ' && *p != ',' && n < 63) tok[n++] = *p++;
                tok[n] = 0;
                if (!tok[0]) break;
                if (str_eq_ci(tok, "inset")) { inset = 1; continue; }
                int q, u;
                int v = css_len(tok, &q, &u);
                if (v >= 0 && (tok[0] == '-' || tok[0] == '+' || (tok[0] >= '0' && tok[0] <= '9') || tok[0] == '.')) {
                    if (nn < 5) nums[nn++] = css_unit_px(v, u);
                } else if (css_color(tok, &sc)) hasc = 1;
            }
            s->has_shadow = 1;
            s->shadow_x = nn > 0 ? nums[0] : 0;
            s->shadow_y = nn > 1 ? nums[1] : 1;
            s->shadow = nn > 2 ? nums[2] : 0;
            s->shadow_spread = nn > 3 ? nums[3] : 0;
            s->shadow_inset = inset;
            if (hasc) { s->shadow_c = sc; s->shadow_a = g_css_alpha; }
        }
    }
    else if (str_eq(prop, "outline") || str_eq(prop, "outline-width")) {
        if (str_eq_ci(val, "none")) { s->has_outline = 0; s->outline_w = 0; }
        else {
            char tok[64];
            const char *p = val;
            while (*p) {
                while (*p == ' ' || *p == '\t') p++;
                int n = 0;
                while (*p && *p != ' ' && *p != '\t' && n < 63) tok[n++] = *p++;
                tok[n] = 0;
                if (!tok[0]) break;
                int q, u;
                int v = css_len(tok, &q, &u);
                if (v >= 0 && tok[0] >= '0' && tok[0] <= '9') { s->outline_w = css_unit_px(v, u); s->has_outline = 1; }
                else if (css_color(tok, &c)) { s->outline_c = c; s->outline_a = g_css_alpha; }
            }
            if (s->outline_w == 0) { s->outline_w = 1; s->has_outline = 1; }
        }
    }
    else if (str_eq(prop, "outline-color")) { if (css_color(val, &c)) { s->outline_c = c; s->outline_a = g_css_alpha; } }
    else if (str_eq(prop, "outline-style")) {
        if (str_eq_ci(val, "none")) { s->has_outline = 0; s->outline_w = 0; }
        else { s->has_outline = 1; if (s->outline_w == 0) s->outline_w = 1; }
    }
    else if (str_eq(prop, "box-sizing")) { }
    else if (str_eq(prop, "flex-basis")) {
        if (str_eq_ci(val, "auto") || str_eq_ci(val, "content")) s->has_flex_basis = 0;
        else { int q; int v = css_len_px(val, &q); if (v >= 0) { s->has_flex_basis = 1; s->flex_basis = v; } }
    }
    else if (str_eq(prop, "order")) { int q; int v = css_num(val, &q); s->has_order = 1; s->order = v; }
    else if (str_eq(prop, "align-content")) {
        if (str_eq_ci(val, "center")) s->align_content = 1;
        else if (str_eq_ci(val, "flex-end") || str_eq_ci(val, "end")) s->align_content = 2;
        else if (str_eq_ci(val, "space-between")) s->align_content = 3;
        else if (str_eq_ci(val, "space-around") || str_eq_ci(val, "space-evenly")) s->align_content = 4;
        else s->align_content = 0;
    }
    else if (str_eq(prop, "list-style-type") || str_eq(prop, "list-style")) {
        s->has_list = 1;
        if (find_substr_ci(val, "none")) s->list_type = 0;
        else if (find_substr_ci(val, "square")) s->list_type = 1;
        else if (find_substr_ci(val, "circle")) s->list_type = 2;
        else if (find_substr_ci(val, "disc")) s->list_type = 3;
        else if (find_substr_ci(val, "upper-alpha") || find_substr_ci(val, "upper-latin")) s->list_type = 5;
        else if (find_substr_ci(val, "lower-alpha") || find_substr_ci(val, "lower-latin")) s->list_type = 6;
        else if (find_substr_ci(val, "roman")) s->list_type = 7;
        else if (find_substr_ci(val, "decimal")) s->list_type = 4;
    }
    else if (str_eq(prop, "text-indent")) { int q; int v = css_len_px(val, &q); if (v >= 0) s->text_indent = v; }
    else if (str_eq(prop, "text-overflow")) { if (find_substr_ci(val, "ellipsis")) s->ellipsis = 1; }
    else if (str_eq(prop, "box-sizing")) { }
    else if (str_eq(prop, "display")) {
        if (str_eq_ci(val, "none")) { s->hide = 1; s->is_flex = 0; }
        else if (str_eq_ci(val, "flex") || str_eq_ci(val, "inline-flex") ||
                 str_eq_ci(val, "grid") || str_eq_ci(val, "inline-grid")) {
            s->is_flex = 1; s->hide = 0;
            if (!s->has_flexdir) s->row = 1;
        }
        else { s->is_flex = 0; s->hide = 0; }
    }
    else if (str_eq(prop, "visibility")) { if (str_eq_ci(val, "hidden")) s->hide = 1; }
    else if (str_eq(prop, "opacity")) {
        const char *p = val;
        while (*p == ' ') p++;
        int ip = 0;
        while (*p >= '0' && *p <= '9') { ip = ip * 10 + (*p - '0'); p++; }
        if (*p == '.') {
            p++; int frac = 0, div = 1;
            while (*p >= '0' && *p <= '9') { frac = frac * 10 + (*p - '0'); div *= 10; p++; }
            s->opacity = ip * 100 + frac * 100 / div;
        } else if (*p == '%') {
            s->opacity = ip;
        } else {
            s->opacity = (ip <= 1) ? ip * 100 : ip;
        }
    }
    else if (str_eq(prop, "flex-direction")) { s->row = (str_eq_ci(val, "row") || str_eq_ci(val, "row-reverse")); s->has_flexdir = 1; }
    else if (str_eq(prop, "flex-wrap")) s->wrap = (str_eq_ci(val, "wrap") || str_eq_ci(val, "wrap-reverse"));
    else if (str_eq(prop, "flex-flow")) {
        s->row = (find_substr_ci(val, "row") != 0);
        s->wrap = (find_substr_ci(val, "wrap") != 0) && !find_substr_ci(val, "nowrap");
        s->has_flexdir = 1;
    }
    else if (str_eq(prop, "justify-content")) {
        if (str_eq_ci(val, "center")) s->justify = 1;
        else if (str_eq_ci(val, "flex-end") || str_eq_ci(val, "end") || str_eq_ci(val, "right")) s->justify = 2;
        else if (str_eq_ci(val, "space-between")) s->justify = 3;
        else if (str_eq_ci(val, "space-around") || str_eq_ci(val, "space-evenly")) s->justify = 4;
        else s->justify = 0;
    }
    else if (str_eq(prop, "align-items")) {
        if (str_eq_ci(val, "center")) s->align_items = 1;
        else if (str_eq_ci(val, "flex-end") || str_eq_ci(val, "end")) s->align_items = 2;
        else s->align_items = 0;
    }
    else if (str_eq(prop, "align-self")) {
        if (str_eq_ci(val, "center")) s->align_self = 1;
        else if (str_eq_ci(val, "flex-end") || str_eq_ci(val, "end")) s->align_self = 2;
        else s->align_self = 0;
    }
    else if (str_eq(prop, "gap") || str_eq(prop, "grid-gap")) { int n = css_len_px(val, &pc); if (n > 0) s->gap = n; }
    else if (str_eq(prop, "flex-grow")) { int n = css_num(val, &pc); s->flex_grow = n; }
    else if (str_eq(prop, "flex")) {
        if (str_eq_ci(val, "none")) s->flex_grow = 0;
        else if (str_eq_ci(val, "initial") || str_eq_ci(val, "auto")) s->flex_grow = 1;
        else {
            const char *p = val;
            while (*p == ' ') p++;
            int n = 0, any = 0;
            while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; any = 1; }
            if (any) s->flex_grow = n;
        }
    }
    else if (str_eq(prop, "padding")) { s->has_pad = 1; css_sides(val, s->pad); }
    else if (str_eq(prop, "padding-top")) { s->has_pad = 1; int v = css_len_px(val, &pc); if (v >= 0) s->pad[0] = v; }
    else if (str_eq(prop, "padding-right")) { s->has_pad = 1; int v = css_len_px(val, &pc); if (v >= 0) s->pad[1] = v; }
    else if (str_eq(prop, "padding-bottom")) { s->has_pad = 1; int v = css_len_px(val, &pc); if (v >= 0) s->pad[2] = v; }
    else if (str_eq(prop, "padding-left")) { s->has_pad = 1; int v = css_len_px(val, &pc); if (v >= 0) s->pad[3] = v; }
    else if (str_eq(prop, "margin")) { s->has_mar = 1; css_sides(val, s->mar); }
    else if (str_eq(prop, "margin-top")) { s->has_mar = 1; int v = css_len_px(val, &pc); if (v >= 0) s->mar[0] = v; }
    else if (str_eq(prop, "margin-right")) { s->has_mar = 1; int v = css_len_px(val, &pc); if (v >= 0) s->mar[1] = v; }
    else if (str_eq(prop, "margin-bottom")) { s->has_mar = 1; int v = css_len_px(val, &pc); if (v >= 0) s->mar[2] = v; }
    else if (str_eq(prop, "margin-left")) { s->has_mar = 1; int v = css_len_px(val, &pc); if (v >= 0) s->mar[3] = v; }
    else if (str_eq(prop, "width")) { if (!css_auto(val)) { int v = css_len_px(val, &pc); if (v >= 0) { s->has_w = 1; s->width = v; s->w_pct = pc; } } }
    else if (str_eq(prop, "height")) { if (!css_auto(val)) { int v = css_len_px(val, &pc); if (v >= 0) { s->has_h = 1; s->height = v; s->h_pct = pc; } } }
    else if (str_eq(prop, "min-width")) { if (!css_auto(val)) { int v = css_len_px(val, &pc); if (v >= 0) { s->has_minw = 1; s->minw = v; s->minw_pct = pc; } } }
    else if (str_eq(prop, "max-width")) { if (!css_auto(val) && !str_eq_ci(val, "none")) { int v = css_len_px(val, &pc); if (v >= 0) { s->has_maxw = 1; s->maxw = v; s->maxw_pct = pc; } } }
    else if (str_eq(prop, "min-height")) { if (!css_auto(val)) { int v = css_len_px(val, &pc); if (v >= 0) { s->has_minh = 1; s->minh = v; s->minh_pct = pc; } } }
    else if (str_eq(prop, "max-height")) { if (!css_auto(val) && !str_eq_ci(val, "none")) { int v = css_len_px(val, &pc); if (v >= 0) { s->has_maxh = 1; s->maxh = v; s->maxh_pct = pc; } } }
    else if (str_eq(prop, "position")) {
        if (str_eq_ci(val, "relative")) s->position = 1;
        else if (str_eq_ci(val, "absolute")) s->position = 2;
        else if (str_eq_ci(val, "fixed")) s->position = 3;
        else s->position = 0;
    }
    else if (str_eq(prop, "inset")) {
        int t[4]; css_sides(val, t);
        s->has_top = 1; s->top = t[0];
        s->has_right = 1; s->right = t[1];
        s->has_bottom = 1; s->bottom = t[2];
        s->has_left = 1; s->left = t[3];
    }
    else if (str_eq(prop, "inset-inline")) {
        int t[4]; css_sides(val, t);
        s->has_left = 1; s->left = t[0];
        s->has_right = 1; s->right = t[1];
    }
    else if (str_eq(prop, "inset-block")) {
        int t[4]; css_sides(val, t);
        s->has_top = 1; s->top = t[0];
        s->has_bottom = 1; s->bottom = t[1];
    }
    else if (str_eq(prop, "top")) { int v = css_len_px(val, &pc); if (v != -1) { s->has_top = 1; s->top = v; } }
    else if (str_eq(prop, "left")) { int v = css_len_px(val, &pc); if (v != -1) { s->has_left = 1; s->left = v; } }
    else if (str_eq(prop, "right")) { int v = css_len_px(val, &pc); if (v != -1) { s->has_right = 1; s->right = v; } }
    else if (str_eq(prop, "bottom")) { int v = css_len_px(val, &pc); if (v != -1) { s->has_bottom = 1; s->bottom = v; } }
    else if (str_eq(prop, "overflow") || str_eq(prop, "overflow-x") || str_eq(prop, "overflow-y")) {
        if (str_eq_ci(val, "hidden") || str_eq_ci(val, "auto") || str_eq_ci(val, "scroll")) s->overflow_hidden = 1;
        else s->overflow_hidden = 0;
    }
    else if (str_eq(prop, "white-space")) s->nowrap = (str_eq_ci(val, "nowrap") || str_eq_ci(val, "pre") || str_eq_ci(val, "pre-wrap"));
    else if (str_eq(prop, "text-transform")) {
        if (str_eq_ci(val, "uppercase")) s->transform = 1;
        else if (str_eq_ci(val, "lowercase")) s->transform = 2;
        else if (str_eq_ci(val, "capitalize")) s->transform = 3;
        else s->transform = 0;
    }
    else if (str_eq(prop, "row-gap") || str_eq(prop, "column-gap")) { int n = css_len_px(val, &pc); if (n > 0) s->gap = n; }
    else if (str_eq(prop, "place-items") || str_eq(prop, "place-content")) {
        if (find_substr_ci(val, "center")) { s->align_items = 1; s->justify = 1; }
        else if (find_substr_ci(val, "end")) { s->align_items = 2; s->justify = 2; }
        else if (find_substr_ci(val, "between")) s->justify = 3;
    }
    else if (str_eq(prop, "place-self")) {
        if (str_eq_ci(val, "center")) s->align_self = 1;
        else if (str_eq_ci(val, "end")) s->align_self = 2;
    }
    else if (str_eq(prop, "object-fit")) {
        if (str_eq_ci(val, "contain")) s->object_fit = 1;
        else if (str_eq_ci(val, "cover")) s->object_fit = 2;
        else if (str_eq_ci(val, "none")) s->object_fit = 3;
        else s->object_fit = 0;
    }
    else if (str_eq(prop, "aspect-ratio")) {
        int a = 0, b = 0;
        const char *p = val;
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; }
        while (*p && *p != '/') p++;
        if (*p == '/') { p++; while (*p == ' ') p++; while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; } }
        if (a > 0) { s->has_aspect = 1; s->aspect = b > 0 ? a * 1000 / b : a * 1000; }
    }
    else if (str_eq(prop, "grid-template-columns")) {
        int cols = 0;
        if (find_substr_ci(val, "repeat(")) {
            const char *p = val;
            while (*p && *p != '(') p++;
            if (*p == '(') { p++; while (*p == ' ') p++; while (*p >= '0' && *p <= '9') { cols = cols * 10 + (*p - '0'); p++; } }
        } else if (!find_substr_ci(val, "auto-fill") && !find_substr_ci(val, "auto-fit")) {
            const char *p = val;
            int bad = 0;
            while (*p) {
                while (*p == ' ') p++;
                if (!*p) break;
                if (find_substr_ci(p, "minmax") || find_substr_ci(p, "min-content") || find_substr_ci(p, "max-content")) { bad = 1; break; }
                while (*p && *p != ' ') p++;
                cols++;
            }
            if (bad) cols = 0;
        }
        if (cols > 0 && cols <= 6) s->grid_cols = cols;
    }
    else if (str_eq(prop, "background-size")) {
        const char *p = val;
        while (*p == ' ') p++;
        if (str_eq_ci(p, "cover")) s->bg_size = 2;
        else if (str_eq_ci(p, "contain")) s->bg_size = 1;
        else if (str_eq_ci(p, "auto")) s->bg_size = 0;
        else {
            int got = 0, vals[2], pcts[2];
            while (*p && got < 2) {
                while (*p == ' ') p++;
                if (!*p) break;
                char tok[32]; int n = 0;
                while (*p && *p != ' ' && n < 31) tok[n++] = *p++;
                tok[n] = 0;
                if (str_eq_ci(tok, "auto")) { vals[got] = -1; pcts[got] = 0; got++; continue; }
                int pc; int v = css_len_px(tok, &pc);
                if (v < 0) { got = 0; break; }
                vals[got] = v; pcts[got] = pc; got++;
            }
            if (got >= 1) {
                s->bg_size = 3;
                s->bg_size_w = vals[0] < 0 ? 0 : vals[0]; s->bg_size_wp = pcts[0];
                if (got >= 2) { s->bg_size_h = vals[1] < 0 ? 0 : vals[1]; s->bg_size_hp = pcts[1]; }
                else { s->bg_size_h = 0; s->bg_size_hp = 0; }
            }
        }
    }
    else if (str_eq(prop, "background-repeat")) {
        if (find_substr_ci(val, "no-repeat")) s->bg_repeat = 1;
        else if (find_substr_ci(val, "repeat-x")) s->bg_repeat = 2;
        else if (find_substr_ci(val, "repeat-y")) s->bg_repeat = 3;
        else if (find_substr_ci(val, "repeat")) s->bg_repeat = 0;
    }
    else if (str_eq(prop, "background-position") || str_eq(prop, "background-position-x") ||
             str_eq(prop, "background-position-y")) {
        const char *p = val;
        int idx = 0;
        while (*p && idx < 2) {
            while (*p == ' ') p++;
            if (!*p) break;
            char tok[32]; int n = 0;
            while (*p && *p != ' ' && n < 31) tok[n++] = *p++;
            tok[n] = 0;
            int isy = (idx == 1) || find_substr_ci(prop, "-y");
            int pos = 0, pc = 0;
            if (str_eq_ci(tok, "left") || str_eq_ci(tok, "top")) { pos = 0; pc = 1; }
            else if (str_eq_ci(tok, "right") || str_eq_ci(tok, "bottom")) { pos = 100; pc = 1; }
            else if (str_eq_ci(tok, "center")) { pos = 50; pc = 1; }
            else { int u; int v = css_len(tok, &pc, &u); if (v < 0) { idx++; continue; } pos = pc ? v : css_unit_px(v, u); }
            if (isy) { s->bg_pos_y = pos; s->bg_pos_yp = pc; } else { s->bg_pos_x = pos; s->bg_pos_xp = pc; }
            s->has_bg_pos = 1;
            idx++;
        }
    }
    else if (str_eq(prop, "transform")) {
        const char *p = val;
        while (*p == ' ') p++;
        if (str_eq_ci(p, "none")) { s->has_translate = 0; s->has_scale = 0; s->has_rot = 0; }
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            char fn[24]; int k = 0;
            while (*p && *p != '(' && k < 23) fn[k++] = lower_c(*p++);
            fn[k] = 0;
            int fl = b_len(fn);
            while (fl > 0 && fn[fl - 1] == ' ') fn[--fl] = 0;
            if (*p == '(') p++;
            char ar[96]; int ai = 0;
            while (*p && *p != ')' && ai < 95) ar[ai++] = *p++;
            ar[ai] = 0;
            if (*p == ')') p++;
            if (str_eq(fn, "translate") || str_eq(fn, "translate3d")) {
                int pc; char t1[48], t2[48]; int n1 = 0, n2 = 0;
                const char *q = ar;
                while (*q == ' ') q++;
                while (*q && *q != ',' && n1 < 47) t1[n1++] = *q++;
                t1[n1] = 0;
                if (*q == ',') q++;
                while (*q == ' ') q++;
                while (*q && *q != ',' && n2 < 47) t2[n2++] = *q++;
                t2[n2] = 0;
                int v1 = css_len_px(t1, &pc);
                if (v1 >= 0) { s->has_translate = 1; s->tx = v1; }
                if (n2) { int v2 = css_len_px(t2, &pc); if (v2 >= 0) { s->has_translate = 1; s->ty = v2; } }
            }
            else if (str_eq(fn, "translatex")) { int pc; int v = css_len_px(ar, &pc); if (v >= 0) { s->has_translate = 1; s->tx = v; } }
            else if (str_eq(fn, "translatey")) { int pc; int v = css_len_px(ar, &pc); if (v >= 0) { s->has_translate = 1; s->ty = v; } }
            else if (str_eq(fn, "scale") || str_eq(fn, "scale3d")) {
                double v1 = css_f(ar);
                const char *q = ar;
                while (*q && *q != ',') q++;
                double v2 = v1;
                if (*q == ',') v2 = css_f(q + 1);
                s->has_scale = 1;
                s->tscalex = (int)(v1 * 256.0 + 0.5);
                s->tscaley = (int)(v2 * 256.0 + 0.5);
            }
            else if (str_eq(fn, "scalex")) { s->has_scale = 1; s->tscalex = (int)(css_f(ar) * 256.0 + 0.5); }
            else if (str_eq(fn, "scaley")) { s->has_scale = 1; s->tscaley = (int)(css_f(ar) * 256.0 + 0.5); }
            else if (str_eq(fn, "rotate") || str_eq(fn, "rotatez")) {
                double deg = css_f(ar);
                s->has_rot = 1;
                s->trot = (int)(deg * 10.0);
            }
            else if (str_eq(fn, "skewx")) { s->has_rot = 1; s->trot = 0; }
        }
    }
    else if (str_eq(prop, "transition") || str_eq(prop, "transition-property") ||
             str_eq(prop, "transition-duration") || str_eq(prop, "transition-timing-function") ||
             str_eq(prop, "transition-delay") || str_eq(prop, "animation") ||
             str_eq(prop, "animation-name") || str_eq(prop, "animation-duration")) { }
    else if (str_eq(prop, "content")) {
        const char *p = val;
        while (*p == ' ') p++;
        if (str_eq_ci(p, "none") || str_eq_ci(p, "normal")) {
            s->has_content = 1; s->content[0] = 0; s->content_url = 0;
        } else if (*p == '"' || *p == '\'') {
            char q = *p++;
            int o = 0;
            s->has_content = 1; s->content_url = 0;
            while (*p && *p != q && o < (int)sizeof(s->content) - 4) {
                if (*p == '\\' && p[1]) {
                    p++;
                    if (p[0] == 'n') { s->content[o++] = '\n'; p++; }
                    else if (p[0] == 't') { s->content[o++] = '\t'; p++; }
                    else if (css_hexval(p[0]) >= 0) {
                        int v = 0, n = 0;
                        while (n < 6 && p[n] && css_hexval(p[n]) >= 0) { v = v * 16 + css_hexval(p[n]); n++; }
                        p += n;
                        if (*p == ' ') p++;
                        if (v < 0x80) s->content[o++] = (char)v;
                        else if (v < 0x800) { s->content[o++] = (char)(0xC0 | (v >> 6)); s->content[o++] = (char)(0x80 | (v & 0x3F)); }
                        else { s->content[o++] = (char)(0xE0 | (v >> 12)); s->content[o++] = (char)(0x80 | ((v >> 6) & 0x3F)); s->content[o++] = (char)(0x80 | (v & 0x3F)); }
                    } else s->content[o++] = *p++;
                } else s->content[o++] = *p++;
            }
            s->content[o] = 0;
        }
        else if (strn_ci_eq(p, "attr(", 5)) {
            char nm[40]; int k = 0;
            p += 5;
            while (*p == ' ') p++;
            while (*p && *p != ')' && k < 39) nm[k++] = *p++;
            nm[k] = 0;
            css_trim(nm);
            s->has_content = 1; s->content_url = 0;
            s->content[0] = 1;
            bcopy_str(s->content + 1, nm, sizeof(s->content) - 1);
        }
        else if (strn_ci_eq(p, "url(", 4)) {
            char u[150]; int k = 0;
            p += 4;
            while (*p == ' ' || *p == '"' || *p == '\'') p++;
            while (*p && *p != ')' && *p != '"' && *p != '\'' && k < 149) u[k++] = *p++;
            u[k] = 0;
            s->has_content = 1; s->content_url = 1;
            bcopy_str(s->content_src, u, sizeof(s->content_src));
            s->content[0] = 0;
        }
        else {
            const char *qs = p;
            while (*qs && *qs != '"' && *qs != '\'') qs++;
            s->has_content = 1; s->content_url = 0;
            if (*qs) {
                char q = *qs++;
                int o = 0;
                while (*qs && *qs != q && o < (int)sizeof(s->content) - 1) s->content[o++] = *qs++;
                s->content[o] = 0;
            } else s->content[0] = 0;
        }
    }
    else if (str_eq(prop, "grid-column") || str_eq(prop, "grid-column-start") ||
             str_eq(prop, "grid-column-end")) {
        const char *p = val;
        while (*p == ' ') p++;
        int span = 0, num = 0;
        if (strn_ci_eq(p, "span", 4)) { span = 1; p += 4; while (*p == ' ') p++; }
        while (*p >= '0' && *p <= '9') { num = num * 10 + (*p - '0'); p++; }
        while (*p && *p != '/') p++;
        int endp = 0;
        if (*p == '/') {
            p++;
            while (*p == ' ') p++;
            if (strn_ci_eq(p, "span", 4)) { p += 4; while (*p == ' ') p++; }
            while (*p >= '0' && *p <= '9') { endp = endp * 10 + (*p - '0'); p++; }
        }
        int sp;
        if (span) sp = num > 0 ? num : 1;
        else if (endp > 0) sp = endp - (num > 0 ? num : 1);
        else sp = 1;
        if (sp < 1) sp = 1;
        if (sp > 6) sp = 6;
        if (!str_eq(prop, "grid-column-start") && !str_eq(prop, "grid-column-end")) {
            s->has_col_span = 1; s->col_span = sp;
        }
    }
    else if (str_eq(prop, "grid-row") || str_eq(prop, "grid-row-start") || str_eq(prop, "grid-row-end")) {
        const char *p = val;
        while (*p == ' ') p++;
        int span = 0, num = 0;
        if (strn_ci_eq(p, "span", 4)) { span = 1; p += 4; while (*p == ' ') p++; }
        while (*p >= '0' && *p <= '9') { num = num * 10 + (*p - '0'); p++; }
        while (*p && *p != '/') p++;
        int endp = 0;
        if (*p == '/') {
            p++;
            while (*p == ' ') p++;
            if (strn_ci_eq(p, "span", 4)) { p += 4; while (*p == ' ') p++; }
            while (*p >= '0' && *p <= '9') { endp = endp * 10 + (*p - '0'); p++; }
        }
        int sp;
        if (span) sp = num > 0 ? num : 1;
        else if (endp > 0) sp = endp - (num > 0 ? num : 1);
        else sp = 1;
        if (sp < 1) sp = 1;
        if (sp > 12) sp = 12;
        if (!str_eq(prop, "grid-row-start") && !str_eq(prop, "grid-row-end")) {
            s->has_row_span = 1; s->row_span = sp;
        }
    }
    else if (str_eq(prop, "grid-template-rows")) {
        int rows = 0;
        if (find_substr_ci(val, "repeat(")) {
            const char *p = val;
            while (*p && *p != '(') p++;
            if (*p == '(') { p++; while (*p == ' ') p++; while (*p >= '0' && *p <= '9') { rows = rows * 10 + (*p - '0'); p++; } }
        } else {
            const char *p = val;
            int bad = 0;
            while (*p) {
                while (*p == ' ') p++;
                if (!*p) break;
                if (find_substr_ci(p, "minmax") || find_substr_ci(p, "min-content") || find_substr_ci(p, "max-content")) { bad = 1; break; }
                while (*p && *p != ' ') p++;
                rows++;
            }
            if (bad) rows = 0;
        }
        if (rows > 0 && rows <= 40) s->grid_rows = rows;
    }
    else if (str_eq(prop, "inset-inline-start")) { int v = css_len_px(val, &pc); if (v != -1) { s->has_left = 1; s->left = v; } }
    else if (str_eq(prop, "inset-inline-end")) { int v = css_len_px(val, &pc); if (v != -1) { s->has_right = 1; s->right = v; } }
    else if (str_eq(prop, "inset-block-start")) { int v = css_len_px(val, &pc); if (v != -1) { s->has_top = 1; s->top = v; } }
    else if (str_eq(prop, "inset-block-end")) { int v = css_len_px(val, &pc); if (v != -1) { s->has_bottom = 1; s->bottom = v; } }
}

static void css_strip_important(char *v) {
    char *p = v;
    while (*p) {
        if (*p == '!') {
            const char *q = p + 1;
            while (*q == ' ') q++;
            if (strn_ci_eq(q, "important", 9)) { *p = 0; return; }
        }
        p++;
    }
}

static void css_apply(css_style_t *s, const char *decl) {
    const char *p = decl;
    while (*p) {
        while (*p == ';' || *p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++;
        if (!*p) break;
        char prop[32];
        int pi = 0;
        while (*p && *p != ':' && *p != ';' && pi < 31) prop[pi++] = *p++;
        prop[pi] = 0;
        css_trim(prop);
        if (*p != ':') { while (*p && *p != ';') p++; continue; }
        p++;
        char val[72];
        int vi = 0;
        while (*p && *p != ';' && vi < 71) val[vi++] = *p++;
        val[vi] = 0;
        css_trim(val);
        css_strip_important(val);
        if (prop[0] == '-' && prop[1] == '-') { css_var_set(prop, val); continue; }
        if (prop[0]) {
            char vs[192];
            css_var_subst(val, vs, sizeof(vs));
            css_set(s, prop, vs);
        }
    }
}

static int class_has(const char *cls, const char *tok, int tl) {
    const char *p = cls;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        const char *s = p;
        while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') s++;
        if ((int)(s - p) == tl) {
            int k = 0;
            while (k < tl && p[k] == tok[k]) k++;
            if (k == tl) return 1;
        }
        p = s;
    }
    return 0;
}

static int strn_ci_eq(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) if (lower_c(a[i]) != lower_c(b[i])) return 0;
    return 1;
}

static int prev_sib(int n) {
    int par = g_dn[n].parent;
    if (par < 0) return -1;
    int prev = -1;
    for (int c = g_dn[par].first; c >= 0 && c != n; c = g_dn[c].next) prev = c;
    return prev;
}

static int dn_attr(int n, const char *name, char *out, int cap) {
    const dnode_t *d = &g_dn[n];
    out[0] = 0;
    if (str_eq_ci(name, "class")) bcopy_str(out, d->cls, cap);
    else if (str_eq_ci(name, "id")) bcopy_str(out, d->id, cap);
    else if (str_eq_ci(name, "type")) bcopy_str(out, d->type, cap);
    else if (str_eq_ci(name, "name")) bcopy_str(out, d->name, cap);
    else if (str_eq_ci(name, "value")) bcopy_str(out, d->value, cap);
    else if (str_eq_ci(name, "placeholder") || str_eq_ci(name, "title")) bcopy_str(out, d->ph, cap);
    else if (str_eq_ci(name, "alt")) bcopy_str(out, d->alt, cap);
    else if (str_eq_ci(name, "src")) bcopy_str(out, d->src, cap);
    else if (str_eq_ci(name, "href")) bcopy_str(out, d->href, cap);
    else if (str_eq_ci(name, "style")) bcopy_str(out, d->istyle, cap);
    return out[0] != 0;
}

static int sel_attr_match(int n, const char *t) {
    char name[48]; int ni = 0;
    const char *p = t;
    if (*p == '*') p++;
    while (*p && *p != '=' && *p != '~' && *p != '|' && *p != '^' &&
           *p != '$' && *p != '*' && ni < 47) name[ni++] = *p++;
    name[ni] = 0;
    css_trim(name);
    for (int i = 0; name[i]; i++) if (name[i] == ' ' || name[i] == '\t') { name[i] = 0; break; }
    char val[80];
    if (!dn_attr(n, name, val, sizeof(val))) return 0;
    if (!*p) return 1;
    char op = *p++;
    if (*p == '=') p++;
    char want[80]; int wi = 0;
    if (*p == '"' || *p == '\'') {
        char q = *p++;
        while (*p && *p != q && wi < 79) want[wi++] = *p++;
        if (*p == q) p++;
    } else {
        while (*p && *p != ' ' && wi < 79) want[wi++] = *p++;
    }
    want[wi] = 0;
    if (op == '=') return str_eq_ci(val, want);
    if (op == '~') {
        const char *q = val;
        while (*q) {
            while (*q == ' ' || *q == '\t') q++;
            const char *s = q;
            while (*s && *s != ' ' && *s != '\t') s++;
            if ((int)(s - q) == wi && strn_ci_eq(q, want, wi)) return 1;
            q = s;
        }
        return 0;
    }
    if (op == '|') {
        if (str_eq_ci(val, want)) return 1;
        int wl = b_len(want);
        return val[wl] == '-' && strn_ci_eq(val, want, wl);
    }
    if (op == '^') {
        int wl = b_len(want);
        return wl > 0 && strn_ci_eq(val, want, wl);
    }
    if (op == '$') {
        int vl = b_len(val), wl = b_len(want);
        return wl > 0 && wl <= vl && strn_ci_eq(val + vl - wl, want, wl);
    }
    if (op == '*') return wi > 0 && find_substr_ci(val, want) != 0;
    return 0;
}

static int nth_match(const char *e, int idx) {
    while (*e == ' ') e++;
    if (str_eq_ci(e, "odd")) return idx % 2 == 1;
    if (str_eq_ci(e, "even")) return idx % 2 == 0;
    int a = 0, b = 0;
    if (*e == 'n' || *e == 'N') { a = 1; e++; }
    else if (*e == '-' && (e[1] == 'n' || e[1] == 'N')) { a = -1; e += 2; }
    else {
        int neg = 0;
        if (*e == '-') { neg = 1; e++; } else if (*e == '+') e++;
        int num = 0;
        while (*e >= '0' && *e <= '9') { num = num * 10 + (*e - '0'); e++; }
        if (*e == 'n' || *e == 'N') { a = neg ? -num : num; e++; }
        else return (neg ? -num : num) == idx;
    }
    if (*e == '+') { e++; while (*e >= '0' && *e <= '9') { b = b * 10 + (*e - '0'); e++; } }
    else if (*e == '-') { e++; while (*e >= '0' && *e <= '9') { b = b * 10 + (*e - '0'); e++; } b = -b; }
    if (a == 0) return idx == b;
    int d = idx - b;
    if (a > 0) return d >= 0 && d % a == 0;
    return d <= 0 && (-d) % (-a) == 0;
}

static int sel_match(int n, const char *sel);
static int sel_spec(const char *sel);

static int pseudo_inner(const char *t, char *out, int cap) {
    const char *a = t;
    while (*a && *a != '(') a++;
    if (*a != '(') { out[0] = 0; return 0; }
    a++;
    int depth = 1, o = 0;
    while (*a && depth) {
        if (*a == '(') depth++;
        else if (*a == ')') { depth--; if (depth == 0) break; }
        if (o < cap - 1) out[o++] = *a;
        a++;
    }
    out[o] = 0;
    return 1;
}

static int split_top(const char *list, char alts[][CSS_SEL], int maxalt) {
    int cnt = 0, depth = 0, o = 0;
    const char *p = list;
    for (;;) {
        char c = *p;
        if (c == 0 || (c == ',' && depth == 0)) {
            if (cnt < maxalt) {
                alts[cnt][o] = 0;
                if (o > 0) cnt++;
            }
            o = 0;
            if (c == 0) break;
            p++;
            continue;
        }
        if (c == '(') depth++;
        else if (c == ')' && depth > 0) depth--;
        if (cnt < maxalt && o < CSS_SEL - 1) alts[cnt][o++] = c;
        p++;
    }
    return cnt;
}

static int sel_list_match(int n, const char *list) {
    char alts[12][CSS_SEL];
    int c = split_top(list, alts, 12);
    for (int i = 0; i < c; i++) if (sel_match(n, alts[i])) return 1;
    return 0;
}

static int has_desc_match(int n, const char *sel) {
    for (int c = g_dn[n].first; c >= 0; c = g_dn[c].next) {
        if (sel_match(c, sel)) return 1;
        if (has_desc_match(c, sel)) return 1;
    }
    return 0;
}

static int sel_has_match(int n, const char *list) {
    char alts[8][CSS_SEL];
    int c = split_top(list, alts, 8);
    for (int i = 0; i < c; i++) {
        const char *p = alts[i];
        while (*p == ' ' || *p == '\t') p++;
        int comb = 0;
        if (*p == '>') { comb = 1; p++; }
        else if (*p == '+') { comb = 2; p++; }
        else if (*p == '~') { comb = 3; p++; }
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) continue;
        if (comb == 1) {
            for (int ch = g_dn[n].first; ch >= 0; ch = g_dn[ch].next)
                if (sel_match(ch, p)) return 1;
        } else if (comb == 2) {
            int s = g_dn[n].next;
            if (s >= 0 && sel_match(s, p)) return 1;
        } else if (comb == 3) {
            for (int s = g_dn[n].next; s >= 0; s = g_dn[s].next)
                if (sel_match(s, p)) return 1;
        } else {
            for (int ch = g_dn[n].first; ch >= 0; ch = g_dn[ch].next) {
                if (sel_match(ch, p)) return 1;
                if (has_desc_match(ch, p)) return 1;
            }
        }
    }
    return 0;
}

static int same_type(int a, int b) { return str_eq(g_dn[a].tag, g_dn[b].tag); }

static int prev_same_type(int n) {
    for (int p = prev_sib(n); p >= 0; p = prev_sib(p)) if (same_type(p, n)) return p;
    return -1;
}

static int next_same_type(int n) {
    for (int s = g_dn[n].next; s >= 0; s = g_dn[s].next) if (same_type(s, n)) return s;
    return -1;
}

static const char *find_of_clause(const char *s) {
    int depth = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '(') depth++;
        else if (*p == ')') { if (depth > 0) depth--; }
        else if (depth == 0 && (*p == ' ' || *p == '\t')) {
            const char *q = p + 1;
            while (*q == ' ') q++;
            if (strn_ci_eq(q, "of", 2) && (q[2] == ' ' || q[2] == '\t')) return p;
        }
    }
    return 0;
}

static int sel_pseudo_match(int n, const char *t) {
    if (starts_with(t, "first-child")) return prev_sib(n) < 0;
    if (starts_with(t, "last-child")) return g_dn[n].next < 0;
    if (starts_with(t, "only-child")) return prev_sib(n) < 0 && g_dn[n].next < 0;
    if (starts_with(t, "first-of-type")) return prev_same_type(n) < 0;
    if (starts_with(t, "last-of-type")) return next_same_type(n) < 0;
    if (starts_with(t, "only-of-type")) return prev_same_type(n) < 0 && next_same_type(n) < 0;
    if (starts_with(t, "root")) return g_dn[n].parent < 0;
    if (starts_with(t, "empty")) return g_dn[n].first < 0 && g_dn[n].text[0] == 0;
    if (starts_with(t, "scope")) return 1;
    if (starts_with(t, "link") || starts_with(t, "visited") || starts_with(t, "any-link"))
        return str_eq(g_dn[n].tag, "a") && g_dn[n].href[0] != 0;
    if (starts_with(t, "is(") || starts_with(t, "where(") || starts_with(t, "matches(")) {
        char inner[CSS_SEL];
        if (pseudo_inner(t, inner, CSS_SEL)) return sel_list_match(n, inner);
        return 0;
    }
    if (starts_with(t, "not(")) {
        char inner[CSS_SEL];
        if (pseudo_inner(t, inner, CSS_SEL)) return !sel_list_match(n, inner);
        return 1;
    }
    if (starts_with(t, "has(")) {
        char inner[CSS_SEL];
        if (pseudo_inner(t, inner, CSS_SEL)) return sel_has_match(n, inner);
        return 0;
    }
    if (starts_with(t, "nth-child(") || starts_with(t, "nth-last-child(")) {
        char inner[CSS_SEL];
        if (!pseudo_inner(t, inner, CSS_SEL)) return 0;
        char e[CSS_SEL], selof[CSS_SEL];
        const char *ofp = find_of_clause(inner);
        if (ofp) {
            int el = (int)(ofp - inner);
            if (el > CSS_SEL - 1) el = CSS_SEL - 1;
            for (int i = 0; i < el; i++) e[i] = inner[i];
            e[el] = 0;
            bcopy_str(selof, ofp + 3, CSS_SEL);
            css_trim(selof);
        } else { bcopy_str(e, inner, CSS_SEL); selof[0] = 0; }
        int idx = 1;
        if (t[4] == 'l') {
            for (int s = g_dn[n].next; s >= 0; s = g_dn[s].next)
                if (!selof[0] || sel_match(s, selof)) idx++;
        } else {
            for (int p = prev_sib(n); p >= 0; p = prev_sib(p))
                if (!selof[0] || sel_match(p, selof)) idx++;
        }
        return nth_match(e, idx);
    }
    if (starts_with(t, "nth-of-type(") || starts_with(t, "nth-last-of-type(")) {
        char inner[CSS_SEL];
        if (!pseudo_inner(t, inner, CSS_SEL)) return 0;
        int idx = 1;
        if (starts_with(t, "nth-last-of-type(")) {
            for (int s = next_same_type(n); s >= 0; s = next_same_type(s)) idx++;
        } else {
            for (int p = prev_same_type(n); p >= 0; p = prev_same_type(p)) idx++;
        }
        return nth_match(inner, idx);
    }
    if (starts_with(t, "lang(")) return 1;
    if (starts_with(t, "hover") || starts_with(t, "focus") || starts_with(t, "active") ||
        starts_with(t, "focus-within") || starts_with(t, "focus-visible") ||
        starts_with(t, "target") || starts_with(t, "disabled") ||
        starts_with(t, "checked") || starts_with(t, "required") ||
        starts_with(t, "placeholder") || starts_with(t, "autofill")) return 0;
    return 0;
}

static int sel_match_simple(int n, const char *sel) {
    const dnode_t *d = &g_dn[n];
    const char *p = sel;
    if (*p == '*') p++;
    else if (*p && *p != '.' && *p != '#' && *p != '[' && *p != ':') {
        const char *s = p;
        int tl = 0;
        while (*p && *p != '.' && *p != '#' && *p != '[' && *p != ':') { p++; tl++; }
        int k = 0;
        while (k < tl && d->tag[k] == s[k]) k++;
        if (k != tl || d->tag[k]) return 0;
    }
    while (*p) {
        if (*p == '.') {
            p++;
            const char *s = p;
            int tl = 0;
            while (*p && *p != '.' && *p != '#' && *p != '[' && *p != ':') { p++; tl++; }
            char tok[64];
            if (tl > 63) tl = 63;
            for (int i = 0; i < tl; i++) tok[i] = s[i];
            tok[tl] = 0;
            if (!class_has(d->cls, tok, tl)) return 0;
        } else if (*p == '#') {
            p++;
            const char *s = p;
            int tl = 0;
            while (*p && *p != '.' && *p != '#' && *p != '[' && *p != ':') { p++; tl++; }
            if (b_len(d->id) != tl) return 0;
            for (int i = 0; i < tl; i++) if (d->id[i] != s[i]) return 0;
        } else if (*p == '[') {
            p++;
            const char *s = p;
            int tl = 0;
            while (*p && *p != ']') { p++; tl++; }
            char tok[80];
            if (tl > 79) tl = 79;
            for (int i = 0; i < tl; i++) tok[i] = s[i];
            tok[tl] = 0;
            if (!sel_attr_match(n, tok)) return 0;
            if (*p == ']') p++;
        } else if (*p == ':') {
            if (p[1] == ':') {
                p += 2;
                while (*p && *p != '.' && *p != '#' && *p != '[' && *p != ':' &&
                       *p != ' ' && *p != '>' && *p != '+' && *p != '~') p++;
                if (*p == '(') {
                    int d = 0;
                    do { if (*p == '(') d++; else if (*p == ')') d--; if (!*p) break; p++; } while (d > 0);
                }
                continue;
            }
            p++;
            const char *s = p;
            while (*p && *p != '(' && *p != '.' && *p != '#' && *p != '[' && *p != ':' &&
                   *p != ' ' && *p != '>' && *p != '+' && *p != '~') p++;
            if (*p == '(') {
                int d = 0;
                do { if (*p == '(') d++; else if (*p == ')') d--; if (!*p) break; p++; } while (d > 0);
            }
            int tl = (int)(p - s);
            char tok[CSS_SEL];
            if (tl > CSS_SEL - 1) tl = CSS_SEL - 1;
            for (int i = 0; i < tl; i++) tok[i] = s[i];
            tok[tl] = 0;
            if (!sel_pseudo_match(n, tok)) return 0;
        } else p++;
    }
    return 1;
}

static int sel_match_from(int n, char *parts[], int comb[], int idx) {
    if (idx < 0) return 1;
    if (!sel_match_simple(n, parts[idx])) return 0;
    if (idx == 0) return 1;
    int c = comb[idx];
    if (c == 1) {
        int par = g_dn[n].parent;
        return par >= 0 && sel_match_from(par, parts, comb, idx - 1);
    }
    if (c == 2) {
        int ps = prev_sib(n);
        return ps >= 0 && sel_match_from(ps, parts, comb, idx - 1);
    }
    if (c == 3) {
        for (int ps = prev_sib(n); ps >= 0; ps = prev_sib(ps))
            if (sel_match_from(ps, parts, comb, idx - 1)) return 1;
        return 0;
    }
    for (int a = g_dn[n].parent; a >= 0; a = g_dn[a].parent)
        if (sel_match_from(a, parts, comb, idx - 1)) return 1;
    return 0;
}

static int sel_pe_strip(char *s) {
    for (char *p = s; *p; p++) {
        if (*p != ':') continue;
        if (p[1] == ':') {
            char nm[20]; int k = 0; const char *x = p + 2;
            while (k < 19 && ((*x >= 'a' && *x <= 'z') || (*x >= 'A' && *x <= 'Z') || *x == '-')) nm[k++] = lower_c(*x++);
            nm[k] = 0;
            if (!nm[0]) continue;
            *p = 0;
            if (str_eq(nm, "before")) return 1;
            if (str_eq(nm, "after")) return 2;
            return 3;
        }
        const char *x = p + 1;
        if (!((*x >= 'a' && *x <= 'z') || (*x >= 'A' && *x <= 'Z'))) continue;
        char nm[20]; int k = 0;
        while (k < 19 && ((*x >= 'a' && *x <= 'z') || (*x >= 'A' && *x <= 'Z'))) nm[k++] = lower_c(*x++);
        nm[k] = 0;
        if (str_eq(nm, "before")) { *p = 0; return 1; }
        if (str_eq(nm, "after")) { *p = 0; return 2; }
        if (str_eq(nm, "first-line") || str_eq(nm, "first-letter")) { *p = 0; return 3; }
    }
    return 0;
}

static int sel_match(int n, const char *sel) {
    char buf[CSS_SEL];
    bcopy_str(buf, sel, CSS_SEL);
    if (sel_pe_strip(buf) != g_pe) return 0;
    char *parts[12];
    int comb[12];
    int np = 0;
    char *p = buf;
    int pending = 0;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        int c = pending;
        pending = 0;
        if (*p == '>') { c = 1; p++; while (*p == ' ') p++; }
        else if (*p == '+') { c = 2; p++; while (*p == ' ') p++; }
        else if (*p == '~') { c = 3; p++; while (*p == ' ') p++; }
        if (!*p) break;
        char *s = p;
        int par = 0;
        while (*p) {
            if (*p == '(') { par++; p++; continue; }
            if (*p == ')' && par > 0) { par--; p++; continue; }
            if (par == 0 && (*p == ' ' || *p == '\t' || *p == '>' || *p == '+' || *p == '~')) break;
            p++;
        }
        char save = *p;
        *p = 0;
        if (np < 12) { parts[np] = s; comb[np] = c; np++; }
        if (save) {
            pending = (save == '>') ? 1 : (save == '+') ? 2 : (save == '~') ? 3 : 0;
            p++;
        }
    }
    if (np == 0) return 1;
    return sel_match_from(n, parts, comb, np - 1);
}

static int sel_list_max_spec(const char *list);

static int sel_spec(const char *sel) {
    int s = 0;
    const char *p = sel;
    while (*p) {
        char c = *p;
        if (c == '[') { s += 10; while (*p && *p != ']') p++; if (*p) p++; continue; }
        if (c == ':') {
            if (p[1] == ':') {
                p += 2;
                while (*p && *p != '.' && *p != '#' && *p != '[' && *p != ':' &&
                       *p != ' ' && *p != '>' && *p != '+' && *p != '~') p++;
                if (*p == '(') { int d = 0; do { if (*p == '(') d++; else if (*p == ')') d--; if (!*p) break; p++; } while (d > 0); }
                continue;
            }
            p++;
            const char *nm = p;
            while (*p && *p != '(' && *p != '.' && *p != '#' && *p != '[' && *p != ':' &&
                   *p != ' ' && *p != '>' && *p != '+' && *p != '~') p++;
            int nlen = (int)(p - nm);
            if (*p == '(') {
                char inner[CSS_SEL];
                pseudo_inner(nm, inner, CSS_SEL);
                int iswhere = (nlen == 5 && strn_ci_eq(nm, "where", 5));
                int isfunc = (nlen == 2 && strn_ci_eq(nm, "is", 2)) ||
                             (nlen == 3 && strn_ci_eq(nm, "not", 3)) ||
                             (nlen == 3 && strn_ci_eq(nm, "has", 3)) ||
                             (nlen == 7 && strn_ci_eq(nm, "matches", 7));
                if (iswhere) { }
                else if (isfunc) s += sel_list_max_spec(inner);
                else s += 10;
                int d = 0;
                do { if (*p == '(') d++; else if (*p == ')') d--; if (!*p) break; p++; } while (d > 0);
                continue;
            }
            s += 10;
            continue;
        }
        if (c == '#') { s += 100; p++; }
        else if (c == '.') { s += 10; p++; }
        else if (c == ' ' || c == '>' || c == '+' || c == '~' || c == '\t' || c == '\n') p++;
        else if (c == '*') { p++; }
        else { s += 1; while (*p && *p != '.' && *p != '#' && *p != ' ' && *p != ':' && *p != '[' && *p != '>' && *p != '+' && *p != '~') p++; }
    }
    return s;
}

static int sel_list_max_spec(const char *list) {
    char alts[12][CSS_SEL];
    int c = split_top(list, alts, 12);
    int m = 0;
    for (int i = 0; i < c; i++) { int v = sel_spec(alts[i]); if (v > m) m = v; }
    return m;
}

static int media_feature_ok(const char *name, const char *val) {
    int v;
    if (find_substr_ci(name, "width")) v = g_view_w > 0 ? g_view_w : 900;
    else if (find_substr_ci(name, "height")) v = g_view_h > 0 ? g_view_h : 600;
    else return 1;
    int pc;
    int n = css_len_px(val, &pc);
    if (n < 0) return 1;
    if (find_substr_ci(name, "min-")) return v >= n;
    if (find_substr_ci(name, "max-")) return v <= n;
    return v == n;
}

static int media_alt(const char *p, int len) {
    char b[256];
    int n = len;
    if (n > 255) n = 255;
    for (int i = 0; i < n; i++) b[i] = p[i];
    b[n] = 0;
    const char *q = b;
    while (*q == ' ') q++;
    int neg = 0;
    if (strn_ci_eq(q, "not", 3) && (q[3] == ' ' || q[3] == 0)) { neg = 1; q += 3; }
    int ok = 1;
    const char *s = q;
    while (*s) {
        if (*s == '(') {
            const char *e = s + 1;
            int d = 1;
            while (*e && d) { if (*e == '(') d++; else if (*e == ')') d--; if (d == 0) break; e++; }
            char ft[128]; int o = 0;
            for (const char *x = s + 1; x < e && o < 127; x++) ft[o++] = *x;
            ft[o] = 0;
            char *colon = ft;
            while (*colon && *colon != ':') colon++;
            if (*colon == ':') {
                *colon = 0;
                char *val = colon + 1;
                css_trim(ft);
                css_trim(val);
                if (!media_feature_ok(ft, val)) ok = 0;
            }
            s = (*e == ')') ? e + 1 : e;
        } else if (strn_ci_eq(s, "print", 5)) { ok = 0; s += 5; }
        else if (strn_ci_eq(s, "screen", 6)) s += 6;
        else if (strn_ci_eq(s, "all", 3)) s += 3;
        else s++;
    }
    return neg ? !ok : ok;
}

static int media_matches(const char *q) {
    const char *p = q;
    while (*p) {
        const char *c = p;
        int depth = 0;
        while (*c && !(*c == ',' && depth == 0)) {
            if (*c == '(') depth++;
            else if (*c == ')' && depth) depth--;
            c++;
        }
        if (media_alt(p, (int)(c - p))) return 1;
        if (!*c) break;
        p = c + 1;
    }
    return 0;
}

static int css_prop_supported(const char *p) {
    if (!p || !p[0]) return 0;
    int n = 0;
    for (const char *q = p; *q; q++) {
        char c = *q;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return 0;
        n++;
    }
    return n >= 1 && n <= 48;
}

static int supports_cond(const char **pp);

static int supports_primary(const char **pp) {
    const char *p = *pp;
    while (*p == ' ') p++;
    if (strn_ci_eq(p, "not", 3) && (p[3] == ' ' || p[3] == '(' || p[3] == 0)) {
        p += 3;
        int r = supports_primary(&p);
        *pp = p;
        return !r;
    }
    if (*p == '(') {
        p++;
        const char *q = p;
        int depth = 1;
        while (*q && depth) { if (*q == '(') depth++; else if (*q == ')') depth--; if (depth == 0) break; q++; }
        char inner[220];
        int n = (int)(q - p);
        if (n > 219) n = 219;
        for (int i = 0; i < n; i++) inner[i] = p[i];
        inner[n] = 0;
        int hascolon = 0, d2 = 0;
        for (char *x = inner; *x; x++) {
            if (*x == '(') d2++;
            else if (*x == ')') d2--;
            else if (*x == ':' && d2 == 0) { hascolon = 1; break; }
        }
        int r;
        if (hascolon) {
            char *colon = inner; d2 = 0;
            for (char *x = inner; *x; x++) {
                if (*x == '(') d2++;
                else if (*x == ')') d2--;
                else if (*x == ':' && d2 == 0) { colon = x; break; }
            }
            *colon = 0;
            char *prop = inner;
            css_trim(prop);
            r = css_prop_supported(prop);
        } else {
            const char *ip = inner;
            r = supports_cond(&ip);
        }
        p = (*q == ')') ? q + 1 : q;
        *pp = p;
        return r;
    }
    while (*p && *p != ' ') p++;
    *pp = p;
    return 0;
}

static int supports_cond(const char **pp) {
    int r = supports_primary(pp);
    const char *p = *pp;
    for (;;) {
        while (*p == ' ') p++;
        if (strn_ci_eq(p, "and", 3) && (p[3] == ' ' || p[3] == '(')) {
            p += 3;
            int b = supports_primary(&p);
            r = r && b;
        } else if (strn_ci_eq(p, "or", 2) && (p[2] == ' ' || p[2] == '(')) {
            p += 2;
            int b = supports_primary(&p);
            r = r || b;
        } else break;
    }
    *pp = p;
    return r;
}

static int supports_matches(const char *q) {
    const char *p = q;
    return supports_cond(&p);
}

static void css_parse(const char *css) {
    const char *p = css;
    while (*p && g_css_count < CSS_RULES) {
        while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r' || *p == '}') p++;
        if (!*p) break;
        if (p[0] == '/' && p[1] == '*') {
            const char *e = p + 2;
            while (*e && !(e[0] == '*' && e[1] == '/')) e++;
            p = (*e) ? e + 2 : e;
            continue;
        }
        const char *sel = p;
        const char *ob = p;
        while (*ob && *ob != '{' && *ob != ';') ob++;
        if (*ob == ';') { p = ob + 1; continue; }
        if (!*ob) break;
        char first = 0;
        const char *q = sel;
        while (q < ob && (*q == ' ' || *q == '\t' || *q == '\n')) q++;
        if (q < ob) first = *q;
        const char *ce = ob + 1;
        int depth = 1;
        while (*ce && depth > 0) {
            if (*ce == '{') depth++;
            else if (*ce == '}') depth--;
            if (depth == 0) break;
            ce++;
        }
        if (first == '@') {
            if (strn_ci_eq(sel, "@media", 6)) {
                char mq[256];
                int l = (int)(ob - (sel + 6));
                if (l < 0) l = 0;
                if (l > 255) l = 255;
                for (int i = 0; i < l; i++) mq[i] = sel[6 + i];
                mq[l] = 0;
                if (media_matches(mq)) {
                    const char *bs = ob + 1;
                    char saved = *ce;
                    *(char *)ce = 0;
                    css_parse(bs);
                    *(char *)ce = saved;
                }
            }
            else if (strn_ci_eq(sel, "@supports", 9)) {
                char cond[256];
                int l = (int)(ob - (sel + 9));
                if (l < 0) l = 0;
                if (l > 255) l = 255;
                for (int i = 0; i < l; i++) cond[i] = sel[9 + i];
                cond[l] = 0;
                if (supports_matches(cond)) {
                    const char *bs = ob + 1;
                    char saved = *ce;
                    *(char *)ce = 0;
                    css_parse(bs);
                    *(char *)ce = saved;
                }
            }
            p = (*ce == '}') ? ce + 1 : ce;
            continue;
        }
        char declb[CSS_DECL];
        int dl = (int)(ce - (ob + 1));
        if (dl > CSS_DECL - 1) dl = CSS_DECL - 1;
        for (int i = 0; i < dl; i++) declb[i] = ob[1 + i];
        declb[dl] = 0;
        css_trim(declb);
        int imp = 0;
        {
            char *ip = declb;
            while (*ip) {
                if (*ip == '!') {
                    const char *qq = ip + 1;
                    while (*qq == ' ') qq++;
                    if (strn_ci_eq(qq, "important", 9)) { imp = 1; break; }
                }
                ip++;
            }
        }
        char tmp[CSS_SEL * 6];
        int tl = (int)(ob - sel);
        if (tl > (int)sizeof(tmp) - 1) tl = (int)sizeof(tmp) - 1;
        for (int i = 0; i < tl; i++) tmp[i] = sel[i];
        tmp[tl] = 0;
        char *cur = tmp;
        while (*cur && g_css_count < CSS_RULES) {
            char *comma = cur;
            int br = 0;
            while (*comma) {
                if (*comma == '(') br++;
                else if (*comma == ')') { if (br) br--; }
                else if (*comma == ',' && !br) break;
                comma++;
            }
            char save = *comma;
            *comma = 0;
            char one[CSS_SEL];
            bcopy_str(one, cur, CSS_SEL);
            css_trim(one);
            if (one[0]) {
                bcopy_str(g_css[g_css_count].sel, one, CSS_SEL);
                g_css[g_css_count].spec = sel_spec(one);
                g_css[g_css_count].imp = imp;
                bcopy_str(g_css[g_css_count].decl, declb, CSS_DECL);
                g_css_count++;
            }
            if (!save) break;
            cur = comma + 1;
        }
        p = (*ce == '}') ? ce + 1 : ce;
    }
}

static int dn_new(const char *tag) {
    if (g_dn_count >= CSS_NODES) return -1;
    int i = g_dn_count++;
    dnode_t *d = &g_dn[i];
    d->tag[0] = 0; d->id[0] = 0; d->cls[0] = 0; d->istyle[0] = 0;
    d->text[0] = 0; d->src[0] = 0; d->href[0] = 0;
    d->name[0] = 0; d->type[0] = 0; d->value[0] = 0; d->ph[0] = 0; d->alt[0] = 0;
    d->wa = -1; d->wa_pct = 0; d->ha = -1; d->ha_pct = 0;
    d->align_attr = -1; d->has_bgc = 0; d->bgc = 0;
    d->form_id = -1; d->widget = 0;
    d->parent = -1; d->first = -1; d->last = -1; d->next = -1;
    bcopy_str(d->tag, tag, sizeof(d->tag));
    return i;
}

static void dn_add(int par, int ch) {
    if (par < 0 || ch < 0) return;
    g_dn[ch].parent = par;
    if (g_dn[par].last < 0) { g_dn[par].first = ch; g_dn[par].last = ch; }
    else { g_dn[g_dn[par].last].next = ch; g_dn[par].last = ch; }
}

/* HTML varligini cozer: &amp; &nbsp; &#NNN; &#xHH; ... Dondurulen deger
 * tuketilen karakter sayisidir; bilinmeyen varlikta 0. */
static int dom_entity(const char *p, int maxlen, uint32_t *cp) {
    if (maxlen < 3 || p[0] != '&') return 0;
    if (p[1] == '#') {
        uint32_t v = 0;
        int i = 2, hex = 0;
        if (i < maxlen && (p[i] == 'x' || p[i] == 'X')) { hex = 1; i++; }
        int start = i;
        while (i < maxlen && p[i] != ';') {
            int d;
            char c = p[i];
            if (c >= '0' && c <= '9') d = c - '0';
            else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return 0;
            v = v * (hex ? 16 : 10) + d;
            i++;
        }
        if (i >= maxlen || p[i] != ';' || i == start) return 0;
        *cp = v;
        return i + 1;
    }
    struct { const char *name; uint32_t cp; } tbl[] = {
        {"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'},
        {"apos;", '\''}, {"nbsp;", ' '}, {"mdash;", 0x2014}, {"ndash;", 0x2013},
        {"hellip;", 0x2026}, {"rsquo;", 0x2019}, {"lsquo;", 0x2018},
        {"ldquo;", 0x201C}, {"rdquo;", 0x201D}, {"copy;", 0x00A9},
        {"reg;", 0x00AE}, {"trade;", 0x2122}, {"middot;", 0x00B7},
        {"deg;", 0x00B0}, {"euro;", 0x20AC}, {"pound;", 0x00A3},
    };
    for (unsigned t = 0; t < sizeof(tbl) / sizeof(tbl[0]); t++) {
        int n = b_len(tbl[t].name);
        if (n >= maxlen) continue;
        int ok = 1;
        for (int j = 0; j < n; j++) if (p[j + 1] != tbl[t].name[j]) { ok = 0; break; }
        if (ok) { *cp = tbl[t].cp; return n + 1; }
    }
    return 0;
}

static void dn_decode(const char *in, char *out, int cap) {
    int o = 0, i = 0;
    while (in[i] && o < cap - 4) {
        if (in[i] == '&') {
            uint32_t cp = 0;
            int adv = dom_entity(in + i, b_len(in + i), &cp);
            if (adv > 0 && cp) {
                if (cp < 0x80) out[o++] = (char)cp;
                else if (cp < 0x800) {
                    out[o++] = (char)(0xC0 | (cp >> 6));
                    out[o++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    out[o++] = (char)(0xE0 | (cp >> 12));
                    out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    out[o++] = (char)(0x80 | (cp & 0x3F));
                }
                i += adv;
                continue;
            }
        }
        out[o++] = in[i++];
    }
    out[o] = 0;
}

static void dom_utf8(dnode_t *d, int *l, uint32_t cp) {
    if (cp == 0) return;
    if (cp < 0x80) { if (*l < CSS_NTEXT - 1) d->text[(*l)++] = (char)cp; return; }
    if (cp < 0x800) {
        if (*l < CSS_NTEXT - 2) {
            d->text[(*l)++] = (char)(0xC0 | (cp >> 6));
            d->text[(*l)++] = (char)(0x80 | (cp & 0x3F));
        }
    } else {
        if (*l < CSS_NTEXT - 3) {
            d->text[(*l)++] = (char)(0xE0 | (cp >> 12));
            d->text[(*l)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            d->text[(*l)++] = (char)(0x80 | (cp & 0x3F));
        }
    }
}

static void dn_text(int n, const char *s, int len) {
    dnode_t *d = &g_dn[n];
    int l = b_len(d->text);
    for (int i = 0; i < len && l < CSS_NTEXT - 1; i++) {
        char c = s[i];
        if (c == '&') {
            uint32_t cp = 0;
            int adv = dom_entity(s + i, len - i, &cp);
            if (adv > 0) { dom_utf8(d, &l, cp); i += adv - 1; continue; }
        }
        if (c == '\n' || c == '\t' || c == '\r') c = ' ';
        d->text[l++] = c;
    }
    d->text[l] = 0;
}

static int dn_void(const char *t) {
    return str_eq(t, "img") || str_eq(t, "br") || str_eq(t, "input") ||
           str_eq(t, "meta") || str_eq(t, "link") || str_eq(t, "hr") ||
           str_eq(t, "wbr") || str_eq(t, "source");
}

static void dom_parse(const char *html) {
    g_dn_count = 0;
    g_pform_count = 0;
    g_script_len = 0;
    g_svg_blob_len = 0;
    int root = dn_new("body");
    int st[80];
    int sp = 1;
    st[0] = root;
    int cur_form = -1;
    const char *p = html;
    while (*p) {
        if (*p == '<') {
            const char *tag_start = p;
            const char *q = p + 1;
            while (*q && *q != '>') q++;
            const char *tag_end = q;
            if (p[1] == '/') {
                char nm[16];
                get_tag_name(p + 1, nm, sizeof(nm));
                if (str_eq(nm, "form")) cur_form = -1;
                for (int i = sp - 1; i >= 1; i--) {
                    if (str_eq(g_dn[st[i]].tag, nm)) { sp = i; break; }
                }
                p = (*q == '>') ? q + 1 : q;
                continue;
            }
            if (p[1] == '!' || p[1] == '?') { p = (*q == '>') ? q + 1 : q; continue; }
            char nm[16];
            get_tag_name(p + 1, nm, sizeof(nm));
            if (nm[0]) {
                if (str_eq(nm, "script")) {
                    char sa[200];
                    const char *c = find_substr_ci(q, "</script");
                    if (!find_attr(tag_start, tag_end, "src", sa, sizeof(sa)) && c) {
                        int n = (int)(c - (q + 1));
                        if (n > 0 && g_script_len + n + 2 < (int)sizeof(g_script_src)) {
                            for (int k = 0; k < n; k++) g_script_src[g_script_len++] = q[1 + k];
                            g_script_src[g_script_len++] = '\n';
                        }
                    }
                    if (c) {
                        const char *ce = c;
                        while (*ce && *ce != '>') ce++;
                        p = (*ce == '>') ? ce + 1 : ce;
                    } else {
                        p = (*q == '>') ? q + 1 : q;
                    }
                    continue;
                }
                if (str_eq(nm, "svg")) {
                    const char *c = find_substr_ci(q, "</svg");
                    const char *ce = c ? c : q;
                    while (*ce && *ce != '>') ce++;
                    const char *end = (*ce == '>') ? ce + 1 : ce;
                    int sl = (int)(end - tag_start);
                    if (sl > 0 && sl < 60000 && g_svg_blob_len + sl + 1 < SVG_BLOB_MAX) {
                        int idx = dn_new("svg");
                        if (idx >= 0) {
                            dn_add(st[sp - 1], idx);
                            g_dn[idx].svg_off = g_svg_blob_len;
                            g_dn[idx].svg_len = sl;
                            for (int k = 0; k < sl; k++) g_svg_blob[g_svg_blob_len++] = tag_start[k];
                            g_svg_blob[g_svg_blob_len++] = 0;
                            char a[200];
                            if (find_attr(tag_start, tag_end, "class", a, sizeof(a))) bcopy_str(g_dn[idx].cls, a, sizeof(g_dn[idx].cls));
                            if (find_attr(tag_start, tag_end, "id", a, sizeof(a))) bcopy_str(g_dn[idx].id, a, sizeof(g_dn[idx].id));
                            if (find_attr(tag_start, tag_end, "style", a, sizeof(a))) bcopy_str(g_dn[idx].istyle, a, sizeof(g_dn[idx].istyle));
                            if (find_attr(tag_start, tag_end, "width", a, sizeof(a))) g_dn[idx].wa = css_num(a, &g_dn[idx].wa_pct);
                            if (find_attr(tag_start, tag_end, "height", a, sizeof(a))) g_dn[idx].ha = css_num(a, &g_dn[idx].ha_pct);
                        }
                    }
                    p = end;
                    continue;
                }
                if (str_eq(nm, "style") || str_eq(nm, "head") ||
                    str_eq(nm, "title") || str_eq(nm, "noscript") || str_eq(nm, "svg") ||
                    str_eq(nm, "template") || str_eq(nm, "iframe") || str_eq(nm, "object") ||
                    str_eq(nm, "canvas") || str_eq(nm, "video") || str_eq(nm, "audio") ||
                    str_eq(nm, "map")) {
                    char close[24];
                    bcopy_str(close, "</", sizeof(close));
                    bcat(close, nm, sizeof(close));
                    const char *c = find_substr_ci(q, close);
                    if (c) {
                        const char *ce = c;
                        while (*ce && *ce != '>') ce++;
                        p = (*ce == '>') ? ce + 1 : ce;
                    } else {
                        p = (*q == '>') ? q + 1 : q;
                    }
                    continue;
                }
                int idx = dn_new(nm);
                if (idx >= 0) {
                    dn_add(st[sp - 1], idx);
                    char a[200];
                    if (find_attr(tag_start, tag_end, "class", a, sizeof(a))) bcopy_str(g_dn[idx].cls, a, sizeof(g_dn[idx].cls));
                    if (find_attr(tag_start, tag_end, "id", a, sizeof(a))) bcopy_str(g_dn[idx].id, a, sizeof(g_dn[idx].id));
                    if (find_attr(tag_start, tag_end, "style", a, sizeof(a))) bcopy_str(g_dn[idx].istyle, a, sizeof(g_dn[idx].istyle));
                    if (find_attr(tag_start, tag_end, "src", a, sizeof(a))) bcopy_str(g_dn[idx].src, a, sizeof(g_dn[idx].src));
                    if (find_attr(tag_start, tag_end, "href", a, sizeof(a))) bcopy_str(g_dn[idx].href, a, sizeof(g_dn[idx].href));
                    if (find_attr(tag_start, tag_end, "name", a, sizeof(a))) bcopy_str(g_dn[idx].name, a, sizeof(g_dn[idx].name));
                    if (find_attr(tag_start, tag_end, "type", a, sizeof(a))) bcopy_str(g_dn[idx].type, a, sizeof(g_dn[idx].type));
                    if (find_attr(tag_start, tag_end, "value", a, sizeof(a))) dn_decode(a, g_dn[idx].value, sizeof(g_dn[idx].value));
                    if (find_attr(tag_start, tag_end, "placeholder", a, sizeof(a))) dn_decode(a, g_dn[idx].ph, sizeof(g_dn[idx].ph));
                    else if (find_attr(tag_start, tag_end, "title", a, sizeof(a))) dn_decode(a, g_dn[idx].ph, sizeof(g_dn[idx].ph));
                    if (find_attr(tag_start, tag_end, "alt", a, sizeof(a))) dn_decode(a, g_dn[idx].alt, sizeof(g_dn[idx].alt));
                    if (find_attr(tag_start, tag_end, "width", a, sizeof(a))) g_dn[idx].wa = css_num(a, &g_dn[idx].wa_pct);
                    if (find_attr(tag_start, tag_end, "height", a, sizeof(a))) g_dn[idx].ha = css_num(a, &g_dn[idx].ha_pct);
                    if (g_dn[idx].wa < 0 && str_eq(nm, "input")) {
                        char sz[16];
                        if (find_attr(tag_start, tag_end, "size", sz, sizeof(sz))) {
                            int pc = 0, v = css_num(sz, &pc);
                            if (v > 0) { g_dn[idx].wa = v * 8; g_dn[idx].wa_pct = 0; }
                        }
                    }
                    if (find_attr(tag_start, tag_end, "align", a, sizeof(a))) {
                        if (str_eq_ci(a, "center")) g_dn[idx].align_attr = 1;
                        else if (str_eq_ci(a, "right")) g_dn[idx].align_attr = 2;
                        else g_dn[idx].align_attr = 0;
                    }
                    if (find_attr(tag_start, tag_end, "bgcolor", a, sizeof(a))) {
                        uint32_t c;
                        if (css_color(a, &c)) { g_dn[idx].has_bgc = 1; g_dn[idx].bgc = c; }
                    }
                    g_dn[idx].form_id = cur_form;
                    if (str_eq(nm, "form")) {
                        if (g_pform_count < 24) {
                            pform_t *f = &g_pforms[g_pform_count];
                            if (find_attr(tag_start, tag_end, "action", a, sizeof(a))) bcopy_str(f->action, a, sizeof(f->action));
                            else f->action[0] = 0;
                            f->node = idx;
                            g_dn[idx].form_id = g_pform_count;
                            cur_form = g_pform_count;
                            g_pform_count++;
                        }
                    }
                    if (str_eq(nm, "br") && sp >= 1) dn_text(st[sp - 1], "\n", 1);
                    int selfclose = (tag_end > tag_start + 1 && tag_end[-1] == '/');
                    if (!dn_void(nm) && !selfclose && sp < 79) st[sp++] = idx;
                }
            }
            p = (*q == '>') ? q + 1 : q;
            continue;
        }
        const char *ts = p;
        while (*p && *p != '<') p++;
        dn_text(st[sp - 1], ts, (int)(p - ts));
    }
}

static void style_defaults(css_style_t *s) {
    s->color = 0x202124; s->has_color = 0;
    s->bg = 0xFFFFFF; s->has_bg = 0; s->bg_transparent = 0;
    s->font_size = 14; s->has_font = 0; s->bold = 0; s->italic = 0;
    s->underline = 0; s->strike = 0; s->align = 0;
    for (int i = 0; i < 4; i++) { s->pad[i] = 0; s->mar[i] = 0; }
    s->has_mar = 0; s->has_pad = 0;
    s->has_w = 0; s->width = 0; s->w_pct = 0;
    s->has_h = 0; s->height = 0; s->h_pct = 0;
    s->has_minw = 0; s->minw = 0; s->minw_pct = 0; s->has_maxw = 0; s->maxw = 0; s->maxw_pct = 0;
    s->has_minh = 0; s->minh = 0; s->minh_pct = 0; s->has_maxh = 0; s->maxh = 0; s->maxh_pct = 0;
    s->radius = 0; s->border_w = 0; s->border_c = 0xDDDDDD; s->shadow = -1; s->hide = 0;
    s->is_flex = 0; s->row = 0; s->wrap = 0; s->justify = 0; s->align_items = 0;
    s->has_flexdir = 0;
    s->align_self = 0; s->gap = 0; s->flex_grow = 0;
    s->position = 0; s->top = s->left = s->right = s->bottom = 0;
    s->has_top = 0; s->has_left = 0; s->has_right = 0; s->has_bottom = 0;
    s->overflow_hidden = 0; s->line_height = 0; s->letter_space = 0;
    s->nowrap = 0; s->opacity = 0;
    s->has_grad = 0; s->grad_c = 0; s->grad_dir = 0; s->transform = 0;
    s->grid_cols = 0; s->object_fit = 0; s->aspect = 0; s->has_aspect = 0;
    s->color_a = 255; s->bg_a = 255; s->border_a = 255;
    s->has_outline = 0; s->outline_w = 0; s->outline_c = 0; s->outline_a = 255;
    s->shadow_x = 0; s->shadow_y = 1; s->shadow_spread = 0; s->shadow_a = 90; s->shadow_inset = 0;
    s->has_shadow = 0; s->shadow_c = 0x000000;
    s->radius_tl = 0; s->radius_tr = 0; s->radius_br = 0; s->radius_bl = 0; s->has_corner_radius = 0;
    s->align_content = 0;
    s->flex_basis = 0; s->has_flex_basis = 0;
    s->order = 0; s->has_order = 0;
    s->list_type = 0; s->has_list = 0;
    s->text_indent = 0;
    s->ellipsis = 0;
    s->bg_url[0] = 0; s->has_bg_url = 0;
    s->bg_size = 0; s->bg_size_w = 0; s->bg_size_h = 0; s->bg_size_wp = 0; s->bg_size_hp = 0;
    s->bg_repeat = -1;
    s->has_bg_pos = 0; s->bg_pos_x = 0; s->bg_pos_y = 0; s->bg_pos_xp = 0; s->bg_pos_yp = 0;
    s->has_content = 0; s->content[0] = 0; s->content_url = 0; s->content_src[0] = 0;
    s->tx = 0; s->ty = 0; s->has_translate = 0;
    s->tscale = 256; s->tscalex = 256; s->tscaley = 256; s->has_scale = 0;
    s->trot = 0; s->has_rot = 0;
    s->grid_rows = 0;
    s->col_span = 1; s->has_col_span = 0;
    s->row_span = 1; s->has_row_span = 0;
}

static void compute_style(int n, css_style_t *ps, css_style_t *s, int pe) {
    int save_pe = g_pe;
    g_pe = pe;
    style_defaults(s);
    if (ps) {
        s->color = ps->color; s->has_color = ps->has_color;
        s->color_a = ps->color_a;
        s->font_size = ps->font_size;
        s->bold = ps->bold; s->italic = ps->italic;
        s->underline = ps->underline; s->strike = ps->strike;
        s->align = ps->align;
        s->line_height = ps->line_height;
        s->letter_space = ps->letter_space;
        s->nowrap = ps->nowrap;
        s->transform = ps->transform;
    }
    g_inh_font = s->font_size;
    static int order[CSS_RULES];
    int cnt = 0;
    for (int i = 0; i < g_css_count; i++)
        if (sel_match(n, g_css[i].sel)) order[cnt++] = i;
    for (int i = 0; i < cnt; i++)
        for (int j = i + 1; j < cnt; j++) {
            int ki = g_css[order[i]].spec + (g_css[order[i]].imp ? 100000 : 0);
            int kj = g_css[order[j]].spec + (g_css[order[j]].imp ? 100000 : 0);
            if (kj < ki) { int t = order[i]; order[i] = order[j]; order[j] = t; }
        }
    for (int i = 0; i < cnt; i++) css_apply(s, g_css[order[i]].decl);
    if (g_dn[n].istyle[0]) css_apply(s, g_dn[n].istyle);

    const dnode_t *d = &g_dn[n];
    if (d->has_bgc) { s->bg = d->bgc; s->has_bg = 1; s->bg_transparent = 0; }
    if (d->align_attr >= 0) s->align = d->align_attr;
    else if (str_eq(d->tag, "td") || str_eq(d->tag, "th")) s->align = 0;
    if (d->wa >= 0 && !s->has_w) { s->has_w = 1; s->width = d->wa; s->w_pct = d->wa_pct; }
    if (d->ha >= 0 && !s->has_h) { s->has_h = 1; s->height = d->ha; s->h_pct = d->ha_pct; }

    if (str_eq(d->tag, "html") || str_eq(d->tag, "body")) {
        if (!s->has_bg) { s->bg = 0xFFFFFF; s->has_bg = 1; }
    }
    else if (str_eq(d->tag, "h1")) { if (!s->has_font) s->font_size = 32; s->bold = 1; if (!s->has_mar) { s->mar[0] = 21; s->mar[2] = 21; } }
    else if (str_eq(d->tag, "h2")) { if (!s->has_font) s->font_size = 24; s->bold = 1; if (!s->has_mar) { s->mar[0] = 19; s->mar[2] = 19; } }
    else if (str_eq(d->tag, "h3")) { if (!s->has_font) s->font_size = 19; s->bold = 1; if (!s->has_mar) { s->mar[0] = 18; s->mar[2] = 18; } }
    else if (str_eq(d->tag, "h4")) { if (!s->has_font) s->font_size = 16; s->bold = 1; if (!s->has_mar) { s->mar[0] = 17; s->mar[2] = 17; } }
    else if (str_eq(d->tag, "h5")) { if (!s->has_font) s->font_size = 14; s->bold = 1; if (!s->has_mar) { s->mar[0] = 16; s->mar[2] = 16; } }
    else if (str_eq(d->tag, "h6")) { if (!s->has_font) s->font_size = 12; s->bold = 1; if (!s->has_mar) { s->mar[0] = 16; s->mar[2] = 16; } }
    else if (str_eq(d->tag, "p") || str_eq(d->tag, "dl")) {
        if (!s->has_mar) { s->mar[0] = 14; s->mar[2] = 14; }
    }
    else if (str_eq(d->tag, "ul") || str_eq(d->tag, "ol")) {
        if (!s->has_pad) s->pad[3] = 30;
        if (!s->has_mar) { s->mar[0] = 14; s->mar[2] = 14; }
        if (!s->has_list) s->list_type = str_eq(d->tag, "ol") ? 4 : 3;
    }
    else if (str_eq(d->tag, "blockquote")) {
        if (!s->has_mar) { s->mar[0] = 14; s->mar[2] = 14; s->mar[3] = 28; }
    }
    else if (str_eq(d->tag, "pre")) {
        if (!s->has_bg) { s->bg = 0xF5F5F5; s->has_bg = 1; }
        if (!s->has_pad) { s->pad[0] = s->pad[1] = s->pad[2] = s->pad[3] = 8; }
        s->nowrap = 1;
    }
    else if (str_eq(d->tag, "hr")) {
        if (s->border_w == 0) s->border_w = 1;
        s->border_c = 0xDDDDDD;
        if (!s->has_mar) { s->mar[0] = 8; s->mar[2] = 8; }
    }
    else if (str_eq(d->tag, "center")) { s->align = 1; s->is_flex = 1; s->row = 1; s->wrap = 1; s->justify = 1; s->align_items = 1; }
    else if (str_eq(d->tag, "tr") || str_eq(d->tag, "table-row")) { s->is_flex = 1; s->row = 1; s->wrap = 1; }
    else if (str_eq(d->tag, "table")) { s->is_flex = 1; s->row = 0; }
    else if (str_eq(d->tag, "td") || str_eq(d->tag, "th")) {
        if (!s->has_pad) { s->pad[0] = s->pad[1] = s->pad[2] = s->pad[3] = 2; }
        for (int c = d->first; c >= 0; c = g_dn[c].next) {
            const char *ct = g_dn[c].tag;
            if (str_eq(ct, "table") || str_eq(ct, "div") ||
                str_eq(ct, "ul") || str_eq(ct, "ol") || str_eq(ct, "blockquote")) { s->flex_grow = 1; break; }
        }
    }
    if ((str_eq(d->tag, "td") || str_eq(d->tag, "th")) && d->align_attr == 1) s->align_items = 1;

    if (str_eq(d->tag, "a") && !s->has_color) s->color = 0x1a0dab;
    if (str_eq(d->tag, "b") || str_eq(d->tag, "strong")) s->bold = 1;
    if (str_eq(d->tag, "i") || str_eq(d->tag, "em")) s->italic = 1;
    if (str_eq(d->tag, "u") || str_eq(d->tag, "ins")) s->underline = 1;
    if (str_eq(d->tag, "s") || str_eq(d->tag, "del") || str_eq(d->tag, "strike")) s->strike = 1;
    if (str_eq(d->tag, "small")) s->font_size = s->font_size * 5 / 6;
    if (str_eq(d->tag, "big")) s->font_size = s->font_size * 6 / 5;
    if (s->grid_cols > 0) { s->is_flex = 1; s->row = 1; s->wrap = 1; }
    g_pe = save_pe;
}

static lv_font_t *g_bfont[8];
static int g_bsize[8];
static int g_bn;

static lv_font_t *bfont(int size) {
    if (size < 10) size = 10;
    if (size > 40) size = 40;
    for (int i = 0; i < g_bn; i++) if (g_bsize[i] == size) return g_bfont[i];
    if (g_bn >= 8) return g_bfont[0];
    lv_font_t *f = lv_tiny_ttf_create_data(font_ttf, font_ttf_len, size);
    if (!f) return g_bn ? g_bfont[0] : LV_FONT_DEFAULT;
    g_bfont[g_bn] = f;
    g_bsize[g_bn] = size;
    g_bn++;
    return f;
}

/* ---------------- Ag kaynaklari (harici CSS + resim baytlari) ---------------- */

static void res_clear(void) {
    for (int i = 0; i < g_res_count; i++) {
        if (g_res_data[i] && g_basi && g_basi->mem) g_basi->mem->free(g_res_data[i]);
        g_res_data[i] = 0;
        g_res_len[i] = 0;
        g_res_url[i][0] = 0;
        g_res_css[i] = 0;
    }
    g_res_count = 0;
}

static int res_find(const char *url) {
    for (int i = 0; i < g_res_count; i++) if (str_eq(g_res_url[i], url)) return i;
    return -1;
}

static int res_add(const char *url, const char *body, uint32_t len, int is_css) {
    if (g_res_count >= RES_MAX || !g_basi || !g_basi->mem) return -1;
    if (len > 900000) len = 900000;
    uint8_t *d = g_basi->mem->alloc((size_t)len + 1);
    if (!d) return -1;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)body[i];
    d[len] = 0;
    bcopy_str(g_res_url[g_res_count], url, sizeof(g_res_url[0]));
    g_res_data[g_res_count] = d;
    g_res_len[g_res_count] = len;
    g_res_css[g_res_count] = is_css;
    g_res_count++;
    return g_res_count - 1;
}

static void fetch_resource(const char *url, int is_css) {
    if (!g_basi || !g_basi->net || g_res_count >= RES_MAX) return;
    if (!url[0] || res_find(url) >= 0) return;
    uint32_t len = 0;
    const char *b;
    if (starts_with(url, "https://")) b = g_basi->net->https_get(url, &len);
    else if (starts_with(url, "http://")) b = g_basi->net->http_get(url, &len);
    else return;
    if (!b || len == 0) return;
    res_add(url, b, len, is_css);
}

/* HTML icindeki <img src> ve <link rel=stylesheet href> kaynaklarini toplar. */
static void collect_resources(const char *html, const char *scheme, const char *host) {
    if (!g_basi || !g_basi->net) return;
    int has_t = (g_basi->time && g_basi->time->ticks_ms) ? 1 : 0;
    uint64_t t0 = has_t ? g_basi->time->ticks_ms() : 0;
    const char *p = html;
    int imgs = 0, cssn = 0;
    while (*p && g_res_count < RES_MAX) {
        if (has_t && g_basi->time->ticks_ms() - t0 > 20000) break;
        const char *ti = find_substr_ci(p, "<img");
        const char *tl = find_substr_ci(p, "<link");
        const char *next;
        if (ti && (!tl || ti < tl)) next = ti;
        else if (tl) next = tl;
        else break;
        const char *q = next;
        while (*q && *q != '>') q++;
        char a[420];
        if (next == ti && imgs < 10) {
            if (find_attr(next, q, "src", a, sizeof(a))) {
                char abs[440];
                resolve_url(scheme, host, a, abs, sizeof(abs));
                if (abs[0]) { fetch_resource(abs, 0); imgs++; }
            }
        } else if (next == tl && cssn < 5) {
            char rel[64];
            if (find_attr(next, q, "href", a, sizeof(a)) &&
                find_attr(next, q, "rel", rel, sizeof(rel)) &&
                find_substr_ci(rel, "stylesheet")) {
                char abs[440];
                resolve_url(scheme, host, a, abs, sizeof(abs));
                if (abs[0]) { fetch_resource(abs, 1); cssn++; }
            }
        }
        p = (*q == '>') ? q + 1 : q;
    }
    {
        const char *u = html;
        int urn = 0;
        while (urn < 20 && g_res_count < RES_MAX) {
            u = find_substr_ci(u, "url(");
            if (!u) break;
            u += 4;
            while (*u == ' ' || *u == '\'' || *u == '"') u++;
            char a[420];
            int n = 0;
            while (u[n] && u[n] != ')' && u[n] != '\'' && u[n] != '"' && n < 419) { a[n] = u[n]; n++; }
            a[n] = 0;
            u += n;
            if (n < 4 || starts_with(a, "data:")) continue;
            if (!(find_substr_ci(a, ".png") || find_substr_ci(a, ".jpg") ||
                  find_substr_ci(a, ".jpeg") || find_substr_ci(a, ".gif") ||
                  find_substr_ci(a, ".webp") || find_substr_ci(a, ".ico") ||
                  find_substr_ci(a, ".bmp"))) continue;
            char abs[440];
            resolve_url(scheme, host, a, abs, sizeof(abs));
            if (abs[0]) { fetch_resource(abs, 0); urn++; }
        }
    }
}

/* ---------------- Resim cozme + olcekleme ---------------- */

#define IMG_EXTRA 96
static uint32_t *g_img_extra[IMG_EXTRA];
static int g_img_extra_n;

#define BG_DSC_MAX 96
static lv_image_dsc_t g_bg_dsc[BG_DSC_MAX];
static int g_bg_dsc_n;

static void images_clear(void) {
    for (int i = 0; i < g_img_count; i++) {
        if (g_imgs[i].px && g_basi && g_basi->mem) g_basi->mem->free(g_imgs[i].px);
        g_imgs[i].px = 0;
        g_imgs[i].url[0] = 0;
        g_imgs[i].w = g_imgs[i].h = 0;
    }
    g_img_count = 0;
    for (int i = 0; i < g_img_extra_n; i++)
        if (g_img_extra[i] && g_basi && g_basi->mem) g_basi->mem->free(g_img_extra[i]);
    g_img_extra_n = 0;
    g_bg_dsc_n = 0;
}

static uint32_t *img_get(const char *url, int *out_w, int *out_h) {
    *out_w = 0; *out_h = 0;
    for (int i = 0; i < g_img_count; i++)
        if (str_eq(g_imgs[i].url, url)) { *out_w = g_imgs[i].w; *out_h = g_imgs[i].h; return g_imgs[i].px; }
    if (g_img_count >= IMG_MAX || !g_basi || !g_basi->mem) return 0;
    int ri = res_find(url);
    if (ri < 0 || !g_res_data[ri] || g_res_css[ri]) return 0;
    uint32_t *px = 0;
    int w = 0, h = 0;
    if (png_decode_argb(g_res_data[ri], g_res_len[ri], g_basi->mem->alloc, g_basi->mem->free, &px, &w, &h) != 0 || !px)
        return 0;
    bcopy_str(g_imgs[g_img_count].url, url, sizeof(g_imgs[0].url));
    g_imgs[g_img_count].px = px;
    g_imgs[g_img_count].w = w;
    g_imgs[g_img_count].h = h;
    g_img_count++;
    *out_w = w; *out_h = h;
    return px;
}

static uint32_t *img_scale(const uint32_t *src, int sw, int sh, int dw, int dh) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || !g_basi || !g_basi->mem) return 0;
    if (g_img_extra_n >= IMG_EXTRA) return 0;
    uint32_t *d = g_basi->mem->alloc((size_t)dw * (size_t)dh * 4);
    if (!d) return 0;
    for (int y = 0; y < dh; y++) {
        int sy = (int)((long)y * sh / dh);
        if (sy >= sh) sy = sh - 1;
        for (int x = 0; x < dw; x++) {
            int sx = (int)((long)x * sw / dw);
            if (sx >= sw) sx = sw - 1;
            d[(long)y * dw + x] = src[(long)sy * sw + sx];
        }
    }
    g_img_extra[g_img_extra_n++] = d;
    return d;
}

static uint32_t *img_scale_box(const uint32_t *src, int sw, int sh, int dw, int dh, int fit, int *ow, int *oh) {
    *ow = 0; *oh = 0;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || !g_basi || !g_basi->mem) return 0;
    if (g_img_extra_n >= IMG_EXTRA) return 0;
    if (fit == 2) {
        uint32_t *d = g_basi->mem->alloc((size_t)dw * dh * 4);
        if (!d) return 0;
        int rw, rh;
        if ((long)dw * sh >= (long)dh * sw) { rw = sw; rh = (int)((long)dh * sw / dw); }
        else { rh = sh; rw = (int)((long)dw * sh / dh); }
        if (rw < 1) rw = 1; if (rh < 1) rh = 1;
        int cx = (sw - rw) / 2, cy = (sh - rh) / 2;
        for (int y = 0; y < dh; y++) {
            int sy = cy + (int)((long)y * rh / dh);
            if (sy >= sh) sy = sh - 1; if (sy < 0) sy = 0;
            for (int x = 0; x < dw; x++) {
                int sx = cx + (int)((long)x * rw / dw);
                if (sx >= sw) sx = sw - 1; if (sx < 0) sx = 0;
                d[(long)y * dw + x] = src[(long)sy * sw + sx];
            }
        }
        g_img_extra[g_img_extra_n++] = d;
        *ow = dw; *oh = dh;
        return d;
    }
    int nw, nh;
    if ((long)dw * sh <= (long)dh * sw) { nw = dw; nh = (int)((long)sh * dw / sw); }
    else { nh = dh; nw = (int)((long)sw * dh / sh); }
    if (nw < 1) nw = 1; if (nh < 1) nh = 1;
    uint32_t *d = g_basi->mem->alloc((size_t)nw * nh * 4);
    if (!d) return 0;
    for (int y = 0; y < nh; y++) {
        int sy = (int)((long)y * sh / nh); if (sy >= sh) sy = sh - 1;
        for (int x = 0; x < nw; x++) {
            int sx = (int)((long)x * sw / nw); if (sx >= sw) sx = sw - 1;
            d[(long)y * nw + x] = src[(long)sy * sw + sx];
        }
    }
    g_img_extra[g_img_extra_n++] = d;
    *ow = nw; *oh = nh;
    return d;
}

static int b64_val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static uint32_t *img_from_data_uri(const char *src, int *out_w, int *out_h) {
    *out_w = 0; *out_h = 0;
    if (!g_basi || !g_basi->mem) return 0;
    const char *comma = src;
    while (*comma && *comma != ',') comma++;
    if (!*comma) return 0;
    int is64 = 0;
    for (const char *q = src; q + 5 < comma; q++)
        if (q[0]=='b'&&q[1]=='a'&&q[2]=='s'&&q[3]=='e'&&q[4]=='6'&&q[5]=='4') { is64 = 1; break; }
    if (!is64) return 0;
    const char *p = comma + 1;
    size_t n = 0;
    while (p[n]) n++;
    size_t cap = (n / 4 + 1) * 3;
    uint8_t *buf = g_basi->mem->alloc(cap);
    if (!buf) return 0;
    size_t o = 0;
    int acc = 0, bits = 0;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '=' ) break;
        int v = b64_val((unsigned char)p[i]);
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; buf[o++] = (uint8_t)((acc >> bits) & 0xFF); }
    }
    uint32_t *px = 0;
    int w = 0, h = 0;
    if (png_decode_argb(buf, (uint32_t)o, g_basi->mem->alloc, g_basi->mem->free, &px, &w, &h) != 0 || !px)
        px = 0;
    g_basi->mem->free(buf);
    if (px) { *out_w = w; *out_h = h; }
    return px;
}

static void apply_bg_image(lv_obj_t *o, css_style_t *s) {
    if (!s->has_bg_url || !s->bg_url[0]) return;
    if (g_bg_dsc_n >= BG_DSC_MAX || !g_basi || !g_basi->mem) return;
    uint32_t *px = 0;
    int w = 0, h = 0;
    if (starts_with(s->bg_url, "data:")) {
        px = img_from_data_uri(s->bg_url, &w, &h);
        if (px && g_img_extra_n < IMG_EXTRA) g_img_extra[g_img_extra_n++] = px;
        else if (px) return;
    } else {
        char abs[440];
        resolve_url(g_cur_scheme, g_cur_host, s->bg_url, abs, sizeof(abs));
        if (!abs[0]) return;
        if (find_substr_ci(abs, ".svg")) return;
        px = img_get(abs, &w, &h);
    }
    if (!px || w <= 0 || h <= 0) return;
    if (s->bg_size == 1 || s->bg_size == 2 || s->bg_size == 3) {
        int cw = (s->has_w && !s->w_pct && s->width > 0) ? s->width : 0;
        int ch = (s->has_h && !s->h_pct && s->height > 0) ? s->height : 0;
        int tw = 0, th = 0;
        if (s->bg_size == 3) {
            tw = s->bg_size_wp ? (cw > 0 ? cw * s->bg_size_w / 100 : 0) : s->bg_size_w;
            th = s->bg_size_hp ? (ch > 0 ? ch * s->bg_size_h / 100 : 0) : s->bg_size_h;
            if (tw <= 0 && th > 0) tw = w * th / h;
            if (th <= 0 && tw > 0) th = h * tw / w;
        } else if (cw > 0 && ch > 0) {
            if (s->bg_size == 2) {
                if (cw * h >= ch * w) { tw = cw; th = h * cw / w; }
                else { th = ch; tw = w * ch / h; }
            } else {
                if (cw * h <= ch * w) { tw = cw; th = h * cw / w; }
                else { th = ch; tw = w * ch / h; }
            }
        }
        if (tw > 0 && th > 0 && (tw != w || th != h) && g_img_extra_n < IMG_EXTRA) {
            uint32_t *sc = img_scale(px, w, h, tw, th);
            if (sc) { g_img_extra[g_img_extra_n++] = sc; px = sc; w = tw; h = th; }
        }
    }
    lv_image_dsc_t *d = &g_bg_dsc[g_bg_dsc_n++];
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_ARGB8888;
    d->header.flags = 0;
    d->header.w = (uint32_t)w;
    d->header.h = (uint32_t)h;
    d->header.stride = (uint32_t)(w * 4);
    d->header.reserved_2 = 0;
    d->data_size = (uint32_t)(w * (size_t)h * 4);
    d->data = (const uint8_t *)px;
    d->reserved = 0;
    d->reserved_2 = 0;
    lv_obj_set_style_bg_image_src(o, d, 0);
    lv_obj_set_style_bg_image_opa(o, LV_OPA_COVER, 0);
    if (s->bg_repeat == 0 || s->bg_repeat == 2 || s->bg_repeat == 3)
        lv_obj_set_style_bg_image_tiled(o, true, 0);
    else if (s->bg_repeat == 1)
        lv_obj_set_style_bg_image_tiled(o, false, 0);
}

static lv_obj_t *make_image(lv_obj_t *parent, const char *src, int wa, int wa_pct, int ha, int ha_pct, int fit) {
    if (!src[0]) return 0;
    int w = 0, h = 0;
    uint32_t *px = 0;
    if (starts_with(src, "data:")) {
        px = img_from_data_uri(src, &w, &h);
    } else {
        char abs[440];
        resolve_url(g_cur_scheme, g_cur_host, src, abs, sizeof(abs));
        if (!abs[0]) return 0;
        px = img_get(abs, &w, &h);
    }
    if (!px) return 0;
    uint32_t *buf = px;
    int bw = w, bh = h;
    int dw = (wa >= 0 && !wa_pct) ? wa : 0;
    int dh = (ha >= 0 && !ha_pct) ? ha : 0;
    if (dw > 0 && dh > 0 && fit >= 1) {
        uint32_t *sc = img_scale_box(px, w, h, dw, dh, fit, &bw, &bh);
        if (sc) buf = sc;
    } else if (dw > 0 && dh > 0 && (dw != w || dh != h)) {
        uint32_t *sc = img_scale(px, w, h, dw, dh);
        if (sc) { buf = sc; bw = dw; bh = dh; }
    }
    lv_obj_t *c = lv_canvas_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(c, bw, bh);
    lv_canvas_set_buffer(c, buf, bw, bh, LV_COLOR_FORMAT_ARGB8888);
    return c;
}

static void apply_box(lv_obj_t *o, css_style_t *s) {
    if (s->has_bg) {
        if (s->bg_transparent) lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
        else {
            lv_obj_set_style_bg_color(o, lv_color_hex(s->bg), 0);
            lv_obj_set_style_bg_opa(o, (lv_opa_t)s->bg_a, 0);
        }
    }
    if (s->has_grad) {
        lv_obj_set_style_bg_grad_color(o, lv_color_hex(s->grad_c), 0);
        lv_obj_set_style_bg_grad_dir(o, s->grad_dir == 2 ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, 0);
    }
    if (s->border_w > 0) {
        lv_obj_set_style_border_width(o, s->border_w, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(s->border_c), 0);
        lv_obj_set_style_border_opa(o, (lv_opa_t)s->border_a, 0);
    }
    if (s->has_corner_radius) {
        int r = s->radius_tl;
        if (s->radius_tr > r) r = s->radius_tr;
        if (s->radius_br > r) r = s->radius_br;
        if (s->radius_bl > r) r = s->radius_bl;
        if (r > 0) lv_obj_set_style_radius(o, r, 0);
    } else if (s->radius > 0) lv_obj_set_style_radius(o, s->radius, 0);
    if (s->has_shadow) {
        if (!s->shadow_inset) {
            lv_obj_set_style_shadow_width(o, s->shadow, 0);
            lv_obj_set_style_shadow_color(o, lv_color_hex(s->shadow_c), 0);
            lv_obj_set_style_shadow_opa(o, (lv_opa_t)s->shadow_a, 0);
            lv_obj_set_style_shadow_ofs_x(o, s->shadow_x, 0);
            lv_obj_set_style_shadow_ofs_y(o, s->shadow_y, 0);
            lv_obj_set_style_shadow_spread(o, s->shadow_spread, 0);
        } else {
            lv_obj_set_style_border_width(o, s->border_w > 0 ? s->border_w : 1, 0);
            lv_obj_set_style_border_color(o, lv_color_hex(s->shadow_c), 0);
            lv_obj_set_style_border_opa(o, (lv_opa_t)s->shadow_a, 0);
        }
    }
    if (s->has_outline) {
        lv_obj_set_style_outline_width(o, s->outline_w, 0);
        lv_obj_set_style_outline_color(o, lv_color_hex(s->outline_c), 0);
        lv_obj_set_style_outline_opa(o, (lv_opa_t)s->outline_a, 0);
        lv_obj_set_style_outline_pad(o, 0, 0);
    }
    lv_obj_set_style_pad_top(o, s->pad[0], 0);
    lv_obj_set_style_pad_right(o, s->pad[1], 0);
    lv_obj_set_style_pad_bottom(o, s->pad[2], 0);
    lv_obj_set_style_pad_left(o, s->pad[3], 0);
    if (s->has_mar || s->mar[0] || s->mar[1] || s->mar[2] || s->mar[3]) {
        lv_obj_set_style_margin_top(o, s->mar[0], 0);
        lv_obj_set_style_margin_right(o, s->mar[1], 0);
        lv_obj_set_style_margin_bottom(o, s->mar[2], 0);
        lv_obj_set_style_margin_left(o, s->mar[3], 0);
    }
    lv_obj_set_width(o, s->has_w ? (s->w_pct ? LV_PCT(s->width) : s->width) : LV_SIZE_CONTENT);
    lv_obj_set_height(o, s->has_h ? (s->h_pct ? LV_PCT(s->height) : s->height) : LV_SIZE_CONTENT);
    if (s->has_minw) lv_obj_set_style_min_width(o, s->minw_pct ? LV_PCT(s->minw) : s->minw, 0);
    if (s->has_maxw) lv_obj_set_style_max_width(o, s->maxw_pct ? LV_PCT(s->maxw) : s->maxw, 0);
    if (s->has_minh) lv_obj_set_style_min_height(o, s->minh_pct ? LV_PCT(s->minh) : s->minh, 0);
    if (s->has_maxh) lv_obj_set_style_max_height(o, s->maxh_pct ? LV_PCT(s->maxh) : s->maxh, 0);
    if (s->opacity > 0 && s->opacity < 100)
        lv_obj_set_style_opa(o, (lv_opa_t)(s->opacity * 255 / 100), 0);
    if (s->has_translate && s->position != 1) {
        lv_obj_set_style_translate_x(o, s->tx, 0);
        lv_obj_set_style_translate_y(o, s->ty, 0);
    }
    if (s->has_scale) {
        lv_obj_set_style_transform_scale_x(o, s->tscalex, 0);
        lv_obj_set_style_transform_scale_y(o, s->tscaley, 0);
    }
    if (s->has_rot) {
        lv_obj_set_style_transform_rotation(o, s->trot, 0);
        lv_obj_set_style_transform_pivot_x(o, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(o, LV_PCT(50), 0);
    }
    apply_bg_image(o, s);
}

static void apply_position(lv_obj_t *o, css_style_t *s) {
    if (s->position == 2 || s->position == 3) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_IGNORE_LAYOUT);
        int right = s->has_right && !s->has_left;
        int bottom = s->has_bottom && !s->has_top;
        int xo = s->has_left ? s->left : (s->has_right ? -s->right : 0);
        int yo = s->has_top ? s->top : (s->has_bottom ? -s->bottom : 0);
        if (right && bottom) lv_obj_align(o, LV_ALIGN_BOTTOM_RIGHT, xo, yo);
        else if (right) lv_obj_align(o, LV_ALIGN_TOP_RIGHT, xo, yo);
        else if (bottom) lv_obj_align(o, LV_ALIGN_BOTTOM_LEFT, xo, yo);
        else lv_obj_set_pos(o, xo, yo);
    } else if (s->position == 1 && (s->has_left || s->has_top || s->has_translate)) {
        int xx = (s->has_left ? s->left : 0) + (s->has_translate ? s->tx : 0);
        int yy = (s->has_top ? s->top : 0) + (s->has_translate ? s->ty : 0);
        if (xx) lv_obj_set_style_translate_x(o, xx, 0);
        if (yy) lv_obj_set_style_translate_y(o, yy, 0);
    }
}

static int is_inline_tag(const char *t) {
    return str_eq(t, "a") || str_eq(t, "span") || str_eq(t, "b") || str_eq(t, "strong") ||
           str_eq(t, "i") || str_eq(t, "em") || str_eq(t, "u") || str_eq(t, "s") ||
           str_eq(t, "del") || str_eq(t, "ins") || str_eq(t, "small") || str_eq(t, "big") ||
           str_eq(t, "code") || str_eq(t, "kbd") || str_eq(t, "samp") || str_eq(t, "var") ||
           str_eq(t, "label") || str_eq(t, "abbr") || str_eq(t, "cite") || str_eq(t, "q") ||
           str_eq(t, "sub") || str_eq(t, "sup") || str_eq(t, "img") || str_eq(t, "input") ||
           str_eq(t, "br") || str_eq(t, "button") || str_eq(t, "select") ||
           str_eq(t, "textarea") || str_eq(t, "time") || str_eq(t, "mark") ||
           str_eq(t, "font") || str_eq(t, "wbr");
}

static int node_inline_container(int n) {
    const dnode_t *d = &g_dn[n];
    if (is_inline_tag(d->tag)) return 1;
    int has_inline = 0, has_block = 0;
    for (int c = d->first; c >= 0; c = g_dn[c].next) {
        if (is_inline_tag(g_dn[c].tag)) has_inline = 1;
        else has_block = 1;
    }
    if (d->text[0]) has_inline = 1;
    return has_inline && !has_block;
}

/* ---------------- Form / link olaylari (CSS katmani) ---------------- */
static void page_submit(int form_id) {
    if (form_id < 0 || form_id >= g_pform_count) return;
    char action[220];
    if (g_pforms[form_id].action[0])
        resolve_url(g_cur_scheme, g_cur_host, g_pforms[form_id].action, action, sizeof(action));
    else
        bcopy_str(action, g_cur_url, sizeof(action));
    if (!action[0]) bcopy_str(action, g_cur_url, sizeof(action));

    char query[URL_MAX];
    query[0] = 0;
    int first = 1;
    for (int i = 0; i < g_dn_count; i++) {
        dnode_t *d = &g_dn[i];
        if (d->form_id != form_id || !d->name[0]) continue;
        const char *value = 0;
        if (str_eq(d->tag, "input")) {
            if (str_eq(d->type, "submit") || str_eq(d->type, "button") ||
                str_eq(d->type, "reset") || str_eq(d->type, "image")) continue;
            if (str_eq(d->type, "hidden")) value = d->value;
            else if (d->widget) value = lv_textarea_get_text(d->widget);
            else value = d->value;
        } else if (str_eq(d->tag, "textarea")) {
            if (d->widget) value = lv_textarea_get_text(d->widget);
            else value = d->value;
        } else continue;
        if (!value) continue;
        bcat(query, first ? "?" : "&", sizeof(query));
        first = 0;
        url_encode_append(query, sizeof(query), d->name);
        bcat(query, "=", sizeof(query));
        url_encode_append(query, sizeof(query), value);
    }

    char full[URL_MAX];
    bcopy_str(full, action, sizeof(full));
    bcat(full, query, sizeof(full));
    if (g_addr) lv_textarea_set_text(g_addr, full);
    navigate(full, 1);
}

static void page_input_focus_cb(lv_event_t *e) {
    int n = (int)(intptr_t)lv_event_get_user_data(e);
    if (n < 0 || n >= g_dn_count) return;
    if (g_group && g_dn[n].widget) lv_group_focus_obj(g_dn[n].widget);
}

static void page_wheel_cb(lv_timer_t *t) {
    (void)t;
    if (!g_basi || !g_basi->input || !g_basi->input->mouse_scroll || !g_content) return;
    int sc = g_basi->input->mouse_scroll();
    if (sc == 0) return;
    int mx = g_basi->input->mouse_x();
    int my = g_basi->input->mouse_y();
    lv_area_t a;
    lv_obj_get_coords(g_content, &a);
    if (mx < a.x1 || mx > a.x2 || my < a.y1 || my > a.y2) return;
    lv_obj_scroll_by(g_content, 0, -sc * 40, LV_ANIM_OFF);
}

static void page_input_ready_cb(lv_event_t *e) {
    int n = (int)(intptr_t)lv_event_get_user_data(e);
    if (n < 0 || n >= g_dn_count) return;
    page_submit(g_dn[n].form_id);
}

static void page_submit_cb(lv_event_t *e) {
    int n = (int)(intptr_t)lv_event_get_user_data(e);
    if (n < 0 || n >= g_dn_count) return;
    page_submit(g_dn[n].form_id);
}

static void page_link_cb(lv_event_t *e) {
    int n = (int)(intptr_t)lv_event_get_user_data(e);
    if (n < 0 || n >= g_dn_count) return;
    char abs[220];
    resolve_url(g_cur_scheme, g_cur_host, g_dn[n].href, abs, sizeof(abs));
    if (abs[0]) navigate(abs, 1);
}

static void text_label(lv_obj_t *o, const char *txt, const css_style_t *s, int wrap) {
    char tb[CSS_NTEXT];
    if (s->transform) {
        int i = 0;
        for (; txt[i] && i < CSS_NTEXT - 1; i++) {
            char c = txt[i];
            if (s->transform == 1 && c >= 'a' && c <= 'z') c = (char)(c - 32);
            else if (s->transform == 2 && c >= 'A' && c <= 'Z') c = (char)(c + 32);
            else if (s->transform == 3 && (i == 0 || txt[i - 1] == ' ') && c >= 'a' && c <= 'z') c = (char)(c - 32);
            tb[i] = c;
        }
        tb[i] = 0;
        txt = tb;
    }
    lv_obj_t *l = lv_label_create(o);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, bfont(s->font_size), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(s->color), 0);
    lv_obj_set_style_text_opa(l, (lv_opa_t)s->color_a, 0);
    lv_obj_set_style_text_align(l, s->align == 1 ? LV_TEXT_ALIGN_CENTER :
                                    s->align == 2 ? LV_TEXT_ALIGN_RIGHT :
                                    s->align == 3 ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
    lv_text_decor_t decor = LV_TEXT_DECOR_NONE;
    if (s->underline) decor = (lv_text_decor_t)(decor | LV_TEXT_DECOR_UNDERLINE);
    if (s->strike) decor = (lv_text_decor_t)(decor | LV_TEXT_DECOR_STRIKETHROUGH);
    if (decor) lv_obj_set_style_text_decor(l, decor, 0);
    if (s->letter_space) lv_obj_set_style_text_letter_space(l, s->letter_space, 0);
    if (s->line_height > s->font_size) lv_obj_set_style_text_line_space(l, s->line_height - s->font_size, 0);
    if (s->text_indent) lv_obj_set_style_pad_left(l, s->text_indent, 0);
    if (s->nowrap) {
        if (s->ellipsis) { lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS); lv_obj_set_width(l, LV_PCT(100)); }
        else lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    }
    else if (wrap) {
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(l, LV_PCT(100));
    }
}

static void roman_num(int v, char *out, int upper) {
    static const int vals[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char *syms[] = {"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"};
    out[0] = 0;
    int o = 0;
    for (int i = 0; i < 13 && v > 0; i++) {
        while (v >= vals[i]) {
            v -= vals[i];
            const char *ss = syms[i];
            while (*ss && o < 15) { char c = *ss++; if (!upper && c >= 'A' && c <= 'Z') c = (char)(c + 32); out[o++] = c; }
        }
    }
    out[o] = 0;
}

static void list_marker(int ltv, int idx, char *out, int cap) {
    if (ltv == 1) bcopy_str(out, "\xe2\x96\xaa", cap);
    else if (ltv == 2) bcopy_str(out, "\xe2\x97\xa6", cap);
    else if (ltv == 3) bcopy_str(out, "\xe2\x80\xa2", cap);
    else if (ltv == 4) {
        char n[16]; int l = 0; char tmp[16]; int t = idx;
        if (t == 0) tmp[l++] = '0';
        while (t > 0) { tmp[l++] = (char)('0' + t % 10); t /= 10; }
        int j = 0;
        while (l > 0) n[j++] = tmp[--l];
        n[j++] = '.';
        n[j] = 0;
        bcopy_str(out, n, cap);
    }
    else if (ltv == 5 || ltv == 6) {
        char c = (char)((idx >= 1 && idx <= 26) ? ((ltv == 5 ? 'A' : 'a') + (idx - 1)) : '?');
        out[0] = c; out[1] = '.'; out[2] = 0;
    }
    else if (ltv == 7) {
        roman_num(idx, out, 1);
        int l = b_len(out);
        if (l < cap - 1) { out[l] = '.'; out[l + 1] = 0; }
    }
    else out[0] = 0;
}

#define GRID_SLOTS 48
#define GRID_ROWS 34
static int32_t g_grid_col[GRID_SLOTS][8];
static int32_t g_grid_row[GRID_SLOTS][GRID_ROWS];
static int g_grid_slot;

static void build_pseudo_content(lv_obj_t *o, int n, css_style_t *ps, int which) {
    css_style_t sb;
    compute_style(n, ps, &sb, which);
    if (!sb.has_content || sb.hide) return;
    if (sb.content_url && sb.content_src[0]) {
        make_image(o, sb.content_src, -1, 0, -1, 0, sb.object_fit);
        return;
    }
    char txt[200];
    if (sb.content[0] == 1) dn_attr(n, sb.content + 1, txt, sizeof(txt));
    else bcopy_str(txt, sb.content, sizeof(txt));
    if (txt[0]) text_label(o, txt, &sb, 0);
}

static void build_node(int n, lv_obj_t *parent, css_style_t *ps) {
    css_style_t s;
    compute_style(n, ps, &s, 0);
    if (s.hide) return;
    if (s.has_flex_basis && ps && ps->is_flex) {
        if (ps->row && !s.has_w) { s.has_w = 1; s.width = s.flex_basis; s.w_pct = 0; }
        else if (!ps->row && !s.has_h) { s.has_h = 1; s.height = s.flex_basis; s.h_pct = 0; }
    }
    const dnode_t *d = &g_dn[n];

    if (str_eq(d->tag, "svg")) {
        lv_obj_t *o = lv_obj_create(parent);
        lv_obj_remove_style_all(o);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
        apply_box(o, &s);
        apply_position(o, &s);
        if (s.flex_grow > 0) lv_obj_set_flex_grow(o, s.flex_grow);
        int rw = 0, rh = 0;
        if (d->svg_len > 0) svg_intrinsic_size(g_svg_blob + d->svg_off, &rw, &rh);
        if (d->wa > 0 && !d->wa_pct) rw = d->wa;
        if (d->ha > 0 && !d->ha_pct) rh = d->ha;
        if (s.has_w && !s.w_pct && s.width > 0) rw = s.width;
        if (s.has_h && !s.h_pct && s.height > 0) rh = s.height;
        if (rw < 1) rw = 24;
        if (rh < 1) rh = 24;
        if (rw > 512) rw = 512;
        if (rh > 512) rh = 512;
        if (parent) {
            lv_obj_set_width(parent, rw);
            lv_obj_set_height(parent, rh);
        }
        if (d->svg_len > 0 && g_basi && g_basi->mem && g_img_extra_n < IMG_EXTRA) {
            uint32_t *px = g_basi->mem->alloc((size_t)rw * (size_t)rh * 4);
            if (px) {
                if (svg_render(g_svg_blob + d->svg_off, rw, rh, px) == 0) {
                    g_img_extra[g_img_extra_n++] = px;
                    lv_obj_t *c = lv_canvas_create(o);
                    lv_obj_remove_style_all(c);
                    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_set_size(c, rw, rh);
                    lv_canvas_set_buffer(c, px, rw, rh, LV_COLOR_FORMAT_ARGB8888);
                    lv_obj_center(c);
                    lv_obj_set_size(o, rw, rh);
                } else {
                    g_basi->mem->free(px);
                }
            }
        }
        return;
    }

    if (str_eq(d->tag, "img")) {
        lv_obj_t *o = lv_obj_create(parent);
        lv_obj_remove_style_all(o);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
        apply_box(o, &s);
        apply_position(o, &s);
        if (s.flex_grow > 0) lv_obj_set_flex_grow(o, s.flex_grow);
        lv_obj_t *c = make_image(o, d->src, d->wa, d->wa_pct, d->ha, d->ha_pct, s.object_fit);
        if (c) lv_obj_center(c);
        else if (d->alt[0]) text_label(o, d->alt, &s, 0);
        return;
    }

    if (str_eq(d->tag, "input") &&
        (d->type[0] == 0 || str_eq(d->type, "text") || str_eq(d->type, "search") ||
         str_eq(d->type, "email") || str_eq(d->type, "url") || str_eq(d->type, "tel") ||
         str_eq(d->type, "password") || str_eq(d->type, "number"))) {
        lv_obj_t *ta = lv_textarea_create(parent);
        lv_obj_remove_style_all(ta);
        lv_obj_remove_flag(ta, LV_OBJ_FLAG_SCROLLABLE);
        lv_textarea_set_one_line(ta, 1);
        lv_obj_set_width(ta, s.has_w ? (s.w_pct ? LV_PCT(s.width) : s.width) : LV_PCT(100));
        lv_obj_set_height(ta, s.has_h ? (s.h_pct ? LV_PCT(s.height) : s.height) : LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(ta, lv_color_hex(s.has_bg ? s.bg : 0xFFFFFF), 0);
        apply_box(ta, &s);
        apply_position(ta, &s);
        if (s.flex_grow > 0) lv_obj_set_flex_grow(ta, s.flex_grow);
        if (s.bg_transparent) lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, 0);
        else if (s.has_bg) lv_obj_set_style_bg_color(ta, lv_color_hex(s.bg), 0);
        if (s.border_w > 0) lv_obj_set_style_border_width(ta, s.border_w, 0);
        else lv_obj_set_style_border_width(ta, 1, 0);
        lv_obj_set_style_border_color(ta, lv_color_hex(s.border_w > 0 ? s.border_c : 0xDFE1E5), 0);
        lv_obj_set_style_text_font(ta, bfont(s.font_size), 0);
        if (s.has_color) lv_obj_set_style_text_color(ta, lv_color_hex(s.color), 0);
        if (d->ph[0]) lv_textarea_set_placeholder_text(ta, d->ph);
        if (d->value[0]) lv_textarea_set_text(ta, d->value);
        g_dn[n].widget = ta;
        if (g_group) {
            lv_group_add_obj(g_group, ta);
            lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        }
        lv_obj_add_event_cb(ta, page_input_ready_cb, LV_EVENT_READY, (void *)(intptr_t)n);
        lv_obj_add_event_cb(ta, page_input_focus_cb, LV_EVENT_CLICKED, (void *)(intptr_t)n);
        return;
    }

    if (str_eq(d->tag, "input") &&
        (str_eq(d->type, "submit") || str_eq(d->type, "button") || str_eq(d->type, "reset"))) {
        lv_obj_t *b = lv_button_create(parent);
        lv_obj_remove_style_all(b);
        apply_box(b, &s);
        apply_position(b, &s);
        if (s.flex_grow > 0) lv_obj_set_flex_grow(b, s.flex_grow);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(s.has_bg ? s.bg : 0xF8F9FA), 0);
        if (s.border_w > 0) lv_obj_set_style_border_width(b, s.border_w, 0);
        else lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(s.border_w > 0 ? s.border_c : 0xF8F9FA), 0);
        if (s.radius == 0) lv_obj_set_style_radius(b, 4, 0);
        const char *txt = d->value[0] ? d->value : (d->alt[0] ? d->alt : "Gonder");
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_font(l, bfont(s.font_size), 0);
        lv_obj_set_style_text_color(l, lv_color_hex(s.has_color ? s.color : 0x3C4043), 0);
        lv_obj_center(l);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, page_submit_cb, LV_EVENT_CLICKED, (void *)(intptr_t)n);
        return;
    }

    if (str_eq(d->tag, "input") || str_eq(d->tag, "hr")) return;

    int is_link = str_eq(d->tag, "a") && d->href[0];

    if (!s.is_flex && !s.has_w && node_inline_container(n)) {
        s.is_flex = 1; s.row = 1; s.wrap = 1;
    }

    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    apply_box(o, &s);
    apply_position(o, &s);
    if (s.flex_grow > 0) lv_obj_set_flex_grow(o, s.flex_grow);
    if (!s.has_w) {
        int inline_el = is_inline_tag(d->tag);
        int prow = ps && ps->is_flex && ps->row;
        int cshrink = ps && ps->is_flex && ps->align_items != 0;
        lv_obj_set_width(o, (prow || inline_el || cshrink) ? LV_SIZE_CONTENT : LV_PCT(100));
    }
    if (s.has_aspect && s.has_w && !s.w_pct && !s.has_h && s.aspect > 0)
        lv_obj_set_height(o, s.width * 1000 / s.aspect);

    if (is_link) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(o, page_link_cb, LV_EVENT_CLICKED, (void *)(intptr_t)n);
    }

    if (str_eq(d->tag, "li")) {
        int ltv = s.has_list ? s.list_type : (ps ? ps->list_type : 3);
        if (ltv) {
            int idx = 1;
            for (int p = prev_sib(n); p >= 0; p = prev_sib(p))
                if (str_eq(g_dn[p].tag, "li")) idx++;
            char mk[24];
            list_marker(ltv, idx, mk, sizeof(mk));
            lv_obj_t *lm = lv_label_create(o);
            lv_label_set_text(lm, mk);
            lv_obj_set_style_text_font(lm, bfont(s.font_size), 0);
            lv_obj_set_style_text_color(lm, lv_color_hex(s.color), 0);
            lv_obj_set_style_text_opa(lm, (lv_opa_t)s.color_a, 0);
            lv_obj_set_style_pad_right(lm, 6, 0);
        }
    }

    if (d->first < 0) {
        build_pseudo_content(o, n, &s, 1);
        if (d->text[0]) {
            int wrap = !(is_inline_tag(d->tag) ||
                         (ps && ps->is_flex && (ps->row || ps->align_items != 0)));
            text_label(o, d->text, &s, wrap);        }
        build_pseudo_content(o, n, &s, 2);
        return;
    }

    build_pseudo_content(o, n, &s, 1);
    if (s.is_flex) {
        lv_obj_set_flex_flow(o, s.row ? (s.wrap ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW)
                                      : LV_FLEX_FLOW_COLUMN);
        lv_flex_align_t main = s.justify == 1 ? LV_FLEX_ALIGN_CENTER :
                               s.justify == 2 ? LV_FLEX_ALIGN_END :
                               s.justify == 3 ? LV_FLEX_ALIGN_SPACE_BETWEEN :
                               s.justify == 4 ? LV_FLEX_ALIGN_SPACE_AROUND : LV_FLEX_ALIGN_START;
        lv_flex_align_t cross = s.align_items == 1 ? LV_FLEX_ALIGN_CENTER :
                                s.align_items == 2 ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START;
        lv_flex_align_t track = cross;
        if (s.align_content == 1) track = LV_FLEX_ALIGN_CENTER;
        else if (s.align_content == 2) track = LV_FLEX_ALIGN_END;
        else if (s.align_content == 3) track = LV_FLEX_ALIGN_SPACE_BETWEEN;
        else if (s.align_content == 4) track = LV_FLEX_ALIGN_SPACE_AROUND;
        lv_obj_set_flex_align(o, main, cross, track);
        if (s.row) lv_obj_set_style_pad_column(o, s.gap, 0);
        else lv_obj_set_style_pad_row(o, s.gap, 0);
    } else {
        lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
        lv_flex_align_t cross = s.align_items == 1 ? LV_FLEX_ALIGN_CENTER :
                                s.align_items == 2 ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START;
        lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, cross, cross);
        lv_obj_set_style_pad_row(o, s.gap, 0);
    }
    int childn = 0;
    for (int c = d->first; c >= 0; c = g_dn[c].next) childn++;
    int corder[128]; int corder_n = 0;
    if (childn > 0 && childn <= 128) {
        int cidx[128]; int m = 0;
        for (int c = d->first; c >= 0; c = g_dn[c].next) cidx[m++] = c;
        int any_order = 0;
        if (s.is_flex) {
            for (int i = 0; i < m; i++) {
                css_style_t t;
                compute_style(cidx[i], &s, &t, 0);
                if (t.has_order && t.order != 0) { any_order = 1; break; }
            }
        }
        if (any_order) {
            for (int i = 1; i < m; i++) {
                int key = cidx[i];
                css_style_t kt;
                compute_style(key, &s, &kt, 0);
                int ko = kt.has_order ? kt.order : 0;
                int j = i - 1;
                while (j >= 0) {
                    css_style_t jt;
                    compute_style(cidx[j], &s, &jt, 0);
                    int jo = jt.has_order ? jt.order : 0;
                    if (jo <= ko) break;
                    cidx[j + 1] = cidx[j];
                    j--;
                }
                cidx[j + 1] = key;
            }
        }
        for (int i = 0; i < m; i++) { build_node(cidx[i], o, &s); if (corder_n < 128) corder[corder_n++] = cidx[i]; }
    } else {
        for (int c = d->first; c >= 0; c = g_dn[c].next) { build_node(c, o, &s); if (corder_n < 128) corder[corder_n++] = c; }
    }
    if (s.grid_cols > 0 && corder_n > 0 && g_grid_slot < GRID_SLOTS) {
        int gc = s.grid_cols;
        if (gc > 6) gc = 6;
        if (gc < 1) gc = 1;
        int32_t *cd = g_grid_col[g_grid_slot];
        int32_t *rd = g_grid_row[g_grid_slot];
        for (int i = 0; i < gc; i++) cd[i] = LV_GRID_FR(1);
        cd[gc] = LV_GRID_TEMPLATE_LAST;
        int rows = s.grid_rows > 0 ? s.grid_rows : (corder_n + gc - 1) / gc;
        if (rows < 1) rows = 1;
        if (rows > GRID_ROWS - 1) rows = GRID_ROWS - 1;
        for (int i = 0; i < rows; i++) rd[i] = LV_GRID_CONTENT;
        rd[rows] = LV_GRID_TEMPLATE_LAST;
        lv_obj_set_grid_dsc_array(o, cd, rd);
        int col = 0, row = 0;
        for (int i = 0; i < corder_n; i++) {
            lv_obj_t *ch = lv_obj_get_child(o, i);
            if (!ch) continue;
            css_style_t cs;
            compute_style(corder[i], &s, &cs, 0);
            int span = cs.has_col_span ? cs.col_span : 1;
            if (span > gc) span = gc;
            if (col + span > gc) { col = 0; row++; }
            int rspan = cs.has_row_span ? cs.row_span : 1;
            if (row + rspan > rows) rspan = rows - row;
            if (rspan < 1) rspan = 1;
            lv_obj_set_grid_cell(ch, LV_GRID_ALIGN_STRETCH, col, span, LV_GRID_ALIGN_START, row, rspan);
            col += span;
        }
        g_grid_slot++;
    }
    build_pseudo_content(o, n, &s, 2);
}

/* HTML icindeki satir ici <style> bloklarini ve harici <link rel=stylesheet>
 * kaynaklarini (onceden indirilmis kaynak arabelleginden) toplar. */
static int css_pos_inert(const char *html, const char *pos) {
    const char *opens[2] = { "<noscript", "<template" };
    const char *closes[2] = { "</noscript", "</template" };
    for (int t = 0; t < 2; t++) {
        const char *ns = html;
        for (;;) {
            const char *a = find_substr_ci(ns, opens[t]);
            if (!a || a >= pos) break;
            const char *b = find_substr_ci(a, closes[t]);
            if (!b || b > pos) return 1;
            ns = b + 1;
        }
    }
    return 0;
}

static void collect_css_from_html(const char *html) {
    const char *p = html;
    while (*p) {
        const char *lt = find_substr_ci(p, "<style");
        const char *lk = find_substr_ci(p, "<link");
        const char *next;
        if (lt && (!lk || lt < lk)) next = lt;
        else if (lk) next = lk;
        else break;

        if (css_pos_inert(html, next)) { p = next + 1; continue; }

        const char *q = next;
        while (*q && *q != '>') q++;

        if (next == lt) {
            const char *cs = (*q == '>') ? q + 1 : q;
            const char *ce = find_substr_ci(cs, "</style");
            if (!ce) break;
            int len = (int)(ce - cs);
            if (len > 0 && g_basi && g_basi->mem) {
                char *buf = g_basi->mem->alloc((size_t)len + 1);
                if (buf) {
                    for (int i = 0; i < len; i++) buf[i] = cs[i];
                    buf[len] = 0;
                    css_parse(buf);
                    g_basi->mem->free(buf);
                }
            }
            const char *end = ce;
            while (*end && *end != '>') end++;
            p = (*end == '>') ? end + 1 : end;
        } else {
            char href[420], rel[64];
            if (find_attr(next, q, "href", href, sizeof(href)) &&
                find_attr(next, q, "rel", rel, sizeof(rel)) &&
                find_substr_ci(rel, "stylesheet")) {
                char abs[440];
                resolve_url(g_cur_scheme, g_cur_host, href, abs, sizeof(abs));
                int ri = res_find(abs);
                if (ri >= 0 && g_res_css[ri] && g_res_data[ri])
                    css_parse((const char *)g_res_data[ri]);
            }
            p = (*q == '>') ? q + 1 : q;
        }
    }
}

/* Agdan gelen gercek sayfayi CSS motoruyla cizer. */
/* ---------------- Wolf Engine DOM koprusu ---------------- */
/* Rust JS motoru, bu C geri cagrilari uzerinden g_dn agacini degistirir.
 * Tarayicida hic Rust kodu yoktur; yalnizca C ABI cagrilari vardir. */

static int wl_eqn(const char *s, const char *p, unsigned n) {
    for (unsigned i = 0; i < n; i++) if (s[i] != p[i]) return 0;
    return s[n] == 0;
}

static int wl_class_has(const char *cls, const char *name, unsigned nlen) {
    const char *p = cls;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        const char *q = p;
        while (*q && *q != ' ' && *q != '\t' && *q != '\n') q++;
        if ((unsigned)(q - p) == nlen) {
            int ok = 1;
            for (unsigned i = 0; i < nlen; i++) if (p[i] != name[i]) { ok = 0; break; }
            if (ok) return 1;
        }
        p = q;
    }
    return 0;
}

static int wl_match_compound(int n, const char *sel, int len) {
    if (n < 0 || n >= g_dn_count || len <= 0) return 0;
    int i = 0;
    if (sel[0] == '#') {
        i = 1;
        int j = i; while (j < len && sel[j] != '.' && sel[j] != '#') j++;
        return wl_eqn(g_dn[n].id, sel + i, (unsigned)(j - i));
    }
    if (sel[0] == '.') {
        i = 1;
        int j = i; while (j < len && sel[j] != '.' && sel[j] != '#') j++;
        return wl_class_has(g_dn[n].cls, sel + i, (unsigned)(j - i));
    }
    int j = 0; while (j < len && sel[j] != '.' && sel[j] != '#') j++;
    if (j > 0 && !wl_eqn(g_dn[n].tag, sel, (unsigned)j)) return 0;
    while (j < len) {
        if (sel[j] == '.') {
            int k = j + 1; while (k < len && sel[k] != '.' && sel[k] != '#') k++;
            if (!wl_class_has(g_dn[n].cls, sel + j + 1, (unsigned)(k - j - 1))) return 0;
            j = k;
        } else if (sel[j] == '#') {
            int k = j + 1; while (k < len && sel[k] != '.' && sel[k] != '#') k++;
            if (!wl_eqn(g_dn[n].id, sel + j + 1, (unsigned)(k - j - 1))) return 0;
            j = k;
        } else j++;
    }
    return 1;
}

static int wl_match_full(int n, const char *sel, int len) {
    int parts[8][2];
    int pc = 0;
    int i = 0;
    while (i < len && pc < 8) {
        while (i < len && (sel[i] == ' ' || sel[i] == '>' || sel[i] == '+' || sel[i] == '\t')) i++;
        int s = i;
        while (i < len && sel[i] != ' ' && sel[i] != '>' && sel[i] != '+' && sel[i] != '\t') i++;
        if (i > s) { parts[pc][0] = s; parts[pc][1] = i - s; pc++; }
    }
    if (pc == 0) return 0;
    if (!wl_match_compound(n, sel + parts[pc - 1][0], parts[pc - 1][1])) return 0;
    int cur = n;
    for (int k = pc - 2; k >= 0; k--) {
        int a = g_dn[cur].parent;
        int found = 0;
        while (a >= 0) {
            if (wl_match_compound(a, sel + parts[k][0], parts[k][1])) { found = 1; break; }
            a = g_dn[a].parent;
        }
        if (!found) return 0;
        cur = a;
    }
    return 1;
}

static long long wh_get_by_id(const char *id, unsigned len) {
    for (int n = 0; n < g_dn_count; n++)
        if (g_dn[n].id[0] && wl_eqn(g_dn[n].id, id, len)) return n + 1;
    return -1;
}
static long long wh_get_by_tag(const char *tag, unsigned len, int index) {
    int c = 0;
    for (int n = 0; n < g_dn_count; n++) {
        if (g_dn[n].tag[0] && wl_eqn(g_dn[n].tag, tag, len)) {
            if (c == index) return n + 1;
            c++;
        }
    }
    return -1;
}
static long long wh_get_by_class(const char *cls, unsigned len, int index) {
    int c = 0;
    for (int n = 0; n < g_dn_count; n++) {
        if (g_dn[n].cls[0] && wl_class_has(g_dn[n].cls, cls, len)) {
            if (c == index) return n + 1;
            c++;
        }
    }
    return -1;
}
static long long wh_query(const char *sel, unsigned len) {
    for (int n = 0; n < g_dn_count; n++) if (wl_match_full(n, sel, (int)len)) return n + 1;
    return -1;
}
static long long wh_query_idx(const char *sel, unsigned len, int index) {
    int c = 0;
    for (int n = 0; n < g_dn_count; n++) if (wl_match_full(n, sel, (int)len)) {
        if (c == index) return n + 1;
        c++;
    }
    return -1;
}
static long long wh_create(const char *tag, unsigned len) {
    char t[12];
    int i = 0;
    for (; i < (int)len && i < 11; i++) t[i] = tag[i];
    t[i] = 0;
    int n = dn_new(t);
    return n < 0 ? -1 : n + 1;
}
static int wh_tag_of(long long h, char *out, unsigned cap) {
    int n = (int)h - 1;
    if (n < 0 || n >= g_dn_count || !out || cap == 0) return -1;
    int i = 0;
    for (; g_dn[n].tag[i] && i < (int)cap - 1; i++) out[i] = g_dn[n].tag[i];
    out[i] = 0;
    return i;
}
static int wh_get_text(long long h, char *out, unsigned cap) {
    int n = (int)h - 1;
    if (n < 0 || n >= g_dn_count || !out || cap == 0) return -1;
    int i = 0;
    for (; g_dn[n].text[i] && i < (int)cap - 1; i++) out[i] = g_dn[n].text[i];
    out[i] = 0;
    return i;
}
static void wh_set_text(long long h, const char *s, unsigned len) {
    int n = (int)h - 1;
    if (n < 0 || n >= g_dn_count || !s) return;
    int i = 0;
    for (; i < (int)len && i < CSS_NTEXT - 1; i++) g_dn[n].text[i] = s[i];
    g_dn[n].text[i] = 0;
}
static int wh_get_attr(long long h, const char *name, unsigned nlen, char *out, unsigned cap) {
    int n = (int)h - 1;
    if (n < 0 || n >= g_dn_count || !out || cap == 0) return -1;
    const char *v = 0;
    if (wl_eqn("id", name, nlen)) v = g_dn[n].id;
    else if (wl_eqn("class", name, nlen)) v = g_dn[n].cls;
    else if (wl_eqn("style", name, nlen)) v = g_dn[n].istyle;
    else if (wl_eqn("src", name, nlen)) v = g_dn[n].src;
    else if (wl_eqn("href", name, nlen)) v = g_dn[n].href;
    else if (wl_eqn("name", name, nlen)) v = g_dn[n].name;
    else if (wl_eqn("type", name, nlen)) v = g_dn[n].type;
    else if (wl_eqn("value", name, nlen)) v = g_dn[n].value;
    else if (wl_eqn("placeholder", name, nlen) || wl_eqn("title", name, nlen)) v = g_dn[n].ph;
    else if (wl_eqn("alt", name, nlen)) v = g_dn[n].alt;
    else return -1;
    int i = 0;
    for (; v[i] && i < (int)cap - 1; i++) out[i] = v[i];
    out[i] = 0;
    return i;
}
static int wh_set_attr(long long h, const char *name, unsigned nlen, const char *val, unsigned vlen) {
    int n = (int)h - 1;
    if (n < 0 || n >= g_dn_count || !val) return -1;
    if (wl_eqn("id", name, nlen)) {
        int i = 0; for (; i < (int)vlen && i < (int)sizeof(g_dn[n].id) - 1; i++) g_dn[n].id[i] = val[i]; g_dn[n].id[i] = 0;
    } else if (wl_eqn("class", name, nlen)) {
        int i = 0; for (; i < (int)vlen && i < (int)sizeof(g_dn[n].cls) - 1; i++) g_dn[n].cls[i] = val[i]; g_dn[n].cls[i] = 0;
    } else if (wl_eqn("style", name, nlen)) {
        int i = 0; for (; i < (int)vlen && i < (int)sizeof(g_dn[n].istyle) - 1; i++) g_dn[n].istyle[i] = val[i]; g_dn[n].istyle[i] = 0;
    } else if (wl_eqn("src", name, nlen)) {
        int i = 0; for (; i < (int)vlen && i < (int)sizeof(g_dn[n].src) - 1; i++) g_dn[n].src[i] = val[i]; g_dn[n].src[i] = 0;
    } else if (wl_eqn("href", name, nlen)) {
        int i = 0; for (; i < (int)vlen && i < (int)sizeof(g_dn[n].href) - 1; i++) g_dn[n].href[i] = val[i]; g_dn[n].href[i] = 0;
    } else if (wl_eqn("name", name, nlen)) {
        int i = 0; for (; i < (int)vlen && i < (int)sizeof(g_dn[n].name) - 1; i++) g_dn[n].name[i] = val[i]; g_dn[n].name[i] = 0;
    } else if (wl_eqn("type", name, nlen)) {
        int i = 0; for (; i < (int)vlen && i < (int)sizeof(g_dn[n].type) - 1; i++) g_dn[n].type[i] = val[i]; g_dn[n].type[i] = 0;
    } else if (wl_eqn("value", name, nlen)) {
        int i = 0; for (; i < (int)vlen && i < (int)sizeof(g_dn[n].value) - 1; i++) g_dn[n].value[i] = val[i]; g_dn[n].value[i] = 0;
    } else if (wl_eqn("placeholder", name, nlen) || wl_eqn("title", name, nlen)) {
        int i = 0; for (; i < (int)vlen && i < (int)sizeof(g_dn[n].ph) - 1; i++) g_dn[n].ph[i] = val[i]; g_dn[n].ph[i] = 0;
    } else if (wl_eqn("alt", name, nlen)) {
        int i = 0; for (; i < (int)vlen && i < (int)sizeof(g_dn[n].alt) - 1; i++) g_dn[n].alt[i] = val[i]; g_dn[n].alt[i] = 0;
    } else {
        return -1;
    }
    return 0;
}
static void wh_append(long long par, long long ch) {
    int p = (int)par - 1, c = (int)ch - 1;
    if (p >= 0 && c >= 0 && p < g_dn_count && c < g_dn_count && p != c) dn_add(p, c);
}
static void wh_remove(long long h) {
    int n = (int)h - 1;
    if (n < 0 || n >= g_dn_count) return;
    int p = g_dn[n].parent;
    if (p >= 0) {
        int prev = -1;
        for (int c = g_dn[p].first; c >= 0; c = g_dn[c].next) {
            if (c == n) break;
            prev = c;
        }
        if (prev < 0) g_dn[p].first = g_dn[n].next;
        else g_dn[prev].next = g_dn[n].next;
        if (g_dn[p].last == n) g_dn[p].last = prev;
    }
    g_dn[n].parent = -1;
    g_dn[n].next = -1;
    g_dn[n].widget = 0;
}
static void wh_set_style(long long h, const char *prop, unsigned plen, const char *val, unsigned vlen) {
    int n = (int)h - 1;
    if (n < 0 || n >= g_dn_count) return;
    int cap = (int)sizeof(g_dn[n].istyle);
    if (!prop || plen == 0) {
        int i = 0;
        if (val) for (; i < (int)vlen && i < cap - 1; i++) g_dn[n].istyle[i] = val[i];
        g_dn[n].istyle[i] = 0;
        return;
    }
    int cur = 0; while (g_dn[n].istyle[cur]) cur++;
    if (cur > 0 && cur < cap - 1) g_dn[n].istyle[cur++] = ';';
    for (unsigned i = 0; i < plen && cur < cap - 1; i++) g_dn[n].istyle[cur++] = prop[i];
    if (cur < cap - 1) g_dn[n].istyle[cur++] = ':';
    if (val) for (unsigned i = 0; i < vlen && cur < cap - 1; i++) g_dn[n].istyle[cur++] = val[i];
    g_dn[n].istyle[cur] = 0;
}
static void wh_navigate(const char *url, unsigned len) {
    char u[URL_MAX];
    int i = 0;
    for (; i < (int)len && i < URL_MAX - 1; i++) u[i] = url[i];
    u[i] = 0;
    if (!u[0]) return;
    if (starts_with(u, "http://") || starts_with(u, "https://")) {
        navigate(u, 1);
        return;
    }
    char abs[URL_MAX];
    if (u[0] == '/' && g_cur_host[0]) {
        bcopy_str(abs, g_cur_scheme, sizeof(abs));
        bcat(abs, "://", sizeof(abs));
        bcat(abs, g_cur_host, sizeof(abs));
        bcat(abs, u, sizeof(abs));
    } else {
        bcopy_str(abs, "https://", sizeof(abs));
        bcat(abs, u, sizeof(abs));
    }
    navigate(abs, 1);
}
static long long wh_body(void) { return 1; }
static long long wh_parent(long long h) {
    int n = (int)h - 1;
    if (n < 0 || n >= g_dn_count) return -1;
    return g_dn[n].parent < 0 ? -1 : g_dn[n].parent + 1;
}
static long long wh_child_count(long long h) {
    int n = (int)h - 1;
    if (n < 0 || n >= g_dn_count) return -1;
    int c = 0;
    for (int x = g_dn[n].first; x >= 0; x = g_dn[x].next) c++;
    return c;
}
static long long wh_child_at(long long h, int index) {
    int n = (int)h - 1;
    if (n < 0 || n >= g_dn_count || index < 0) return -1;
    int c = 0;
    for (int x = g_dn[n].first; x >= 0; x = g_dn[x].next) {
        if (c == index) return x + 1;
        c++;
    }
    return -1;
}

static void blog_s(const char *s);
static void blog_i(int v);
static void blog(const char *s, unsigned len);
static int wh_set_cookie(const char *name, unsigned nlen, const char *val, unsigned vlen) {
    if (!g_basi || !g_basi->net || !g_basi->net->cookie_set || !g_cur_host[0]) return -1;
    char n[64], v[4096];
    unsigned i;
    for (i = 0; i < nlen && i < sizeof(n) - 1; i++) n[i] = name[i];
    n[i] = 0;
    unsigned nl = i;
    for (i = 0; i < vlen && i < sizeof(v) - 1; i++) v[i] = val[i];
    v[i] = 0;
    unsigned vl = i;
    g_basi->net->cookie_set(g_cur_host, n, v);
    blog_s("[ck] js set "); blog(n, nl);
    blog_s(" len="); blog_i((int)vl);
    blog_s(" host="); blog_s(g_cur_host); blog_s("\n");
    return 0;
}
static int wh_current_url(char *out, unsigned cap) {
    if (!out || cap == 0) return 0;
    unsigned i = 0;
    for (; g_cur_url[i] && i < cap - 1; i++) out[i] = g_cur_url[i];
    out[i] = 0;
    return (int)i;
}

/* 0.6: sayfa JS'inin fetch/XHR cagrilari icin ham HTTP(S) GET.
 * iFrame/relative URL'leri sayfanin kendi adresine gore cozer. */
static int wh_fetch(const char *url, unsigned urlen,
                    const char *method, unsigned mlen,
                    const char *body, unsigned blen,
                    char *out, unsigned cap, unsigned *len_out) {
    (void)method; (void)mlen; (void)body; (void)blen;
    if (!url || !urlen || !out || !cap) return -1;
    if (!g_basi || !g_basi->net || !g_basi->net->download) return -1;

    char u[512];
    unsigned n = urlen < sizeof(u) - 1 ? urlen : (unsigned)sizeof(u) - 1;
    for (unsigned i = 0; i < n; i++) u[i] = url[i];
    u[n] = 0;

    /* goreli yol: sayfanin host + semasi ile birlestir */
    if (u[0] == '/') {
        char full[512];
        int o = 0;
        const char *sch = g_cur_scheme[0] ? g_cur_scheme : "https";
        for (int i = 0; sch[i] && o < (int)sizeof(full) - 1; i++) full[o++] = sch[i];
        full[o++] = ':'; full[o++] = '/'; full[o++] = '/';
        for (int i = 0; g_cur_host[i] && o < (int)sizeof(full) - 1; i++) full[o++] = g_cur_host[i];
        for (unsigned i = 0; u[i] && o < (int)sizeof(full) - 1; i++) full[o++] = u[i];
        full[o] = 0;
        for (unsigned i = 0; i < sizeof(u) && full[i]; i++) u[i] = full[i];
        u[sizeof(u) - 1] = 0;
    }

    uint32_t got = 0;
    int rc = g_basi->net->download(u, out, cap, &got);
    if (len_out) *len_out = got;
    return rc;
}

static wolf_host_t g_wolf_host = {
    wh_get_by_id, wh_get_by_tag, wh_get_by_class, wh_query, wh_query_idx, wh_create,
    wh_tag_of, wh_get_text, wh_set_text, wh_get_attr, wh_set_attr,
    wh_append, wh_remove, wh_set_style, wh_navigate,
    wh_body, wh_parent, wh_child_count, wh_child_at,
    wh_set_cookie, wh_current_url, wh_fetch
};

static inline void blog_outb(unsigned short p, unsigned char v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p));
}
static inline unsigned char blog_inb(unsigned short p) {
    unsigned char r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(p));
    return r;
}
static void blog(const char *s, unsigned len) {
    for (unsigned i = 0; i < len; i++) {
        while (!(blog_inb(0x3FD) & 0x20)) { }
        blog_outb(0x3F8, (unsigned char)s[i]);
    }
}
static void blog_s(const char *s) {
    while (s && *s) { blog(s, 1); s++; }
}
static void blog_i(int v) {
    char b[16];
    int i = 0;
    if (v == 0) { blog_s("0"); return; }
    if (v < 0) { blog_s("-"); v = -v; }
    while (v) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) blog(&b[--i], 1);
}
static void browser_wolf_print(const char *s, unsigned len) {
    blog(s, len);
}

static void run_page_scripts(void) {
    if (g_script_len <= 0) return;
    g_script_src[g_script_len] = 0;
    blog_s("[js] eval bytes=");
    blog_i(g_script_len);
    blog_s("\n");
    wolf_engine_set_print(browser_wolf_print);
    wolf_engine_set_host(&g_wolf_host);
    wolf_engine_set_limits(400000000, 64);
    wolf_engine_reset();
    char out[512];
    out[0] = 0;
    int rc = wolf_engine_eval(g_script_src, (unsigned)g_script_len, out, sizeof(out));
    blog_s("[js] rc=");
    blog_i(rc);
    blog_s(" out=");
    blog_s(out);
    blog_s("\n");
}

static int page_is_blocked(const char *h) {
    static const char *markers[] = {
        "enablejs", "unusual traffic", "Son bir ad", "Devam etmek",
        "captcha", "challenge-platform", "just a moment",
        "checking your browser", "cf-chl", "sorry/index", 0
    };
    int blocked = 0;
    for (int i = 0; markers[i]; i++) {
        if (find_substr_ci(h, markers[i])) { blocked = 1; break; }
    }
    if (!blocked) return 0;

    /* Gercek arama sonucu sayfalari da bu kelimeleri (i18n/JS) icerebilir.
     * Sonuc kabi varsa sayfa engelli degildir. */
    if (find_substr_ci(h, "id=\"b_results\"") ||
        find_substr_ci(h, "class=\"b_algo\"") ||
        find_substr_ci(h, "id=\"rso\"") ||
        find_substr_ci(h, "id=\"search\"") ||
        find_substr_ci(h, "id=\"links\""))
        return 0;
    return 1;
}

static void push_notice_first(const char *msg) {
    int note = dn_new("div");
    if (note < 0) return;
    bcopy_str(g_dn[note].istyle,
              "background:#fff3cd;color:#664d03;padding:10px;border:1px solid #ffe69c;"
              "margin:8px;border-radius:6px;font-size:13px;", sizeof(g_dn[note].istyle));
    dn_text(note, msg, b_len(msg));
    int first = g_dn[0].first;
    g_dn[note].parent = 0;
    g_dn[note].next = first;
    g_dn[0].first = note;
    if (g_dn[0].last < 0) g_dn[0].last = note;
}

static void render_page(const char *html) {
    if (!g_content) return;
    lv_obj_clean(g_content);
    images_clear();
    g_css_count = 0;
    g_vcount = 0;
    g_pform_count = 0;
    g_grid_slot = 0;

    int vw = lv_obj_get_width(g_content);
    int vh = lv_obj_get_height(g_content);
    g_view_w = (vw > 40) ? vw : 900;
    g_view_h = (vh > 40) ? vh : 600;

    lv_obj_set_style_bg_color(g_content, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(g_content, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_content, 0, 0);
    lv_obj_set_style_pad_row(g_content, 0, 0);
    lv_obj_set_flex_flow(g_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_content, LV_DIR_VER);

    if (g_group && g_addr) {
        lv_group_remove_all_objs(g_group);
        lv_group_add_obj(g_group, g_addr);
    }

    collect_css_from_html(html);
    dom_parse(html);
    run_page_scripts();

    if (page_is_blocked(html))
        push_notice_first("Bu site, robot dogrulamasi (bot korumasi) istedigi icin "
                          "icerigi bu tarayiciya gostermedi. Site JavaScript dogrulamasi "
                          "gerektiriyor; lutfen baska bir kaynak deneyin.");

    css_style_t base;
    style_defaults(&base);
    if (g_dn_count > 1) build_node(0, g_content, &base);
}

/* ---------------- Ust bar olaylari ---------------- */
static int looks_like_query(const char *s) {
    if (!s || !s[0]) return 0;
    if (starts_with(s, "http://") || starts_with(s, "https://")) return 0;
    if (str_eq(s, "localhost") || starts_with(s, "localhost:")) return 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == ' ') return 1;
        if (s[i] == '.' || s[i] == ':' || s[i] == '/') return 0;
    }
    return 1;
}

static void go_cb(lv_event_t *e) {
    (void)e;
    const char *t = lv_textarea_get_text(g_addr);
    char url[URL_MAX];
    if (looks_like_query(t)) {
        bcopy_str(url, "https://www.bing.com/search?q=", sizeof(url));
        int bl = b_len(url);
        search_url_encode(t, url + bl, URL_MAX - bl);
    } else {
        bcopy_str(url, t, sizeof(url));
    }
    navigate(url, 1);
}

/* ================= Yer imleri + Indirme Yoneticisi (0.6) =================
 * Yer imleri C:/etc/yerimleri.cfg dosyasinda satir satir saklanir.
 * Indirmeler C:/indirilenler klasorunden okunur; kayit defteri
 * C:/etc/indirmeler.log dosyasina yazilir. */
#define BM_FILE   "C:/etc/yerimleri.cfg"
#define BM_MAX    24
#define BM_URLLEN 200

static lv_obj_t *s_panel;
static lv_obj_t *s_panel_list;
static int       s_panel_mode;      /* 0 kapali, 1 yer imleri, 2 indirmeler */
static char      s_bm[BM_MAX][BM_URLLEN];
static int       s_bm_n;

static int bm_load(void) {
    s_bm_n = 0;
    if (!g_basi || !g_basi->fs || !g_basi->fs->read_path) return 0;
    static char buf[BM_MAX * (BM_URLLEN + 2)];
    uint32_t n = g_basi->fs->read_path(BM_FILE, buf, sizeof(buf) - 1);
    if (!n) return 0;
    buf[n] = 0;
    int k = 0;
    for (uint32_t i = 0; i <= n; i++) {
        char c = (i < n) ? buf[i] : '\n';
        if (c == '\n' || c == '\r') {
            if (k > 0 && s_bm_n < BM_MAX) {
                s_bm[s_bm_n][k] = 0;
                s_bm_n++;
            }
            k = 0;
            continue;
        }
        if (k < BM_URLLEN - 1) s_bm[s_bm_n < BM_MAX ? s_bm_n : BM_MAX - 1][k++] = c;
    }
    return s_bm_n;
}

static void bm_save(void) {
    if (!g_basi || !g_basi->fs || !g_basi->fs->create_in || !g_basi->fs->write) return;
    if (g_basi->fs->mkdir) g_basi->fs->mkdir("C:/etc");
    void *h = g_basi->fs->create_in("C:/etc", "yerimleri.cfg", 0);
    if (!h) return;
    for (int i = 0; i < s_bm_n; i++) {
        uint32_t l = (uint32_t)b_len(s_bm[i]);
        if (l) g_basi->fs->write(h, s_bm[i], l);
        g_basi->fs->write(h, "\n", 1);
    }
}

static int bm_find(const char *url) {
    for (int i = 0; i < s_bm_n; i++)
        if (str_eq(s_bm[i], url)) return i;
    return -1;
}

/* Indirme kaydi (tarih + ad + boyut + url) */
static void dl_log(const char *name, uint32_t size, const char *url) {
    if (!g_basi || !g_basi->fs || !g_basi->fs->create_in || !g_basi->fs->write) return;
    if (g_basi->fs->mkdir) g_basi->fs->mkdir("C:/etc");
    void *h = g_basi->fs->create_in("C:/etc", "indirmeler.log", 0);
    if (!h) return;
    char b[64];
    int n = 0;
    const char *p = name ? name : "?";
    while (*p && n < 40) b[n++] = *p++;
    b[n++] = '\t';
    uint32_t v = size;
    char tmp[12]; int k = 0;
    if (!v) tmp[k++] = '0';
    while (v && k < 12) { tmp[k++] = (char)('0' + v % 10); v /= 10; }
    while (k) b[n++] = tmp[--k];
    b[n++] = '\t';
    b[n] = 0;
    g_basi->fs->write(h, b, (uint32_t)n);
    const char *u = url ? url : "";
    g_basi->fs->write(h, u, (uint32_t)b_len(u));
    g_basi->fs->write(h, "\n", 1);
}

static void panel_close(void) {
    if (s_panel) lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    s_panel_mode = 0;
}

static void panel_item_cb(lv_event_t *e) {
    int code = (int)(intptr_t)lv_event_get_user_data(e);
    int mode = code / 1000;
    int idx = code % 1000;
    if (mode == 1) {
        if (idx >= 0 && idx < s_bm_n) {
            lv_textarea_set_text(g_addr, s_bm[idx]);
            navigate(s_bm[idx], 1);
        }
    } else if (mode == 2) {
        char path[200];
        int n = 0;
        const char *pre = "C:/indirilenler/";
        while (pre[n]) { path[n] = pre[n]; n++; }
        char nm[120]; int isd = 0;
        if (g_basi->fs->list_dir && g_basi->fs->list_dir("C:/indirilenler", idx, nm, sizeof(nm), &isd) == 0) {
            for (int i = 0; nm[i] && n < (int)sizeof(path) - 1; i++) path[n++] = nm[i];
            path[n] = 0;
            arctian_shell_open_file(path);
        }
    } else if (mode == 3) {
        /* Gecmis paneli: kayitlar en yeniden en eskiye gosterilir. */
        int total = g_hist_count + g_fwd_count;
        int k = total - 1 - idx;
        const char *u = 0;
        if (k >= g_fwd_count && k - g_fwd_count < g_hist_count)
            u = g_history[k - g_fwd_count];
        else if (k < g_fwd_count)
            u = g_fwd[k];
        if (u && u[0]) {
            lv_textarea_set_text(g_addr, u);
            navigate(u, 1);
        }
    }
    panel_close();
}

static void panel_row(const char *text, int code) {
    lv_obj_t *r = lv_obj_create(s_panel_list);
    lv_obj_remove_style_all(r);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(r, LV_PCT(100), 34);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(ADL_ACCENT), LV_STATE_PRESSED);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, panel_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    lv_obj_t *l = lv_label_create(r);
    lv_label_set_text(l, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, LV_PCT(94));
    lv_obj_set_style_text_color(l, lv_color_hex(ADL_TEXT), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
}

static void panel_fill(void) {
    if (!s_panel_list) return;
    lv_obj_clean(s_panel_list);
    if (s_panel_mode == 1) {
        bm_load();
        if (s_bm_n == 0) { panel_row("(yer imi yok)", 1999); return; }
        for (int i = 0; i < s_bm_n; i++) panel_row(s_bm[i], 1000 + i);
    } else if (s_panel_mode == 2) {        if (!g_basi->fs || !g_basi->fs->list_dir) { panel_row("(dosya sistemi yok)", 2999); return; }
        int n = g_basi->fs->count_dir ? g_basi->fs->count_dir("C:/indirilenler") : 0;
        if (n <= 0) { panel_row("(indirme yok)", 2999); return; }
        int shown = 0;
        for (int i = 0; i < n && shown < 24; i++) {
            char nm[120]; int isd = 0;
            if (g_basi->fs->list_dir("C:/indirilenler", i, nm, sizeof(nm), &isd) != 0) continue;
            if (isd) continue;
            panel_row(nm, 2000 + i);
            shown++;
        }
    } else if (s_panel_mode == 3) {
        int total = g_hist_count + g_fwd_count;
        if (total == 0) { panel_row("(gecmis bos)", 3999); return; }
        for (int i = total - 1, shown = 0; i >= 0 && shown < 24; i--, shown++) {
            const char *u = 0;
            if (i >= g_fwd_count && i - g_fwd_count < g_hist_count)
                u = g_history[i - g_fwd_count];
            else if (i < g_fwd_count)
                u = g_fwd[i];
            if (u && u[0]) panel_row(u, 3000 + (total - 1 - i));
        }
    }
}

static void panel_toggle(int mode) {
    if (!s_panel) return;
    if (s_panel_mode == mode) { panel_close(); return; }
    s_panel_mode = mode;
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);
    panel_fill();
}

static void bm_btn_cb(lv_event_t *e) { (void)e; panel_toggle(1); }
static void dl_btn_cb(lv_event_t *e) { (void)e; panel_toggle(2); }
static void hist_btn_cb(lv_event_t *e) { (void)e; panel_toggle(3); }

/* Gecerli adresi yer imlerine ekler/cikarir. */
static void bm_star_cb(lv_event_t *e) {
    (void)e;
    const char *u = g_addr ? lv_textarea_get_text(g_addr) : "";
    if (!u || !u[0]) u = g_basi && g_basi->net && g_basi->net->last_url ? g_basi->net->last_url() : "";
    if (!u || !u[0]) { if (g_status) lv_label_set_text(g_status, "Once bir sayfa acin"); return; }
    bm_load();
    int i = bm_find(u);
    if (i >= 0) {
        for (int k = i; k < s_bm_n - 1; k++) bcopy_str(s_bm[k], s_bm[k + 1], BM_URLLEN);
        s_bm_n--;
        bm_save();
        if (g_status) lv_label_set_text(g_status, "Yer imi kaldirildi");
    } else {
        if (s_bm_n >= BM_MAX) { if (g_status) lv_label_set_text(g_status, "Yer imi listesi dolu"); return; }
        bcopy_str(s_bm[s_bm_n], u, BM_URLLEN);
        s_bm_n++;
        bm_save();
        if (g_status) lv_label_set_text(g_status, "Yer imlerine eklendi");
    }
    if (s_panel_mode == 1 && s_panel) panel_fill();
}

static void download_cb(lv_event_t *e) {
    (void)e;
    if (!g_basi || !g_basi->net || !g_basi->net->download) return;
    const char *u = g_addr ? lv_textarea_get_text(g_addr) : "";
    if (!u || !u[0]) u = g_basi->net->last_url ? g_basi->net->last_url() : "";
    if (!u || !u[0]) { if (g_status) lv_label_set_text(g_status, "Indirilecek adres yok"); return; }

    /* dosya adi: URL'nin son parcasi; yoksa indir.bin */
    const char *base = u;
    for (const char *p = u; *p; p++) if (*p == '/') base = p + 1;
    char name[80];
    int n = 0;
    for (int i = 0; base[i] && base[i] != '?' && n < 60; i++) {
        char c = base[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        name[n++] = ok ? c : '_';
    }
    if (n == 0 || name[0] == '_') { const char *d = "indir.bin"; n = 0; for (; d[n]; n++) name[n] = d[n]; }
    name[n] = 0;

    static uint8_t *buf;
    const uint32_t cap = 4u * 1024u * 1024u;
    if (!buf) buf = (uint8_t *)g_basi->mem->alloc(cap);
    if (!buf) { if (g_status) lv_label_set_text(g_status, "Bellek yetersiz"); return; }

    if (g_status) lv_label_set_text(g_status, "Indiriliyor...");
    uint32_t len = 0;
    int rc = g_basi->net->download(u, buf, cap, &len);
    if (rc != 0) {
        if (g_status) lv_label_set_text(g_status, "Indirme basarisiz (ag/TLS)");
        arctian_notify("Indirme", "Basarisiz: ag veya TLS hatasi");
        return;
    }
    asi_fs_t *fs = g_basi->fs;
    if (fs && fs->mkdir) fs->mkdir("C:/indirilenler");
    void *h = (fs && fs->create_in) ? fs->create_in("C:/indirilenler", name, 0) : 0;
    if (!h) { if (g_status) lv_label_set_text(g_status, "Dosya olusturulamadi"); return; }
    fs->write(h, buf, len);
    if (g_status) lv_label_set_text(g_status, "Kaydedildi: C:/indirilenler");
    dl_log(name, len, u);
    arctian_notify("Indirme tamam", name);
}


static void addr_ready_cb(lv_event_t *e) { go_cb(e); }

static void back_cb(lv_event_t *e) {
    (void)e;
    if (g_hist_count <= 0) return;
    /* Mevcut sayfayi ileri yiginina koy (0.6). */
    if (g_cur_url[0] && g_fwd_count < HIST_MAX)
        bcopy_str(g_fwd[g_fwd_count++], g_cur_url, URL_MAX);
    g_hist_count--;
    char url[URL_MAX];
    bcopy_str(url, g_history[g_hist_count], sizeof(url));
    if (g_addr) lv_textarea_set_text(g_addr, url);
    navigate(url, 0);
}

/* Ileri: ileri yiginindan bir sayfa geri alir, mevcut sayfayi gecmise iter. */
static void forward_cb(lv_event_t *e) {
    (void)e;
    if (g_fwd_count <= 0) return;
    g_fwd_count--;
    char url[URL_MAX];
    bcopy_str(url, g_fwd[g_fwd_count], sizeof(url));
    if (g_cur_url[0] && g_hist_count < HIST_MAX)
        bcopy_str(g_history[g_hist_count++], g_cur_url, URL_MAX);
    if (g_addr) lv_textarea_set_text(g_addr, url);
    navigate(url, 0);
}

static void browser_del_cb(lv_event_t *e) {
    (void)e;
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, NULL);
    if (g_wheel_timer) { lv_timer_delete(g_wheel_timer); g_wheel_timer = 0; }
    images_clear();
    res_clear();
    g_addr = 0; g_status = 0; g_content = 0;
    s_panel = 0; s_panel_list = 0; s_panel_mode = 0;
    g_fwd_count = 0;
}

/* ---------------- Giris noktasi ---------------- */
void app_entry(lv_obj_t *win, asi_t *asi) {
    g_basi = asi;
    g_block_count = 0;
    g_form_count = 0;
    g_hist_count = 0;
    g_cur_url[0] = 0;
    g_cur_host[0] = 0;
    bcopy_str(g_cur_scheme, "https", sizeof(g_cur_scheme));

    /* --- Duzen: TUM pencere tek bir dikey flex kutusu. --- *
     * Onceki surumde icerik alaninin yuksekligi acilista bir kere piksel
     * olarak hesaplaniyordu (h - 44 - 22) ve pencere sonradan buyutulunce
     * (orn. tam ekran) bu deger hic guncellenmiyordu - bu yuzden icerik eski
     * boyutunda kaliyordu. Cozum: piksel hesabi yapmiyoruz. win'i dikey flex
     * konteyner yapip topbar/durum satirini sabit yukseklikte, icerik alanini
     * ise flex_grow(1) ile "kalan tum alani doldur" olarak isaretliyoruz.
     * LVGL, ebeveyninin (win) boyutu her degistiginde (tam ekrana gecis dahil)
     * flex cocuklarini otomatik yeniden hesaplar - manuel piksel takibi
     * gerekmez. */
    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(win, 0, 0);
    lv_obj_set_style_pad_row(win, 0, 0);

    /* Ust bar: geri / adres / git */
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

    /* 0.6: ileri dugmesi (geri yigini ile simetrik) */
    lv_obj_t *fwd = lv_button_create(topbar);
    lv_obj_set_size(fwd, 40, 36);
    lv_obj_set_style_bg_color(fwd, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_radius(fwd, 8, 0);
    lv_obj_add_event_cb(fwd, forward_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *fwl = lv_label_create(fwd);
    lv_label_set_text(fwl, ">");
    lv_obj_set_style_text_color(fwl, lv_color_hex(ADL_TEXT), 0);
    lv_obj_center(fwl);

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

    /* Indir: gecerli adresi C:/indirilenler'e kaydeder. Onceden
     * tarayicida hic indirme akisi yoktu. */
    lv_obj_t *dl = lv_button_create(topbar);
    lv_obj_set_size(dl, 40, 36);
    lv_obj_set_style_bg_color(dl, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_radius(dl, 8, 0);
    lv_obj_add_event_cb(dl, download_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dll = lv_label_create(dl);
    lv_label_set_text(dll, "v");
    lv_obj_set_style_text_color(dll, lv_color_hex(ADL_TEXT), 0);
    lv_obj_center(dll);

    /* 0.6: yer imi yildizi ve indirme yoneticisi */
    lv_obj_t *star = lv_button_create(topbar);
    lv_obj_set_size(star, 40, 36);
    lv_obj_set_style_bg_color(star, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_radius(star, 8, 0);
    lv_obj_add_event_cb(star, bm_star_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stl = lv_label_create(star);
    lv_label_set_text(stl, "*");
    lv_obj_set_style_text_color(stl, lv_color_hex(ADL_WARN), 0);
    lv_obj_center(stl);

    lv_obj_t *dls = lv_button_create(topbar);
    lv_obj_set_size(dls, 40, 36);
    lv_obj_set_style_bg_color(dls, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_radius(dls, 8, 0);
    lv_obj_add_event_cb(dls, dl_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dlsl = lv_label_create(dls);
    lv_label_set_text(dlsl, "H");
    lv_obj_set_style_text_color(dlsl, lv_color_hex(ADL_TEXT), 0);
    lv_obj_center(dlsl);

    lv_obj_t *bms = lv_button_create(topbar);
    lv_obj_set_size(bms, 40, 36);
    lv_obj_set_style_bg_color(bms, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_radius(bms, 8, 0);
    lv_obj_add_event_cb(bms, bm_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bmsl = lv_label_create(bms);
    lv_label_set_text(bmsl, "Y");
    lv_obj_set_style_text_color(bmsl, lv_color_hex(ADL_TEXT), 0);
    lv_obj_center(bmsl);

    /* 0.6: gecmis paneli */
    lv_obj_t *his = lv_button_create(topbar);
    lv_obj_set_size(his, 40, 36);
    lv_obj_set_style_bg_color(his, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_radius(his, 8, 0);
    lv_obj_add_event_cb(his, hist_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hisl = lv_label_create(his);
    lv_label_set_text(hisl, "G");
    lv_obj_set_style_text_color(hisl, lv_color_hex(ADL_TEXT), 0);
    lv_obj_center(hisl);
    /* Durum satiri */
    g_status = lv_label_create(win);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_status, LV_PCT(96));
    lv_obj_set_style_pad_left(g_status, 6, 0);
    lv_label_set_text(g_status, "Hazir");
    lv_obj_set_style_text_color(g_status, lv_color_hex(ADL_TEXT_DIM), 0);

    /* Icerik alani (kaydirilabilir, dikey akan bloklar). Sabit piksel
     * yukseklik YOK - flex_grow(1) kalan tum alani otomatik doldurur ve
     * pencere yeniden boyutlandiginda (tam ekran dahil) kendini gunceller. */
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

    /* 0.6: yer imi + indirme paneli (sag ustte acilir) */
    s_panel = lv_obj_create(win);
    lv_obj_remove_style_all(s_panel);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_panel, 360, 300);
    lv_obj_align(s_panel, LV_ALIGN_TOP_RIGHT, -8, 48);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(ADL_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_panel, 12, 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_pad_all(s_panel, 10, 0);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    s_panel_list = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_panel_list);
    lv_obj_set_size(s_panel_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_panel_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_panel_list, 6, 0);
    lv_obj_set_scroll_dir(s_panel_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_panel_list, LV_SCROLLBAR_MODE_AUTO);
    s_panel_mode = 0;
    lv_obj_add_event_cb(win, browser_del_cb, LV_EVENT_DELETE, NULL);

    if (!g_group) g_group = lv_group_create();
    lv_group_remove_all_objs(g_group);
    lv_group_add_obj(g_group, g_addr);
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, g_group);
    if (g_wheel_timer) { lv_timer_delete(g_wheel_timer); g_wheel_timer = 0; }
    g_wheel_timer = lv_timer_create(page_wheel_cb, 30, NULL);

    /* Karsilama ekrani */
    push_block(BLK_H1, "Arctian Tarayici", 0);
    push_block(BLK_TEXT, "Adres yazip Enter'a basin (ornek: google.com, github.com)", 0);
    push_block(BLK_TEXT, "Linklere tiklayarak gezinebilir, \"<\" ile geri donebilirsiniz.", 0);
    push_block(BLK_TEXT, "Arama/form kutularina yazip Enter'a basarak veya \"Gonder\" butonuna tiklayarak formlari gonderebilirsiniz.", 0);
    render_blocks();
}
