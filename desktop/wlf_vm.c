/**
 * Wolf bytecode VM — OS icinden derlenen .wlf uygulamalari (0.7)
 *
 * Isletim sisteminin icinde tam bir C derleyicisi yok; bu yuzden `wf build`
 * C-altkume kaynagini Wolf bytecode'a cevirir (wolfc.c) ve bu VM calistirir.
 * API ayni libWolf'tur: program yine `wolf_label(...)`, `wolf_fs_read(...)`
 * gibi cagrilar yapar.
 *
 * Program bicimi (WLFB):
 *   u32 magic "WLFB"
 *   u32 nslots            ; slot sayisi (bilgi)
 *   u32 nstrings          ; metin tablosu uzunlugu (bayt)
 *   u32 code_words        ; komut sayisi
 *   u8  strings[nstrings] ; NUL ile ayrilmis metinler
 *   u32 code[code_words]
 *
 * Calisma modeli:
 *   - Giris noktasi kodun 0. kelimesidir. `wolf_main` bitene kadar calisir.
 *   - Olaylar (`wolf_on_click`) bir islev adresi kaydeder. Widget'a tiklaninca
 *     olay kuyruga girer; LVGL zamanlayicisi islevi VM'de calistirir. Boylece
 *     program bloklamadan etkilesimli kalir.
 *   - Metin parametreleri OP_PUSHS ile -(idx+1) olarak tasinir.
 *   - Donus adresleri ayri yiginda; operand yigini donus degeri icin serbest.
 *   - Yerel degiskenler tek slot uzayinda (ozyineleme yok).
 */
#include "dsk.h"

#define VM_MAGIC   0x42464C57u   /* "WLFB" */
#define VM_MAX_INST 4
#define VM_EVQ     32
#define VM_MAX_HANDLERS 32

enum {
    OP_HALT = 0,
    OP_PUSHI, OP_PUSHS, OP_LOAD, OP_STORE,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_NOT, OP_NEG, OP_AND, OP_OR,
    OP_JMP, OP_JZ, OP_JNZ,
    OP_CALLB, OP_CALLU, OP_RET, OP_POP
};

typedef struct {
    int wid;
    uint32_t fn;
    int kind;        /* 0 = tik, 1 = degisim */
} vm_handler_t;

typedef struct {
    int used;
    lv_obj_t *body;
    const uint8_t *base;       /* ham program (kaynak bellekte kalir) */
    const uint32_t *code;
    uint32_t words;
    const char *strs;
    uint32_t strs_len;
    int32_t stack[512];
    int sp;
    int32_t rstack[64];
    int rsp;
    int32_t slot[256];
    vm_handler_t h[VM_MAX_HANDLERS];
    int hn;
    struct { int wid; int kind; } evq[VM_EVQ];
    int evq_n;
    int64_t steps;
} vm_inst_t;

static vm_inst_t s_inst[VM_MAX_INST];
static lv_timer_t *s_timer;

static int push(vm_inst_t *v, int32_t x) {
    if (v->sp >= (int)(sizeof(v->stack) / sizeof(v->stack[0]))) return -1;
    v->stack[v->sp++] = x;
    return 0;
}
static int pop(vm_inst_t *v, int32_t *out) {
    if (v->sp <= 0) return -1;
    *out = v->stack[--v->sp];
    return 0;
}
static uint32_t rd(vm_inst_t *v, uint32_t *pc) {
    uint32_t at = (*pc)++;
    return (at < v->words) ? v->code[at] : 0;
}

static const char *vm_str(const int32_t *args, int i, const vm_inst_t *v) {
    int32_t x = args[i];
    if (x >= 0) return "";
    uint32_t idx = (uint32_t)(-(x + 1));
    if (idx >= v->strs_len) return "";
    return v->strs + idx;
}

/* Olay kuyruguna ekle (host geri cagrilari) */
static void vm_enqueue(int wid, int kind) {
    for (int i = 0; i < VM_MAX_INST; i++) {
        vm_inst_t *v = &s_inst[i];
        if (!v->used) continue;
        for (int k = 0; k < v->hn; k++) {
            if (v->h[k].wid == wid && v->h[k].kind == kind) {
                if (v->evq_n < VM_EVQ) {
                    v->evq[v->evq_n].wid = wid;
                    v->evq[v->evq_n].kind = kind;
                    v->evq_n++;
                }
                return;
            }
        }
    }
}

