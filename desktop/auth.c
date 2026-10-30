#include "app.h"
#include "asi.h"
#include <stdint.h>

#define AUTH_PATH  "C:/etc/users"
#define AUTH_ITER  4096
#define AUTH_MAX   8

typedef struct {
    uint32_t h[8];
    uint8_t  buf[64];
    uint64_t len;
    uint32_t used;
} sha256_ctx;

static const uint32_t SHA_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_block(sha256_ctx *c, const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
               ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i-15],7) ^ ror32(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = ror32(w[i-2],17) ^ ror32(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=c->h[0],b=c->h[1],cc=c->h[2],d=c->h[3];
    uint32_t e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ror32(e,6) ^ ror32(e,11) ^ ror32(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + SHA_K[i] + w[i];
        uint32_t S0 = ror32(a,2) ^ ror32(a,13) ^ ror32(a,22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + maj;
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d;
    c->h[4]+=e; c->h[5]+=f; c->h[6]+=g; c->h[7]+=h;
}

static void sha256_init(sha256_ctx *c) {
    c->h[0]=0x6a09e667; c->h[1]=0xbb67ae85; c->h[2]=0x3c6ef372; c->h[3]=0xa54ff53a;
    c->h[4]=0x510e527f; c->h[5]=0x9b05688c; c->h[6]=0x1f83d9ab; c->h[7]=0x5be0cd19;
    c->len = 0; c->used = 0;
}

static void sha256_update(sha256_ctx *c, const uint8_t *d, uint32_t n) {
    c->len += (uint64_t)n * 8u;
    while (n) {
        uint32_t take = 64u - c->used;
        if (take > n) take = n;
        for (uint32_t i = 0; i < take; i++) c->buf[c->used + i] = d[i];
        c->used += take; d += take; n -= take;
        if (c->used == 64) { sha256_block(c, c->buf); c->used = 0; }
    }
}

static void sha256_final(sha256_ctx *c, uint8_t out[32]) {
    uint64_t bits = c->len;
    uint32_t i = c->used;
    c->buf[i++] = 0x80;
    if (i > 56) {
        while (i < 64) c->buf[i++] = 0;
        sha256_block(c, c->buf);
        i = 0;
    }
    while (i < 56) c->buf[i++] = 0;
    for (int j = 0; j < 8; j++) c->buf[i++] = (uint8_t)(bits >> (56 - j * 8));
    sha256_block(c, c->buf);
    for (int j = 0; j < 8; j++) {
        out[j*4]   = (uint8_t)(c->h[j] >> 24);
        out[j*4+1] = (uint8_t)(c->h[j] >> 16);
        out[j*4+2] = (uint8_t)(c->h[j] >> 8);
        out[j*4+3] = (uint8_t)(c->h[j]);
    }
}

static void sha256(const uint8_t *d, uint32_t n, uint8_t out[32]) {
    sha256_ctx c; sha256_init(&c); sha256_update(&c, d, n); sha256_final(&c, out);
}

static uint32_t slen(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static int ceq(const char *a, const char *b) {
    int i = 0; for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static void to_hex(const uint8_t *p, int n, char *out) {
    const char *h = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[i*2] = h[p[i] >> 4]; out[i*2+1] = h[p[i] & 15]; }
    out[n*2] = 0;
}
static int from_hex(const char *s, uint8_t *out, int n) {
    for (int i = 0; i < n; i++) {
        int a = hexval(s[i*2]), b = hexval(s[i*2+1]);
        if (a < 0 || b < 0) return -1;
        out[i] = (uint8_t)((a << 4) | b);
    }
    return 0;
}

typedef struct {
    char     name[32];
    uint32_t uid;
    uint8_t  salt[8];
    uint8_t  hash[32];
    int      has_pw;
} auth_user_t;

static auth_user_t s_users[AUTH_MAX];
static int         s_count;
static int         s_loaded;
static char        s_current[32];

static void derive(const uint8_t salt[8], const char *pw, uint8_t out[32]) {
    uint8_t buf[56];
    uint32_t pl = slen(pw);
    if (pl > 48) pl = 48;
    for (int i = 0; i < 8; i++) buf[i] = salt[i];
    for (uint32_t i = 0; i < pl; i++) buf[8 + i] = (uint8_t)pw[i];
    sha256(buf, 8 + pl, out);
    for (int i = 1; i < AUTH_ITER; i++) sha256(out, 32, out);
}

static int fs_read(const char *path, char *buf, int cap) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->find) return -1;
    void *h = g_asi->fs->find(0, path);
    if (!h) return -1;
    uint32_t n = g_asi->fs->read(h, buf, (uint32_t)(cap - 1));
    if (n >= (uint32_t)cap) n = (uint32_t)(cap - 1);
    buf[n] = 0;
    return (int)n;
}

static int fs_write(const char *path, const void *data, uint32_t n) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->create) return -1;
    if (g_asi->fs->mkdir && g_asi->fs->mkdir("C:/etc") != 0) return -1;
    void *h = g_asi->fs->find(0, path);
    if (!h) h = g_asi->fs->create(0, path, 0);
    if (!h) return -1;
    return g_asi->fs->write(h, data, n);
}

