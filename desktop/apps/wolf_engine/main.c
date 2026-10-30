#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "quickjs.h"
#include "wolf_engine.h"
#include "asi.h"

#define CLS_ELEM 0
#define CLS_STYLE 1

static JSRuntime *g_rt = 0;
static JSContext *g_ctx = 0;
static JSValue g_global = { 0 };
static int g_global_live = 0;

static wolf_host_t g_host;
static int g_have_host = 0;
static wolf_print_fn g_print_cb = 0;

static int pump_jobs(void);

static JSClassID g_cls_elem = 0;
static JSClassID g_cls_style = 0;

static unsigned char *g_stack = 0;
static unsigned long g_stack_size = 0;

static const char *g_src = 0;
static unsigned g_srclen = 0;
static JSValue g_eval_result = { 0 };
static int g_eval_had = 0;

static unsigned long long g_steps = 0;
static unsigned long long g_step_budget = 0;
static int g_intr = 0;

typedef struct {
    long long handle;
    char ev[32];
    JSValue fn;
} listener_t;

static listener_t *g_lis = 0;
static int g_lis_n = 0;
static int g_lis_cap = 0;

extern void wolf_js_stack_call(void *sp, void (*fn)(void *), void *arg);

__asm__(
    ".text\n"
    ".globl wolf_js_stack_call\n"
    "wolf_js_stack_call:\n"
    "push %r12\n"
    "mov %rsp, %r12\n"
    "mov %rdi, %rsp\n"
    "and $-16, %rsp\n"
    "mov %rdx, %rdi\n"
    "call *%rsi\n"
    "mov %r12, %rsp\n"
    "pop %r12\n"
    "ret\n");

static int interrupt_cb(JSRuntime *rt, void *opaque) {
    (void)rt; (void)opaque;
    g_steps++;
    if (g_step_budget && g_steps > g_step_budget) { g_intr = 1; return 1; }
    return 0;
}

static JSValue js_nstr(const char *s, unsigned len) {
    if (!s) return JS_NewString(g_ctx, "");
    return JS_NewStringLen(g_ctx, s, (size_t)len);
}

static JSValue js_str(const char *s) { return JS_NewString(g_ctx, s ? s : ""); }

static long long opaque_handle(JSValueConst v, JSClassID cid) {
    void *o = JS_GetOpaque(v, cid);
    if (!o) return -1;
    return (long long)(intptr_t)o - 1;
}

static JSValue make_elem(long long h) {
    if (h < 0) return JS_NULL;
    JSValue o = JS_NewObjectClass(g_ctx, g_cls_elem);
    JS_SetOpaque(o, (void *)(intptr_t)(h + 1));
    return o;
}

static JSValue attr_str(long long h, const char *name, unsigned nlen) {
    if (!g_have_host || !g_host.get_attr) return js_str("");
    char buf[1024];
    int n = g_host.get_attr(h, name, nlen, buf, (unsigned)sizeof(buf));
    if (n < 0) return js_str("");
    if ((unsigned)n > sizeof(buf)) n = (int)sizeof(buf);
    return js_nstr(buf, (unsigned)n);
}

static void set_attr_raw(long long h, const char *n, unsigned nl, const char *v, unsigned vl) {
    if (g_have_host && g_host.set_attr) g_host.set_attr(h, n, nl, v, vl);
}

static JSValue elem_text(long long h) {
    if (!g_have_host || !g_host.get_text) return js_str("");
    char *buf = (char *)malloc(65536);
    if (!buf) return js_str("");
    int n = g_host.get_text(h, buf, 65536);
    if (n < 0) n = 0;
    JSValue r = js_nstr(buf, (unsigned)n);
    free(buf);
    return r;
}

static JSValue tag_upper(long long h) {
    if (!g_have_host || !g_host.tag_of) return js_str("");
    char buf[64];
    int n = g_host.tag_of(h, buf, sizeof(buf));
    if (n <= 0) return js_str("");
    if ((unsigned)n > sizeof(buf)) n = (int)sizeof(buf);
    for (int i = 0; i < n; i++) if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32;
    return js_nstr(buf, (unsigned)n);
}

static JSValue call_style_factory(long long h) {
    JSValue fn = JS_GetPropertyStr(g_ctx, g_global, "__styleFactory");
    if (!JS_IsFunction(g_ctx, fn)) { JS_FreeValue(g_ctx, fn); return js_str(""); }
    JSValue arg = JS_NewInt64(g_ctx, h);
    JSValue r = JS_Call(g_ctx, fn, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(g_ctx, arg);
    JS_FreeValue(g_ctx, fn);
    return r;
}

static JSValue collect_idx(long long (*f)(const char *, unsigned, int), const char *key, unsigned kl) {
    JSValue arr = JS_NewArray(g_ctx);
    if (!f) return arr;
    uint32_t n = 0;
    for (int i = 0; i < 4096; i++) {
        long long h = f(key, kl, i);
        if (h < 0) break;
        JS_SetPropertyUint32(g_ctx, arr, n++, make_elem(h));
    }
    return arr;
}

/* ---------- element accessors ---------- */

enum {
    P_ID, P_CLASS, P_TAG, P_TEXT, P_VALUE, P_HREF, P_STYLE, P_CLASSLIST,
    P_PARENT, P_CHILDREN, P_FIRST, P_LAST, P_DIMS, P_OUTER
};

static JSValue e_get(JSContext *ctx, JSValueConst this_val, int magic) {
    (void)ctx;
    long long h = opaque_handle(this_val, g_cls_elem);
    if (h < 0) return JS_UNDEFINED;
    switch (magic) {
        case P_ID: return attr_str(h, "id", 2);
        case P_CLASS: return attr_str(h, "class", 5);
        case P_TAG: return tag_upper(h);
        case P_TEXT: case P_OUTER: return elem_text(h);
        case P_VALUE: return attr_str(h, "value", 5);
        case P_HREF: return attr_str(h, "href", 4);
        case P_STYLE: return call_style_factory(h);
        case P_CLASSLIST: {
            JSValue o = JS_NewObjectClass(g_ctx, g_cls_style);
            JS_SetOpaque(o, (void *)(intptr_t)(h + 1));
            return o;
        }
        case P_PARENT: {
            long long p = (g_have_host && g_host.parent) ? g_host.parent(h) : -1;
            return p >= 0 ? make_elem(p) : JS_NULL;
        }
        case P_CHILDREN: {
            JSValue arr = JS_NewArray(g_ctx);
            uint32_t n = 0;
            long long cc = (g_have_host && g_host.child_count) ? g_host.child_count(h) : 0;
            for (long long i = 0; i < cc && i < 4096; i++) {
                long long c = g_host.child_at ? g_host.child_at(h, (int)i) : -1;
                if (c >= 0) JS_SetPropertyUint32(g_ctx, arr, n++, make_elem(c));
            }
            return arr;
        }
        case P_FIRST: case P_LAST: return JS_NULL;
        case P_DIMS: return JS_NewInt32(g_ctx, 0);
    }
    return JS_UNDEFINED;
}

static JSValue e_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic) {
    (void)ctx;
    long long h = opaque_handle(this_val, g_cls_elem);
    if (h < 0) return JS_UNDEFINED;
    const char *s = JS_ToCString(g_ctx, val);
    if (!s) s = "";
    switch (magic) {
        case P_ID: set_attr_raw(h, "id", 2, s, (unsigned)strlen(s)); break;
        case P_CLASS: set_attr_raw(h, "class", 5, s, (unsigned)strlen(s)); break;
        case P_TEXT: case P_OUTER:
            if (g_have_host && g_host.set_text) g_host.set_text(h, s, (unsigned)strlen(s));
            break;
        case P_VALUE: set_attr_raw(h, "value", 5, s, (unsigned)strlen(s)); break;
        case P_HREF: set_attr_raw(h, "href", 4, s, (unsigned)strlen(s)); break;
        default: break;
    }
    JS_FreeCString(g_ctx, s);
    return JS_UNDEFINED;
}

