#include "klog.h"
#include "serial.h"

static klog_entry_t s_ring[KLOG_MAX];
static int s_head;
static int s_count;
static uint32_t s_seq;
static void (*s_sink)(const klog_entry_t *, void *);
static void *s_sink_ud;

static void klog_copy(char *d, int cap, const char *s) {
    int i = 0;
    if (!s) { d[0] = 0; return; }
    for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}

const char *klog_level_str(int level) {
    switch (level) {
        case KLOG_DEBUG: return "DBG";
        case KLOG_INFO:  return "INF";
        case KLOG_WARN:  return "WRN";
        case KLOG_ERR:   return "ERR";
        case KLOG_FATAL: return "FTL";
        default:         return "???";
    }
}

void klog_init(void) {
    s_head = 0;
    s_count = 0;
    s_seq = 0;
    s_sink = 0;
    s_sink_ud = 0;
}

void klog_write(int level, const char *tag, const char *msg) {
    klog_entry_t *e = &s_ring[s_head];
    e->level = (uint8_t)level;
    e->tick = ++s_seq;
    klog_copy(e->tag, KLOG_TAG, tag);
    klog_copy(e->msg, KLOG_MSG, msg);
    s_head = (s_head + 1) % KLOG_MAX;
    if (s_count < KLOG_MAX) s_count++;

    serial_printf("[%s] %s: %s\n", klog_level_str(level),
                  e->tag, e->msg);
}

int klog_count(void) {
    return s_count;
}

int klog_get(int i, klog_entry_t *out) {
    if (i < 0 || i >= s_count || !out) return -1;
    int start = (s_head - s_count + KLOG_MAX) % KLOG_MAX;
    *out = s_ring[(start + i) % KLOG_MAX];
    return 0;
}

void klog_clear(void) {
    s_head = 0;
    s_count = 0;
}

void klog_set_sink(void (*sink)(const klog_entry_t *e, void *ud), void *ud) {
    s_sink = sink;
    s_sink_ud = ud;
}

int klog_flush(void) {
    if (!s_sink) return -1;
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        klog_entry_t e;
        if (klog_get(i, &e) != 0) continue;
        s_sink(&e, s_sink_ud);
        n++;
    }
    return n;
}