static int parse_uint(const char *s, uint32_t *out) {
    uint32_t v = 0; int n = 0;
    while (s[n] >= '0' && s[n] <= '9') { v = v * 10 + (uint32_t)(s[n] - '0'); n++; }
    if (!n) return 0;
    *out = v; return n;
}

static int disk_locked(void) {
    if (!g_asi || !g_asi->crypt || !g_asi->crypt->present) return 0;
    if (!g_asi->crypt->present()) return 0;
    if (!g_asi->crypt->unlocked) return 1;
    return !g_asi->crypt->unlocked();
}

static void auth_load(void) {
    if (s_loaded) return;
    if (disk_locked()) return;
    s_loaded = 1;
    s_count = 0;
    char buf[1024];
    int n = fs_read(AUTH_PATH, buf, sizeof(buf));
    if (n <= 0) return;
    char *p = buf;
    while (*p && s_count < AUTH_MAX) {
        while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
        if (!*p) break;
        auth_user_t *u = &s_users[s_count];
        int i = 0;
        while (p[i] && p[i] != ':' && i < 31) { u->name[i] = p[i]; i++; }
        u->name[i] = 0;
        if (!p[i] || p[i] != ':') break;
        p += i + 1;
        u->uid = 1000 + (uint32_t)s_count;
        int un = parse_uint(p, &u->uid);
        p += un;
        if (*p == ':') p++;
        int k = 0;
        while (p[k] && p[k] != ':' && p[k] != '\n' && p[k] != '\r') k++;
        int has_salt = (k == 16);
        if (has_salt) {
            char tmp[20]; for (int j = 0; j < 16; j++) tmp[j] = p[j]; tmp[16] = 0;
            has_salt = (from_hex(tmp, u->salt, 8) == 0);
        }
        p += k;
        if (*p == ':') p++;
        int h = 0;
        while (p[h] && p[h] != '\n' && p[h] != '\r') h++;
        u->has_pw = 0;
        if (has_salt && h == 64) {
            char tmp[80]; for (int j = 0; j < 64; j++) tmp[j] = p[j]; tmp[64] = 0;
            if (from_hex(tmp, u->hash, 32) == 0) u->has_pw = 1;
        }
        p += h;
        if (*p == '\r') p++;
        if (*p == '\n') p++;
        if (u->name[0]) s_count++;
    }
}

static void auth_store(void) {
    char out[1024];
    int o = 0;
    for (int u = 0; u < s_count; u++) {
        auth_user_t *us = &s_users[u];
        for (int i = 0; us->name[i] && o < (int)sizeof(out) - 2; i++) out[o++] = us->name[i];
        out[o++] = ':';
        char nb[12]; int nd = 0; uint32_t v = us->uid;
        if (!v) nb[nd++] = '0';
        while (v && nd < 11) { nb[nd++] = (char)('0' + v % 10); v /= 10; }
        for (int i = nd - 1; i >= 0; i--) out[o++] = nb[i];
        out[o++] = ':';
        if (us->has_pw && o < (int)sizeof(out) - 100) {
            char salt_hex[20], hash_hex[70];
            to_hex(us->salt, 8, salt_hex);
            to_hex(us->hash, 32, hash_hex);
            for (int i = 0; salt_hex[i]; i++) out[o++] = salt_hex[i];
            out[o++] = ':';
            for (int i = 0; hash_hex[i]; i++) out[o++] = hash_hex[i];
        } else {
            out[o++] = ':';
        }
        out[o++] = '\n';
    }
    fs_write(AUTH_PATH, out, (uint32_t)o);
}

static void new_salt(uint8_t out[8]) {
    asi_datetime_t dt;
    uint32_t seed = 0x9e3779b9u;
    if (g_asi && g_asi->time && g_asi->time->now && g_asi->time->now(&dt)) {
        seed ^= (uint32_t)dt.year * 1000003u;
        seed ^= (uint32_t)dt.month << 24;
        seed ^= (uint32_t)dt.day << 16;
        seed ^= (uint32_t)dt.hour << 8;
        seed ^= (uint32_t)dt.minute;
        seed ^= (uint32_t)dt.second * 2654435761u;
    }
    seed ^= (uint32_t)(uintptr_t)&dt;
    for (int i = 0; i < 8; i++) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        out[i] = (uint8_t)(seed >> 24);
    }
}

static int user_find(const char *name) {
    auth_load();
    if (!name || !name[0]) return -1;
    for (int i = 0; i < s_count; i++) if (ceq(s_users[i].name, name)) return i;
    return -1;
}

static void home_path(const char *name, char *out, int cap) {
    int o = 0;
    const char *pre = "C:/Users/";
    for (int i = 0; pre[i] && o < cap - 1; i++) out[o++] = pre[i];
    if (name) for (int i = 0; name[i] && o < cap - 1; i++) out[o++] = name[i];
    out[o] = 0;
}

