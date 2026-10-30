#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>
#include <stdbool.h>

bool rtl8139_send_packet(void *ctx, const uint8_t *data, uint16_t length);
bool rtl8139_receive_packet(void *ctx, uint8_t *buf, uint16_t *length);
bool rtl8139_has_packet(void *ctx);

#endif
