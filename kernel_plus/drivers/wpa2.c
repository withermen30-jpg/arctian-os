/* ===================================================================== *
 * wpa2.c — WPA2-PSK kriptografik katman (EAPOL 4-way handshake)
 *
 * Bu dosya Arctian 0.5 Wi-Fi sürücüsü için yazılmıştır. mbedTLS
 * (third_party/mbedtls) SHA-1 ve AES primitiflerini kullanır; Arctian
 * zaten TLS için bu kütüphaneyi içerir.
 *
 * Sağlanan fonksiyonlar:
 *   wpa2_derive_pmk  : PBKDF2-HMAC-SHA1 (4096 iter) → PMK (32 byte)
 *   wpa2_derive_ptk  : 802.11i PRF-256/512 → PTK (KCK/KEK/TK)
 *   wpa2_mic         : EAPOL-MIC (HMAC-SHA1, ilk 16 byte)
 *   wpa2_unwrap_gtk  : RFC 3394 AES Key Unwrap (GTK için)
 *
 * Doğrulama: WPA2_TEST_MAIN makrosu ile derlenince RFC 6070 PBKDF2
 * vektörlerini ve RFC 3394 AES-KW vektörlerini çalıştırır.
 *
 * ===================================================================== */

#include "kernel.h"
#include <stdint.h>
#include <string.h>

#include "mbedtls/sha1.h"
#include "mbedtls/aes.h"

/* ------------------------------------------------------------------ */
/* HMAC-SHA1                                                           */
/* ------------------------------------------------------------------ */
static void hmac_sha1(const uint8_t *key, uint32_t key_len,
                      const uint8_t *data, uint32_t data_len,
                      uint8_t out[20]) {
    uint8_t kipad[64], kopad[64];
    uint8_t tk[20];
    mbedtls_sha1_context ctx;
    uint32_t i;

    if (key_len > 64) {
        mbedtls_sha1_init(&ctx);
        mbedtls_sha1_starts(&ctx);
        mbedtls_sha1_update(&ctx, key, key_len);
        mbedtls_sha1_finish(&ctx, tk);
        key = tk; key_len = 20;
    }
    memset(kipad, 0x36, 64);
    memset(kopad, 0x5c, 64);
    for (i = 0; i < key_len; i++) { kipad[i] ^= key[i]; kopad[i] ^= key[i]; }

    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, kipad, 64);
    mbedtls_sha1_update(&ctx, data, data_len);
    mbedtls_sha1_finish(&ctx, out);

    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, kopad, 64);
    mbedtls_sha1_update(&ctx, out, 20);
    mbedtls_sha1_finish(&ctx, out);
}

/* ------------------------------------------------------------------ */
/* PBKDF2-HMAC-SHA1 (RFC 2898)                                        */
/* ------------------------------------------------------------------ */
static void pbkdf2_sha1(const uint8_t *pass, uint32_t pass_len,
                        const uint8_t *salt, uint32_t salt_len,
                        uint32_t iters, uint8_t *dk, uint32_t dk_len) {
    uint32_t blocks = (dk_len + 19) / 20;
    uint32_t i, j, k;
    for (i = 1; i <= blocks; i++) {
        uint8_t saltblk[64 + 4];
        uint8_t u[20], t[20];
        uint32_t slen = salt_len;
        memcpy(saltblk, salt, salt_len);
        saltblk[salt_len] = (uint8_t)((i >> 24) & 0xff);
        saltblk[salt_len + 1] = (uint8_t)((i >> 16) & 0xff);
        saltblk[salt_len + 2] = (uint8_t)((i >> 8) & 0xff);
        saltblk[salt_len + 3] = (uint8_t)(i & 0xff);
        slen += 4;

        hmac_sha1(pass, pass_len, saltblk, slen, u);
        memcpy(t, u, 20);
        for (j = 1; j < iters; j++) {
            hmac_sha1(pass, pass_len, u, 20, u);
            for (k = 0; k < 20; k++) t[k] ^= u[k];
        }
        uint32_t off = (i - 1) * 20;
        uint32_t cp = dk_len - off;
        if (cp > 20) cp = 20;
        memcpy(dk + off, t, cp);
    }
}

