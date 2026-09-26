#include "wifi.h"
#include "kernel.h"


static const wifi_driver_t *g_drv = 0;

static int  g_state = ASI_WIFI_OFF;
static char g_ssid[33] = "";

static uint16_t g_hw_vendor = 0;
static uint16_t g_hw_device = 0;
static bool     g_hw_seen = false;

void wifi_register_driver(const wifi_driver_t *drv) {
    g_drv = drv;
}

void wifi_init(net_controller_t *nc) {
    if (!nc || nc->net_type != NET_TYPE_WIFI) return;

    g_hw_seen = true;
    g_hw_vendor = nc->dev.vendor;
    g_hw_device = nc->dev.device;

    if (g_drv && g_drv->init) {
        if (g_drv->init(nc) == 0) {
            nc->initialized = true;
            kernel_debug("WiFi: %s surucusu hazir (%04X:%04X)",
                         g_drv->name, nc->dev.vendor, nc->dev.device);
        } else {
            kernel_debug("WiFi: %s surucusu bu karti desteklemiyor (%04X:%04X)",
                         g_drv->name, nc->dev.vendor, nc->dev.device);
        }
    } else {
        kernel_debug("WiFi: 802.11 karti tespit edildi (%04X:%04X) ama surucu kayitli degil",
                     nc->dev.vendor, nc->dev.device);
    }
}

bool wifi_hw_detected(void) {
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
        return -1;
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
