#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <stdbool.h>
#include "pci.h"

bool e1000_send_packet(void *ctx, const uint8_t *data, uint16_t length);
bool e1000_receive_packet(void *ctx, uint8_t *buf, uint16_t *length);
bool e1000_has_packet(void *ctx);
bool e1000_link_up(void);

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 8

#endif