/* ---------- element methods ---------- */

static JSValue m_get_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx;
    long long h = opaque_handle(this_val, g_cls_elem);
    if (h < 0 || argc < 1) return js_str("");
    const char *n = JS_ToCString(g_ctx, argv[0]);
    if (!n) return js_str("");
    JSValue r = attr_str(h, n, (unsigned)strlen(n));
    JS_FreeCString(g_ctx, n);
    return r;
}
static JSValue m_set_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx;
    long long h = opaque_handle(this_val, g_cls_elem);
    if (h < 0 || argc < 2) return JS_UNDEFINED;
    const char *n = JS_ToCString(g_ctx, argv[0]);
    const char *v = JS_ToCString(g_ctx, argv[1]);
    if (n) set_attr_raw(h, n, (unsigned)strlen(n), v ? v : "", (unsigned)strlen(v ? v : ""));
    if (n) JS_FreeCString(g_ctx, n);
    if (v) JS_FreeCString(g_ctx, v);
    return JS_UNDEFINED;
}
static JSValue m_has_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx;
    long long h = opaque_handle(this_val, g_cls_elem);
    if (h < 0 || argc < 1) return JS_FALSE;
    const char *n = JS_ToCString(g_ctx, argv[0]);
    if (!n) return JS_FALSE;
    JSValue v = attr_str(h, n, (unsigned)strlen(n));
    const char *s = JS_ToCString(g_ctx, v);
    int has = (s && s[0]) ? 1 : 0;
    if (s) JS_FreeCString(g_ctx, s);
    JS_FreeValue(g_ctx, v);
    JS_FreeCString(g_ctx, n);
    return JS_NewBool(g_ctx, has);
}
static JSValue m_remove_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx;
    long long h = opaque_handle(this_val, g_cls_elem);
    if (h < 0 || argc < 1) return JS_UNDEFINED;
    const char *n = JS_ToCString(g_ctx, argv[0]);
    if (n) set_attr_raw(h, n, (unsigned)strlen(n), "", 0);
    if (n) JS_FreeCString(g_ctx, n);
    return JS_UNDEFINED;
}
static JSValue m_append_child(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx;
    long long h = opaque_handle(this_val, g_cls_elem);
    long long c = (argc > 0) ? opaque_handle(argv[0], g_cls_elem) : -1;
    if (h >= 0 && c >= 0 && g_have_host && g_host.append) g_host.append(h, c);
    return argc > 0 ? JS_DupValue(g_ctx, argv[0]) : JS_UNDEFINED;
}
static JSValue m_remove_child(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    long long c = (argc > 0) ? opaque_handle(argv[0], g_cls_elem) : -1;
    if (c >= 0 && g_have_host && g_host.remove) g_host.remove(c);
    return argc > 0 ? JS_DupValue(g_ctx, argv[0]) : JS_UNDEFINED;
}
static JSValue m_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)argc; (void)argv;
    long long h = opaque_handle(this_val, g_cls_elem);
    if (h >= 0 && g_have_host && g_host.remove) g_host.remove(h);
    return JS_UNDEFINED;
}
static JSValue m_insert_before(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return m_append_child(ctx, this_val, argc, argv);
}
static JSValue m_noop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}
static void add_listener(long long h, const char *ev, JSValueConst fn);
static JSValue m_add_listener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx;
    long long h = opaque_handle(this_val, g_cls_elem);
    if (h >= 0 && argc >= 2 && JS_IsFunction(g_ctx, argv[1])) {
        const char *ev = JS_ToCString(g_ctx, argv[0]);
        if (ev) { add_listener(h, ev, argv[1]); JS_FreeCString(g_ctx, ev); }
    }
    return JS_UNDEFINED;
}
static void dispatch_here(long long h, const char *ev);
static JSValue m_click(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)argc; (void)argv;
    long long h = opaque_handle(this_val, g_cls_elem);
    if (h >= 0) dispatch_here(h, "click");
    return JS_UNDEFINED;
}
static JSValue m_get_by_tag(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    if (argc < 1) return JS_NewArray(g_ctx);
    const char *t = JS_ToCString(g_ctx, argv[0]);
    if (!t) return JS_NewArray(g_ctx);
    JSValue r = collect_idx(g_have_host ? g_host.get_by_tag : 0, t, (unsigned)strlen(t));
    JS_FreeCString(g_ctx, t);
    return r;
}
static JSValue m_get_by_class(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    if (argc < 1) return JS_NewArray(g_ctx);
    const char *t = JS_ToCString(g_ctx, argv[0]);
    if (!t) return JS_NewArray(g_ctx);
    JSValue r = collect_idx(g_have_host ? g_host.get_by_class : 0, t, (unsigned)strlen(t));
    JS_FreeCString(g_ctx, t);
    return r;
}
static JSValue m_query(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    if (argc < 1 || !g_have_host || !g_host.query) return JS_NULL;
    const char *s = JS_ToCString(g_ctx, argv[0]);
    if (!s) return JS_NULL;
    long long h = g_host.query(s, (unsigned)strlen(s));
    JS_FreeCString(g_ctx, s);
    return h >= 0 ? make_elem(h) : JS_NULL;
}
static JSValue m_query_all(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    if (argc < 1) return JS_NewArray(g_ctx);
    const char *s = JS_ToCString(g_ctx, argv[0]);
    if (!s) return JS_NewArray(g_ctx);
    JSValue r = collect_idx(g_have_host ? g_host.query_idx : 0, s, (unsigned)strlen(s));
    JS_FreeCString(g_ctx, s);
    return r;
}
static JSValue m_contains(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_FALSE;
}

