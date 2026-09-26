#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stdbool.h>

#define NET_MAC_LEN      6
#define NET_PKT_MAX      2048
#define NET_RING_SIZE    32

#define ETHER_TYPE_IPV4  0x0800
#define ETHER_TYPE_ARP   0x0806
#define ETHER_TYPE_IPV6  0x86DD

typedef struct {
    uint8_t mac[NET_MAC_LEN];
    bool valid;
    uint16_t rx_head;
    uint16_t rx_tail;
    uint16_t queue_count;
    struct {
        uint8_t data[NET_PKT_MAX];
        uint16_t length;
        bool in_use;
    } pkts[NET_RING_SIZE];
} net_if_t;

typedef struct {
    int type;
    void *driver_ctx;
    bool (*send_packet)(void *ctx, const uint8_t *data, uint16_t length);
    bool (*has_packet)(void *ctx);
    bool (*receive_packet)(void *ctx, uint8_t *buf, uint16_t *length);
} net_driver_t;

extern net_if_t net_if;

void net_init(void);
bool net_send_packet(const uint8_t *data, uint16_t length);
bool net_receive_packet(uint8_t *buf, uint16_t *length);
bool net_has_packet(void);
const uint8_t *net_get_mac(void);
int  net_driver_type(void);
bool net_link_up(void);

#endif
