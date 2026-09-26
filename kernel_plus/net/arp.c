#include "arp.h"
#include "ipv4.h"
#include "net.h"
#include "kernel.h"

static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static uint32_t arp_ticks = 0;

void arp_init(void) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].valid = false;
        arp_cache[i].timestamp = 0;
        for (int j = 0; j < 6; j++) arp_cache[i].mac[j] = 0;
        for (int j = 0; j < 4; j++) arp_cache[i].ip[j] = 0;
    }
}

static void arp_cache_add(const uint8_t ip[4], const uint8_t mac[6]) {
    int oldest = 0;
    uint32_t oldest_time = 0xFFFFFFFF;

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid &&
            ip[0] == arp_cache[i].ip[0] && ip[1] == arp_cache[i].ip[1] &&
            ip[2] == arp_cache[i].ip[2] && ip[3] == arp_cache[i].ip[3]) {
            for (int j = 0; j < 6; j++) arp_cache[i].mac[j] = mac[j];
            arp_cache[i].timestamp = arp_ticks;
            return;
        }
        if (!arp_cache[i].valid || arp_cache[i].timestamp < oldest_time) {
            oldest = i;
            oldest_time = arp_cache[i].timestamp;
        }
    }

    for (int j = 0; j < 4; j++) arp_cache[oldest].ip[j] = ip[j];
    for (int j = 0; j < 6; j++) arp_cache[oldest].mac[j] = mac[j];
    arp_cache[oldest].timestamp = arp_ticks;
    arp_cache[oldest].valid = true;
}

bool arp_resolve(const uint8_t ip[4], uint8_t mac_out[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid &&
            ip[0] == arp_cache[i].ip[0] && ip[1] == arp_cache[i].ip[1] &&
            ip[2] == arp_cache[i].ip[2] && ip[3] == arp_cache[i].ip[3]) {
            if (arp_ticks - arp_cache[i].timestamp < ARP_CACHE_TIMEOUT) {
                for (int j = 0; j < 6; j++) mac_out[j] = arp_cache[i].mac[j];
                return true;
            }
        }
    }

    const uint8_t *my_mac = net_get_mac();
    if (!my_mac) return false;

    uint8_t arp_packet[42];
    for (int i = 0; i < 42; i++) arp_packet[i] = 0;

    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    for (int i = 0; i < 6; i++) arp_packet[i] = broadcast_mac[i];
    for (int i = 0; i < 6; i++) arp_packet[6 + i] = my_mac[i];
    arp_packet[12] = 0x08; arp_packet[13] = 0x06;
    arp_packet[14] = 0x00; arp_packet[15] = 0x01;
    arp_packet[16] = 0x08; arp_packet[17] = 0x00;
    arp_packet[18] = 0x06; arp_packet[19] = 0x04;
    arp_packet[20] = 0x00; arp_packet[21] = 0x01;

    for (int i = 0; i < 6; i++) arp_packet[22 + i] = my_mac[i];
    for (int i = 0; i < 4; i++) arp_packet[28 + i] = ipv4_config.src_ip[i];
    for (int i = 0; i < 6; i++) arp_packet[32 + i] = 0;
    for (int i = 0; i < 4; i++) arp_packet[38 + i] = ip[i];

    net_send_packet(arp_packet, 42);

    for (int attempt = 0; attempt < 50; attempt++) {
        for (int wait = 0; wait < 20000; wait++) { __asm__ volatile("pause"); }
        ipv4_poll();
        for (int i = 0; i < ARP_CACHE_SIZE; i++) {
            if (arp_cache[i].valid &&
                ip[0] == arp_cache[i].ip[0] && ip[1] == arp_cache[i].ip[1] &&
                ip[2] == arp_cache[i].ip[2] && ip[3] == arp_cache[i].ip[3]) {
                for (int j = 0; j < 6; j++) mac_out[j] = arp_cache[i].mac[j];
                return true;
            }
        }
    }
    return false;
}

void arp_receive(const uint8_t *packet, uint16_t length) {
    if (length < 42) return;
    uint16_t op = (packet[20] << 8) | packet[21];

    if (op == ARP_REPLY || op == ARP_REQUEST) {
        arp_cache_add(&packet[28], &packet[22]);
    }

    if (op == ARP_REQUEST) {
        uint8_t target_ip[4];
        for (int i = 0; i < 4; i++) target_ip[i] = packet[38 + i];
        if (target_ip[0] == ipv4_config.src_ip[0] &&
            target_ip[1] == ipv4_config.src_ip[1] &&
            target_ip[2] == ipv4_config.src_ip[2] &&
            target_ip[3] == ipv4_config.src_ip[3]) {

            const uint8_t *my_mac = net_get_mac();
            if (!my_mac) return;

            uint8_t reply[42];
            for (int i = 0; i < 42; i++) reply[i] = 0;
            for (int i = 0; i < 6; i++) { reply[i] = packet[6 + i]; }
            for (int i = 0; i < 6; i++) { reply[6 + i] = my_mac[i]; }
            reply[12] = 0x08; reply[13] = 0x06;
            reply[14] = 0x00; reply[15] = 0x01;
            reply[16] = 0x08; reply[17] = 0x00;
            reply[18] = 0x06; reply[19] = 0x04;
            reply[20] = 0x00; reply[21] = 0x02;
            for (int i = 0; i < 6; i++) reply[22 + i] = my_mac[i];
            for (int i = 0; i < 4; i++) reply[28 + i] = ipv4_config.src_ip[i];
            for (int i = 0; i < 6; i++) reply[32 + i] = packet[22 + i];
            for (int i = 0; i < 4; i++) reply[38 + i] = packet[28 + i];

            net_send_packet(reply, 42);
        }
    }
}
