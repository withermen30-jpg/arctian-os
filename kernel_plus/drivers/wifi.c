#include "wifi.h"
#include "kernel.h"

/* Wi-Fi alt sistemi cekirdegi.
 *
 * Onemli: Burada ag listesi veya baglanti taklidi YOKTUR. Gercek tarama ve
 * baglanti yalnizca wifi_register_driver() ile kaydedilen bir 802.11 surucusu
 * varsa mumkundur. Surucu yoksa fonksiyonlar durustce 0/hata dondurur ve UI
 * "surucu yok" olarak gosterir. */

/* Birden fazla surucu kayit edilebilir; wifi_init her birini sirayla
 * kartla eslestirmeyi dener (Intel iwl, Realtek/Atheros/Broadcom genel
 * surucu vb.). */
#define WIFI_MAX_DRIVERS 8
static const wifi_driver_t *g_drvs[WIFI_MAX_DRIVERS];
static int g_drv_count = 0;
static const wifi_driver_t *g_drv = 0;

/* Surucu tarafindan yonetilen baglanti durumu (surucu yoksa OFF). */
static int  g_state = ASI_WIFI_OFF;
static char g_ssid[33] = "";

/* Netlink'te bildirilen son tespit edilen kart (hata ayiklama icin). */
static uint16_t g_hw_vendor = 0;
static uint16_t g_hw_device = 0;
static bool     g_hw_seen = false;

void wifi_register_driver(const wifi_driver_t *drv) {
    if (!drv) return;
    if (g_drv_count < WIFI_MAX_DRIVERS) g_drvs[g_drv_count++] = drv;
    if (!g_drv) g_drv = drv;
}

void wifi_init(net_controller_t *nc) {
    if (!nc || nc->net_type != NET_TYPE_WIFI) return;

    g_hw_seen = true;
    g_hw_vendor = nc->dev.vendor;
    g_hw_device = nc->dev.device;

    /* Kayitli suruculeri sirayla dene; ilk kabul eden aktif olur. */
    for (int i = 0; i < g_drv_count; i++) {
        const wifi_driver_t *d = g_drvs[i];
        if (!d || !d->init) continue;
        if (d->init(nc) == 0) {
            g_drv = d;
            nc->initialized = true;
            kernel_debug("WiFi: %s surucusu hazir (%04X:%04X)",
                         d->name, nc->dev.vendor, nc->dev.device);
            return;
        }
        kernel_debug("WiFi: %s bu karti desteklemiyor (%04X:%04X)",
                     d->name, nc->dev.vendor, nc->dev.device);
    }
    kernel_debug("WiFi: 802.11 karti tespit edildi (%04X:%04X) ama uygun surucu yok",
                 nc->dev.vendor, nc->dev.device);
}

bool wifi_hw_detected(void) {
    /* net_init sirasinda break nedeniyle wifi_init cagrilmamis olabilir;
     * bu yuzden PCI listesini dogrudan tarariz. */
    for (int i = 0; i < net_controller_count; i++) {
        if (net_controllers[i].net_type == NET_TYPE_WIFI) return true;
    }
    return g_hw_seen;
}

const char *wifi_driver_name(void) {
    return g_drv ? g_drv->name : "";
}

static void scopy33(char *dst, const char *src) {
    int i = 0;
    if (!src) { dst[0] = 0; return; }
    for (; src[i] && i < 32; i++) dst[i] = src[i];
    dst[i] = 0;
}

int wifi_scan(asi_wifi_net_t *out, int max) {
    if (!g_drv || !g_drv->scan || !out || max <= 0) return 0;
    return g_drv->scan(out, max);
}

int wifi_connect(const char *ssid, const char *pass) {
    if (!ssid || !ssid[0]) { g_state = ASI_WIFI_FAILED; return -1; }
    if (!g_drv || !g_drv->connect) {
        g_state = ASI_WIFI_FAILED;
        return -1;   /* gercek surucu yok: baglanti iddia edilmez */
    }

    g_state = ASI_WIFI_CONNECTING;
    int r = g_drv->connect(ssid, pass);
    if (r == 0) {
        scopy33(g_ssid, ssid);
        g_state = ASI_WIFI_CONNECTED;
    } else {
        g_state = ASI_WIFI_FAILED;
    }
    return r;
}

int wifi_disconnect(void) {
    if (g_drv && g_drv->disconnect) g_drv->disconnect();
    g_state = ASI_WIFI_OFF;
    g_ssid[0] = 0;
    return 0;
}

int wifi_state(void) {
    if (g_drv && g_drv->state) return g_drv->state();
    return g_state;
}

const char *wifi_ssid(void) {
    if (g_drv && g_drv->ssid) return g_drv->ssid();
    return (g_state == ASI_WIFI_CONNECTED) ? g_ssid : "";
}

bool wifi_up(void) { return wifi_state() == ASI_WIFI_CONNECTED; }

/* ---- Veri yolu dispatch: etkin surucu uzerinden (net.c bunlari cagirir) ---- */
bool wifi_send_packet(void *ctx, const uint8_t *data, uint16_t len) {
    return (g_drv && g_drv->send) ? g_drv->send(ctx, data, len) : false;
}
bool wifi_has_packet(void *ctx) {
    return (g_drv && g_drv->has) ? g_drv->has(ctx) : false;
}
bool wifi_receive_packet(void *ctx, uint8_t *buf, uint16_t *len) {
    return (g_drv && g_drv->recv) ? g_drv->recv(ctx, buf, len) : false;
}