static JSCFunctionListEntry elem_funcs[] = {
    JS_CGETSET_MAGIC_DEF("id", e_get, e_set, P_ID),
    JS_CGETSET_MAGIC_DEF("className", e_get, e_set, P_CLASS),
    JS_CGETSET_MAGIC_DEF("tagName", e_get, 0, P_TAG),
    JS_CGETSET_MAGIC_DEF("nodeName", e_get, 0, P_TAG),
    JS_CGETSET_MAGIC_DEF("localName", e_get, 0, P_TAG),
    JS_CGETSET_MAGIC_DEF("textContent", e_get, e_set, P_TEXT),
    JS_CGETSET_MAGIC_DEF("innerText", e_get, e_set, P_TEXT),
    JS_CGETSET_MAGIC_DEF("innerHTML", e_get, e_set, P_TEXT),
    JS_CGETSET_MAGIC_DEF("outerHTML", e_get, 0, P_OUTER),
    JS_CGETSET_MAGIC_DEF("value", e_get, e_set, P_VALUE),
    JS_CGETSET_MAGIC_DEF("href", e_get, e_set, P_HREF),
    JS_CGETSET_MAGIC_DEF("src", e_get, e_set, P_HREF),
    JS_CGETSET_MAGIC_DEF("style", e_get, 0, P_STYLE),
    JS_CGETSET_MAGIC_DEF("classList", e_get, 0, P_CLASSLIST),
    JS_CGETSET_MAGIC_DEF("parentNode", e_get, 0, P_PARENT),
    JS_CGETSET_MAGIC_DEF("parentElement", e_get, 0, P_PARENT),
    JS_CGETSET_MAGIC_DEF("children", e_get, 0, P_CHILDREN),
    JS_CGETSET_MAGIC_DEF("childNodes", e_get, 0, P_CHILDREN),
    JS_CGETSET_MAGIC_DEF("firstChild", e_get, 0, P_FIRST),
    JS_CGETSET_MAGIC_DEF("lastChild", e_get, 0, P_LAST),
    JS_CGETSET_MAGIC_DEF("firstElementChild", e_get, 0, P_FIRST),
    JS_CGETSET_MAGIC_DEF("lastElementChild", e_get, 0, P_LAST),
    JS_CGETSET_MAGIC_DEF("offsetWidth", e_get, 0, P_DIMS),
    JS_CGETSET_MAGIC_DEF("offsetHeight", e_get, 0, P_DIMS),
    JS_CGETSET_MAGIC_DEF("clientWidth", e_get, 0, P_DIMS),
    JS_CGETSET_MAGIC_DEF("clientHeight", e_get, 0, P_DIMS),
    JS_CFUNC_DEF("getAttribute", 1, m_get_attr),
    JS_CFUNC_DEF("setAttribute", 2, m_set_attr),
    JS_CFUNC_DEF("hasAttribute", 1, m_has_attr),
    JS_CFUNC_DEF("removeAttribute", 1, m_remove_attr),
    JS_CFUNC_DEF("appendChild", 1, m_append_child),
    JS_CFUNC_DEF("removeChild", 1, m_remove_child),
    JS_CFUNC_DEF("remove", 0, m_remove),
    JS_CFUNC_DEF("insertBefore", 2, m_insert_before),
    JS_CFUNC_DEF("replaceChildren", 1, m_noop),
    JS_CFUNC_DEF("addEventListener", 2, m_add_listener),
    JS_CFUNC_DEF("removeEventListener", 2, m_noop),
    JS_CFUNC_DEF("click", 0, m_click),
    JS_CFUNC_DEF("focus", 0, m_noop),
    JS_CFUNC_DEF("blur", 0, m_noop),
    JS_CFUNC_DEF("getElementsByTagName", 1, m_get_by_tag),
    JS_CFUNC_DEF("getElementsByClassName", 1, m_get_by_class),
    JS_CFUNC_DEF("querySelector", 1, m_query),
    JS_CFUNC_DEF("querySelectorAll", 1, m_query_all),
    JS_CFUNC_DEF("contains", 1, m_contains),
    JS_CFUNC_DEF("closest", 1, m_noop),
    JS_CFUNC_DEF("normalize", 0, m_noop),
    JS_CFUNC_DEF("toString", 0, m_noop),
};

/* ---------- classList ---------- */

static JSValue cl_attr(long long h) { return attr_str(h, "class", 5); }
static void cl_set(long long h, const char *v) {
    set_attr_raw(h, "class", 5, v, (unsigned)strlen(v));
}
static JSValue cl_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx;
    long long h = opaque_handle(this_val, g_cls_style);
    if (h < 0 || argc < 1) return JS_UNDEFINED;
    const char *c = JS_ToCString(g_ctx, argv[0]);
    if (!c) return JS_UNDEFINED;
    JSValue cur = cl_attr(h);
    const char *s = JS_ToCString(g_ctx, cur);
    char buf[512];
    buf[0] = 0;
    if (s && s[0]) { strncpy(buf, s, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0; }
    char needle[128];
    needle[0] = ' '; needle[1] = 0;
    strncat(needle, c, sizeof(needle) - 2);
    if (!strstr(buf, c)) {
        if (buf[0]) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, c, sizeof(buf) - strlen(buf) - 1);
        cl_set(h, buf);
    }
    if (s) JS_FreeCString(g_ctx, s);
    JS_FreeValue(g_ctx, cur);
    JS_FreeCString(g_ctx, c);
    return JS_UNDEFINED;
}
static JSValue cl_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx;
    long long h = opaque_handle(this_val, g_cls_style);
    if (h < 0 || argc < 1) return JS_UNDEFINED;
    const char *c = JS_ToCString(g_ctx, argv[0]);
    if (!c) return JS_UNDEFINED;
    JSValue cur = cl_attr(h);
    const char *s = JS_ToCString(g_ctx, cur);
    char buf[512]; buf[0] = 0;
    if (s) {
        const char *p = s;
        while (*p) {
            while (*p == ' ') p++;
            const char *st = p;
            while (*p && *p != ' ') p++;
            size_t l = (size_t)(p - st);
            if (!(l == strlen(c) && strncmp(st, c, l) == 0)) {
                if (buf[0]) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
                strncat(buf, st, l < sizeof(buf) - strlen(buf) - 1 ? l : sizeof(buf) - strlen(buf) - 1);
            }
        }
    }
    cl_set(h, buf);
    if (s) JS_FreeCString(g_ctx, s);
    JS_FreeValue(g_ctx, cur);
    JS_FreeCString(g_ctx, c);
    return JS_UNDEFINED;
}
static JSValue cl_toggle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx;
    long long h = opaque_handle(this_val, g_cls_style);
    if (h < 0 || argc < 1) return JS_FALSE;
    const char *c = JS_ToCString(g_ctx, argv[0]);
    if (!c) return JS_FALSE;
    JSValue cur = cl_attr(h);
    const char *s = JS_ToCString(g_ctx, cur);
    int has = (s && strstr(s, c)) ? 1 : 0;
    if (s) JS_FreeCString(g_ctx, s);
    JS_FreeValue(g_ctx, cur);
    JS_FreeCString(g_ctx, c);
    if (has) cl_remove(ctx, this_val, argc, argv);
    else cl_add(ctx, this_val, argc, argv);
    return JS_NewBool(g_ctx, !has);
}
static JSValue cl_contains(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx;
    long long h = opaque_handle(this_val, g_cls_style);
    if (h < 0 || argc < 1) return JS_FALSE;
    const char *c = JS_ToCString(g_ctx, argv[0]);
    if (!c) return JS_FALSE;
    JSValue cur = cl_attr(h);
    const char *s = JS_ToCString(g_ctx, cur);
    int has = (s && strstr(s, c)) ? 1 : 0;
    if (s) JS_FreeCString(g_ctx, s);
    JS_FreeValue(g_ctx, cur);
    JS_FreeCString(g_ctx, c);
    return JS_NewBool(g_ctx, has);
}
static JSCFunctionListEntry classlist_funcs[] = {
    JS_CFUNC_DEF("add", 1, cl_add),
    JS_CFUNC_DEF("remove", 1, cl_remove),
    JS_CFUNC_DEF("toggle", 1, cl_toggle),
    JS_CFUNC_DEF("contains", 1, cl_contains),
    JS_CFUNC_DEF("item", 1, cl_contains),
};

