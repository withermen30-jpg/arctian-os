#ifndef DHCP_H
#define DHCP_H

#include <stdint.h>
#include <stdbool.h>

/* Gercek DHCP istemcisi (DISCOVER/OFFER/REQUEST/ACK).
 * Basarili olursa ipv4_config'i doldurur ve true doner; aksi halde
 * mevcut yapilandirmayi degistirmeden false doner. */
bool dhcp_configure(void);

#endif
