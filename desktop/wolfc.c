/**
 * wolfc — OS icinde calisan kucuk C derleyicisi (0.7)
 *
 * Isletim sisteminde gcc yok; `wf build` kaynagi bu derleyiciyle Wolf
 * bytecode'a cevirir. Cikti `.wlf` paketine konur ve ayni libWolf API'siyle
 * calisir (wlf_vm.c).
 *
 * Desteklenen C alt kumesi (bilincli olarak kucuk):
 *   - int/char turleri; global ve yerel degiskenler (ilkleme dahil)
 *   - tamsayi ve metin literalleri (\n \t \r \\ \" kacislari)
 *   - + - * / %  == != < <= > >=  ! && ||  parantez, tekli -
 *   - if/else, while, for, break, return, blok, bos deyim
 *   - islev tanimi/cagrisi: `int f(int a, int b)`, en fazla 8 parametre
 *   - i++, i--, i += n, i -= n, i = expr
 *   - libWolf cagrilari (wolf_label, wolf_button, wolf_fs_read, ...)
 *   - wolf_on_click(w, islev) / wolf_on_change(w, islev) → islev adresi
 *
 * Desteklenmeyenler net hata verir: struct, pointer, dizi, float, #include.
 * Yerlesim: global 0..199, parametre 200..207, yerel 208..255.
 * Ozyineleme yok (slot uzayi tektir).
 */
#include "dsk.h"

#define WC_MAX_CODE 8192
#define WC_MAX_STR  8192
#define WC_MAX_SYM  96
#define WC_MAX_FUNC 24
#define WC_MAX_PARAM 8
#define WC_MAX_TOK  6000
#define WC_LOCAL_BASE 208

/* VM ile ayni opcode numaralari (wlf_vm.c) */
enum {
    OP_HALT = 0,
    OP_PUSHI, OP_PUSHS, OP_LOAD, OP_STORE,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_NOT, OP_NEG, OP_AND, OP_OR,
    OP_JMP, OP_JZ, OP_JNZ,
    OP_CALLB, OP_CALLU, OP_RET, OP_POP
};

#define MAX_BREAK 8

/* ---------------- cikti ---------------- */
typedef struct {
    uint32_t code[WC_MAX_CODE];
    int      n;
    char     strs[WC_MAX_STR];
    int      strs_n;
    int      err;
    char     msg[128];
} wc_t;

static wc_t O;

static void cerr(const char *m) {
    if (!O.err) { O.err = 1; bin_copy(O.msg, sizeof(O.msg), m); }
}
static void emit(uint32_t w) {
    if (O.n >= WC_MAX_CODE) { cerr("program cok buyuk"); return; }
    O.code[O.n++] = w;
}
static int here(void) { return O.n; }
static void patch_at(int at, uint32_t v) {
    if (at >= 0 && at < WC_MAX_CODE) O.code[at] = v;
}

static int str_add(const char *s) {
    int i = 0;
    while (i < O.strs_n) {
        int j = 0;
        while (s[j] && i + j < O.strs_n && O.strs[i + j] == s[j]) j++;
        if (!s[j] && i + j < O.strs_n && O.strs[i + j] == 0) return i;
        while (i < O.strs_n && O.strs[i]) i++;
        i++;
    }
    int len = slen2(s);
    if (O.strs_n + len + 1 > WC_MAX_STR) { cerr("metin havuzu dolu"); return 0; }
    int at = O.strs_n;
    for (int k = 0; k < len; k++) O.strs[O.strs_n++] = s[k];
    O.strs[O.strs_n++] = 0;
    return at;
}

/* ---------------- lexer ---------------- */
enum { T_EOF = 0, T_NUM, T_STR, T_ID, T_PUNCT, T_KW };

typedef struct {
    int  kind;
    int  num;
    char str[200];
    char txt[48];
} tok_t;

static tok_t T[WC_MAX_TOK];
static int   Tn, Ti;

static int is_alpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int is_digit(int c) { return c >= '0' && c <= '9'; }

