/* ===================================================================== *
 * wifi_generic.c — Genel 802.11 Wi-Fi sürücü (çoklu vendor desteği)
 *
 * Arctian 0.5. iwl_ax201.c (Intel iwlwifi) dışında kalan yaygın Wi-Fi
 * kartlarını kapsayan genel bir sürücü ailesi sağlar:
 *
 *   - Realtek  RTL8188EE / RTL8192 / RTL8723BE / RTL8723DE /
 *             RTL8821CE / RTL8822CE  (USB/PCI, 802.11n/ac)
 *   - Atheros   AR9485 / AR9462 / AR9565 / AR9580 / QCA9880 /
 *             QCA6174 / QCA9377 / QCA9984
 *   - Broadcom  BCM4360 / BCM4352 / BCM4356 / BCM43602
 *
 * DÜRÜST DURUM: Bu sürücü donanımın gerçek register/makine protokolünü
 * uygulamaz — kartın firmware'i ile konuşan komut seti kart modeline
 * özgüdür ve canlı donanımda doğrulanmadan güvenilir sonuç üretilemez.
 * Bu yuzden:
 *   - Kart tespiti (PCI ID + MMIO/I/O BAR) gerçektir.
 *   - Baglanti durumu ASLA "baglandi" olarak uydurulmaz.
 *   - Ayni anda tek aktif surucu olur (iwl surucusu varsa o onceliklidir).
 *
 * Diger suruculerin ayni anda denemesi icin "generic" adli bu surucu
 * listenin sonunda durur.
 * ===================================================================== */

#include "kernel.h"
#include "pci.h"
#include "wifi.h"
#include "asi.h"
#include "net.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Kart tanimlari                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    uint16_t vendor;
    uint16_t device;
    const char *name;
    const char *family;   /* UI'da gosterilecek aile adi */
} wifi_card_t;

static const wifi_card_t g_cards[] = {
    /* Intel (iwl surucusu yoksa genel surucu devreye girer) */
    { 0x8086, 0x08B1, "Intel Wireless 7260",  "Intel Wi-Fi" },
    { 0x8086, 0x08B2, "Intel Wireless 7260",  "Intel Wi-Fi" },
    { 0x8086, 0x08B3, "Intel Wireless 3160",  "Intel Wi-Fi" },
    { 0x8086, 0x08B4, "Intel Wireless 3160",  "Intel Wi-Fi" },
    { 0x8086, 0x095A, "Intel Wireless 7265",  "Intel Wi-Fi" },
    { 0x8086, 0x095B, "Intel Wireless 7265",  "Intel Wi-Fi" },
    { 0x8086, 0x24F3, "Intel Wireless 8260",  "Intel Wi-Fi" },
    { 0x8086, 0x24FD, "Intel Wireless 8265",  "Intel Wi-Fi" },
    { 0x8086, 0x2526, "Intel Wireless 9260",  "Intel Wi-Fi" },
    { 0x8086, 0x9DF0, "Intel Wireless 9560",  "Intel Wi-Fi" },
    { 0x8086, 0xA370, "Intel Wireless 9560",  "Intel Wi-Fi" },
    { 0x8086, 0x2723, "Intel AX200",          "Intel Wi-Fi" },
    { 0x8086, 0x2725, "Intel AX210",          "Intel Wi-Fi" },
    { 0x8086, 0x51F0, "Intel AX211",          "Intel Wi-Fi" },
    { 0x8086, 0x7AF0, "Intel AX411",          "Intel Wi-Fi" },

    /* Realtek */
    { 0x10EC, 0x8179, "Realtek RTL8188EE",   "Realtek Wi-Fi" },
    { 0x10EC, 0x8192, "Realtek RTL8192",     "Realtek Wi-Fi" },
    { 0x10EC, 0xB723, "Realtek RTL8723BE",   "Realtek Wi-Fi" },
    { 0x10EC, 0xD723, "Realtek RTL8723DE",   "Realtek Wi-Fi" },
    { 0x10EC, 0xC821, "Realtek RTL8821CE",   "Realtek Wi-Fi" },
    { 0x10EC, 0xC822, "Realtek RTL8822CE",   "Realtek Wi-Fi" },

    /* Atheros / Qualcomm */
    { 0x168C, 0x0032, "Atheros AR9485",      "Atheros Wi-Fi" },
    { 0x168C, 0x0034, "Atheros AR9462",      "Atheros Wi-Fi" },
    { 0x168C, 0x0036, "Atheros AR9565",      "Atheros Wi-Fi" },
    { 0x168C, 0x003A, "Atheros AR9580",      "Atheros Wi-Fi" },
    { 0x168C, 0x003C, "Qualcomm QCA9880",    "Qualcomm Wi-Fi" },
    { 0x168C, 0x003E, "Qualcomm QCA6174",    "Qualcomm Wi-Fi" },
    { 0x168C, 0x0042, "Qualcomm QCA9377",    "Qualcomm Wi-Fi" },
    { 0x168C, 0x0046, "Qualcomm QCA9984",    "Qualcomm Wi-Fi" },

    /* Broadcom */
    { 0x14E4, 0x43A0, "Broadcom BCM4360",    "Broadcom Wi-Fi" },
    { 0x14E4, 0x43B1, "Broadcom BCM4352",    "Broadcom Wi-Fi" },
    { 0x14E4, 0x43D3, "Broadcom BCM4356",    "Broadcom Wi-Fi" },
    { 0x14E4, 0x4365, "Broadcom BCM43602",   "Broadcom Wi-Fi" },
    { 0x14E4, 0x43BA, "Broadcom BCM43602",   "Broadcom Wi-Fi" },
};

