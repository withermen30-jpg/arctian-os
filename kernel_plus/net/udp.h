#ifndef UDP_H
#define UDP_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

bool udp_send(const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port,
              const uint8_t *data, uint16_t data_len);
bool udp_receive(const uint8_t *src_ip, const uint8_t *packet, uint16_t length,
                 uint16_t *src_port_out, uint16_t *dst_port_out,
                 const uint8_t **data_out, uint16_t *data_len_out);

typedef void (*udp_handler_t)(const uint8_t src_ip[4], uint16_t src_port,
                              uint16_t dst_port, const uint8_t *data, uint16_t len);
void udp_bind(uint16_t port, udp_handler_t handler);
void udp_dispatch(const uint8_t src_ip[4], const uint8_t *packet, uint16_t length);

#endif