static void ensure_home(const char *name) {
    if (!g_asi || !g_asi->fs || !g_asi->fs->mkdir) return;
    char p[80];
    home_path(name, p, sizeof(p));
    g_asi->fs->mkdir("C:/Users");
    g_asi->fs->mkdir(p);
    char sub[96];
    const char *bel = "/Belgeler", *mas = "/Masaustu";
    int o = 0; for (int i = 0; p[i] && o < (int)sizeof(sub) - 12; i++) sub[o++] = p[i];
    for (int i = 0; bel[i] && o < (int)sizeof(sub) - 1; i++) sub[o++] = bel[i];
    sub[o] = 0; g_asi->fs->mkdir(sub);
    o = 0; for (int i = 0; p[i] && o < (int)sizeof(sub) - 12; i++) sub[o++] = p[i];
    for (int i = 0; mas[i] && o < (int)sizeof(sub) - 1; i++) sub[o++] = mas[i];
    sub[o] = 0; g_asi->fs->mkdir(sub);
}

void arctian_auth_set_current(const char *name) {
    int i = 0;
    if (name) for (; name[i] && i < 31; i++) s_current[i] = name[i];
    s_current[i] = 0;
}

int arctian_auth_user_count(void) { auth_load(); return s_count; }
const char *arctian_auth_user_name(int i) { auth_load(); if (i < 0 || i >= s_count) return ""; return s_users[i].name; }
int arctian_auth_user_has_password(int i) { auth_load(); if (i < 0 || i >= s_count) return 0; return s_users[i].has_pw; }
uint32_t arctian_auth_user_uid(int i) { auth_load(); if (i < 0 || i >= s_count) return 0; return s_users[i].uid; }
int arctian_auth_find(const char *name) { return user_find(name); }

const char *arctian_auth_home(const char *name) {
    static char h[80];
    if (!name || !name[0]) name = s_current;
    home_path(name, h, sizeof(h));
    return h;
}

int arctian_auth_has_user(void) { auth_load(); return s_count > 0; }
const char *arctian_auth_username(void) {
    auth_load();
    if (s_current[0]) return s_current;
    return s_count > 0 ? s_users[0].name : "";
}
int arctian_auth_has_password(void) {
    int i = user_find(s_current);
    if (i < 0 && s_count > 0) i = 0;
    return i >= 0 ? s_users[i].has_pw : 0;
}
const char *arctian_auth_current(void) { return s_current; }
uint32_t arctian_auth_current_uid(void) {
    int i = user_find(s_current);
    return i >= 0 ? s_users[i].uid : 0;
}

int arctian_auth_create_user(const char *name, const char *pw_or_null) {
    auth_load();
    if (!name || !name[0]) return -1;
    if (s_count >= AUTH_MAX) return -5;
    for (int i = 0; name[i]; i++)
        if (name[i] == ':' || name[i] == '/' || name[i] == '\\' || name[i] == ' ') return -2;
    if (user_find(name) >= 0) return -6;
    auth_user_t *u = &s_users[s_count];
    int n = 0; while (name[n] && n < 31) { u->name[n] = name[n]; n++; }
    u->name[n] = 0;
    u->uid = 1000 + (uint32_t)s_count;
    u->has_pw = 0;
    if (pw_or_null && pw_or_null[0]) {
        new_salt(u->salt);
        derive(u->salt, pw_or_null, u->hash);
        u->has_pw = 1;
    }
    s_count++;
    auth_store();
    ensure_home(u->name);
    return 0;
}

int arctian_auth_remove_user(const char *name) {
    int i = user_find(name);
    if (i < 0) return -1;
    for (int j = i; j < s_count - 1; j++) s_users[j] = s_users[j + 1];
    s_count--;
    if (ceq(s_current, name)) s_current[0] = 0;
    auth_store();
    return 0;
}

int arctian_auth_verify_user(const char *name, const char *pw) {
    int i = user_find(name);
    if (i < 0) return 0;
    if (!s_users[i].has_pw) return 1;
    if (!pw) return 0;
    uint8_t out[32];
    derive(s_users[i].salt, pw, out);
    uint8_t diff = 0;
    for (int j = 0; j < 32; j++) diff |= (uint8_t)(out[j] ^ s_users[i].hash[j]);
    return diff == 0;
}

int arctian_auth_set_password_for(const char *name, const char *pw, const char *pw2) {
    int i = user_find(name);
    if (i < 0) return -1;
    if (!pw || !pw[0]) return -2;
    if (!ceq(pw, pw2)) return -3;
    if (slen(pw) < 4) return -4;
    new_salt(s_users[i].salt);
    derive(s_users[i].salt, pw, s_users[i].hash);
    s_users[i].has_pw = 1;
    auth_store();
    return 0;
}

int arctian_auth_set_password(const char *pw, const char *pw2) {
    const char *who = s_current[0] ? s_current : arctian_auth_username();
    if (user_find(who) < 0) return -1;
    return arctian_auth_set_password_for(who, pw, pw2);
}

int arctian_auth_verify(const char *pw) {
    int i = user_find(s_current);
    if (i < 0 && s_count > 0) i = 0;
    if (i < 0) return 0;
    return arctian_auth_verify_user(s_users[i].name, pw);
}