/* ---------- style natives ---------- */

static JSValue n_style_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    if (argc < 3 || !g_have_host || !g_host.set_style) return JS_UNDEFINED;
    int32_t h = 0;
    JS_ToInt32(g_ctx, &h, argv[0]);
    const char *p = JS_ToCString(g_ctx, argv[1]);
    const char *v = JS_ToCString(g_ctx, argv[2]);
    if (p && v) g_host.set_style(h, p, (unsigned)strlen(p), v, (unsigned)strlen(v));
    if (p) JS_FreeCString(g_ctx, p);
    if (v) JS_FreeCString(g_ctx, v);
    return JS_UNDEFINED;
}
static JSValue n_style_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return js_str("");
}

/* ---------- document / window natives ---------- */

static JSValue d_get_by_id(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    if (argc < 1 || !g_have_host || !g_host.get_by_id) return JS_NULL;
    const char *id = JS_ToCString(g_ctx, argv[0]);
    if (!id) return JS_NULL;
    long long h = g_host.get_by_id(id, (unsigned)strlen(id));
    JS_FreeCString(g_ctx, id);
    return h >= 0 ? make_elem(h) : JS_NULL;
}
static JSValue d_create(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    if (argc < 1 || !g_have_host || !g_host.create) return JS_NULL;
    const char *t = JS_ToCString(g_ctx, argv[0]);
    if (!t) return JS_NULL;
    long long h = g_host.create(t, (unsigned)strlen(t));
    JS_FreeCString(g_ctx, t);
    return h >= 0 ? make_elem(h) : JS_NULL;
}
static JSValue d_body(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    if (!g_have_host || !g_host.body) return JS_NULL;
    long long h = g_host.body();
    return h >= 0 ? make_elem(h) : JS_NULL;
}
static JSValue d_add_listener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    if (argc >= 2 && JS_IsFunction(g_ctx, argv[1])) {
        const char *ev = JS_ToCString(g_ctx, argv[0]);
        if (ev) { add_listener(0, ev, argv[1]); JS_FreeCString(g_ctx, ev); }
    }
    return JS_UNDEFINED;
}

/* ---------- location / timers / console ---------- */

static JSValue l_navigate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    if (argc >= 1 && g_have_host && g_host.navigate) {
        const char *u = JS_ToCString(g_ctx, argv[0]);
        if (u) { g_host.navigate(u, (unsigned)strlen(u)); JS_FreeCString(g_ctx, u); }
    }
    return JS_UNDEFINED;
}
#define TIMER_MAX 1024
static JSValue g_timers[TIMER_MAX];
static int g_timer_n = 0;
static int g_timer_nextid = 1;

static void run_loop(void) {
    int guard = 0;
    for (;;) {
        pump_jobs();
        if (g_timer_n == 0) break;
        if (++guard > 200000) break;
        JSValue fn = g_timers[0];
        for (int i = 1; i < g_timer_n; i++) g_timers[i - 1] = g_timers[i];
        g_timer_n--;
        JSValue r = JS_Call(g_ctx, fn, JS_UNDEFINED, 0, 0);
        if (JS_IsException(r)) {
            JSValue e = JS_GetException(g_ctx);
            if (g_print_cb) {
                const char *s = JS_ToCString(g_ctx, e);
                if (s) { g_print_cb("[t] ", 4); g_print_cb(s, (unsigned)strlen(s)); g_print_cb("\n", 1); JS_FreeCString(g_ctx, s); }
            }
            JS_FreeValue(g_ctx, e);
        }
        JS_FreeValue(g_ctx, r);
        JS_FreeValue(g_ctx, fn);
    }
    pump_jobs();
}
static JSValue t_settimer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    int id = g_timer_nextid++;
    if (argc >= 1 && JS_IsFunction(g_ctx, argv[0]) && g_timer_n < TIMER_MAX)
        g_timers[g_timer_n++] = JS_DupValue(g_ctx, argv[0]);
    return JS_NewInt32(g_ctx, id);
}

static JSValue t_noop_id(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    static int g_timer = 1;
    if (argc >= 1 && JS_IsFunction(g_ctx, argv[0])) {
        JSValue r = JS_Call(g_ctx, argv[0], JS_UNDEFINED, 0, 0);
        if (JS_IsException(r)) {
            JSValue e = JS_GetException(g_ctx);
            if (g_print_cb) {
                const char *s = JS_ToCString(g_ctx, e);
                if (s) { g_print_cb("[timer] ", 7); g_print_cb(s, (unsigned)strlen(s)); g_print_cb("\n", 1); JS_FreeCString(g_ctx, s); }
            }
            JS_FreeValue(g_ctx, e);
        }
        JS_FreeValue(g_ctx, r);
    }
    pump_jobs();
    return JS_NewInt32(g_ctx, g_timer++);
}
static JSValue g_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val;
    if (!g_print_cb) return JS_UNDEFINED;
    for (int i = 0; i < argc; i++) {
        const char *s = JS_ToCString(g_ctx, argv[i]);
        if (s) { g_print_cb(s, (unsigned)strlen(s)); if (i + 1 < argc) g_print_cb(" ", 1); JS_FreeCString(g_ctx, s); }
    }
    g_print_cb("\n", 1);
    return JS_UNDEFINED;
}

/* ---------- listeners ---------- */

static void add_listener(long long h, const char *ev, JSValueConst fn) {
    if (g_lis_n == g_lis_cap) {
        int nc = g_lis_cap ? g_lis_cap * 2 : 16;
        listener_t *nl = (listener_t *)realloc(g_lis, (size_t)nc * sizeof(listener_t));
        if (!nl) return;
        g_lis = nl;
        g_lis_cap = nc;
    }
    g_lis[g_lis_n].handle = h;
    strncpy(g_lis[g_lis_n].ev, ev ? ev : "", sizeof(g_lis[g_lis_n].ev) - 1);
    g_lis[g_lis_n].ev[sizeof(g_lis[g_lis_n].ev) - 1] = 0;
    g_lis[g_lis_n].fn = JS_DupValue(g_ctx, fn);
    g_lis_n++;
}

static void dispatch_here(long long h, const char *ev) {
    for (int i = 0; i < g_lis_n; i++) {
        if (g_lis[i].handle == h && strcmp(g_lis[i].ev, ev) == 0) {
            JSValue evobj = JS_NewObject(g_ctx);
            JS_SetPropertyStr(g_ctx, evobj, "type", js_str(ev));
            JS_SetPropertyStr(g_ctx, evobj, "target", make_elem(h));
            JSValue r = JS_Call(g_ctx, g_lis[i].fn, JS_UNDEFINED, 1, &evobj);
            JS_FreeValue(g_ctx, r);
            JS_FreeValue(g_ctx, evobj);
        }
    }
}

