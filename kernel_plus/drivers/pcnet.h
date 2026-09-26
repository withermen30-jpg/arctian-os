#ifndef PCNET_H
#define PCNET_H

#include <stdint.h>
#include <stdbool.h>
#include "pci.h"


void pcnet_init(net_controller_t *nc);
bool pcnet_send_packet(void *ctx, const uint8_t *data, uint16_t length);
bool pcnet_receive_packet(void *ctx, uint8_t *buf, uint16_t *length);
bool pcnet_has_packet(void *ctx);
bool pcnet_link_up(void);

#endif
