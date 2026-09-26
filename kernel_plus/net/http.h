#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>
#include <stdbool.h>

#define HTTP_PORT          80
#define HTTP_TIMEOUT       5000
#define HTTP_MAX_RESPONSE  196608
#define HTTP_MAX_HEADERS   32
#define HTTP_BUFFER_SIZE   4096

typedef struct {
    char  name[64];
    char  value[256];
} http_header_t;

typedef struct {
    int  status_code;
    char status_text[64];
    http_header_t headers[HTTP_MAX_HEADERS];
    int  header_count;
    char *body;
    uint32_t body_length;
} http_response_t;

void http_init(void);
bool http_get(const char *url, http_response_t *response);
void http_free_response(http_response_t *response);
const char *http_header_get(const http_response_t *resp, const char *name);

int http_download(const char *url, void *dst, uint32_t cap, uint32_t *len_out);

#endif
