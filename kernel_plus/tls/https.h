#ifndef ARCTIAN_HTTPS_H
#define ARCTIAN_HTTPS_H

#include <stdint.h>
#include <stdbool.h>


void https_init(void);

const char *https_get(const char *host, uint16_t port, const char *path,
                      uint32_t *len_out);

const char *https_get_url(const char *url, uint32_t *len_out);

const char *https_fetch_body(const char *url, uint32_t *len_out);

void https_last_error(char *buf, int cap);

#endif
