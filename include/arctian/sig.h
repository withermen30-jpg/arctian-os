/**
 * Arctian paket imzasi.
 *
 * .dpk / .wlf ve guncelleme manifestosu OS ile gelen RSA-2048 acik
 * anahtariyla dogrulanir. Imza, dosyanin SONUNA eklenen 260 baytlik
 * bloktur:
 *
 *   [imzalanan govde] [ "ASG1" (4) | RSA-2048 PKCS#1 v1.5 imza (256) ]
 *
 * Imza SHA-256 ozeti uzerindedir ve govdenin tamamini kapsar.
 * Imzasiz veya bozuk paket reddedilir.
 */
#ifndef ARCTIAN_SIG_H
#define ARCTIAN_SIG_H

#include <stdint.h>
#include <stddef.h>

#define SIG_MAGIC        "ASG1"
#define SIG_MAGIC_LEN    4
#define SIG_RSA_LEN      256
#define SIG_TRAILER_LEN  (SIG_MAGIC_LEN + SIG_RSA_LEN)

/* SHA-256 (32 bayt cikti). */
void sig_sha256(const void *data, uint32_t len, uint8_t out[32]);

/* govde: imzalanan baytlar; sig: 256 baytlik imza.
 * 0 = gecerli, <0 = gecersiz. */
int sig_verify(const uint8_t *body, uint32_t len, const uint8_t sig[SIG_RSA_LEN]);

/* Trailer'i olan paket icin: 0 = imza gecerli, -1 imza yok, -2 bozuk. */
int sig_verify_pkg(const uint8_t *buf, uint32_t size);

/* Trailer varsa imzalanan (govde) uzunlugu, yoksa size. */
uint32_t sig_body_len(const uint8_t *buf, uint32_t size);

/* Manifesto metni gibi serbest govdeler icin imza dogrulama.
 * sig_hex: 512 karakter hex (256 bayt). 0 = gecerli. */
int sig_verify_hex(const char *body, uint32_t len, const char *sig_hex);

/* Test yardimcisi: govdeye imza trailer'i ekler. Yalniz ARCTIAN_07_TEST
 * derlemesinde gercek imza uretir (gelistirme anahtari); aksi halde -1. */
int sig_test_seal(uint8_t *buf, uint32_t cap, uint32_t len, uint32_t *out_len);

#endif /* ARCTIAN_SIG_H */