static void vm_click_trampoline(lv_event_t *e) {
    int wid = (int)(intptr_t)lv_event_get_user_data(e);
    vm_enqueue(wid, 0);
}
static void vm_change_trampoline(lv_event_t *e) {
    int wid = (int)(intptr_t)lv_event_get_user_data(e);
    vm_enqueue(wid, 1);
}

static int vm_exec(vm_inst_t *v, uint32_t entry);

/* Kuytudaki olaylari calistir. */
static void vm_drain(vm_inst_t *v) {
    int guard = 0;
    while (v->evq_n > 0 && guard++ < VM_EVQ) {
        int wid = v->evq[0].wid;
        int kind = v->evq[0].kind;
        for (int i = 1; i < v->evq_n; i++) v->evq[i - 1] = v->evq[i];
        v->evq_n--;

        for (int k = 0; k < v->hn; k++) {
            if (v->h[k].wid != wid || v->h[k].kind != kind) continue;
            v->sp = 0;
            v->rsp = 0;
            v->slot[200] = wid;
            vm_exec(v, v->h[k].fn);
            break;
        }
    }
}

static void vm_timer_cb(lv_timer_t *t) {
    (void)t;
    for (int i = 0; i < VM_MAX_INST; i++)
        if (s_inst[i].used) vm_drain(&s_inst[i]);
}

/* ------------------------------------------------------------------ */
/* Host koprusu                                                        */
/* ------------------------------------------------------------------ */

static wolf_api_t *vm_api(vm_inst_t *v) { return wlf_api_get(v->body); }