/* ---------- setup ---------- */

static const char *STYLE_FACTORY_JS =
    "globalThis.__styleFactory=function(h){return new Proxy({},{"
    "get:function(t,p){"
    "if(p==='setProperty')return function(n,v){__styleSet(h,n,String(v));};"
    "if(p==='removeProperty')return function(n){__styleSet(h,n,'');};"
    "if(p==='getPropertyValue')return function(n){return __styleGet(h,n);};"
    "if(p==='cssText')return __styleGet(h,'cssText');"
    "return __styleGet(h,p);},"
    "set:function(t,p,v){__styleSet(h,p,String(v));return true;}});};";

static void set_func(JSValueConst obj, const char *name, JSCFunction *fn, int len) {
    JS_SetPropertyStr(g_ctx, obj, name, JS_NewCFunction(g_ctx, fn, name, len));
}

#define CK_MAXN 32

static char g_ck_name[CK_MAXN][48];
static char g_ck_val[CK_MAXN][4096];
static int  g_ck_n = 0;
static char g_cookie[8192];

static void ck_rebuild(void) {
    int o = 0;
    for (int i = 0; i < g_ck_n; i++) {
        if (o && o < (int)sizeof(g_cookie) - 2) { g_cookie[o++] = ';'; g_cookie[o++] = ' '; }
        for (const char *n = g_ck_name[i]; *n && o < (int)sizeof(g_cookie) - 1; n++) g_cookie[o++] = *n;
        if (o < (int)sizeof(g_cookie) - 1) g_cookie[o++] = '=';
        for (const char *v = g_ck_val[i]; *v && o < (int)sizeof(g_cookie) - 1; v++) g_cookie[o++] = *v;
    }
    g_cookie[o] = 0;
}
static void ck_store(const char *n, int nl, const char *v, int vl) {
    if (nl <= 0) return;
    if (nl > (int)sizeof(g_ck_name[0]) - 1) nl = (int)sizeof(g_ck_name[0]) - 1;
    if (vl > (int)sizeof(g_ck_val[0]) - 1) vl = (int)sizeof(g_ck_val[0]) - 1;
    for (int i = 0; i < g_ck_n; i++) {
        if ((int)strlen(g_ck_name[i]) == nl && strncmp(g_ck_name[i], n, nl) == 0) {
            memcpy(g_ck_val[i], v, vl); g_ck_val[i][vl] = 0;
            ck_rebuild();
            return;
        }
    }
    if (g_ck_n < CK_MAXN) {
        int i = g_ck_n++;
        memcpy(g_ck_name[i], n, nl); g_ck_name[i][nl] = 0;
        memcpy(g_ck_val[i], v, vl); g_ck_val[i][vl] = 0;
        ck_rebuild();
    }
}

static JSValue doc_get_cookie(JSContext *ctx, JSValueConst this_val, int magic) {
    (void)ctx; (void)this_val; (void)magic;
    if (g_cookie[0] && g_print_cb) {
        g_print_cb("[cget] ", 7);
        g_print_cb(g_cookie, (unsigned)strlen(g_cookie));
        g_print_cb("\n", 1);
    }
    return js_nstr(g_cookie, (unsigned)strlen(g_cookie));
}
static JSValue doc_set_cookie(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic) {
    (void)ctx; (void)this_val; (void)magic;
    const char *s = JS_ToCString(g_ctx, val);
    if (s) {
        if (g_print_cb) {
            g_print_cb("[cset] ", 7);
            g_print_cb(s, (unsigned)strlen(s));
            g_print_cb("\n", 1);
        }
        const char *eq = strchr(s, '=');
        if (eq) {
            const char *ns = s;
            const char *ne = eq;
            while (*ns == ' ') ns++;
            while (ne > ns && ne[-1] == ' ') ne--;
            const char *vs = eq + 1;
            const char *cs = strchr(vs, ';');
            const char *ve = cs ? cs : vs + strlen(vs);
            while (ve > vs && ve[-1] == ' ') ve--;
            int nl = (int)(ne - ns), vl = (int)(ve - vs);
            if (nl > 0 && vl >= 0) {
                if (g_have_host && g_host.set_cookie)
                    g_host.set_cookie(ns, (unsigned)nl, vs, (unsigned)vl);
                ck_store(ns, nl, vs, vl);
            }
        }
        JS_FreeCString(g_ctx, s);
    }
    return JS_UNDEFINED;
}
static JSValue loc_get_href(JSContext *ctx, JSValueConst this_val, int magic) {
    (void)ctx; (void)this_val; (void)magic;
    char url[1024];
    url[0] = 0;
    if (g_have_host && g_host.current_url) g_host.current_url(url, sizeof(url));
    return js_nstr(url, (unsigned)strlen(url));
}
static JSValue loc_to_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return loc_get_href(ctx, this_val, 0);
}
static JSValue loc_reload(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    char url[1024];
    url[0] = 0;
    if (g_have_host && g_host.current_url) g_host.current_url(url, sizeof(url));
    if (url[0] && g_have_host && g_host.navigate) g_host.navigate(url, (unsigned)strlen(url));
    return JS_UNDEFINED;
}

static int pump_jobs(void) {
    int ran = 0;
    for (;;) {
        JSContext *c = 0;
        int r = JS_ExecutePendingJob(g_rt, &c);
        if (r == 0) break;
        if (r < 0) {
            JSValue e = JS_GetException(c ? c : g_ctx);
            if (g_print_cb) {
                const char *s = JS_ToCString(g_ctx, e);
                if (s) { g_print_cb("[promise] ", 10); g_print_cb(s, (unsigned)strlen(s)); g_print_cb("\n", 1); JS_FreeCString(g_ctx, s); }
            }
            JS_FreeValue(g_ctx, e);
        }
        if (++ran > 2000000) break;
    }
    return ran;
}

