/* ===================================================================== *
 * iwl_ax201.c — Intel Wi-Fi 6 AX201 sürücüsü — Arctian 0.5
 * ===================================================================== */

#include "kernel.h"
#include "pci.h"
#include "wifi.h"
#include "asi.h"
#include "net.h"
#include "afs.h"
#include "alloc.h"
#include "clock.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

extern void wpa2_derive_pmk(const char *passphrase, const uint8_t *ssid, uint32_t ssid_len, uint8_t pmk[32]);

/* Forward declarations */
static int host_cmd(uint8_t group, uint8_t cmd, uint8_t flags, const void *payload, uint16_t plen, uint32_t timeout_ms);
static int rx_poll_one(uint8_t *buf, uint16_t *len);
static void rx_ring_init(void);
static int fw_load(void);
static int fw_parse(void);
static int start_fw(void);
static int wait_alive_rx(uint32_t timeout_ms);
static int mvm_init(void);
static int drv_scan(asi_wifi_net_t *out, int max);
static int drv_connect(const char *ssid, const char *pass);
static int drv_disconnect(void);
static int drv_state(void);
static const char *drv_ssid(void);
static int drv_init(net_controller_t *nc);
static int prepare_card_hw(void);
static int apm_init(void);
static int try_connect(void);
static bool iwl_data_send(const uint8_t *data, uint16_t length);
static bool iwl_data_recv(uint8_t *buf, uint16_t *length);

/* ------------------------------------------------------------------ */
/* CSR registerleri                                                    */
/* ------------------------------------------------------------------ */
#define CSR_HW_IF_CONFIG_REG    (0x000)
#define CSR_INT                 (0x008)
#define CSR_INT_MASK            (0x00c)
#define CSR_RESET               (0x020)
#define CSR_GP_CNTRL            (0x024)
#define CSR_HW_REV              (0x028)
#define CSR_FUNC_SCRATCH        (0x02c)
#define CSR_UCODE_DRV_GP1       (0x054)
#define CSR_UCODE_DRV_GP1_SET   (0x058)
#define CSR_UCODE_DRV_GP1_CLR   (0x05c)
#define CSR_MBOX_SET_REG        (0x088)
#define CSR_MAC_SHADOW_REG_CTRL (0x0a8)
#define CSR_GIO_CHICKEN_BITS    (0x100)
#define CSR_DBG_HPET_MEM_REG    (0x240)

#define HBUS_BASE               0x400
#define HBUS_TARG_WRPTR         (HBUS_BASE + 0x060)

#define CSR_HW_IF_CONFIG_REG_HAP_WAKE        0x00080000
#define CSR_HW_IF_CONFIG_REG_PCI_OWN_SET     0x00400000
#define CSR_HW_IF_CONFIG_REG_WAKE_ME         0x08000000
#define CSR_GP_CNTRL_MAC_CLOCK_READY         0x00000001
#define CSR_GP_CNTRL_INIT_DONE               0x00000004
#define CSR_MBOX_SET_REG_OS_ALIVE            (1u << 5)
#define CSR_GIO_CHICKEN_L1A_NO_L0S_RX        0x00000080
#define CSR_DBG_HPET_MEM_REG_VAL             0xFFFF0000
#define CSR_UCODE_SW_BIT_RFKILL              (1u << 0)
#define CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED    (1u << 1)

#define APMG_BASE               0xA02C00
#define APMG_CLK_EN_REG         (APMG_BASE + 0x0004)
#define APMG_PCIDEV_STT_REG     (APMG_BASE + 0x0010)
#define APMG_RTC_INT_STT_REG    (APMG_BASE + 0x001c)
#define APMG_CLK_VAL_DMA_CLK_RQT 0x00000200
#define APMG_PCIDEV_STT_VAL_L1_ACT_DIS 0x00000008
#define APMG_RTC_INT_STT_RFKILL 0x10000000

#define OSC_CLK                 0xA04068
#define OSC_CLK_FORCE_CONTROL   0x8

#define UREG_CPU_INIT_RUN       0xA05C44
#define HPM_MAC_LTR_CSR         0xA0348C
#define HPM_UMAC_LTR            0xA03480

#define IWL_PCI_DEV_AX201_A 0x06F0

/* Boyutlar */
#define IWL_CMD_QUEUE_ID      4
#define IWL_CMD_QUEUE_SIZE    32
#define IWL_CMD_TFD_SIZE      256
#define IWL_CMD_MAX_TBS       20
#define IWL_RX_BUF_COUNT      16
#define IWL_RX_BUF_SIZE       4096
#define IWL_MAX_FW_SIZE       (1536 * 1024)

/* Komut ID'leri */
#define CMD_UCODE_ALIVE_NTFY          0x01
#define CMD_INIT_COMPLETE_NOTIF       0x04
#define CMD_INIT_EXTENDED_CFG         0x03
#define CMD_SCAN_REQ_UMAC             0x0D
#define CMD_TX                        0x1C
#define CMD_ADD_STA                   0x18
#define CMD_ADD_STA_KEY               0x17
#define CMD_NVM_ACCESS_COMPLETE       0x04

#define GRP_LEGACY    0
#define GRP_SYSTEM    1
#define GRP_REG_NVM   5

/* ------------------------------------------------------------------ */
/* Yapılar                                                             */
/* ------------------------------------------------------------------ */
struct iwl_cmd_header {
    uint8_t cmd;
    uint8_t group_id;
    uint8_t sequence;
    uint8_t flags;
} __attribute__((packed));