/* ------------------------------------------------------------------ */
/* WPA2 API                                                            */
/* ------------------------------------------------------------------ */
void wpa2_derive_pmk(const char *passphrase, const uint8_t *ssid, uint32_t ssid_len,
                     uint8_t pmk[32]) {
    /* PMK = PBKDF2-HMAC-SHA1(passphrase, ssid, 4096, 256 bit) */
    pbkdf2_sha1((const uint8_t *)passphrase, (uint32_t)strlen(passphrase),
                ssid, ssid_len, 4096, pmk, 32);
}

/* 802.11i PRF (HMAC-SHA1 based). out_len <= 512 bit (64 byte) typical. */
static void wpa2_prf(const uint8_t *key, uint32_t key_len,
                    const char *label,
                    const uint8_t *data, uint32_t data_len,
                    uint8_t *out, uint32_t out_len) {
    /* PRF(K, A, B) = T1 || T2 || ... ; Ti = HMAC-SHA1(K, A || 0x00 || B || i)
       (i = 0,1,2,...) — 802.11i form: Ti = PRF(K, A || 0 || B || i) */
    uint32_t i = 0, pos = 0;
    while (pos < out_len) {
        /* build A || 0 || B || i */
        uint32_t labellen = (uint32_t)strlen(label);
        uint8_t buf[256];
        uint32_t blen = 0;
        memcpy(buf + blen, label, labellen); blen += labellen;
        buf[blen++] = 0;
        memcpy(buf + blen, data, data_len); blen += data_len;
        buf[blen++] = (uint8_t)i;
        uint8_t t[20];
        hmac_sha1(key, key_len, buf, blen, t);
        uint32_t cp = out_len - pos;
        if (cp > 20) cp = 20;
        memcpy(out + pos, t, cp);
        pos += cp;
        i++;
    }
}

void wpa2_derive_ptk(const uint8_t pmk[32],
                     const uint8_t aa[6], const uint8_t spa[6],
                     const uint8_t anonce[32], const uint8_t snonce[32],
                     uint8_t ptk[64]) {
    /* PTK = PRF-X(PMK, "Pairwise key expansion",
     *             min(AA,SPA) || max(AA,SPA) ||
     *             min(ANonce,SNonce) || max(ANonce,SNonce)) */
    uint8_t data[6 + 6 + 32 + 32];
    uint32_t dlen = 0;
    /* min/max AA */
    if (memcmp(aa, spa, 6) < 0) { memcpy(data + dlen, aa, 6); dlen += 6; memcpy(data + dlen, spa, 6); dlen += 6; }
    else { memcpy(data + dlen, spa, 6); dlen += 6; memcpy(data + dlen, aa, 6); dlen += 6; }
    /* min/max nonce */
    if (memcmp(anonce, snonce, 32) < 0) { memcpy(data + dlen, anonce, 32); dlen += 32; memcpy(data + dlen, snonce, 32); dlen += 32; }
    else { memcpy(data + dlen, snonce, 32); dlen += 32; memcpy(data + dlen, anonce, 32); dlen += 32; }
    wpa2_prf(pmk, 32, "Pairwise key expansion", data, dlen, ptk, 64);
    /* ptk[0..15]=KCK, [16..31]=KEK, [32..63]=TK */
}

void wpa2_mic(const uint8_t kck[16], const uint8_t *eapol, uint32_t eapol_len,
              uint8_t mic[16]) {
    uint8_t full[20];
    hmac_sha1(kck, 16, eapol, eapol_len, full);
    memcpy(mic, full, 16);
}