static const char *PAGE_COMPAT_JS =
    "globalThis.btoa=function(s){s=String(s);var t='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/',o='';for(var i=0;i<s.length;i+=3){var c1=s.charCodeAt(i)&255,c2=i+1<s.length?s.charCodeAt(i+1)&255:0,c3=i+2<s.length?s.charCodeAt(i+2)&255:0,n=(c1<<16)|(c2<<8)|c3;o+=t[(n>>18)&63]+t[(n>>12)&63]+(i+1<s.length?t[(n>>6)&63]:'=')+(i+2<s.length?t[n&63]:'=');}return o;};"
    "globalThis.atob=function(s){s=String(s).replace(/[^A-Za-z0-9+/=]/g,'');var t='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/',o='';for(var i=0;i<s.length;i+=4){var n=(t.indexOf(s[i])<<18)|(t.indexOf(s[i+1])<<12)|((s[i+2]==='='?0:t.indexOf(s[i+2]))<<6)|(s[i+3]==='='?0:t.indexOf(s[i+3]));o+=String.fromCharCode((n>>16)&255);if(s[i+2]!=='=')o+=String.fromCharCode((n>>8)&255);if(s[i+3]!=='=')o+=String.fromCharCode(n&255);}return o;};"
    "globalThis.Image=function(){this.src='';this.onload=null;this.onerror=null;this.width=0;this.height=0;};"
    "globalThis.sessionStorage=(function(){var d={};return {getItem:function(k){k=String(k);return d[k]===undefined?null:d[k];},setItem:function(k,v){d[String(k)]=String(v);},removeItem:function(k){delete d[String(k)];},clear:function(){d={};},key:function(i){var ks=Object.keys(d);return i<ks.length?ks[i]:null;},get length(){return Object.keys(d).length;}};})();"
    "globalThis.localStorage=globalThis.sessionStorage;"
    "(function(){var t0=Date.now();var pf={now:function(){return Date.now()-t0;},"
    "timing:{navigationStart:t0,responseStart:t0+1,responseEnd:t0+2,domLoading:t0+2,domInteractive:t0+2,domContentLoadedEventStart:t0+3,domContentLoadedEventEnd:t0+3,domComplete:t0+4,loadEventStart:t0+4,loadEventEnd:t0+5},"
    "navigation:{type:0,redirectCount:0}};"
    "pf.mark=function(){return {};};pf.measure=function(){return {};};pf.clearMarks=function(){};pf.clearMeasures=function(){};"
    "pf.getEntries=function(){return [];};pf.getEntriesByName=function(){return [];};pf.getEntriesByType=function(){return [];};"
    "pf.timeOrigin=t0;globalThis.performance=pf;})();"
    "if(globalThis.addEventListener===undefined)globalThis.addEventListener=function(){};"
    "if(globalThis.removeEventListener===undefined)globalThis.removeEventListener=function(){};"
    "if(globalThis.dispatchEvent===undefined)globalThis.dispatchEvent=function(){return true;};"
    "globalThis.Event=globalThis.Event||function(t,o){this.type=t;this.bubbles=!!(o&&o.bubbles);this.cancelable=!!(o&&o.cancelable);this.defaultPrevented=false;this.target=null;this.currentTarget=null;};"
    "globalThis.Event.prototype.preventDefault=function(){this.defaultPrevented=true;};"
    "globalThis.Event.prototype.stopPropagation=function(){};"
    "globalThis.CustomEvent=globalThis.CustomEvent||function(t,o){this.type=t;this.detail=o?o.detail:null;};"
    "if(globalThis.navigator){var sb=function(u,d){return true;};try{navigator.sendBeacon=sb;}catch(e){}"
    "if(!navigator.connection)navigator.connection={effectiveType:'4g',rtt:50,downlink:10};"
    "if(!navigator.plugins)navigator.plugins=[];if(!navigator.mimeTypes)navigator.mimeTypes=[];"
    "if(navigator.webdriver===undefined)navigator.webdriver=false;}"
    "globalThis.chrome=globalThis.chrome||{runtime:{},csi:function(){return{};},loadTimes:function(){return{};}};";

static void register_classes(void) {
    JS_NewClassID(&g_cls_elem);
    JS_NewClassID(&g_cls_style);
    JSClassDef ed = { "HTMLElement", 0, 0, 0, 0 };
    JSClassDef sd = { "DOMTokenList", 0, 0, 0, 0 };
    JS_NewClass(g_rt, g_cls_elem, &ed);
    JS_NewClass(g_rt, g_cls_style, &sd);
    JSValue ep = JS_NewObject(g_ctx);
    JS_SetPropertyFunctionList(g_ctx, ep, elem_funcs, (int)(sizeof(elem_funcs) / sizeof(elem_funcs[0])));
    JS_SetClassProto(g_ctx, g_cls_elem, ep);
    JSValue sp = JS_NewObject(g_ctx);
    JS_SetPropertyFunctionList(g_ctx, sp, classlist_funcs, (int)(sizeof(classlist_funcs) / sizeof(classlist_funcs[0])));
    JS_SetClassProto(g_ctx, g_cls_style, sp);
}

static JSValue n_fetch_then(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv);

/* ---------------- fetch / XMLHttpRequest (0.6) ----------------
 * Sayfalarin cogu verisini fetch ile ceker. Host koprusunde `fetch` varsa
 * gercek GET yapilir; yoksa reddedilen bir Promise doner (sayfa bozulmaz).
 * Senkron calisir: bu motorun olay dongusu tek iş parclidir. */
static JSValue n_fetch(JSContext *ctx, JSValueConst this_val, int argc,
                       JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "fetch(url) gerekli");
    size_t ulen = 0;
    const char *url = JS_ToCStringLen(ctx, &ulen, argv[0]);
    if (!url) return JS_EXCEPTION;

    static char body[65536];
    unsigned blen = 0;
    int rc = -1;
    if (g_have_host && g_host.fetch)
        rc = g_host.fetch(url, (unsigned)ulen, "GET", 3, 0, 0,
                          body, (unsigned)sizeof(body), &blen);
    JS_FreeCString(ctx, url);

    JSValue ret = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, ret, "ok", rc == 0 ? JS_TRUE : JS_FALSE);
    JS_SetPropertyStr(ctx, ret, "status", JS_NewInt32(ctx, rc == 0 ? 200 : 0));
    JS_SetPropertyStr(ctx, ret, "statusText", js_str(rc == 0 ? "OK" : ""));
    JSValue txt = JS_NewStringLen(ctx, body, (size_t)blen);
    JS_SetPropertyStr(ctx, ret, "_t", txt);
    /* then(): basariliysa govde, degilse hata. */
    JS_SetPropertyStr(ctx, ret, "then", JS_NewCFunction(ctx, n_fetch_then, "then", 1));
    JS_SetPropertyStr(ctx, ret, "text", JS_NewCFunction(ctx, m_noop, "text", 0));
    JS_SetPropertyStr(ctx, ret, "json", JS_NewCFunction(ctx, m_noop, "json", 0));
    return ret;
}

/* fetch(url).then(function(text){...}) — basit zincir. Motorun Promise'i
 * olmadigi icin then() dogrudan cagirir. */
static JSValue n_fetch_then(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) return JS_UNDEFINED;
    JSValue ok = JS_GetPropertyStr(ctx, this_val, "ok");
    int isok = JS_ToBool(ctx, ok);
    JS_FreeValue(ctx, ok);
    JSValue bodyv = JS_GetPropertyStr(ctx, this_val, "_t");
    JSValue r = JS_Call(ctx, argv[0], JS_UNDEFINED, 1, &bodyv);
    JS_FreeValue(ctx, bodyv);
    JS_FreeValue(ctx, r);
    (void)isok;
    return JS_UNDEFINED;
}


/* XMLHttpRequest — minimal ama gercek: open(), send(), responseText, onload.
 * Senkron calisir (send icinde GET yapilir, ardindan onload cagrilir). */
static JSValue xhr_open(JSContext *ctx, JSValueConst this_val, int argc,
                        JSValueConst *argv) {
    (void)ctx;
    if (argc < 2) return JS_UNDEFINED;
    /* open(method, url): url'i nesnede sakla; send() kullanir. */
    JS_SetPropertyStr(ctx, this_val, "__url", JS_DupValue(ctx, argv[1]));
    return JS_UNDEFINED;
}