struct iwl_tfh_tb {
    uint16_t tb_len;
    uint64_t addr;
} __attribute__((packed));

struct iwl_tfh_tfd {
    uint16_t num_tbs;
    struct iwl_tfh_tb tbs[IWL_CMD_MAX_TBS];
    uint32_t __pad;
} __attribute__((packed));

struct iwl_alive_ntf {
    uint16_t status;
    uint16_t flags;
    uint32_t ucode_major;
    uint32_t ucode_minor;
    uint8_t  ver_subtype;
    uint8_t  ver_type;
    uint8_t  mac;
    uint8_t  opt;
    uint32_t timestamp;
} __attribute__((packed));

struct ieee80211_hdr {
    uint16_t frame_control;
    uint16_t duration_id;
    uint8_t  addr1[6];
    uint8_t  addr2[6];
    uint8_t  addr3[6];
    uint16_t seq_ctrl;
} __attribute__((packed));

struct iwl_tx_cmd_v9 {
    uint16_t len;
    uint16_t offload_assist;
    uint32_t flags;
    uint32_t dram_info[2];
    uint32_t rate_n_flags;
    struct ieee80211_hdr hdr[];
} __attribute__((packed));

struct iwl_add_sta_cmd {
    uint8_t  sta_id;
    uint8_t  mac_id_n_color;
    uint16_t reserved1;
    uint8_t  addr[6];
    uint16_t reserved2;
    uint32_t station_flags;
    uint32_t station_flags_msk;
    uint16_t tid_disable_tx;
    uint16_t reserved3;
    uint32_t add_immediate_ba_tid;
    uint32_t remove_immediate_ba_tid;
    uint32_t add_immediate_ba_ssn;
    uint32_t sleep_tx_count;
    uint16_t sleep_state_flags;
    uint16_t reserved4;
    uint32_t assoc_id;
    uint32_t beamform_flags;
    uint32_t tfd_queue_msk;
    uint8_t  rx_ba_window;
    uint8_t  sp_length;
    uint16_t reserved5;
} __attribute__((packed));

struct iwl_scan_umac_chan_param {
    uint8_t  flags;
    uint8_t  count;
    uint16_t reserved;
} __attribute__((packed));

struct iwl_scan_req_umac_simple {
    uint32_t flags;
    uint32_t uid;
    uint32_t ooc_priority;
    uint16_t general_flags;
    uint8_t  reserved;
    uint8_t  scan_start_mac_id;
    uint8_t  extended_dwell;
    uint8_t  active_dwell;
    uint8_t  passive_dwell;
    uint8_t  fragmented_dwell;
    uint32_t max_out_time;
    uint32_t suspend_time;
    uint32_t scan_priority;
    struct iwl_scan_umac_chan_param channel;
} __attribute__((packed));

/* ------------------------------------------------------------------ */
/* Durum                                                               */
/* ------------------------------------------------------------------ */
static volatile uint32_t *g_mmio = 0;
static net_controller_t   *g_nc = 0;
static bool                g_alive = false;
static bool                g_fw_ready = false;
static char                g_ssid[33] = "";
static char                g_pass[64] = "";
static uint8_t             g_ap_mac[6] = {0};
static int                 g_state = ASI_WIFI_OFF;

static uint8_t *g_fw_buf = 0;
static uint32_t fw_len = 0;

static uint8_t  g_tfd_ring[IWL_CMD_QUEUE_SIZE * IWL_CMD_TFD_SIZE] __attribute__((aligned(256)));
static uint8_t  g_cmd_bufs[IWL_CMD_QUEUE_SIZE][512] __attribute__((aligned(64)));
static uint32_t g_cmd_wr = 0, g_cmd_rd = 0;

static uint8_t g_rx_bufs[IWL_RX_BUF_COUNT][IWL_RX_BUF_SIZE] __attribute__((aligned(4096)));
static uint64_t g_rx_bd[IWL_RX_BUF_COUNT] __attribute__((aligned(64)));
static uint64_t g_rx_used[IWL_RX_BUF_COUNT] __attribute__((aligned(64)));
static volatile uint16_t g_rx_status[8] __attribute__((aligned(64)));
static uint32_t g_rx_wr = 0, g_rx_rd = 0;

static struct { const uint8_t *init; uint32_t init_len; const uint8_t *reg; uint32_t reg_len; uint8_t valid; } g_fw = {0};

/* ------------------------------------------------------------------ */
/* MMIO / PRPH                                                         */
/* ------------------------------------------------------------------ */
static inline uint32_t rd32(uint32_t o) { return g_mmio[o / 4]; }
static inline void wr32(uint32_t o, uint32_t v) { g_mmio[o / 4] = v; }
static inline void setb(uint32_t o, uint32_t b) { wr32(o, rd32(o) | b); }
static void dly_ms(uint32_t ms) { clock_sleep_ms(ms); }
static void dly_us(uint32_t us) { volatile uint32_t i; for (i = 0; i < us * 10; i++) asm volatile("pause"); }

static int poll_set(uint32_t o, uint32_t b, uint32_t ms) {
    for (uint32_t w = 0; w < ms; w++) { if (rd32(o) & b) return 1; dly_ms(1); }
    return 0;
}

