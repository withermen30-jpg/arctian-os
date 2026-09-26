#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include "pci.h"
#include "asi.h"


typedef struct {
    const char *name;
    int  (*init)(net_controller_t *nc);
    int  (*scan)(asi_wifi_net_t *out, int max);
    int  (*connect)(const char *ssid, const char *pass);
    int  (*disconnect)(void);
    int  (*state)(void);
    const char *(*ssid)(void);
} wifi_driver_t;

void        wifi_init(net_controller_t *nc);
void        wifi_register_driver(const wifi_driver_t *drv);

bool        wifi_hw_detected(void);
const char *wifi_driver_name(void);

int         wifi_scan(asi_wifi_net_t *out, int max);
int         wifi_connect(const char *ssid, const char *pass);
int         wifi_disconnect(void);
int         wifi_state(void);
const char *wifi_ssid(void);
bool        wifi_up(void);

#endif