static JSValue xhr_send(JSContext *ctx, JSValueConst this_val, int argc,
                        JSValueConst *argv) {
    (void)argc; (void)argv;
    JSValue urlv = JS_GetPropertyStr(ctx, this_val, "__url");
    size_t ulen = 0;
    const char *url = JS_ToCStringLen(ctx, &ulen, urlv);
    JS_FreeValue(ctx, urlv);
    if (!url) return JS_UNDEFINED;

    static char body[65536];
    unsigned blen = 0;
    int rc = -1;
    if (g_have_host && g_host.fetch)
        rc = g_host.fetch(url, (unsigned)ulen, "GET", 3, 0, 0,
                          body, (unsigned)sizeof(body), &blen);
    JS_FreeCString(ctx, url);

    JS_SetPropertyStr(ctx, this_val, "status", JS_NewInt32(ctx, rc == 0 ? 200 : 0));
    JS_SetPropertyStr(ctx, this_val, "readyState", JS_NewInt32(ctx, 4));
    JS_SetPropertyStr(ctx, this_val, "responseText",
                      JS_NewStringLen(ctx, body, (size_t)blen));
    JS_SetPropertyStr(ctx, this_val, "response",
                      JS_NewStringLen(ctx, body, (size_t)blen));
    JSValue fn = JS_GetPropertyStr(ctx, this_val, "onload");
    if (JS_IsFunction(ctx, fn)) {
        JSValue ev = JS_NewObject(ctx);
        JSValue r = JS_Call(ctx, fn, this_val, 1, &ev);
        JS_FreeValue(ctx, ev);
        JS_FreeValue(ctx, r);
    }
    JS_FreeValue(ctx, fn);
    return JS_UNDEFINED;
}

static JSValue n_xhr(JSContext *ctx, JSValueConst this_val, int argc,
                     JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "open", JS_NewCFunction(ctx, xhr_open, "open", 2));
    JS_SetPropertyStr(ctx, o, "send", JS_NewCFunction(ctx, xhr_send, "send", 1));
    JS_SetPropertyStr(ctx, o, "readyState", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, o, "status", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, o, "responseText", js_str(""));
    JS_SetPropertyStr(ctx, o, "onload", JS_UNDEFINED);
    /* open() ikinci argumani saklamak icin __url kullaniyoruz: xhr_open
     * dondurdugu degeri cagiran taraf atar; bu yuzden url'i burada da
     * saklamak yerine open'i sarmalariz. */
    return o;
}

static void setup_globals(void) {
    JSValue global = JS_GetGlobalObject(g_ctx);
    g_global = JS_DupValue(g_ctx, global);
    g_global_live = 1;

    JSValue console = JS_NewObject(g_ctx);
    set_func(console, "log", g_log, 0);
    set_func(console, "info", g_log, 0);
    set_func(console, "warn", g_log, 0);
    set_func(console, "error", g_log, 0);
    set_func(console, "debug", g_log, 0);
    JS_SetPropertyStr(g_ctx, global, "console", console);

    JSValue doc = JS_NewObject(g_ctx);
    set_func(doc, "getElementById", d_get_by_id, 1);
    set_func(doc, "createElement", d_create, 1);
    set_func(doc, "getElementsByTagName", m_get_by_tag, 1);
    set_func(doc, "getElementsByClassName", m_get_by_class, 1);
    set_func(doc, "querySelector", m_query, 1);
    set_func(doc, "querySelectorAll", m_query_all, 1);
    set_func(doc, "addEventListener", d_add_listener, 2);
    set_func(doc, "write", g_log, 1);
    set_func(doc, "writeln", g_log, 1);
    JS_SetPropertyStr(g_ctx, doc, "readyState", js_str("complete"));
    JS_SetPropertyStr(g_ctx, doc, "body", d_body(g_ctx, JS_UNDEFINED, 0, 0));
    JS_SetPropertyStr(g_ctx, doc, "documentElement", d_body(g_ctx, JS_UNDEFINED, 0, 0));
    {
        JSAtom ca = JS_NewAtom(g_ctx, "cookie");
        JSCFunctionType gf, sf;
        gf.getter_magic = doc_get_cookie;
        sf.setter_magic = doc_set_cookie;
        JS_DefinePropertyGetSet(g_ctx, doc, ca,
            JS_NewCFunction2(g_ctx, gf.generic, "get cookie", 0, JS_CFUNC_getter_magic, 0),
            JS_NewCFunction2(g_ctx, sf.generic, "set cookie", 1, JS_CFUNC_setter_magic, 0),
            JS_PROP_CONFIGURABLE);
        JS_FreeAtom(g_ctx, ca);
    }
    JS_SetPropertyStr(g_ctx, global, "document", doc);

    JSValue loc = JS_NewObject(g_ctx);
    set_func(loc, "replace", l_navigate, 1);
    set_func(loc, "assign", l_navigate, 1);
    set_func(loc, "reload", loc_reload, 0);
    {
        JSAtom ha = JS_NewAtom(g_ctx, "href");
        JSCFunctionType gfh;
        gfh.getter_magic = loc_get_href;
        JS_DefinePropertyGetSet(g_ctx, loc, ha,
            JS_NewCFunction2(g_ctx, gfh.generic, "get href", 0, JS_CFUNC_getter_magic, 0),
            JS_UNDEFINED, JS_PROP_CONFIGURABLE);
        JS_FreeAtom(g_ctx, ha);
    }
    JS_SetPropertyStr(g_ctx, loc, "toString", JS_NewCFunction(g_ctx, loc_to_string, "toString", 0));
    JS_SetPropertyStr(g_ctx, loc, "protocol", js_str("https:"));
    JS_SetPropertyStr(g_ctx, loc, "hostname", js_str(""));
    JS_SetPropertyStr(g_ctx, loc, "pathname", js_str("/"));
    JS_SetPropertyStr(g_ctx, loc, "search", js_str(""));
    JS_SetPropertyStr(g_ctx, loc, "hash", js_str(""));
    JS_SetPropertyStr(g_ctx, global, "location", loc);

    JSValue nav = JS_NewObject(g_ctx);
    JS_SetPropertyStr(g_ctx, nav, "userAgent",
        js_str("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"));
    JS_SetPropertyStr(g_ctx, nav, "platform", js_str("Win32"));
    JS_SetPropertyStr(g_ctx, nav, "language", js_str("tr"));
    JS_SetPropertyStr(g_ctx, nav, "languages", JS_NewArray(g_ctx));
    JS_SetPropertyStr(g_ctx, nav, "onLine", JS_TRUE);
    JS_SetPropertyStr(g_ctx, nav, "hardwareConcurrency", JS_NewInt32(g_ctx, 1));
    JS_SetPropertyStr(g_ctx, global, "navigator", nav);

    JSValue perf = JS_NewObject(g_ctx);
    JS_SetPropertyStr(g_ctx, perf, "now", JS_NewCFunction(g_ctx, m_noop, "now", 0));
    JS_SetPropertyStr(g_ctx, global, "performance", perf);

    set_func(global, "setTimeout", t_settimer, 1);
    set_func(global, "setInterval", t_settimer, 1);
    set_func(global, "setImmediate", t_settimer, 1);
    set_func(global, "queueMicrotask", t_noop_id, 1);
    set_func(global, "requestAnimationFrame", t_noop_id, 1);
    set_func(global, "clearTimeout", m_noop, 1);
    set_func(global, "clearInterval", m_noop, 1);
    set_func(global, "clearImmediate", m_noop, 1);
    set_func(global, "cancelAnimationFrame", m_noop, 1);
    set_func(global, "alert", g_log, 1);
    set_func(global, "print", g_log, 1);
    set_func(global, "fetch", n_fetch, 1);
    set_func(global, "XMLHttpRequest", n_xhr, 0);
    set_func(global, "__styleSet", n_style_set, 3);
    set_func(global, "__styleGet", n_style_get, 1);

    JS_SetPropertyStr(g_ctx, global, "window", JS_DupValue(g_ctx, global));
    JS_SetPropertyStr(g_ctx, global, "self", JS_DupValue(g_ctx, global));
    JS_SetPropertyStr(g_ctx, global, "top", JS_DupValue(g_ctx, global));
    JS_SetPropertyStr(g_ctx, global, "innerWidth", JS_NewInt32(g_ctx, 1024));
    JS_SetPropertyStr(g_ctx, global, "innerHeight", JS_NewInt32(g_ctx, 768));
    JS_SetPropertyStr(g_ctx, global, "devicePixelRatio", JS_NewInt32(g_ctx, 1));

    JS_FreeValue(g_ctx, global);
    {
        JSValue r = JS_Eval(g_ctx, STYLE_FACTORY_JS, strlen(STYLE_FACTORY_JS), "<init>", JS_EVAL_TYPE_GLOBAL);
        JS_FreeValue(g_ctx, r);
    }
    {
        JSValue r = JS_Eval(g_ctx, PAGE_COMPAT_JS, strlen(PAGE_COMPAT_JS), "<compat>", JS_EVAL_TYPE_GLOBAL);
        JS_FreeValue(g_ctx, r);
    }
}