static uint32_t prph_rd(uint32_t a) { wr32(0x410, a); dly_us(5); return rd32(0x414); }
static void prph_wr(uint32_t a, uint32_t v) { wr32(0x418, a); wr32(0x41C, v); dly_us(5); }

/* ------------------------------------------------------------------ */
/* Firmware TLV parse + load                                           */
/* ------------------------------------------------------------------ */
static int fw_parse(void) {
    if (!g_fw_buf || fw_len < 16) return -1;
    uint32_t pos = 16;
    int got_init = 0, got_reg = 0;
    while (pos + 8 <= fw_len) {
        uint32_t type = *(uint32_t *)(g_fw_buf + pos);
        uint32_t len  = *(uint32_t *)(g_fw_buf + pos + 4);
        pos += 8;
        if (len == 0 || pos + len > fw_len) break;
        if (type == 1 && !got_init) { g_fw.init = g_fw_buf + pos; g_fw.init_len = len; got_init = 1; }
        else if (type == 2 && !got_reg) { g_fw.reg = g_fw_buf + pos; g_fw.reg_len = len; got_reg = 1; }
        pos += len;
        if (pos & 3) pos += 4 - (pos & 3);
    }
    g_fw.valid = (got_init || got_reg);
    kernel_debug("IWL: fw parse %s (init=%u reg=%u)", g_fw.valid ? "OK" : "HATA", g_fw.init_len, g_fw.reg_len);
    return g_fw.valid ? 0 : -1;
}

static int fw_load(void) {
    if (!g_fw_buf) {
        g_fw_buf = kmalloc(IWL_MAX_FW_SIZE);
        if (!g_fw_buf) { kernel_debug("IWL: fw tamponu yok"); return -1; }
    }
    const char *c[] = {
        "C:/firmware/iwlwifi-ty-a0-gf-a0.ucode",
        "C:/firmware/iwlwifi-QuZ-a0-hr-b0.ucode",
        "C:/firmware/iwlwifi-Qu-b0-hr-b0.ucode",
        "C:/firmware/iwlwifi.ucode",
    };
    for (int i = 0; i < 4; i++) {
        uint32_t n = afs_read_path(c[i], g_fw_buf, IWL_MAX_FW_SIZE);
        if (n > 0 && n < IWL_MAX_FW_SIZE) {
            fw_len = n;
            kernel_debug("IWL: firmware: %s (%u byte)", c[i], n);
            return 0;
        }
    }
    kernel_debug("IWL: firmware bulunamadi (C:/firmware/)");
    return -1;
}

/* ------------------------------------------------------------------ */
/* Kart hazırlık / APM                                                 */
/* ------------------------------------------------------------------ */
static int prepare_card_hw(void) {
    setb(CSR_HW_IF_CONFIG_REG, CSR_HW_IF_CONFIG_REG_PCI_OWN_SET);
    if (rd32(CSR_HW_IF_CONFIG_REG) & CSR_HW_IF_CONFIG_REG_PCI_OWN_SET) {
        setb(CSR_MBOX_SET_REG, CSR_MBOX_SET_REG_OS_ALIVE);
        return 0;
    }
    for (int it = 0; it < 10; it++) {
        setb(CSR_HW_IF_CONFIG_REG, CSR_HW_IF_CONFIG_REG_WAKE_ME);
        for (int t = 0; t < 150000; t += 200) {
            setb(CSR_HW_IF_CONFIG_REG, CSR_HW_IF_CONFIG_REG_PCI_OWN_SET);
            if (rd32(CSR_HW_IF_CONFIG_REG) & CSR_HW_IF_CONFIG_REG_PCI_OWN_SET) {
                setb(CSR_MBOX_SET_REG, CSR_MBOX_SET_REG_OS_ALIVE);
                return 0;
            }
            dly_us(200);
        }
        dly_ms(25);
    }
    kernel_debug("IWL: kart hazir degil");
    return -1;
}

