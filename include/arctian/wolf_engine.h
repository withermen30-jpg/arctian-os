#ifndef ARCTIAN_WOLF_ENGINE_H
#define ARCTIAN_WOLF_ENGINE_H

/* Wolf Engine - Rust ile yazilmis JavaScript motoru icin C ABI.
 * Uygulamalar (ozellikle tarayici) bu arayuz uzerinden motora baglanir.
 * Motorun Rust kodu tarayiciya dahil edilmez; yalnizca bu baslik kullanilir. */

#ifdef __cplusplus
extern "C" {
#endif

/* --- DOM kopru geri cagrilari. Rust tarafi #repr(C) ayni sirada bekler. --- */
typedef struct {
    long long (*get_by_id)(const char *id, unsigned len);
    long long (*get_by_tag)(const char *tag, unsigned len, int index);
    long long (*get_by_class)(const char *cls, unsigned len, int index);
    long long (*query)(const char *sel, unsigned len);
    long long (*query_idx)(const char *sel, unsigned len, int index);
    long long (*create)(const char *tag, unsigned len);
    int       (*tag_of)(long long h, char *out, unsigned cap);
    int       (*get_text)(long long h, char *out, unsigned cap);
    void      (*set_text)(long long h, const char *s, unsigned len);
    int       (*get_attr)(long long h, const char *name, unsigned nlen, char *out, unsigned cap);
    int       (*set_attr)(long long h, const char *name, unsigned nlen, const char *val, unsigned vlen);
    void      (*append)(long long parent, long long child);
    void      (*remove)(long long h);
    void      (*set_style)(long long h, const char *prop, unsigned plen, const char *val, unsigned vlen);
    void      (*navigate)(const char *url, unsigned len);
    long long (*body)(void);
    long long (*parent)(long long h);
    long long (*child_count)(long long h);
    long long (*child_at)(long long h, int index);
    int       (*set_cookie)(const char *name, unsigned nlen, const char *val, unsigned vlen);
    int       (*current_url)(char *out, unsigned cap);
    /* 0.6: fetch/XHR icin ham HTTP(S) GET. Basarida 0 doner; govde
     * boyutu *len_out'a yazilir. NULL ise fetch devre disidir. */
    int       (*fetch)(const char *url, unsigned urlen,
                       const char *method, unsigned mlen,
                       const char *body, unsigned blen,
                       char *out, unsigned cap, unsigned *len_out);
} wolf_host_t;

typedef void (*wolf_print_fn)(const char *s, unsigned len);

unsigned wolf_engine_version(void);
void     wolf_engine_set_print(wolf_print_fn cb);
void     wolf_engine_set_host(const wolf_host_t *host);
void     wolf_engine_set_limits(unsigned steps, unsigned depth);
int      wolf_engine_eval(const char *src, unsigned len, char *out, unsigned out_cap);
void     wolf_engine_reset(void);
void     wolf_engine_dispatch_event(long long handle, const char *ev, unsigned len);
unsigned wolf_engine_listener_count(void);

#ifdef __cplusplus
}
#endif

#endif