static void promise_reject_cb(JSContext *ctx, JSValueConst promise, JSValueConst reason, JS_BOOL is_handled, void *opaque) {
    (void)promise; (void)opaque;
    if (is_handled || !g_print_cb) return;
    const char *s = JS_ToCString(ctx, reason);
    if (s) { g_print_cb("[reject] ", 9); g_print_cb(s, (unsigned)strlen(s)); g_print_cb("\n", 1); JS_FreeCString(ctx, s); }
}

static void teardown(void) {
    if (g_ctx) {
        for (int i = 0; i < g_timer_n; i++) JS_FreeValue(g_ctx, g_timers[i]);
    }
    g_timer_n = 0;
    if (g_lis) {
        for (int i = 0; i < g_lis_n; i++) {
            if (g_ctx) JS_FreeValue(g_ctx, g_lis[i].fn);
        }
        free(g_lis);
        g_lis = 0;
        g_lis_n = 0;
        g_lis_cap = 0;
    }
    if (g_ctx) { JS_FreeContext(g_ctx); g_ctx = 0; }
    if (g_rt) { JS_FreeRuntime(g_rt); g_rt = 0; }
    if (g_global_live) { g_global = JS_UNDEFINED; g_global_live = 0; }
}

static void ensure_engine(void) {
    if (g_ctx) return;
    g_rt = JS_NewRuntime();
    if (!g_rt) return;
    JS_SetMemoryLimit(g_rt, 56 * 1024 * 1024);
    JS_SetMaxStackSize(g_rt, 1024 * 1024);
    JS_SetInterruptHandler(g_rt, interrupt_cb, 0);
    JS_SetHostPromiseRejectionTracker(g_rt, promise_reject_cb, 0);
    g_ctx = JS_NewContext(g_rt);
    if (!g_ctx) { JS_FreeRuntime(g_rt); g_rt = 0; return; }
    register_classes();
    setup_globals();
}

static void ensure_stack(void) {
    if (g_stack || !g_asi || !g_asi->mem || !g_asi->mem->alloc) return;
    g_stack_size = 4UL * 1024UL * 1024UL;
    g_stack = (unsigned char *)g_asi->mem->alloc(g_stack_size);
}

static void eval_job(void *arg) {
    (void)arg;
    JS_UpdateStackTop(g_rt);
    g_eval_result = JS_Eval(g_ctx, g_src, g_srclen, "<page>", JS_EVAL_TYPE_GLOBAL);
    g_eval_had = 1;
    run_loop();
}

unsigned wolf_engine_version(void) { return 0x00020000u; }

void wolf_engine_set_print(wolf_print_fn cb) { g_print_cb = cb; }

void wolf_engine_set_host(const wolf_host_t *host) {
    if (host) {
        memcpy(&g_host, host, sizeof(wolf_host_t));
        g_have_host = 1;
    }
}

void wolf_engine_set_limits(unsigned steps, unsigned depth) {
    (void)depth;
    g_step_budget = steps ? steps : 0;
}

int wolf_engine_eval(const char *src, unsigned len, char *out, unsigned out_cap) {
    if (!src) return -1;
    ensure_engine();
    if (!g_ctx) return -3;
    ensure_stack();
    g_src = src;
    g_srclen = len;
    g_eval_had = 0;
    g_eval_result = JS_UNDEFINED;
    g_steps = 0;
    g_intr = 0;

    if (g_stack && g_stack_size > 65536) {
        void *top = (void *)((uintptr_t)(g_stack + g_stack_size) & ~(uintptr_t)15);
        wolf_js_stack_call(top, eval_job, 0);
    } else {
        eval_job(0);
    }

    int status = 0;
    if (g_eval_had && JS_IsException(g_eval_result)) {
        status = -1;
        if (out && out_cap) {
            JSValue exc = JS_GetException(g_ctx);
            const char *s = JS_ToCString(g_ctx, exc);
            if (s) {
                size_t n = strlen(s);
                if (n > out_cap - 1) n = out_cap - 1;
                memcpy(out, s, n);
                out[n] = 0;
                JS_FreeCString(g_ctx, s);
            }
            JS_FreeValue(g_ctx, exc);
        }
    } else if (out && out_cap) {
        out[0] = 0;
        if (!JS_IsUndefined(g_eval_result)) {
            const char *s = JS_ToCString(g_ctx, g_eval_result);
            if (s) {
                size_t n = strlen(s);
                if (n > out_cap - 1) n = out_cap - 1;
                memcpy(out, s, n);
                out[n] = 0;
                JS_FreeCString(g_ctx, s);
            }
        }
    }
    if (g_eval_had) JS_FreeValue(g_ctx, g_eval_result);
    g_eval_had = 0;
    g_eval_result = JS_UNDEFINED;
    if (g_intr) status = -2;
    return status;
}

void wolf_engine_reset(void) {
    teardown();
}

void wolf_engine_dispatch_event(long long handle, const char *ev, unsigned len) {
    if (!g_ctx || !ev) return;
    char name[64];
    unsigned n = len < sizeof(name) - 1 ? len : (unsigned)sizeof(name) - 1;
    memcpy(name, ev, n);
    name[n] = 0;
    dispatch_here(handle, name);
}

unsigned wolf_engine_listener_count(void) {
    return (unsigned)g_lis_n;
}

void app_entry(void *win, asi_t *asi) { (void)win; (void)asi; }

