#include "diskcrypt.h"
#include "storage.h"
#include "afs.h"
#include "diskcrypt_hdr.h"
#include <stddef.h>
#include <stdint.h>

#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/md.h"

extern int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);

#define DISKCRYPT_ITER      4096u
#define DISKCRYPT_RESERVE   2048u
#define DISKCRYPT_SECTOR    512u
#define DISKCRYPT_CHUNK     128u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t region_base;
    uint32_t region_end;
    uint32_t iterations;
    uint8_t  salt[16];
    uint8_t  verifier[32];
    uint8_t  pad[444];
} diskcrypt_hdr_t;

static int      s_present = 0;
static int      s_unlocked = 0;
static uint32_t s_region_base = 0;
static uint32_t s_region_end = 0;
static uint32_t s_iter = 0;
static uint8_t  s_salt[16];
static uint8_t  s_verifier[32];
static const char *s_status = "Sifreleme yok";

static mbedtls_aes_xts_context s_enc;
static mbedtls_aes_xts_context s_dec;
static uint8_t s_scratch[DISKCRYPT_CHUNK * DISKCRYPT_SECTOR];

static uint32_t dlen(const char *s) {
    uint32_t n = 0;
    if (s) while (s[n]) n++;
    return n;
}

static void xts_do(mbedtls_aes_xts_context *ctx, int mode, uint8_t *p, uint64_t lba) {
    uint8_t du[16];
    for (int i = 0; i < 8; i++) du[i] = (uint8_t)(lba >> (i * 8));
    for (int i = 8; i < 16; i++) du[i] = 0;
    mbedtls_aes_crypt_xts(ctx, mode, DISKCRYPT_SECTOR, du, p, p);
}

static int dc_read(uint32_t lba, uint32_t count, void *buf) {
    if (storage_raw_read(lba, count, buf) != 0) return -1;
    uint8_t *p = (uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++)
        xts_do(&s_dec, MBEDTLS_AES_DECRYPT, p + (size_t)i * DISKCRYPT_SECTOR, (uint64_t)lba + i);
    return 0;
}

static int dc_write(uint32_t lba, uint32_t count, const void *buf) {
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t done = 0;
    while (done < count) {
        uint32_t n = count - done;
        if (n > DISKCRYPT_CHUNK) n = DISKCRYPT_CHUNK;
        for (uint32_t i = 0; i < n * DISKCRYPT_SECTOR; i++)
            s_scratch[i] = src[(size_t)done * DISKCRYPT_SECTOR + i];
        for (uint32_t i = 0; i < n; i++)
            xts_do(&s_enc, MBEDTLS_AES_ENCRYPT, s_scratch + (size_t)i * DISKCRYPT_SECTOR,
                   (uint64_t)lba + done + i);
        if (storage_raw_write(lba + done, n, s_scratch) != 0) return -1;
        done += n;
    }
    return 0;
}

static int derive(const char *pass, uint32_t iter, uint8_t out[32]) {
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                         (const unsigned char *)pass, dlen(pass),
                                         s_salt, sizeof(s_salt), iter, 32, out);
}

static void verifier_of(const uint8_t key[32], uint8_t out[32]) {
    mbedtls_sha256(key, 32, out, 0);
}

static int verifier_match(const uint8_t key[32]) {
    uint8_t ver[32];
    uint8_t diff = 0;
    verifier_of(key, ver);
    for (int i = 0; i < 32; i++) diff |= (uint8_t)(ver[i] ^ s_verifier[i]);
    return diff == 0;
}

static void crypt_enable(const uint8_t key[32]) {
    mbedtls_aes_xts_init(&s_enc);
    mbedtls_aes_xts_init(&s_dec);
    mbedtls_aes_xts_setkey_enc(&s_enc, key, 256);
    mbedtls_aes_xts_setkey_dec(&s_dec, key, 256);
    storage_crypt_t c;
    c.read = dc_read;
    c.write = dc_write;
    c.base_lba = s_region_base;
    c.end_lba = s_region_end;
    storage_set_crypt(&c);
}

void diskcrypt_init(void) {
    uint8_t sec[DISKCRYPT_SECTOR];
    s_present = 0;
    s_unlocked = 0;
    if (storage_raw_read(DISKCRYPT_HDR_LBA, 1, sec) != 0) {
        s_status = "Disk okunamadi";
        return;
    }
    diskcrypt_hdr_t *h = (diskcrypt_hdr_t *)sec;
    if (h->magic != DISKCRYPT_MAGIC || h->version != DISKCRYPT_VERSION) {
        s_status = "Sifreleme yok";
        return;
    }
    uint64_t secs = storage_sector_count();
    if (h->region_base != AFS_BASE_LBA || h->region_end <= h->region_base ||
        h->region_end > secs || h->iterations == 0) {
        s_status = "Sifreleme bozuk";
        return;
    }
    s_region_base = h->region_base;
    s_region_end = h->region_end;
    s_iter = h->iterations;
    for (int i = 0; i < 16; i++) s_salt[i] = h->salt[i];
    for (int i = 0; i < 32; i++) s_verifier[i] = h->verifier[i];
    s_present = 1;
    s_status = "Kilitli";
    afs_set_locked(1);
}