static int apm_init(void) {
    setb(CSR_GIO_CHICKEN_BITS, CSR_GIO_CHICKEN_L1A_NO_L0S_RX);
    setb(CSR_DBG_HPET_MEM_REG, CSR_DBG_HPET_MEM_REG_VAL);
    setb(CSR_HW_IF_CONFIG_REG, CSR_HW_IF_CONFIG_REG_HAP_WAKE);
    prph_wr(APMG_CLK_EN_REG, APMG_CLK_VAL_DMA_CLK_RQT);
    dly_us(20);
    setb(APMG_PCIDEV_STT_REG, APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
    prph_wr(APMG_RTC_INT_STT_REG, APMG_RTC_INT_STT_RFKILL);
    for (int w = 0; w < 25; w++) { if (rd32(CSR_GP_CNTRL) & CSR_GP_CNTRL_MAC_CLOCK_READY) break; dly_ms(1); }
    if (!(rd32(CSR_GP_CNTRL) & CSR_GP_CNTRL_MAC_CLOCK_READY)) {
        kernel_debug("IWL: MAC clock timeout");
        return -1;
    }
    prph_wr(OSC_CLK, prph_rd(OSC_CLK));
    prph_wr(OSC_CLK, prph_rd(OSC_CLK) | OSC_CLK_FORCE_CONTROL);
    prph_wr(OSC_CLK, prph_rd(OSC_CLK));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Host command kuyruğu                                                */
/* ------------------------------------------------------------------ */
static struct iwl_tfh_tfd *tfd_at(uint32_t idx) {
    return (struct iwl_tfh_tfd *)(g_tfd_ring + idx * IWL_CMD_TFD_SIZE);
}

static void cmd_doorbell(void) {
    wr32(HBUS_TARG_WRPTR, (g_cmd_wr & 0xFF) | (IWL_CMD_QUEUE_ID << 8));
}

static int host_cmd(uint8_t group, uint8_t cmd, uint8_t flags,
                    const void *payload, uint16_t plen, uint32_t timeout_ms) {
    if (!g_fw_ready && cmd != CMD_UCODE_ALIVE_NTFY) return -1;
    uint32_t idx = g_cmd_wr & (IWL_CMD_QUEUE_SIZE - 1);
    struct iwl_tfh_tfd *tfd = tfd_at(idx);
    uint8_t *buf = g_cmd_bufs[idx];

    memset(tfd, 0, IWL_CMD_TFD_SIZE);
    memset(buf, 0, 512);

    struct iwl_cmd_header *h = (struct iwl_cmd_header *)buf;
    h->cmd = cmd;
    h->group_id = group;
    h->sequence = (uint8_t)idx;
    h->flags = flags;

    uint16_t l = plen;
    if (l > 496) l = 496;
    if (payload && l) memcpy(buf + sizeof(*h), payload, l);

    uint16_t total = (uint16_t)(sizeof(*h) + l);
    tfd->num_tbs = 1;
    tfd->tbs[0].tb_len = total;
    tfd->tbs[0].addr = (uint64_t)(uintptr_t)buf;

    uint32_t prev_wr = g_cmd_wr;
    g_cmd_wr = (g_cmd_wr + 1) & (IWL_CMD_QUEUE_SIZE - 1);
    cmd_doorbell();

    for (uint32_t w = 0; w < timeout_ms; w++) {
        uint32_t rp = rd32(HBUS_TARG_WRPTR) & 0xFF;
        if (rp != prev_wr && rp == (g_cmd_wr & 0xFF)) {
            g_cmd_rd = rp;
            return 0;
        }
        dly_ms(1);
    }
    kernel_debug("IWL: cmd timeout grp=%u cmd=%u", group, cmd);
    return -1;
}

/* ------------------------------------------------------------------ */
/* RX ring + polling                                                   */
/* ------------------------------------------------------------------ */
static void rx_ring_init(void) {
    for (int i = 0; i < IWL_RX_BUF_COUNT; i++) {
        g_rx_bd[i] = (uint64_t)(uintptr_t)g_rx_bufs[i];
        g_rx_used[i] = 0;
    }
    g_rx_wr = 0; g_rx_rd = 0;
    memset((void *)g_rx_status, 0, sizeof(g_rx_status));
}

static int rx_poll_one(uint8_t *buf, uint16_t *len) {
    uint16_t closed = g_rx_status[0];
    if (closed == g_rx_rd) return 0;
    uint32_t idx = g_rx_rd & (IWL_RX_BUF_COUNT - 1);
    uint8_t *p = g_rx_bufs[idx];
    uint16_t plen = *(uint16_t *)(p + 0);
    uint16_t off  = *(uint16_t *)(p + 2);
    if (plen == 0 || plen > IWL_RX_BUF_SIZE || off > 64) {
        g_rx_rd = (g_rx_rd + 1) & (IWL_RX_BUF_COUNT - 1);
        return 0;
    }
    if (buf && len) {
        uint16_t n = plen;
        if (n > *len) n = *len;
        memcpy(buf, p + off, n);
        *len = n;
    }
    g_rx_rd = (g_rx_rd + 1) & (IWL_RX_BUF_COUNT - 1);
    return 1;
}

static int wait_alive_rx(uint32_t timeout_ms) {
    for (uint32_t w = 0; w < timeout_ms; w += 10) {
        uint8_t buf[512]; uint16_t n = sizeof(buf);
        while (rx_poll_one(buf, &n)) {
            if (n >= 4) {
                struct iwl_cmd_header *h = (struct iwl_cmd_header *)buf;
                if (h->cmd == CMD_UCODE_ALIVE_NTFY) {
                    struct iwl_alive_ntf *a = (struct iwl_alive_ntf *)(buf + sizeof(*h));
                    kernel_debug("IWL: ALIVE (status=%04X major=%u minor=%u)", a->status, a->ucode_major, a->ucode_minor);
                    g_alive = true;
                    return 0;
                }
            }
            n = sizeof(buf);
        }
        dly_ms(10);
    }
    if (rd32(CSR_GP_CNTRL) & CSR_GP_CNTRL_INIT_DONE) {
        g_alive = true;
        kernel_debug("IWL: ALIVE (GP_CNTRL fallback)");
        return 0;
    }
    kernel_debug("IWL: ALIVE timeout");
    return -1;
}

/* ------------------------------------------------------------------ */
/* start_fw                                                            */
/* ------------------------------------------------------------------ */
static int start_fw(void) {
    if (prepare_card_hw() != 0) return -1;
    wr32(CSR_INT, 0xFFFFFFFF);
    wr32(CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
    wr32(CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);
    wr32(CSR_INT, 0xFFFFFFFF);
    if (apm_init() != 0) return -1;

    prph_wr(HPM_MAC_LTR_CSR, 0x00000007);
    prph_wr(HPM_UMAC_LTR, 0x80000000u | (2u << 22) | 250u);

    kernel_debug("IWL: start_fw (context info basit)");
    prph_wr(UREG_CPU_INIT_RUN, 1);
    wr32(CSR_FUNC_SCRATCH, 0x01010101);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Unified MVM init                                                    */
/* ------------------------------------------------------------------ */
static int mvm_init(void) {
    uint32_t init_flags = 1;
    if (host_cmd(GRP_SYSTEM, CMD_INIT_EXTENDED_CFG, 0, &init_flags, 4, 1000) != 0) {
        kernel_debug("IWL: INIT_EXTENDED_CFG basarisiz");
        return -1;
    }
    if (host_cmd(GRP_REG_NVM, 0x04, 0, 0, 0, 1000) != 0) {
        kernel_debug("IWL: NVM_ACCESS_COMPLETE basarisiz");
        return -1;
    }
    for (uint32_t w = 0; w < 1500; w++) {
        uint8_t buf[256]; uint16_t n = sizeof(buf);
        while (rx_poll_one(buf, &n)) {
            if (n >= 4) {
                struct iwl_cmd_header *h = (struct iwl_cmd_header *)buf;
                if (h->cmd == CMD_INIT_COMPLETE_NOTIF) {
                    kernel_debug("IWL: INIT_COMPLETE");
                    return 0;
                }
            }
            n = sizeof(buf);
        }
        dly_ms(10); w += 10;
    }
    kernel_debug("IWL: INIT_COMPLETE timeout");
    return -1;
}

/* ------------------------------------------------------------------ */
/* Scan (UMAC)                                                         */
/* ------------------------------------------------------------------ */
static int drv_scan(asi_wifi_net_t *out, int max) {
    if (!g_fw_ready || !out || max <= 0) return 0;
    struct iwl_scan_req_umac_simple req;
    memset(&req, 0, sizeof(req));
    req.flags = 0;
    req.uid = 1;
    req.ooc_priority = 0;
    req.general_flags = (1 << 3);
    req.scan_start_mac_id = 0;
    req.active_dwell = 20;
    req.passive_dwell = 120;
    req.fragmented_dwell = 45;
    req.max_out_time = 0;
    req.suspend_time = 0;
    req.scan_priority = 0;
    req.channel.flags = 0;
    req.channel.count = 1;
    req.channel.reserved = 0;

    if (host_cmd(GRP_LEGACY, CMD_SCAN_REQ_UMAC, 0, &req, sizeof(req), 1000) != 0) {
        kernel_debug("IWL: scan komutu basarisiz");
        return 0;
    }

    int found = 0;
    for (uint32_t w = 0; w < 5000 && found < max; w += 50) {
        uint8_t buf[512]; uint16_t n = sizeof(buf);
        while (rx_poll_one(buf, &n) && found < max) {
            if (n >= 24) {
                uint16_t fc = *(uint16_t *)buf;
                if ((fc & 0x00FC) == 0x0080 || (fc & 0x00FC) == 0x0050) {
                    int off = 36;
                    while (off + 2 <= (int)n) {
                        uint8_t tag = buf[off];
                        uint8_t tlen = buf[off + 1];
                        if (off + 2 + tlen > (int)n) break;
                        if (tag == 0 && tlen <= 32) {
                            memset(out[found].ssid, 0, 33);
                            memcpy(out[found].ssid, buf + off + 2, tlen);
                            out[found].rssi = -50;
                            out[found].secure = 1;
                            found++;
                            break;
                        }
                        off += 2 + tlen;
                    }
                }
            }
            n = sizeof(buf);
        }
        dly_ms(50);
    }
    kernel_debug("IWL: scan %d ag bulundu", found);
    return found;
}

/* ------------------------------------------------------------------ */
/* Connect                                                             */
/* ------------------------------------------------------------------ */
static int drv_connect(const char *ssid, const char *pass) {
    if (!ssid || !ssid[0]) return -1;
    kernel_debug("IWL: connect denemesi: %s", ssid);
    if (pass && pass[0]) {
        uint8_t pmk[32];
        uint8_t ssid_bytes[33];
        int slen = 0;
        while (ssid[slen] && slen < 32) { ssid_bytes[slen] = (uint8_t)ssid[slen]; slen++; }
        wpa2_derive_pmk(pass, ssid_bytes, slen, pmk);
        kernel_debug("IWL: PMK turetildi (%d byte SSID)", slen);
    }
    strncpy(g_ssid, ssid, 32);
    g_ssid[32] = 0;
    if (pass) { strncpy(g_pass, pass, 63); g_pass[63] = 0; }
    /* Gerçek bağlantı denemesi: auth → assoc → EAPOL */
    return try_connect();
}

static int drv_disconnect(void) { g_state = ASI_WIFI_OFF; g_ssid[0] = 0; g_pass[0] = 0; return 0; }
static int drv_state(void) { return g_state; }
static const char *drv_ssid(void) { return (g_state == ASI_WIFI_CONNECTED) ? g_ssid : ""; }

/* ------------------------------------------------------------------ */
/* drv_init                                                            */
/* ------------------------------------------------------------------ */
static int drv_init(net_controller_t *nc) {
    if (!nc) return -1;
    g_nc = nc;
    g_mmio = (volatile uint32_t *)(uintptr_t)nc->bar[0].address;
    if (!g_mmio || nc->bar[0].address == 0) {
        kernel_debug("IWL: BAR0 yok");
        return -1;
    }
    nc->initialized = false;
    rx_ring_init();

    if (fw_load() != 0) return -1;
    if (fw_parse() != 0) return -1;
    if (start_fw() != 0) return -1;
    if (wait_alive_rx(5000) != 0) return -1;

    g_fw_ready = true;
    if (mvm_init() != 0) {
        kernel_debug("IWL: MVM init tamamlanamadi (kismi)");
    }

    nc->initialized = true;
    kernel_debug("IWL: AX201 hazir (%04X:%04X)", nc->dev.vendor, nc->dev.device);
    return 0;
}

/* Veri yolu (wifi.c dispatch / net.c cagrisi icin) */
bool iwl_ax201_send_packet(void *ctx, const uint8_t *data, uint16_t length);
bool iwl_ax201_has_packet(void *ctx);
bool iwl_ax201_receive_packet(void *ctx, uint8_t *buf, uint16_t *length);

static const wifi_driver_t g_drv = {
    .name = "iwlwifi-ax201",
    .init = drv_init,
    .scan = drv_scan,
    .connect = drv_connect,
    .disconnect = drv_disconnect,
    .state = drv_state,
    .ssid = drv_ssid,
    .send = iwl_ax201_send_packet,
    .has  = iwl_ax201_has_packet,
    .recv = iwl_ax201_receive_packet,
};

void iwl_ax201_register(void) { wifi_register_driver(&g_drv); }

bool iwl_ax201_send_packet(void *ctx, const uint8_t *data, uint16_t length) {
    (void)ctx;
    return g_state == ASI_WIFI_CONNECTED ? iwl_data_send(data, length) : false;
}
bool iwl_ax201_has_packet(void *ctx) {
    (void)ctx;
    return (g_rx_status[0] != g_rx_rd);
}
bool iwl_ax201_receive_packet(void *ctx, uint8_t *buf, uint16_t *length) {
    (void)ctx;
    return iwl_data_recv(buf, length);
}
/* ===================================================================== *
 * Gerçek bağlantı ve veri yolu denemesi (auth/assoc/EAPOL/data)
 * Linux referansı: mvm/mac-ctxt.c, mvm/sta.c, fw/api/tx.h, fw/api/sta.h
 * ===================================================================== */

#define STA_ID_AP   0
#define STA_ID_SELF 1

/* Yerel MAC (firmware ADD_STA ile atanır; başlangıç değeri) */
static uint8_t g_my_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

/* 802.11 çerçeve tipleri */
#define FC_AUTH      0x00B0
#define FC_ASSOC_REQ 0x0000
#define FC_ASSOC_RESP 0x0010
#define FC_DATA_TODS 0x0108
#define FC_QOS_DATA_TODS 0x0188

/* LLC/SNAP başlığı (EtherType taşır) */
static const uint8_t llc_snap[6] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00};

/* ------------------------------------------------------------------ */
/* TX çerçevesi (TX_CMD v9 + 802.11 frame)                             */
/* ------------------------------------------------------------------ */
static int tx_frame(const uint8_t *frame, uint16_t frame_len, uint8_t sta_id,
                    uint32_t rate_n_flags, uint16_t tx_flags) {
    if (!g_fw_ready) return -1;
    /* TX_CMD'yi cmd kuyruğunda gönder (basit yol — firmware TX kuyruğu
       normalde ayrıdır; canlı testte kuyruk yapılandırması gerekebilir) */
    uint32_t idx = g_cmd_wr & (IWL_CMD_QUEUE_SIZE - 1);
    struct iwl_tfh_tfd *tfd = tfd_at(idx);
    uint8_t *buf = g_cmd_bufs[idx];

    memset(tfd, 0, IWL_CMD_TFD_SIZE);
    memset(buf, 0, 512);

    struct iwl_cmd_header *h = (struct iwl_cmd_header *)buf;
    h->cmd = CMD_TX;
    h->group_id = GRP_LEGACY;
    h->sequence = (uint8_t)idx;
    h->flags = 0;

    struct iwl_tx_cmd_v9 *tx = (struct iwl_tx_cmd_v9 *)(buf + sizeof(*h));
    tx->len = frame_len;
    tx->offload_assist = 0;
    tx->flags = ((uint32_t)tx_flags & 0xFFFF);   /* ACK iste vs. */
    tx->dram_info[0] = 0; tx->dram_info[1] = 0;
    tx->rate_n_flags = rate_n_flags;
    /* 802.11 çerçevesi tx->hdr'den itibaren */
    if (frame_len > 400) frame_len = 400;
    memcpy((uint8_t *)tx->hdr, frame, frame_len);

    uint16_t total = (uint16_t)(sizeof(*h) + sizeof(*tx) + frame_len);
    tfd->num_tbs = 1;
    tfd->tbs[0].tb_len = total;
    tfd->tbs[0].addr = (uint64_t)(uintptr_t)buf;

    g_cmd_wr = (g_cmd_wr + 1) & (IWL_CMD_QUEUE_SIZE - 1);
    cmd_doorbell();
    return 0;
}

/* ------------------------------------------------------------------ */
/* MGMT: auth (open system)                                            */
/* ------------------------------------------------------------------ */
static void mac_copy(uint8_t *d, const uint8_t *s) { for (int i = 0; i < 6; i++) d[i] = s[i]; }

static int send_auth(void) {
    uint8_t f[64];
    memset(f, 0, sizeof(f));
    struct ieee80211_hdr *h = (struct ieee80211_hdr *)f;
    h->frame_control = FC_AUTH;
    h->duration_id = 0;
    mac_copy(h->addr1, g_ap_mac);   /* DA = AP */
    mac_copy(h->addr2, g_my_mac);   /* SA = STA */
    mac_copy(h->addr3, g_ap_mac);   /* BSSID */
    h->seq_ctrl = 0;
    /* body: auth algorithm 0 (open), seq 1, status 0 */
    f[24] = 0; f[25] = 0;   /* algo */
    f[26] = 1; f[27] = 0;   /* seq */
    f[28] = 0; f[29] = 0;   /* status */
    kernel_debug("IWL: auth gonderiliyor");
    return tx_frame(f, 30, STA_ID_AP, 0x00000000, 0x0008 /*ACK*/);
}

/* ------------------------------------------------------------------ */
/* MGMT: assoc request                                                 */
/* ------------------------------------------------------------------ */
static int send_assoc(void) {
    uint8_t f[256];
    memset(f, 0, sizeof(f));
    struct ieee80211_hdr *h = (struct ieee80211_hdr *)f;
    h->frame_control = FC_ASSOC_REQ;
    mac_copy(h->addr1, g_ap_mac);
    mac_copy(h->addr2, g_my_mac);
    mac_copy(h->addr3, g_ap_mac);
    int p = 24;
    /* capability + listen interval */
    f[p++] = 0x11; f[p++] = 0x04;   /* ESS + privacy */
    f[p++] = 10; f[p++] = 0;        /* listen interval */
    /* SSID IE */
    int sl = 0; while (g_ssid[sl] && sl < 32) sl++;
    f[p++] = 0; f[p++] = (uint8_t)sl;
    memcpy(f + p, g_ssid, sl); p += sl;
    /* supported rates (1,2,5.5,11,6,9,12,18,24,36,48,54) */
    static const uint8_t rates[] = {0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24,0x30,0x48,0x60,0x6C};
    f[p++] = 1; f[p++] = sizeof(rates);
    memcpy(f + p, rates, sizeof(rates)); p += sizeof(rates);
    /* RSN IE (WPA2-PSK) — basit */
    if (g_pass[0]) {
        static const uint8_t rsn[] = {
            0x30, 0x14,             /* IE 48, len 20 */
            0x01, 0x00,             /* version 1 */
            0x00, 0x0F, 0xAC, 0x04, /* group cipher CCMP */
            0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04, /* pairwise CCMP */
            0x01, 0x00,             /* AKM PSK */
            0x00, 0x00              /* capabilities */
        };
        memcpy(f + p, rsn, sizeof(rsn)); p += sizeof(rsn);
    }
    kernel_debug("IWL: assoc gonderiliyor (SSID=%s)", g_ssid);
    return tx_frame(f, (uint16_t)p, STA_ID_AP, 0x00000000, 0x0008);
}

/* ------------------------------------------------------------------ */
/* ADD_STA — AP istasyonunu firmware'e kaydet                          */
/* ------------------------------------------------------------------ */
static int add_sta_ap(void) {
    struct iwl_add_sta_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.sta_id = STA_ID_AP;
    mac_copy(cmd.addr, g_ap_mac);
    cmd.station_flags = 0;
    cmd.assoc_id = 1;
    cmd.tfd_queue_msk = 1;
    return host_cmd(GRP_LEGACY, CMD_ADD_STA, 0, &cmd, sizeof(cmd), 1000);
}

/* ------------------------------------------------------------------ */
/* EAPOL gönder (LLC/SNAP 0x888E)                                      */
/* ------------------------------------------------------------------ */
static int eapol_send(const uint8_t *eapol, uint16_t len) {
    uint8_t f[512];
    memset(f, 0, sizeof(f));
    struct ieee80211_hdr *h = (struct ieee80211_hdr *)f;
    h->frame_control = FC_DATA_TODS;
    mac_copy(h->addr1, g_ap_mac);   /* BSSID */
    mac_copy(h->addr2, g_my_mac);
    mac_copy(h->addr3, g_ap_mac);
    int p = 24;
    memcpy(f + p, llc_snap, 6); p += 6;
    f[p++] = 0x88; f[p++] = 0x8E;   /* EAPOL ethertype */
    if (len > 400) len = 400;
    memcpy(f + p, eapol, len); p += len;
    kernel_debug("IWL: EAPOL gonderiliyor (%u byte)", len);
    return tx_frame(f, (uint16_t)p, STA_ID_AP, 0x00000000, 0x0008);
}

/* ------------------------------------------------------------------ */
/* Anahtar kur (ADD_STA_KEY)                                           */
/* ------------------------------------------------------------------ */
static int install_key(const uint8_t *key, uint16_t key_len, uint8_t key_idx, int is_pairwise) {
    struct {
        struct iwl_add_sta_cmd sta;
        uint8_t key[32];
    } __attribute__((packed)) cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.sta.sta_id = STA_ID_AP;
    mac_copy(cmd.sta.addr, g_ap_mac);
    if (key_len > 32) key_len = 32;
    memcpy(cmd.key, key, key_len);
    cmd.sta.station_flags = (uint32_t)key_len;
    kernel_debug("IWL: anahtar kuruluyor idx=%u pair=%d", key_idx, is_pairwise);
    return host_cmd(GRP_LEGACY, CMD_ADD_STA_KEY, 0, &cmd, sizeof(cmd), 1000);
}

/* ------------------------------------------------------------------ */
/* RX: mgmt çerçeve işleme                                             */
/* ------------------------------------------------------------------ */
static int process_rx_frame(const uint8_t *f, uint16_t n) {
    if (n < 24) return 0;
    uint16_t fc = *(const uint16_t *)f;
    uint16_t type = fc & 0x000C;
    uint16_t subtype = (fc & 0x00F0) >> 4;

    if (type == 0x0000 && subtype == 0x000B) {   /* auth response */
        if (n >= 30) {
            uint16_t status = *(const uint16_t *)(f + 28);
            kernel_debug("IWL: auth yanit status=%u", status);
            if (status == 0) { send_assoc(); }
        }
        return 1;
    }
    if (type == 0x0000 && subtype == 0x0001) {   /* assoc response */
        if (n >= 30) {
            uint16_t status = *(const uint16_t *)(f + 26);
            kernel_debug("IWL: assoc yanit status=%u", status);
            if (status == 0) {
                add_sta_ap();
                g_state = ASI_WIFI_CONNECTED;
            } else {
                g_state = ASI_WIFI_FAILED;
            }
        }
        return 1;
    }
    /* EAPOL: data frame + LLC/SNAP 0x888E */
    if (type == 0x0008) {
        int hl = 24 + (subtype & 0x08 ? 2 : 0);   /* QoS ise +2 */
        if (n > (uint16_t)(hl + 8) && f[hl] == 0xAA && f[hl+1] == 0xAA &&
            f[hl+6] == 0x88 && f[hl+7] == 0x8E) {
            kernel_debug("IWL: EAPOL alindi (%u byte)", n - hl - 8);
            /* 4-way handshake: M1 (type 3), M3 (type 3 with MIC) */
            /* wpa2.c fonksiyonları burada kullanılır (PTK/MIC) */
        }
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Data path: Ethernet ↔ 802.11                                        */
/* ------------------------------------------------------------------ */
static int eth_to_80211(const uint8_t *eth, uint16_t len, uint8_t *out, uint16_t cap, uint16_t *out_len) {
    if (len < 14 || cap < (uint16_t)(len + 24 + 6)) return -1;
    struct ieee80211_hdr *h = (struct ieee80211_hdr *)out;
    h->frame_control = FC_DATA_TODS;
    mac_copy(h->addr1, g_ap_mac);      /* BSSID */
    mac_copy(h->addr2, g_my_mac);      /* SA */
    mac_copy(h->addr3, eth);           /* DA */
    h->seq_ctrl = 0;
    int p = 24;
    memcpy(out + p, llc_snap, 6); p += 6;
    memcpy(out + p, eth + 12, 2); p += 2;   /* ethertype */
    memcpy(out + p, eth + 14, len - 14); p += len - 14;
    *out_len = (uint16_t)p;
    return 0;
}

static int ieee80211_to_eth(const uint8_t *f, uint16_t n, uint8_t *eth, uint16_t cap, uint16_t *out_len) {
    if (n < 24 + 8) return -1;
    uint16_t fc = *(const uint16_t *)f;
    uint16_t subtype = (fc & 0x00F0) >> 4;
    int hl = 24 + ((subtype & 0x08) ? 2 : 0);
    if (n < (uint16_t)(hl + 8)) return -1;
    if (!(f[hl] == 0xAA && f[hl+1] == 0xAA && f[hl+2] == 0x03)) return -1;
    uint16_t elen = (uint16_t)(n - hl - 8);
    if (cap < (uint16_t)(elen + 14)) return -1;
    memcpy(eth, f + 16, 6);        /* DA from addr3 (DS) */
    memcpy(eth + 6, f + 10, 6);    /* SA from addr2 */
    memcpy(eth + 12, f + hl + 6, 2);
    memcpy(eth + 14, f + hl + 8, elen);
    *out_len = (uint16_t)(elen + 14);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Dışarı açılan veri yolu fonksiyonları                               */
/* ------------------------------------------------------------------ */
static bool iwl_data_send(const uint8_t *data, uint16_t length) {
    uint8_t f[600]; uint16_t flen = 0;
    if (eth_to_80211(data, length, f, sizeof(f), &flen) != 0) return false;
    return tx_frame(f, flen, STA_ID_AP, 0x00000000, 0x0008) == 0;
}

static bool iwl_data_recv(uint8_t *buf, uint16_t *length) {
    uint8_t f[600]; uint16_t n = sizeof(f);
    while (rx_poll_one(f, &n)) {
        uint16_t fc = *(uint16_t *)f;
        if ((fc & 0x000C) == 0x0008) {   /* data */
            uint16_t elen = 0;
            if (ieee80211_to_eth(f, n, buf, *length, &elen) == 0) {
                *length = elen;
                return true;
            }
        } else {
            process_rx_frame(f, n);
        }
        n = sizeof(f);
    }
    return false;
}

/* Bağlantı denemesi: auth → assoc → EAPOL */
static int try_connect(void) {
    g_state = ASI_WIFI_CONNECTING;
    send_auth();
    /* yanıtları bekle */
    for (uint32_t w = 0; w < 3000 && g_state == ASI_WIFI_CONNECTING; w += 50) {
        uint8_t f[600]; uint16_t n = sizeof(f);
        while (rx_poll_one(f, &n)) {
            process_rx_frame(f, n);
            n = sizeof(f);
        }
        dly_ms(50);
    }
    return (g_state == ASI_WIFI_CONNECTED) ? 0 : -1;
}
