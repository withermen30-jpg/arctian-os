#include "spool.h"
#include "asi.h"
#include <string.h>

#define SPOOL_MAX 8

typedef struct {
    int used;
    int id;
    char name[48];
    uint32_t len;
    uint8_t *data;
} spool_job_t;

static spool_job_t s_jobs[SPOOL_MAX];
static int s_next_id = 1;
static char s_last[96];

static void job_free(spool_job_t *j) {
    if (j->data && g_asi && g_asi->mem) g_asi->mem->free(j->data);
    j->data = 0;
    j->used = 0;
    j->len = 0;
    j->name[0] = 0;
}

void spool_init(void) {
    for (int i = 0; i < SPOOL_MAX; i++) {
        s_jobs[i].data = 0;
        s_jobs[i].used = 0;
        s_jobs[i].len = 0;
        s_jobs[i].name[0] = 0;
    }
    s_last[0] = 0;
}

int spool_submit(const char *name, const void *data, uint32_t len) {
    if (!data || len == 0 || !g_asi || !g_asi->mem) return -1;
    for (int i = 0; i < SPOOL_MAX; i++) {
        if (s_jobs[i].used) continue;
        uint8_t *buf = (uint8_t *)g_asi->mem->alloc(len);
        if (!buf) return -1;
        memcpy(buf, data, len);
        s_jobs[i].used = 1;
        s_jobs[i].id = s_next_id++;
        s_jobs[i].len = len;
        s_jobs[i].data = buf;
        int k = 0;
        if (name) for (; name[k] && k < 47; k++) s_jobs[i].name[k] = name[k];
        s_jobs[i].name[k] = 0;
        return s_jobs[i].id;
    }
    return -1;
}

int spool_count(void) {
    int n = 0;
    for (int i = 0; i < SPOOL_MAX; i++) if (s_jobs[i].used) n++;
    return n;
}

int spool_info(int i, int *id, char *name, int cap, uint32_t *len) {
    int k = 0;
    for (int s = 0; s < SPOOL_MAX; s++) {
        if (!s_jobs[s].used) continue;
        if (k == i) {
            if (id) *id = s_jobs[s].id;
            if (name && cap > 0) {
                int j = 0;
                for (; s_jobs[s].name[j] && j < cap - 1; j++) name[j] = s_jobs[s].name[j];
                name[j] = 0;
            }
            if (len) *len = s_jobs[s].len;
            return 0;
        }
        k++;
    }
    return -1;
}

int spool_cancel(int id) {
    for (int i = 0; i < SPOOL_MAX; i++)
        if (s_jobs[i].used && s_jobs[i].id == id) { job_free(&s_jobs[i]); return 0; }
    return -1;
}

static void sanitize(char *dst, int cap, const char *src) {
    int k = 0;
    if (!src) src = "belge";
    for (int i = 0; src[i] && k < cap - 1; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        dst[k++] = ok ? c : '_';
    }
    if (k == 0) {
        const char *d = "belge";
        for (int i = 0; d[i] && k < cap - 1; i++) dst[k++] = d[i];
    }
    dst[k] = 0;
}

int spool_process(void) {
    if (!g_asi || !g_asi->fs) return -1;
    asi_fs_t *fs = g_asi->fs;
    if (fs->mkdir) fs->mkdir("C:/logs/print/");

    int done = 0;
    for (int i = 0; i < SPOOL_MAX; i++) {
        spool_job_t *j = &s_jobs[i];
        if (!j->used) continue;

        char safe[48];
        char base[96];
        sanitize(safe, sizeof(safe), j->name);

        int o = 0;
        char num[8];
        int n = 0;
        uint32_t v = (uint32_t)j->id;
        if (v == 0) num[n++] = '0';
        while (v && n < 7) { num[n++] = (char)('0' + (v % 10)); v /= 10; }
        while (n && o < 4) base[o++] = num[--n];
        base[o++] = '_';
        for (int s = 0; safe[s] && o < 90; s++) base[o++] = safe[s];
        const char *ext = ".txt";
        for (int s = 0; ext[s] && o < 94; s++) base[o++] = ext[s];
        base[o] = 0;

        void *h = fs->create_in ? fs->create_in("C:/logs/print/", base, 0) : 0;
        if (!h && fs->create) h = fs->create(0, base, 0);
        if (h && fs->write) fs->write(h, j->data, j->len);

        const char *p = "C:/logs/print/";
        int k2 = 0;
        for (; p[k2] && k2 < 90; k2++) s_last[k2] = p[k2];
        for (int s = 0; base[s] && k2 < 94; s++) s_last[k2++] = base[s];
        s_last[k2] = 0;

        job_free(j);
        done++;
    }
    return done;
}

int spool_last_path(char *out, int cap) {
    if (!out || cap <= 0) return -1;
    int i = 0;
    for (; s_last[i] && i < cap - 1; i++) out[i] = s_last[i];
    out[i] = 0;
    return 0;
}