static int vm_builtin(vm_inst_t *v, int id, const int32_t *a, int argc) {
    wolf_api_t *A = vm_api(v);
    if (!A) return -1;
    char tmp[512];
#define S(i) vm_str(a, (i), v)

    switch (id) {
    /* cizim */
    case 1:  return A->root();
    case 2:  return A->panel(a[0], a[1], a[2], a[3], a[4]);
    case 3:  return A->label(a[0], a[1], a[2], S(3));
    case 4:  return A->button(a[0], a[1], a[2], a[3], a[4], S(5));
    case 5:  return A->input(a[0], a[1], a[2], a[3], a[4], S(5));
    case 6:  return A->listbox(a[0], a[1], a[2], a[3], a[4]);
    case 7:  return A->image(a[0], a[1], a[2], a[3], a[4], S(5));
    case 8:  return A->progress(a[0], a[1], a[2], a[3], a[4], a[5]);
    case 9:  return A->slider(a[0], a[1], a[2], a[3], a[4], a[5]);
    case 10: return A->separator(a[0], a[1], a[2], a[3]);
    case 11: return A->checkbox(a[0], a[1], a[2], S(3), a[4]);

    /* ozellikler */
    case 20: A->set_text(a[0], S(1)); return 0;
    case 22: A->set_value(a[0], a[1]); return 0;
    case 23: return A->get_value(a[0]);
    case 24: A->set_pos(a[0], a[1], a[2]); return 0;
    case 25: A->set_size(a[0], a[1], a[2]); return 0;
    case 26: A->set_color(a[0], (uint32_t)a[1]); return 0;
    case 27: A->set_visible(a[0], a[1]); return 0;
    case 28: A->set_enabled(a[0], a[1]); return 0;
    case 29: A->clear(a[0]); return 0;
    case 30: A->destroy(a[0]); return 0;

    /* liste */
    case 40: A->list_add(a[0], S(1)); return 0;
    case 41: A->list_clear(a[0]); return 0;
    case 42: return A->list_count(a[0]);
    case 43: return A->list_selected(a[0]);

    /* sistem */
    case 60: A->log(S(0)); return 0;
    case 61: A->notify(S(0), S(1)); return 0;
    case 62: A->dialog(S(0), S(1), S(2)); return 0;
    case 63: return (int)A->ticks_ms();
    case 64: A->sleep_ms((uint32_t)a[0]); return 0;
    case 65: A->os_version(tmp, sizeof(tmp)); return 0;
    case 66: return A->open_app(S(0));
    case 67: A->clip_set(S(0)); return 0;
    case 68: return A->exec(S(0), tmp, sizeof(tmp));

    /* pencere */
    case 80: A->win_title(S(0)); return 0;
    case 81: A->win_size(a[0], a[1]); return 0;

    /* dosya sistemi */
    case 90: return A->fs_exists(S(0));
    case 91: return A->fs_mkdir(S(0));
    case 92: return A->fs_count(S(0));
    case 93: return A->fs_remove(S(0));
    case 94: return A->fs_rename(S(0), S(1));
    case 95: return A->fs_read(S(0), tmp, sizeof(tmp));

    /* olaylar: islev adresi (kod kelime indeksi) */
    case 100:
        if (v->hn < VM_MAX_HANDLERS) {
            v->h[v->hn].wid = a[0];
            v->h[v->hn].fn = (uint32_t)a[1];
            v->h[v->hn].kind = 0;
            v->hn++;
            /* Gercek LVGL geri cagrisi: host tarafinda trampolin. */
            lv_obj_t *o = wolf_host_obj(a[0]);
            if (o) lv_obj_add_event_cb(o, vm_click_trampoline, LV_EVENT_CLICKED,
                                       (void *)(intptr_t)a[0]);
        }
        return 0;
    case 101:
        if (v->hn < VM_MAX_HANDLERS) {
            v->h[v->hn].wid = a[0];
            v->h[v->hn].fn = (uint32_t)a[1];
            v->h[v->hn].kind = 1;
            v->hn++;
            lv_obj_t *o = wolf_host_obj(a[0]);
            if (o) lv_obj_add_event_cb(o, vm_change_trampoline, LV_EVENT_VALUE_CHANGED,
                                       (void *)(intptr_t)a[0]);
        }
        return 0;
    /* libWolf istemci tarafi: bytecode'da baglama gerekmez (API zaten
     * tablo uzerinden gelir). Ayni kaynak native ve bytecode derlensin. */
    case 110: return 0;
    case 111: return 1;

    default:
        return -1;
    }
#undef S
}

/* ------------------------------------------------------------------ */
/* Yorumlayici                                                         */
/* ------------------------------------------------------------------ */