int diskcrypt_present(void)  { return s_present; }
int diskcrypt_unlocked(void) { return s_unlocked; }
const char *diskcrypt_status(void) { return s_status; }

int diskcrypt_unlock(const char *pass) {
    if (!s_present) return -1;
    if (!pass || !pass[0]) return -2;
    uint8_t key[32];
    if (derive(pass, s_iter, key) != 0) return -3;
    if (!verifier_match(key)) {
        s_status = "Parola hatali";
        return -4;
    }
    crypt_enable(key);
    s_unlocked = 1;
    s_status = "Acik";
    afs_set_locked(0);
    return 0;
}

int diskcrypt_setup(const char *pass) {
    if (s_present || storage_crypt_active()) return -1;
    if (!pass || dlen(pass) < 4) return -2;
    uint64_t secs = storage_sector_count();
    if (secs < (uint64_t)AFS_BASE_LBA + 4096u) return -3;
    uint32_t cap = (uint32_t)(secs - DISKCRYPT_RESERVE);
    if (cap <= AFS_BASE_LBA) cap = (uint32_t)secs;

    uint8_t salt[16];
    size_t olen = 0;
    mbedtls_hardware_poll(0, salt, sizeof(salt), &olen);
    if (olen != sizeof(salt)) return -4;

    s_region_base = AFS_BASE_LBA;
    s_region_end = cap;
    s_iter = DISKCRYPT_ITER;
    for (int i = 0; i < 16; i++) s_salt[i] = salt[i];

    uint8_t key[32];
    if (derive(pass, s_iter, key) != 0) return -5;
    verifier_of(key, s_verifier);

    uint32_t used = afs_data_next();
    if (used < s_region_base + 1u) used = s_region_base + 1u;
    if (used > s_region_end) used = s_region_end;

    crypt_enable(key);

    uint8_t sec[DISKCRYPT_SECTOR];
    for (uint32_t lba = s_region_base; lba < used; lba++) {
        if (storage_raw_read(lba, 1, sec) != 0) { storage_set_crypt(0); return -6; }
        xts_do(&s_enc, MBEDTLS_AES_ENCRYPT, sec, lba);
        if (storage_raw_write(lba, 1, sec) != 0) { storage_set_crypt(0); return -7; }
    }

    for (int i = 0; i < DISKCRYPT_SECTOR; i++) sec[i] = 0;
    diskcrypt_hdr_t *h = (diskcrypt_hdr_t *)sec;
    h->magic = DISKCRYPT_MAGIC;
    h->version = DISKCRYPT_VERSION;
    h->region_base = s_region_base;
    h->region_end = s_region_end;
    h->iterations = s_iter;
    for (int i = 0; i < 16; i++) h->salt[i] = s_salt[i];
    for (int i = 0; i < 32; i++) h->verifier[i] = s_verifier[i];
    if (storage_raw_write(DISKCRYPT_HDR_LBA, 1, sec) != 0) return -8;
    storage_flush();

    s_present = 1;
    s_unlocked = 1;
    s_status = "Acik";
    afs_set_locked(0);
    return 0;
}

int diskcrypt_disable(const char *pass) {
    if (!s_present) return -1;
    if (!pass || !pass[0]) return -2;
    uint8_t key[32];
    if (derive(pass, s_iter, key) != 0) return -3;
    if (!verifier_match(key)) return -4;

    mbedtls_aes_xts_context dec;
    mbedtls_aes_xts_init(&dec);
    mbedtls_aes_xts_setkey_dec(&dec, key, 256);

    uint32_t used = afs_data_next();
    if (used < s_region_base) used = s_region_base;
    if (used > s_region_end) used = s_region_end;

    storage_set_crypt(0);
    uint8_t sec[DISKCRYPT_SECTOR];
    for (uint32_t lba = s_region_base; lba < used; lba++) {
        if (storage_raw_read(lba, 1, sec) != 0) { mbedtls_aes_xts_free(&dec); return -5; }
        xts_do(&dec, MBEDTLS_AES_DECRYPT, sec, lba);
        if (storage_raw_write(lba, 1, sec) != 0) { mbedtls_aes_xts_free(&dec); return -6; }
    }
    mbedtls_aes_xts_free(&dec);

    for (int i = 0; i < DISKCRYPT_SECTOR; i++) sec[i] = 0;
    storage_raw_write(DISKCRYPT_HDR_LBA, 1, sec);
    storage_flush();

    s_present = 0;
    s_unlocked = 0;
    s_status = "Sifreleme yok";
    afs_set_locked(0);
    return 0;
}