static const char *const KW[] = {
    "int", "char", "if", "else", "while", "for", "break", "return", "void", 0
};
static int kw_id(const char *s) {
    for (int i = 0; KW[i]; i++) if (s_eq(s, KW[i])) return i;
    return -1;
}

static int lex(const char *src) {
    int i = 0;
    for (;;) {
        while (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n') i++;
        if (src[i] == '/' && src[i + 1] == '/') { while (src[i] && src[i] != '\n') i++; continue; }
        if (src[i] == '/' && src[i + 1] == '*') {
            i += 2;
            while (src[i] && !(src[i] == '*' && src[i + 1] == '/')) i++;
            if (src[i]) i += 2;
            continue;
        }
        if (src[i] == '#') { while (src[i] && src[i] != '\n') i++; continue; }
        if (Tn >= WC_MAX_TOK) { cerr("kaynak cok uzun"); return -1; }

        tok_t *t = &T[Tn];
        t->str[0] = 0; t->txt[0] = 0; t->num = 0;
        if (!src[i]) { t->kind = T_EOF; Tn++; return 0; }

        if (is_digit(src[i])) {
            long v = 0;
            while (is_digit(src[i])) { v = v * 10 + (src[i] - '0'); i++; }
            t->kind = T_NUM; t->num = (int)v; Tn++; continue;
        }
        if (src[i] == '"') {
            i++;
            int k = 0;
            while (src[i] && src[i] != '"') {
                char c = src[i++];
                if (c == '\\' && src[i]) {
                    char e = src[i++];
                    if (e == 'n') c = '\n';
                    else if (e == 't') c = '\t';
                    else if (e == 'r') c = '\r';
                    else c = e;
                }
                if (k < (int)sizeof(t->str) - 1) t->str[k++] = c;
            }
            if (src[i] == '"') i++;
            t->str[k] = 0;
            t->kind = T_STR; Tn++; continue;
        }
        if (is_alpha(src[i])) {
            int k = 0;
            while (is_alpha(src[i]) || is_digit(src[i])) {
                if (k < (int)sizeof(t->txt) - 1) t->txt[k++] = src[i];
                i++;
            }
            t->txt[k] = 0;
            t->kind = (kw_id(t->txt) >= 0) ? T_KW : T_ID;
            Tn++; continue;
        }
        {
            static const char *two[] = { "==", "!=", "<=", ">=", "&&", "||",
                                         "++", "--", "+=", "-=", 0 };
            int done = 0;
            for (int k = 0; two[k]; k++) {
                if (src[i] == two[k][0] && src[i + 1] == two[k][1]) {
                    bin_copy(t->txt, sizeof(t->txt), two[k]);
                    t->kind = T_PUNCT;
                    i += 2; Tn++; done = 1; break;
                }
            }
            if (done) continue;
        }
        t->txt[0] = src[i++];
        t->txt[1] = 0;
        t->kind = T_PUNCT;
        Tn++;
    }
}

static tok_t *cur(void) { return &T[Ti < Tn ? Ti : Tn - 1]; }
static tok_t *peek(int n) {
    int k = Ti + n;
    return &T[k < Tn ? k : Tn - 1];
}
static int punct_is(const char *p) { return cur()->kind == T_PUNCT && s_eq(cur()->txt, p); }
static int kw_is(const char *k) { return cur()->kind == T_KW && s_eq(cur()->txt, k); }
static int accept(const char *p) { if (punct_is(p)) { Ti++; return 1; } return 0; }
static void expect(const char *p) {
    if (accept(p)) return;
    char m[80];
    bin_copy(m, sizeof(m), "beklenen: ");
    int l = slen2(m);
    bin_copy(m + l, sizeof(m) - l, p);
    cerr(m);
}

/* ---------------- semboller ---------------- */
typedef struct {
    char name[40];
    int  slot;
    int  is_func;
    int  addr;
    int  nparam;
} sym_t;

static sym_t S[WC_MAX_SYM];
static int   Sn;
static int   g_slot_next;
static int   l_slot_next;

static sym_t *sym_find(const char *n) {
    for (int i = Sn - 1; i >= 0; i--) if (s_eq(S[i].name, n)) return &S[i];
    return 0;
}
static sym_t *sym_add(const char *n, int slot, int is_func, int addr, int np) {
    if (Sn >= WC_MAX_SYM) { cerr("cok fazla sembol"); return 0; }
    sym_t *s = &S[Sn++];
    bin_copy(s->name, sizeof(s->name), n);
    s->slot = slot; s->is_func = is_func; s->addr = addr; s->nparam = np;
    return s;
}

/* ---------------- libWolf yerlesikleri (wlf_vm.c ile ayni id'ler) ------- */
typedef struct { const char *name; int id; } builtin_t;
static const builtin_t BUILTINS[] = {
    {"wolf_root", 1}, {"wolf_panel", 2}, {"wolf_label", 3}, {"wolf_button", 4},
    {"wolf_input", 5}, {"wolf_listbox", 6}, {"wolf_image", 7},
    {"wolf_progress", 8}, {"wolf_slider", 9}, {"wolf_separator", 10},
    {"wolf_checkbox", 11},
    {"wolf_text", 20}, {"wolf_value", 22}, {"wolf_value_of", 23},
    {"wolf_move", 24}, {"wolf_resize", 25}, {"wolf_color", 26},
    {"wolf_visible", 27}, {"wolf_enabled", 28}, {"wolf_clear", 29},
    {"wolf_destroy", 30},
    {"wolf_list_add", 40}, {"wolf_list_clear", 41}, {"wolf_list_count", 42},
    {"wolf_list_selected", 43},
    {"wolf_log", 60}, {"wolf_notify", 61}, {"wolf_dialog", 62},
    {"wolf_ticks_ms", 63}, {"wolf_sleep_ms", 64}, {"wolf_version", 65},
    {"wolf_open_app", 66}, {"wolf_clip_set", 67}, {"wolf_exec", 68},
    {"wolf_win_title", 80}, {"wolf_win_size", 81},
    {"wolf_fs_exists", 90}, {"wolf_fs_mkdir", 91}, {"wolf_fs_count", 92},
    {"wolf_fs_remove", 93}, {"wolf_fs_rename", 94},
    {"wolf_bind", 110}, {"wolf_ready", 111},
    {"wolf_on_click", 100}, {"wolf_on_change", 101},
    {0, 0}
};
static int builtin_id(const char *n) {
    for (int i = 0; BUILTINS[i].name; i++)
        if (s_eq(BUILTINS[i].name, n)) return BUILTINS[i].id;
    return -1;
}

/* ---------------- dongu yigini ---------------- */
typedef struct {
    int breaks[MAX_BREAK];
    int nb;
    int cont;      /* continue hedefi */
} loop_t;
static loop_t L[8];
static int    Ln;

static void expr(void);
static void stmt(void);
static void block(void);

/* ---------------- ifade ---------------- */
static void primary(void) {
    tok_t *t = cur();
    if (t->kind == T_NUM) { emit(OP_PUSHI); emit((uint32_t)t->num); Ti++; return; }
    if (t->kind == T_STR) { int ix = str_add(t->str); emit(OP_PUSHS); emit((uint32_t)ix); Ti++; return; }
    if (accept("(")) { expr(); expect(")"); return; }
    if (t->kind == T_ID) {
        char nm[48];
        bin_copy(nm, sizeof(nm), t->txt);
        Ti++;
        if (punct_is("(")) {
            Ti++;
            int argc = 0;
            int bid0 = builtin_id(nm);
            /* wolf_on_click(w, islev): 2. arguman islev ADRESIDIR (cagrilmaz).
             * Bytecode'da islev adresi = kod kelime indeksi. */
            if (bid0 == 100 || bid0 == 101) {
                expr();                 /* widget kimligi */
                argc++;
                if (accept(",")) {
                    if (cur()->kind == T_ID) {
                        sym_t *fn = sym_find(cur()->txt);
                        if (fn && fn->is_func) {
                            emit(OP_PUSHI); emit((uint32_t)fn->addr);
                            Ti++;
                            argc++;
                        } else {
                            char m[110];
                            bin_copy(m, sizeof(m), "olay islevi bulunamadi: ");
                            int l2 = slen2(m);
                            bin_copy(m + l2, sizeof(m) - l2, cur()->txt);
                            cerr(m);
                            Ti++;
                        }
                    } else {
                        cerr("olay icin islev adi gerekli");
                    }
                }
                expect(")");
                emit(OP_CALLB); emit((uint32_t)bid0); emit((uint32_t)argc);
                return;
            }
            if (!punct_is(")")) {
                for (;;) {
                    expr();
                    argc++;
                    if (accept(",")) continue;
                    break;
                }
            }
            expect(")");
            int bid = builtin_id(nm);
            sym_t *f = sym_find(nm);
            if (bid >= 0) { emit(OP_CALLB); emit((uint32_t)bid); emit((uint32_t)argc); }
            else if (f && f->is_func) { emit(OP_CALLU); emit((uint32_t)f->addr); emit((uint32_t)argc); }
            else {
                char m[96];
                bin_copy(m, sizeof(m), "bilinmeyen islev: ");
                int l = slen2(m);
                bin_copy(m + l, sizeof(m) - l, nm);
                cerr(m);
            }
            return;
        }
        sym_t *s = sym_find(nm);
        if (!s) {
            char m[96];
            bin_copy(m, sizeof(m), "tanimsiz degisken: ");
            int l = slen2(m);
            bin_copy(m + l, sizeof(m) - l, nm);
            cerr(m);
            emit(OP_PUSHI); emit(0);
            return;
        }
        emit(OP_LOAD); emit((uint32_t)s->slot);
        return;
    }
    cerr("ifade bekleniyordu");
}

static void unary(void) {
    if (accept("!")) { unary(); emit(OP_NOT); return; }
    if (accept("-")) { unary(); emit(OP_NEG); return; }
    if (accept("+")) { unary(); return; }
    if (punct_is("++") || punct_is("--")) {
        int inc = punct_is("++");
        Ti++;
        if (cur()->kind != T_ID) { cerr("++ sonrasi degisken gerekli"); return; }
        sym_t *s = sym_find(cur()->txt);
        Ti++;
        if (!s) { cerr("tanimsiz degisken"); return; }
        emit(OP_LOAD); emit((uint32_t)s->slot);
        emit(OP_PUSHI); emit(1);
        emit(inc ? OP_ADD : OP_SUB);
        emit(OP_STORE); emit((uint32_t)s->slot);
        return;
    }
    primary();
    if ((punct_is("++") || punct_is("--")) && Ti > 0 && T[Ti - 1].kind == T_ID) {
        int inc = punct_is("++");
        sym_t *s = sym_find(T[Ti - 1].txt);
        if (s) {
            Ti++;
            emit(OP_PUSHI); emit(1);
            emit(inc ? OP_ADD : OP_SUB);
            emit(OP_STORE); emit((uint32_t)s->slot);
        }
    }
}

static void mul_expr(void) {
    unary();
    for (;;) {
        if (accept("*")) { unary(); emit(OP_MUL); }
        else if (accept("/")) { unary(); emit(OP_DIV); }
        else if (accept("%")) { unary(); emit(OP_MOD); }
        else return;
    }
}
static void add_expr(void) {
    mul_expr();
    for (;;) {
        if (accept("+")) { mul_expr(); emit(OP_ADD); }
        else if (accept("-")) { mul_expr(); emit(OP_SUB); }
        else return;
    }
}
static void rel_expr(void) {
    add_expr();
    for (;;) {
        if (accept("<=")) { add_expr(); emit(OP_LE); }
        else if (accept(">=")) { add_expr(); emit(OP_GE); }
        else if (accept("<")) { add_expr(); emit(OP_LT); }
        else if (accept(">")) { add_expr(); emit(OP_GT); }
        else return;
    }
}
static void eq_expr(void) {
    rel_expr();
    for (;;) {
        if (accept("==")) { rel_expr(); emit(OP_EQ); }
        else if (accept("!=")) { rel_expr(); emit(OP_NE); }
        else return;
    }
}
static void and_expr(void) { eq_expr(); while (accept("&&")) { eq_expr(); emit(OP_AND); } }
static void or_expr(void)  { and_expr(); while (accept("||")) { and_expr(); emit(OP_OR); } }
static void expr(void) { or_expr(); }

/* ---------------- deyimler ---------------- */
static void var_decl(void) {
    for (;;) {
        if (cur()->kind != T_ID) { cerr("degisken adi bekleniyordu"); return; }
        char nm[40];
        bin_copy(nm, sizeof(nm), cur()->txt);
        Ti++;
        if (punct_is("[")) { cerr("dizi desteklenmiyor"); return; }
        if (punct_is("*")) { cerr("pointer desteklenmiyor"); return; }
        int slot = (l_slot_next > 0) ? l_slot_next++ : g_slot_next++;
        if (slot > 255) { cerr("cok fazla degisken"); return; }
        sym_add(nm, slot, 0, 0, 0);
        if (accept("=")) {
            expr();
            emit(OP_STORE); emit((uint32_t)slot);
        }
        if (accept(",")) continue;
        break;
    }
    expect(";");
}

static void expr_stmt(void) {
    /* atama bicimleri */
    if (cur()->kind == T_ID && peek(1)->kind == T_PUNCT) {
        const char *op = peek(1)->txt;
        if (s_eq(op, "=") || s_eq(op, "+=") || s_eq(op, "-=")) {
            sym_t *s = sym_find(cur()->txt);
            if (!s) { cerr("tanimsiz degisken"); return; }
            int add = s_eq(op, "+=");
            int sub = s_eq(op, "-=");
            Ti += 2;
            if (add || sub) { emit(OP_LOAD); emit((uint32_t)s->slot); }
            expr();
            if (add) emit(OP_ADD);
            else if (sub) emit(OP_SUB);
            emit(OP_STORE); emit((uint32_t)s->slot);
            expect(";");
            return;
        }
    }
    expr();
    emit(OP_POP);
    expect(";");
}

static void stmt(void) {
    if (O.err) return;
    if (punct_is("{")) { block(); return; }
    if (accept(";")) return;
    if (kw_is("int") || kw_is("char")) { Ti++; var_decl(); return; }
    if (kw_is("if")) {
        Ti++;
        expect("(");
        expr();
        expect(")");
        int jz = here();
        emit(OP_JZ); emit(0);
        stmt();
        if (kw_is("else")) {
            int jmp = here();
            emit(OP_JMP); emit(0);
            patch_at(jz + 1, (uint32_t)here());
            Ti++;
            stmt();
            patch_at(jmp + 1, (uint32_t)here());
        } else {
            patch_at(jz + 1, (uint32_t)here());
        }
        return;
    }
    if (kw_is("while")) {
        Ti++;
        int top = here();
        expect("(");
        expr();
        expect(")");
        int jz = here();
        emit(OP_JZ); emit(0);
        if (Ln < 8) { L[Ln].nb = 0; L[Ln].cont = top; Ln++; }
        stmt();
        emit(OP_JMP); emit((uint32_t)top);
        patch_at(jz + 1, (uint32_t)here());
        if (Ln > 0) {
            Ln--;
            for (int i = 0; i < L[Ln].nb; i++) patch_at(L[Ln].breaks[i], (uint32_t)here());
        }
        return;
    }
    if (kw_is("for")) {
        Ti++;
        expect("(");
        /* baslangic */
        if (!punct_is(";")) {
            if (cur()->kind == T_ID && peek(1)->kind == T_PUNCT &&
                (s_eq(peek(1)->txt, "=") || s_eq(peek(1)->txt, "++") ||
                 s_eq(peek(1)->txt, "--"))) {
                sym_t *s = sym_find(cur()->txt);
                if (!s) { cerr("tanimsiz degisken"); return; }
                if (s_eq(peek(1)->txt, "=")) {
                    Ti += 2; expr();
                    emit(OP_STORE); emit((uint32_t)s->slot);
                }
            } else { expr(); emit(OP_POP); }
        }
        expect(";");
        int top = here();
        int jz = -1;
        if (!punct_is(";")) {
            expr();
            jz = here();
            emit(OP_JZ); emit(0);
        }
        expect(";");
        /* adim kodu govdeden sonra calisir; once govdeye atla */
        int skip = here();
        emit(OP_JMP); emit(0);
        int step = here();
        if (!punct_is(")")) {
            if (cur()->kind == T_ID && peek(1)->kind == T_PUNCT &&
                (s_eq(peek(1)->txt, "=") || s_eq(peek(1)->txt, "++") ||
                 s_eq(peek(1)->txt, "--"))) {
                sym_t *s = sym_find(cur()->txt);
                if (!s) { cerr("tanimsiz degisken"); return; }
                const char *op = peek(1)->txt;
                if (s_eq(op, "=")) {
                    Ti += 2; expr();
                    emit(OP_STORE); emit((uint32_t)s->slot);
                } else {
                    int inc = s_eq(op, "++");
                    Ti += 2;
                    emit(OP_LOAD); emit((uint32_t)s->slot);
                    emit(OP_PUSHI); emit(1);
                    emit(inc ? OP_ADD : OP_SUB);
                    emit(OP_STORE); emit((uint32_t)s->slot);
                }
            } else { expr(); emit(OP_POP); }
        }
        expect(")");
        emit(OP_JMP); emit((uint32_t)top);
        int body = here();
        patch_at(skip + 1, (uint32_t)body);
        if (Ln < 8) { L[Ln].nb = 0; L[Ln].cont = step; Ln++; }
        stmt();
        emit(OP_JMP); emit((uint32_t)step);
        if (jz >= 0) patch_at(jz + 1, (uint32_t)here());
        if (Ln > 0) {
            Ln--;
            for (int i = 0; i < L[Ln].nb; i++) patch_at(L[Ln].breaks[i], (uint32_t)here());
        }
        return;
    }
    if (kw_is("break")) {
        Ti++;
        expect(";");
        if (Ln > 0) {
            int at = here();
            emit(OP_JMP); emit(0);
            if (L[Ln - 1].nb < MAX_BREAK) L[Ln - 1].breaks[L[Ln - 1].nb++] = at + 1;
        }
        return;
    }
    if (kw_is("return")) {
        Ti++;
        if (!punct_is(";")) expr();
        else { emit(OP_PUSHI); emit(0); }
        expect(";");
        emit(OP_RET);
        return;
    }
    if (kw_is("void")) { cerr("void yalnizca islev turunde kullanilabilir"); return; }
    expr_stmt();
}

static void block(void) {
    expect("{");
    while (!O.err && !punct_is("}") && cur()->kind != T_EOF) stmt();
    expect("}");
}

/* ---------------- islev / program ---------------- */
static void parse_func(void) {
    if (cur()->kind != T_ID) { cerr("islev adi bekleniyordu"); return; }
    char nm[48];
    bin_copy(nm, sizeof(nm), cur()->txt);
    Ti++;
    expect("(");
    int np = 0;
    char pn[WC_MAX_PARAM][40];
    if (!punct_is(")")) {
        for (;;) {
            /* Parametre adi: ',' ya da ')' ile takip edilen son kimlik.
             * "int a", "const wolf_api_t *api", "void *user" hepsi gecer. */
            char nm2[40];
            nm2[0] = 0;
            int guard = 0;
            while (guard++ < 8) {
                if (cur()->kind == T_ID && peek(1)->kind == T_PUNCT &&
                    (s_eq(peek(1)->txt, ",") || s_eq(peek(1)->txt, ")"))) {
                    bin_copy(nm2, sizeof(nm2), cur()->txt);
                    Ti++;
                    break;
                }
                if (cur()->kind == T_ID || cur()->kind == T_KW ||
                    punct_is("*") || punct_is("&") || punct_is("const")) {
                    if (cur()->kind == T_ID) bin_copy(nm2, sizeof(nm2), cur()->txt);
                    Ti++;
                    continue;
                }
                cerr("parametre adi bekleniyordu");
                return;
            }
            if (!nm2[0]) { cerr("parametre adi bekleniyordu"); return; }
            if (np < WC_MAX_PARAM) bin_copy(pn[np], sizeof(pn[np]), nm2);
            np++;
            if (accept(",")) continue;
            break;
        }
    }
    expect(")");
    if (np > WC_MAX_PARAM) { cerr("en fazla 8 parametre"); return; }
    if (accept(";")) return;    /* prototip */

    sym_add(nm, 0, 1, here(), np);
    int saved = l_slot_next;
    l_slot_next = WC_LOCAL_BASE;
    for (int i = 0; i < np; i++) sym_add(pn[i], 200 + i, 0, 0, 0);
    block();
    emit(OP_PUSHI); emit(0);
    emit(OP_RET);
    l_slot_next = saved;
}

static uint32_t build_program(uint8_t *out, uint32_t cap) {
    uint32_t hdr = 16u + (uint32_t)O.strs_n;
    uint32_t total = hdr + (uint32_t)O.n * 4u;
    if (total > cap) { cerr("cikti tamponu kucuk"); return 0; }
    uint32_t *w = (uint32_t *)out;
    w[0] = 0x42464C57u;
    w[1] = (uint32_t)g_slot_next;
    w[2] = (uint32_t)O.strs_n;
    w[3] = (uint32_t)O.n;
    uint8_t *sp = out + 16;
    for (int i = 0; i < O.strs_n; i++) sp[i] = (uint8_t)O.strs[i];
    uint8_t *cp = out + hdr;
    for (int i = 0; i < O.n; i++) {
        uint32_t v = O.code[i];
        cp[0] = (uint8_t)(v & 0xFF);
        cp[1] = (uint8_t)((v >> 8) & 0xFF);
        cp[2] = (uint8_t)((v >> 16) & 0xFF);
        cp[3] = (uint8_t)((v >> 24) & 0xFF);
        cp += 4;
    }
    return total;
}

int wolfc_compile(const char *src, uint8_t *out, uint32_t cap,
                  char *errmsg, int errcap);

int wolfc_compile(const char *src, uint8_t *out, uint32_t cap,
                  char *errmsg, int errcap) {
    for (unsigned i = 0; i < sizeof(O); i++) ((char *)&O)[i] = 0;
    Sn = 0; Ti = 0; Tn = 0; Ln = 0;
    g_slot_next = 0; l_slot_next = 0;
    if (errmsg && errcap > 0) errmsg[0] = 0;

    if (lex(src) != 0) { if (errmsg) bin_copy(errmsg, errcap, O.msg); return -1; }

    /* Giris prologu: 0..3 = CALLU <main>, HALT. Boylece VM daima 0'dan
     * baslar ve kullanicinin ilk tanimladigi islev nerede olursa olsun
     * giris noktasi main/wolf_main olur. */
    emit(OP_CALLU); emit(0); emit(0);
    emit(OP_HALT);

    while (!O.err && cur()->kind != T_EOF) {
        if (kw_is("int") || kw_is("char") || kw_is("void")) {
            Ti++;
            if (cur()->kind == T_ID && peek(1)->kind == T_PUNCT && s_eq(peek(1)->txt, "("))
                parse_func();
            else
                var_decl();
            continue;
        }
        cerr("beklenmeyen simge: yalnizca islev veya degisken tanimi");
        break;
    }
    if (!O.err) {
        sym_t *e = sym_find("main");
        if (!e) e = sym_find("wolf_main");
        if (!e || !e->is_func) cerr("giris islevi yok: main ya da wolf_main");
        else patch_at(1, (uint32_t)e->addr);
    }
    if (O.err) { if (errmsg) bin_copy(errmsg, errcap, O.msg); return -2; }
    uint32_t n = build_program(out, cap);
    if (!n) { if (errmsg) bin_copy(errmsg, errcap, O.msg); return -3; }
    return (int)n;
}
