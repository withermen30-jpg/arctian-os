#ifndef DNS_H
#define DNS_H

#include <stdint.h>
#include <stdbool.h>

#define DNS_PORT 53
#define DNS_MAX_NAME_LEN 256
#define DNS_RESULT_MAX 4

typedef struct {
    uint8_t ip[4];
} dns_result_t;

void dns_init(void);
bool dns_resolve(const char *hostname, dns_result_t results[DNS_RESULT_MAX], int *count);
void dns_set_server(const uint8_t server[4]);

#endif