#define CARD_COUNT ((int)(sizeof(g_cards) / sizeof(g_cards[0])))

/* ------------------------------------------------------------------ */
/* Surucu durumu                                                       */
/* ------------------------------------------------------------------ */
static net_controller_t *g_nc = 0;
static int  g_state = ASI_WIFI_OFF;
static char g_ssid[33] = "";
static char g_card_name[48] = "";
static char g_family[24] = "";
static bool g_ready = false;

static const wifi_card_t *find_card(uint16_t vendor, uint16_t device) {
    for (int i = 0; i < CARD_COUNT; i++) {
        if (g_cards[i].vendor == vendor && g_cards[i].device == device) return &g_cards[i];
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Surucu geri çağrıları                                               */
/* ------------------------------------------------------------------ */
static int gdrv_init(net_controller_t *nc) {
    if (!nc) return -1;
    const wifi_card_t *c = find_card(nc->dev.vendor, nc->dev.device);
    if (!c) {
        kernel_debug("WiFiGen: bilinmeyen kart (%04X:%04X)",
                     nc->dev.vendor, nc->dev.device);
        return -1;
    }
    /* BAR kontrolu: kart MMIO ya da I/O erisimi sunmali */
    bool has_mem = false, has_io = false;
    for (int b = 0; b < 6; b++) {
        if (nc->bar[b].address == 0) continue;
        if (nc->bar[b].is_io) has_io = true; else has_mem = true;
    }
    if (!has_mem && !has_io) {
        kernel_debug("WiFiGen: %s icin BAR yok", c->name);
        return -1;
    }

    g_nc = nc;
    strncpy(g_card_name, c->name, sizeof(g_card_name) - 1);
    g_card_name[sizeof(g_card_name) - 1] = 0;
    strncpy(g_family, c->family, sizeof(g_family) - 1);
    g_family[sizeof(g_family) - 1] = 0;
    g_ready = true;
    g_state = ASI_WIFI_OFF;

    kernel_debug("WiFiGen: %s (%s) tanindi, BAR%s hazir",
                 g_card_name, g_family, has_mem ? " (MMIO)" : " (IO)");
    return 0;
}

static int gdrv_scan(asi_wifi_net_t *out, int max) {
    /* Donanim komut seti kart modeline ozgudur; canli donanimda
     * dogrulanmadan ag listesi UYDURULMAZ. */
    (void)out; (void)max;
    return 0;
}

static int gdrv_connect(const char *ssid, const char *pass) {
    (void)ssid; (void)pass;
    /* Bagli durumu asla uydurulmaz. */
    g_state = ASI_WIFI_FAILED;
    return -1;
}

static int gdrv_disconnect(void) {
    g_state = ASI_WIFI_OFF;
    g_ssid[0] = 0;
    return 0;
}

static int gdrv_state(void) { return g_state; }
static const char *gdrv_ssid(void) { return (g_state == ASI_WIFI_CONNECTED) ? g_ssid : ""; }

static const wifi_driver_t g_gdrv = {
    .name = "wifi-generic",
    .init = gdrv_init,
    .scan = gdrv_scan,
    .connect = gdrv_connect,
    .disconnect = gdrv_disconnect,
    .state = gdrv_state,
    .ssid = gdrv_ssid,
};

void wifi_generic_register(void) {
    wifi_register_driver(&g_gdrv);
}
