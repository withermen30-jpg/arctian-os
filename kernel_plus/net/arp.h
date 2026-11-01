#ifndef ARP_H
#define ARP_H

#include <stdint.h>
#include <stdbool.h>

#define ARP_HARDWARE_ETHERNET 1
#define ARP_REQUEST          1
#define ARP_REPLY            2
#define ARP_CACHE_SIZE       16
/* ms cinsinden; sayac clock_ms() ile ayni saati kullanir. Eskiden bu
 * 300 "tik" sayiliyordu ama sayaci hicbir yer artirmadigi icin kayitlar
 * hicbir zaman asmi sayiliyordu. */
#define ARP_CACHE_TIMEOUT    600000u

typedef struct {
    uint8_t  ip[4];
    uint8_t  mac[6];
    uint32_t timestamp;
    bool     valid;
} arp_entry_t;

void arp_init(void);
bool arp_resolve(const uint8_t ip[4], uint8_t mac_out[6]);
void arp_receive(const uint8_t *packet, uint16_t length);

#endif