/* ------------------------------------------------------------------ */
/* RFC 3394 AES Key Unwrap (GTK için)                                  */
/* ------------------------------------------------------------------ */
int wpa2_unwrap_gtk(const uint8_t kek[16], const uint8_t *wrapped, uint32_t wrapped_len,
                    uint8_t *gtk, uint32_t *gtk_len) {
    /* wrapped_len must be multiple of 8 and >= 24 (16 IV + at least 8 data) */
    if (wrapped_len < 24 || (wrapped_len % 8) != 0) return -1;
    uint32_t n = (wrapped_len / 8) - 1;   /* number of 8-byte blocks */
    uint8_t a[8];
    uint8_t r[8 * 8];  /* up to 8 blocks (64 bytes) — yeterli */
    if (n > 8) return -1;
    memcpy(a, wrapped, 8);
    memcpy(r, wrapped + 8, wrapped_len - 8);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, kek, 128);

    for (int j = 5; j >= 0; j--) {
        for (uint32_t i = n; i >= 1; i--) {
            uint8_t b[16];
            /* A ^= (n*j + i) */
            uint64_t t = (uint64_t)n * j + i;
            for (int k = 7; k >= 0; k--) { a[k] ^= (uint8_t)(t & 0xff); t >>= 8; }
            /* R[i] = D(K, A || R[i]) */
            memcpy(b, a, 8);
            memcpy(b + 8, r + (i - 1) * 8, 8);
            uint8_t out[16];
            mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, b, out);
            memcpy(a, out, 8);
            memcpy(r + (i - 1) * 8, out + 8, 8);
        }
    }
    mbedtls_aes_free(&aes);

    /* IV check: A must equal 0xA6A6A6A6A6A6A6A6 */
    const uint8_t iv[8] = {0xA6,0xA6,0xA6,0xA6,0xA6,0xA6,0xA6,0xA6};
    if (memcmp(a, iv, 8) != 0) return -2;  /* integrity fail */

    *gtk_len = n * 8;
    memcpy(gtk, r, *gtk_len);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test harness (WPA2_TEST_MAIN ile derlenince çalışır)                */
/* ------------------------------------------------------------------ */
#ifdef WPA2_TEST_MAIN
#include <stdio.h>

static int hexeq(const char *name, const uint8_t *got, const uint8_t *exp, uint32_t n) {
    if (memcmp(got, exp, n) == 0) { printf("  OK   %s\n", name); fflush(stdout); return 0; }
    printf("  FAIL %s\n", name);
    printf("    got: "); for (uint32_t i=0;i<n;i++) printf("%02x", got[i]); printf("\n");
    printf("    exp: "); for (uint32_t i=0;i<n;i++) printf("%02x", exp[i]); printf("\n");
    return 1;
}

int main(void) {
    int fail = 0;
    /* RFC 6070 PBKDF2-HMAC-SHA1 vektörleri */
    {
        uint8_t dk[32];
        const uint8_t exp1[20] = {0x0c,0x60,0xc8,0x0f,0x96,0x1f,0x0e,0x71,0xf3,0xa9,0xb5,0x24,0xaf,0x60,0x12,0x06,0x2f,0xe0,0x37,0xa6};
        pbkdf2_sha1((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 1, dk, 20);
        fail += hexeq("PBKDF2 c=1", dk, exp1, 20);
    }
    {
        uint8_t dk[32];
        const uint8_t exp2[20] = {0xea,0x6c,0x01,0x4d,0xc7,0x2d,0x6f,0x8c,0xcd,0x1e,0xd9,0x2a,0xce,0x1d,0x41,0xf0,0xd8,0xde,0x89,0x57};
        pbkdf2_sha1((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 2, dk, 20);
        fail += hexeq("PBKDF2 c=2", dk, exp2, 20);
    }
    {
        uint8_t dk[32];
        const uint8_t exp3[20] = {0x4b,0x00,0x79,0x01,0xb7,0x65,0x48,0x9a,0xbe,0xad,0x49,0xd9,0x26,0xf7,0x21,0xd0,0x65,0xa4,0x29,0xc1};
        pbkdf2_sha1((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 4096, dk, 20);
        fail += hexeq("PBKDF2 c=4096", dk, exp3, 20);
    }
    /* RFC 3394 AES-KW unwrap vektörü */
    {
        uint8_t kek[16] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
        uint8_t wrapped[24] = {0x1f,0xa6,0x8b,0x0a,0x81,0x12,0xb4,0x47,
                               0xae,0xf3,0x4b,0xd8,0xfb,0x5a,0x7b,0x82,
                               0x9d,0x3e,0x86,0x23,0x71,0xd2,0xcf,0xe5};
        uint8_t gtk[16] = {0}; uint32_t glen = 0;
        int r = wpa2_unwrap_gtk(kek, wrapped, 24, gtk, &glen);
        const uint8_t exp[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
        printf("  %s AES-KW unwrap (rc=%d, len=%u)\n", (r==0)?"OK":"FAIL", r, glen);
        if (r != 0) fail++;
        else fail += hexeq("AES-KW gtk", gtk, exp, 16);
    }
    printf("WPA2 test: %s (%d hata)\n", fail ? "BASARISIZ" : "GECTI", fail);
    return fail;
}
#endif
