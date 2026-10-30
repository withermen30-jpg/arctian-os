#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include "pci.h"
#include "asi.h"

/* Wi-Fi alt sistemi.
 *
 * Gercek 802.11 PHY/MAC erisimi, bu arayuzu uygulayan bir cihaz surucusu
 * tarafindan saglanir (bkz. wifi_register_driver). Kayitli surucu yoksa
 * tarama ve baglanti fonksiyonlari HICBIR SEY UYDURMAZ: sirasiyla 0 ve hata
 * dondururler. Once bu dosyada bulunan ornek/demo ag listesi kaldirilmistir. */

typedef struct {
    const char *name;
    int  (*init)(net_controller_t *nc);          /* 0 = bu kart desteklendi */
    int  (*scan)(asi_wifi_net_t *out, int max);  /* gercek beacon taramasi */
    int  (*connect)(const char *ssid, const char *pass);
    int  (*disconnect)(void);
    int  (*state)(void);                         /* ASI_WIFI_* */
    const char *(*ssid)(void);                   /* bagli SSID veya "" */

    /* Veri yolu: net.c bunlari cagirir; surucu desteklemiyorsa birakilir
     * (kayit siralamasi icin bkz. wifi.c dispatch). */
    bool (*send)(void *ctx, const uint8_t *data, uint16_t len);
    bool (*has)(void *ctx);
    bool (*recv)(void *ctx, uint8_t *buf, uint16_t *len);
} wifi_driver_t;

void        wifi_init(net_controller_t *nc);
void        wifi_register_driver(const wifi_driver_t *drv);

/* Etkin surucunun veri yolu (net.c tarafindan kullanilir). */
bool        wifi_send_packet(void *ctx, const uint8_t *data, uint16_t len);
bool        wifi_has_packet(void *ctx);
bool        wifi_receive_packet(void *ctx, uint8_t *buf, uint16_t *len);

bool        wifi_hw_detected(void);   /* PCI'da 802.11 karti var mi */
const char *wifi_driver_name(void);   /* kayitli surucu adi veya "" */

int         wifi_scan(asi_wifi_net_t *out, int max);
int         wifi_connect(const char *ssid, const char *pass);
int         wifi_disconnect(void);
int         wifi_state(void);
const char *wifi_ssid(void);
bool        wifi_up(void);

#endif /* WIFI_H */
