#include "ipv4.h"
#include "arp.h"
#include "udp.h"
#include "dhcp.h"
#include "net.h"
#include "kernel.h"

extern bool tcp_receive_packet(const uint8_t *src_ip, const uint8_t *packet, uint16_t length);

ipv4_config_t ipv4_config;
uint8_t ipv4_broadcast[4] = {255, 255, 255, 255};

static uint16_t ipv4_id = 0;

uint16_t ipv4_checksum(const uint8_t *data, uint16_t length) {
    uint32_t sum = 0;
    for (uint16_t i = 0; i < length - 1; i += 2) {
        sum += ((uint16_t)data[i] << 8) | data[i + 1];
    }
    if (length & 1) sum += (uint16_t)data[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

void ipv4_init(void) {
    ipv4_config.src_ip[0] = 192; ipv4_config.src_ip[1] = 168;
    ipv4_config.src_ip[2] = 1;   ipv4_config.src_ip[3] = 100;
    ipv4_config.gateway[0] = 192; ipv4_config.gateway[1] = 168;
    ipv4_config.gateway[2] = 1;   ipv4_config.gateway[3] = 1;
    ipv4_config.subnet_mask[0] = 255; ipv4_config.subnet_mask[1] = 255;
    ipv4_config.subnet_mask[2] = 255; ipv4_config.subnet_mask[3] = 0;
    ipv4_config.dns_server[0] = 8; ipv4_config.dns_server[1] = 8;
    ipv4_config.dns_server[2] = 8; ipv4_config.dns_server[3] = 8;
    ipv4_config.mtu = 1500;
    ipv4_config.configured = false;
}

bool ipv4_configure(const uint8_t ip[4], const uint8_t gateway[4],
                    const uint8_t mask[4], const uint8_t dns[4]) {
    for (int i = 0; i < 4; i++) ipv4_config.src_ip[i] = ip[i];
    for (int i = 0; i < 4; i++) ipv4_config.gateway[i] = gateway[i];
    for (int i = 0; i < 4; i++) ipv4_config.subnet_mask[i] = mask[i];
    for (int i = 0; i < 4; i++) ipv4_config.dns_server[i] = dns[i];
    ipv4_config.configured = true;
    return true;
}

void ipv4_set_default_gateway(const uint8_t gw[4]) {
    for (int i = 0; i < 4; i++) ipv4_config.gateway[i] = gw[i];
}

void ipv4_config_dhcp(void) {
    if (dhcp_configure()) return;

    uint8_t dhcp_ip[4]    = {10, 0, 2, 15};
    uint8_t dhcp_gw[4]    = {10, 0, 2, 2};
    uint8_t dhcp_mask[4]  = {255, 255, 255, 0};
    uint8_t dhcp_dns[4]   = {10, 0, 2, 3};
    ipv4_configure(dhcp_ip, dhcp_gw, dhcp_mask, dhcp_dns);
}

static bool is_same_subnet(const uint8_t a[4], const uint8_t b[4]) {
    for (int i = 0; i < 4; i++) {
        if ((a[i] & ipv4_config.subnet_mask[i]) !=
            (b[i] & ipv4_config.subnet_mask[i])) return false;
    }
    return true;
}

bool ipv4_send(const uint8_t dst_ip[4], uint8_t protocol,
               const uint8_t *payload, uint16_t payload_len) {
    if (!ipv4_config.configured) return false;
    uint16_t total_len = 20 + payload_len;
    if (total_len > ipv4_config.mtu) return false;

    const uint8_t *my_mac = net_get_mac();
    if (!my_mac) return false;

    const uint8_t *next_hop = dst_ip;
    if (!is_same_subnet(ipv4_config.src_ip, dst_ip)) {
        next_hop = ipv4_config.gateway;
    }

    uint8_t dst_mac[6];
    if (!arp_resolve(next_hop, dst_mac)) return false;

    uint8_t packet[NET_PKT_MAX];
    uint16_t pkt_len = 14 + total_len;

    for (int i = 0; i < 6; i++) packet[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) packet[6 + i] = my_mac[i];
    packet[12] = 0x08; packet[13] = 0x00;

    uint8_t *iph = packet + 14;
    iph[0] = 0x45;
    iph[1] = 0x00;
    iph[2] = (total_len >> 8) & 0xFF;
    iph[3] = total_len & 0xFF;
    iph[4] = (ipv4_id >> 8) & 0xFF;
    iph[5] = ipv4_id & 0xFF;
    ipv4_id++;
    iph[6] = 0x40; iph[7] = 0x00;
    iph[8] = 64;
    iph[9] = protocol;
    iph[10] = 0; iph[11] = 0;
    for (int i = 0; i < 4; i++) iph[12 + i] = ipv4_config.src_ip[i];
    for (int i = 0; i < 4; i++) iph[16 + i] = dst_ip[i];

    uint16_t hdr_csum = ipv4_checksum(iph, 20);
    iph[10] = (hdr_csum >> 8) & 0xFF;
    iph[11] = hdr_csum & 0xFF;

    for (uint16_t i = 0; i < payload_len; i++) iph[20 + i] = payload[i];

    return net_send_packet(packet, pkt_len);
}

bool ipv4_receive(const uint8_t *packet, uint16_t length,
                  uint8_t *protocol_out, uint8_t *src_ip_out,
                  const uint8_t **payload_out, uint16_t *payload_len_out) {
    if (length < 34) return false;

    const uint8_t *iph = packet + 14;
    if ((iph[0] >> 4) != 4) return false;

    uint16_t total_len = ((uint16_t)iph[2] << 8) | iph[3];
    if (total_len < 20 || 14 + total_len > length) return false;
    uint16_t hdr_len = (iph[0] & 0x0F) * 4;
    if (hdr_len < 20) return false;

    bool for_us = false;
    for (int i = 0; i < 4; i++) {
        if (iph[16 + i] != ipv4_config.src_ip[i]) break;
        if (i == 3) for_us = true;
    }
    if (!for_us && ipv4_config.src_ip[0]) return false;

    if (protocol_out) *protocol_out = iph[9];
    if (src_ip_out) for (int i = 0; i < 4; i++) src_ip_out[i] = iph[12 + i];
    if (payload_out) *payload_out = iph + hdr_len;
    if (payload_len_out) *payload_len_out = total_len - hdr_len;

    return true;
}

void ipv4_poll(void) {
    static bool in_poll = false;
    if (in_poll) return;
    in_poll = true;

    for (int n = 0; n < 64; n++) {
        uint8_t rx_buf[NET_PKT_MAX];
        uint16_t rx_len = sizeof(rx_buf);
        if (!net_receive_packet(rx_buf, &rx_len)) break;
        if (rx_len < 14) continue;

        uint16_t ether_type = ((uint16_t)rx_buf[12] << 8) | rx_buf[13];
        if (ether_type == 0x0806) {
            arp_receive(rx_buf, rx_len);
            continue;
        }
        if (ether_type != 0x0800) continue;

        uint8_t protocol;
        uint8_t src_ip[4];
        const uint8_t *payload;
        uint16_t payload_len;

        if (!ipv4_receive(rx_buf, rx_len, &protocol, src_ip, &payload, &payload_len))
            continue;

        if (protocol == IPV4_PROTO_TCP)
            tcp_receive_packet(src_ip, payload, payload_len);
        else if (protocol == IPV4_PROTO_UDP)
            udp_dispatch(src_ip, payload, payload_len);
    }

    in_poll = false;
}
