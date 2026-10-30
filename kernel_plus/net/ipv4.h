#ifndef IPV4_H
#define IPV4_H

#include <stdint.h>
#include <stdbool.h>

#define IPV4_PROTO_ICMP  1
#define IPV4_PROTO_TCP    6
#define IPV4_PROTO_UDP   17

#define IPV4_FLAG_DF     0x4000
#define IPV4_FLAG_MF     0x2000

typedef struct {
    uint8_t  src_ip[4];
    uint8_t  dst_ip[4];
    uint8_t  gateway[4];
    uint8_t  subnet_mask[4];
    uint8_t  dns_server[4];
    uint16_t mtu;
    bool     configured;
} ipv4_config_t;

extern ipv4_config_t ipv4_config;
extern uint8_t ipv4_broadcast[4];

uint16_t ipv4_checksum(const uint8_t *data, uint16_t length);

void ipv4_init(void);
bool ipv4_configure(const uint8_t ip[4], const uint8_t gateway[4],
                    const uint8_t mask[4], const uint8_t dns[4]);
bool ipv4_send(const uint8_t dst_ip[4], uint8_t protocol,
               const uint8_t *payload, uint16_t payload_len);
bool ipv4_receive(const uint8_t *packet, uint16_t length,
                  uint8_t *protocol_out, uint8_t *src_ip_out,
                  const uint8_t **payload_out, uint16_t *payload_len_out);
void ipv4_poll(void);
void ipv4_set_default_gateway(const uint8_t gw[4]);
void ipv4_config_dhcp(void);

#endif
