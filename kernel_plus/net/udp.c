#include "udp.h"
#include "ipv4.h"
#include "kernel.h"

#define UDP_MAX_BINDS 8

static struct {
    uint16_t port;
    udp_handler_t handler;
} udp_binds[UDP_MAX_BINDS];
static int udp_bind_count = 0;

void udp_bind(uint16_t port, udp_handler_t handler) {
    if (udp_bind_count >= UDP_MAX_BINDS) return;
    udp_binds[udp_bind_count].port = port;
    udp_binds[udp_bind_count].handler = handler;
    udp_bind_count++;
}

void udp_dispatch(const uint8_t src_ip[4], const uint8_t *packet, uint16_t length) {
    uint16_t src_port, dst_port;
    const uint8_t *data;
    uint16_t data_len;
    if (!udp_receive(src_ip, packet, length, &src_port, &dst_port, &data, &data_len))
        return;
    for (int i = 0; i < udp_bind_count; i++) {
        if (udp_binds[i].port == dst_port && udp_binds[i].handler)
            udp_binds[i].handler(src_ip, src_port, dst_port, data, data_len);
    }
}

bool udp_send(const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port,
              const uint8_t *data, uint16_t data_len) {
    uint16_t total_len = 8 + data_len;
    uint8_t packet[1500];

    packet[0] = (src_port >> 8) & 0xFF;
    packet[1] = src_port & 0xFF;
    packet[2] = (dst_port >> 8) & 0xFF;
    packet[3] = dst_port & 0xFF;
    packet[4] = (total_len >> 8) & 0xFF;
    packet[5] = total_len & 0xFF;
    packet[6] = 0; packet[7] = 0;

    for (uint16_t i = 0; i < data_len; i++) packet[8 + i] = data[i];

    uint8_t pseudo[12 + total_len];
    for (int i = 0; i < 4; i++) pseudo[i] = ipv4_config.src_ip[i];
    for (int i = 0; i < 4; i++) pseudo[4 + i] = dst_ip[i];
    pseudo[8] = 0;
    pseudo[9] = IPV4_PROTO_UDP;
    pseudo[10] = (total_len >> 8) & 0xFF;
    pseudo[11] = total_len & 0xFF;
    for (uint16_t i = 0; i < total_len; i++) pseudo[12 + i] = packet[i];

    uint16_t csum = ipv4_checksum(pseudo, 12 + total_len);
    if (csum == 0) csum = 0xFFFF;
    packet[6] = (csum >> 8) & 0xFF;
    packet[7] = csum & 0xFF;

    return ipv4_send(dst_ip, IPV4_PROTO_UDP, packet, total_len);
}

bool udp_receive(const uint8_t *src_ip, const uint8_t *packet, uint16_t length,
                 uint16_t *src_port_out, uint16_t *dst_port_out,
                 const uint8_t **data_out, uint16_t *data_len_out) {
    (void)src_ip;
    if (length < 8) return false;
    if (src_port_out) *src_port_out = ((uint16_t)packet[0] << 8) | packet[1];
    if (dst_port_out) *dst_port_out = ((uint16_t)packet[2] << 8) | packet[3];
    uint16_t total_len = ((uint16_t)packet[4] << 8) | packet[5];
    if (total_len < 8 || total_len > length) return false;
    if (data_out) *data_out = packet + 8;
    if (data_len_out) *data_len_out = total_len - 8;
    return true;
}
