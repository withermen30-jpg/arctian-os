#ifndef ARCTIAN_FIREWALL_H
#define ARCTIAN_FIREWALL_H

#include <stdint.h>
#include <stdbool.h>
#include "asi.h"

/* Cekirdek guvenlik duvari. Kural listesi AFS'te ("firewall.cfg") tutulur,
 * ipv4/tcp kancalariyla uygulanir. Kural eslesmezse varsayilan izin. */

void firewall_init(void);

/* 1 = izin ver, 0 = engelle */
int firewall_check(int dir, uint8_t proto, const uint8_t ip[4], uint16_t port);
int firewall_outbound(uint8_t proto, const uint8_t dst_ip[4], uint16_t dst_port);
int firewall_inbound(uint8_t proto, const uint8_t src_ip[4], uint16_t dst_port);

int firewall_enabled(void);
int firewall_set_enabled(int on);
int firewall_count(void);
int firewall_get_rule(int idx, asi_fw_rule_t *out);
int firewall_add_rule(const asi_fw_rule_t *r);
int firewall_remove_rule(int idx);
int firewall_clear(void);
int firewall_save(void);
int firewall_load(void);
int firewall_selftest(void);

#endif /* ARCTIAN_FIREWALL_H */