static int vm_exec(vm_inst_t *v, uint32_t entry) {
    uint32_t pc = entry;
    int rc = 0;
    v->rsp = 0;
    for (;;) {
        if (++v->steps > 4000000) { rc = -6; break; }
        if (pc >= v->words) break;
        uint32_t op = rd(v, &pc);
        int32_t a = 0, b = 0;
        switch (op) {
        case OP_HALT: return 0;
        case OP_PUSHI: if (push(v, (int32_t)rd(v, &pc))) return -7; break;
        case OP_PUSHS: {
            uint32_t idx = rd(v, &pc);
            if (idx >= v->strs_len) { rc = -8; break; }
            if (push(v, -(int32_t)(idx + 1))) return -7;
            break;
        }
        case OP_LOAD: {
            uint32_t s = rd(v, &pc);
            if (s >= 256) return -9;
            if (push(v, v->slot[s])) return -7;
            break;
        }
        case OP_STORE: {
            uint32_t s = rd(v, &pc);
            if (s >= 256) return -9;
            if (pop(v, &a)) return -7;
            v->slot[s] = a;
            break;
        }
        case OP_ADD: if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, a + b)) return -7; break;
        case OP_SUB: if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, a - b)) return -7; break;
        case OP_MUL: if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, a * b)) return -7; break;
        case OP_DIV: if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, b ? a / b : 0)) return -7; break;
        case OP_MOD: if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, b ? a % b : 0)) return -7; break;
        case OP_EQ: if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, a == b)) return -7; break;
        case OP_NE: if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, a != b)) return -7; break;
        case OP_LT: if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, a < b)) return -7; break;
        case OP_LE: if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, a <= b)) return -7; break;
        case OP_GT: if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, a > b)) return -7; break;
        case OP_GE: if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, a >= b)) return -7; break;
        case OP_NOT: if (pop(v, &a)) return -7; if (push(v, !a)) return -7; break;
        case OP_NEG: if (pop(v, &a)) return -7; if (push(v, -a)) return -7; break;
        case OP_AND: if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, a && b)) return -7; break;
        case OP_OR:  if (pop(v, &b) || pop(v, &a)) return -7; if (push(v, a || b)) return -7; break;
        case OP_JMP: pc = rd(v, &pc); break;
        case OP_JZ:  { uint32_t t = rd(v, &pc); if (pop(v, &a)) return -7; if (!a) pc = t; break; }
        case OP_JNZ: { uint32_t t = rd(v, &pc); if (pop(v, &a)) return -7; if (a) pc = t; break; }
        case OP_POP: if (pop(v, &a)) return -7; break;
        case OP_CALLU: {
            uint32_t addr = rd(v, &pc);
            uint32_t argc = rd(v, &pc);
            if (argc > 8) return -10;
            int32_t av[8];
            for (int i = (int)argc - 1; i >= 0; i--) if (pop(v, &av[i])) return -7;
            if (v->rsp >= (int)(sizeof(v->rstack) / sizeof(v->rstack[0]))) return -11;
            v->rstack[v->rsp++] = (int32_t)pc;
            for (uint32_t i = 0; i < argc; i++) v->slot[200 + i] = av[i];
            pc = addr;
            break;
        }
        case OP_RET:
            if (v->rsp <= 0) return 0;
            pc = (uint32_t)v->rstack[--v->rsp];
            break;
        case OP_CALLB: {
            uint32_t id = rd(v, &pc);
            uint32_t argc = rd(v, &pc);
            if (argc > 8) return -10;
            int32_t av[8];
            for (int i = (int)argc - 1; i >= 0; i--) if (pop(v, &av[i])) return -7;
            int r = vm_builtin(v, (int)id, av, (int)argc);
            if (push(v, r)) return -7;
            break;
        }
        default:
            return -12;
        }
        if (rc) break;
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Giris                                                               */
/* ------------------------------------------------------------------ */

/* Govde silinince VM'i serbest birak. */
static void vm_body_del_cb(lv_event_t *e) {
    lv_obj_t *body = (lv_obj_t *)lv_event_get_user_data(e);
    for (int i = 0; i < VM_MAX_INST; i++) {
        if (s_inst[i].used && s_inst[i].body == body) {
            s_inst[i].used = 0;
            s_inst[i].body = 0;
        }
    }
}

int wlf_vm_run(const uint8_t *code, uint32_t len, lv_obj_t *body, asi_t *asi) {
    (void)asi;
    if (!code || len < 16) return -1;
    const uint32_t *w = (const uint32_t *)code;
    if (w[0] != VM_MAGIC) return -2;
    uint32_t nslots = w[1];
    uint32_t slen = w[2];
    uint32_t words = w[3];
    uint64_t hdr = 16ull + slen;
    if (hdr + (uint64_t)words * 4ull > len) return -3;
    if (nslots > 256) return -4;

    vm_inst_t *v = 0;
    for (int i = 0; i < VM_MAX_INST; i++)
        if (!s_inst[i].used) { v = &s_inst[i]; break; }
    if (!v) return -5;

    for (unsigned i = 0; i < sizeof(vm_inst_t); i++) ((char *)v)[i] = 0;
    v->used = 1;
    v->body = body;
    v->base = code;
    v->code = (const uint32_t *)(code + hdr);
    v->words = words;
    v->strs = (const char *)(code + 16);
    v->strs_len = slen;

    lv_obj_add_event_cb(body, vm_body_del_cb, LV_EVENT_DELETE, body);

    int rc = vm_exec(v, 0);
    if (!s_timer) s_timer = lv_timer_create(vm_timer_cb, 60, 0);
    if (rc != 0) return rc;
    vm_drain(v);        /* ana akis sirasinda biriken olaylar */
    return 0;
}

