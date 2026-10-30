#ifndef ARCTIAN_HTTPS_H
#define ARCTIAN_HTTPS_H

#include <stdint.h>
#include <stdbool.h>

/* mbedTLS tabanli HTTPS (TLS 1.3) istemcisi. */

/* Platform/TLS altyapisini baslatir (birden fazla cagrilabilir). */
void https_init(void);

/* https://host:port/path icin GET yapar. Yanit govdesi dahili tamponda
 * doner ve bir sonraki cagriya kadar gecerlidir. Hata: NULL. */
const char *https_get(const char *host, uint16_t port, const char *path,
                      uint32_t *len_out);

/* "https://host[:port]/path" seklindeki URL'yi ayristirip https_get yapar. */
const char *https_get_url(const char *url, uint32_t *len_out);

/* http/https ortak: yonlendirmeleri takip eder, chunked cozer, YALNIZCA
 * govdeyi dondurur (bir sonraki cagriya kadar gecerli). Hata: NULL. */
const char *https_fetch_body(const char *url, uint32_t *len_out);

/* Son https_fetch_body cagrisinin ulastigi nihai (yonlendirme sonrasi) URL. */
const char *https_fetch_last_url(void);

/* Son TLS hatasini okunabilir metin olarak yazar (hata ayiklama). */
void https_last_error(char *buf, int cap);

/* JS document.cookie tarafindan yazilan cerezi depoya ekler. */
void https_cookie_set(const char *host, const char *name, const char *value);

#endif /* ARCTIAN_HTTPS_H */
