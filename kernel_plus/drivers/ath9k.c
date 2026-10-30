/* ===================================================================== *
 * ath9k.c — Atheros AR9280 / AR9285 / AR9287 Wi-Fi sürücüsü — Arctian 0.5
 * ===================================================================== *
 * Linux ath9k (v2.6.39) kaynak kodundan port edilmistir. Referans:
 *   hw.c       : RTC reset (ath9k_hw_set_reset_power_on / set_reset)
 *   pci.c      : EEPROM okuma (ath_pci_eeprom_read)
 *   mac.c      : QCU/DCU kuyruk kurulumu, AR_QTXDP / AR_Q_TXE
 *   ar9002_mac.c : descriptor doldurma/isleme (ar5416_desc)
 *   ar9002_phy.c : kanal secimi (AR_PHY_SYNTH_CONTROL + CHANSEL_2G)
 *   phy.h      : CHANSEL_2G = (freq * 0x10000) / 15
 *   eeprom.h   : 4K/9287 baslik -> MAC byte offset 10
 *
 * Destek: AR928x tek-cip aile (PCIe, EDMA'siz halka yolu).
 * Sifreleme donanim anahtar onbellegi yerine host tarafinda yapilir
 * (mbedtls CCM) — bu sayede donanim key-cache kayitlari gerekmez.
 * ===================================================================== */

#include "kernel.h"
#include "pci.h"
#include "wifi.h"
#include "asi.h"
#include "net.h"
#include "alloc.h"
#include "clock.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "mbedtls/ccm.h"

/* wpa2.c — RFC 6070 PBKDF2 + RFC 3394 AES-KW vektor testlerinden gecmis */
extern void wpa2_derive_pmk(const char *passphrase, const uint8_t *ssid,
                            uint32_t ssid_len, uint8_t pmk[32]);
extern void wpa2_derive_ptk(const uint8_t pmk[32], const uint8_t aa[6],
                            const uint8_t spa[6], const uint8_t anonce[32],
                            const uint8_t snonce[32], uint8_t ptk[64]);
extern void wpa2_mic(const uint8_t kck[16], const uint8_t *eapol,
                     uint32_t eapol_len, uint8_t mic[16]);
extern int  wpa2_unwrap_gtk(const uint8_t kek[16], const uint8_t *wrapped,
                            uint32_t wrapped_len, uint8_t *gtk,
                            uint32_t *gtk_len);

/* ===================================================================== *
 * Register tanimlari (Linux ath9k reg.h / phy.h / ar9002_phy.h)
 * ===================================================================== */
#define AR_CR                  0x0008
#define AR_CR_RXE_11N          0x0000000C   /* AR9300-sonrasi RX enable */
#define AR_CR_RXD              0x00000020
#define AR_RXDP                0x000C       /* legacy RX halka baslangici */
#define AR_CFG                 0x0014
#define AR_CFG_PHOK            0x00000100
#define AR_CFG_CLK_GATE_DIS    0x00000400
#define AR_RXBP_THRESH         0x0018
#define AR_IER                 0x0024
#define AR_IER_ENABLE          0x00000001
#define AR_TXCFG               0x0030
#define AR_TXCFG_DMASZ_32B     0x00000003
#define AR_RXCFG               0x0034
#define AR_RXCFG_ZLFDMA        0x00000010
#define AR_DATABUF_SIZE        0x0060
#define AR_DATABUF_SIZE_MASK   0x00000FFF
#define AR_GTXTO               0x0064
#define AR_GTXTO_TIMEOUT_LIMIT_S 16
#define AR_CST                 0x006C
#define AR_CST_TIMEOUT_LIMIT_S   16
#define AR_ISR                 0x0080
#define AR_IMR                 0x00A0
#define AR_QTXDP(_i)           (0x0800 + ((_i) << 2))
#define AR_Q_TXE               0x0840        /* kuyruk baslatma (1<<q) */
#define AR_Q_TXD               0x0880        /* kuyruk durdurma */
#define AR_Q_TXD_M             0x000003FF
#define AR_Q0_MISC             0x09C0
#define AR_QMISC(_i)           (AR_Q0_MISC + ((_i) << 2))
#define AR_Q_MISC_DCU_EARLY_TERM_REQ 0x00000800
#define AR_Q0_STS              0x0A00
#define AR_QSTS(_i)            (AR_Q0_STS + ((_i) << 2))
#define AR_Q_STS_PEND_FR_CNT   0x00000003
#define AR_D0_LCL_IFS          0x1040
#define AR_DLCL_IFS(_i)        (AR_D0_LCL_IFS + ((_i) << 2))
#define AR_D_LCL_IFS_CWMIN     0x000003FF
#define AR_D_LCL_IFS_CWMAX     0x000FFC00
#define AR_D_LCL_IFS_AIFS      0x0FF00000
#define AR_D0_RETRY_LIMIT      0x1080
#define AR_DRETRY_LIMIT(_i)    (AR_D0_RETRY_LIMIT + ((_i) << 2))
#define AR_D_RETRY_LIMIT_FR_SH 0x0000000F
#define AR_D_RETRY_LIMIT_STA_SH 0x00003F00
#define AR_D_RETRY_LIMIT_STA_LG 0x000FC000
#define AR_D0_CHNTIME          0x10C0
#define AR_DCHNTIME(_i)        (AR_D0_CHNTIME + ((_i) << 2))
#define AR_D0_MISC             0x1100
#define AR_DMISC(_i)           (AR_D0_MISC + ((_i) << 2))
#define AR_D_MISC_FRAG_WAIT_EN 0x00000100
#define AR_D_MISC_CW_BKOFF_EN  0x00001000

#define AR_RC                  0x4000
#define AR_RC_AHB              0x00000001
#define AR_WA                  0x4004
#define AR_WA_D3_L1_DISABLE          (1u << 14)
#define AR_WA_ASPM_TIMER_BASED_DISABLE (1u << 17)
#define AR_SREV                 0x4020
#define AR_EEPROM_STATUS_DATA   0x407C      /* AR9300-oncesi (AR928x) */
#define AR_EEPROM_STATUS_DATA_VAL  0x0000FFFF
#define AR_EEPROM_STATUS_DATA_BUSY 0x00010000
#define AR_EEPROM_STATUS_DATA_PROT_ACCESS 0x00040000
#define AR5416_EEPROM_OFFSET    0x2000
#define AR5416_EEPROM_S         2

#define AR_RTC_RC               0x7000
#define AR_RTC_RC_M             0x00000003
#define AR_RTC_RC_MAC_WARM      0x00000001
#define AR_RTC_RC_MAC_COLD      0x00000002
#define AR_RTC_RESET            0x7040
#define AR_RTC_RESET_EN         0x00000001
#define AR_RTC_STATUS           0x7044
#define AR_RTC_STATUS_M         0x0000000F
#define AR_RTC_STATUS_ON        0x00000002
#define AR_RTC_FORCE_WAKE       0x704C
#define AR_RTC_FORCE_WAKE_EN    0x00000001
#define AR_RTC_FORCE_WAKE_ON_INT 0x00000002

#define AR_RXFIFO_CFG           0x8114
#define AR_STA_ID0              0x8000      /* MAC[0..3] */
#define AR_STA_ID1              0x8004      /* MAC[4..5] + bayraklar */
#define AR_STA_ID1_SADH_MASK    0x0000FFFF
#define AR_BSS_ID0              0x8008
#define AR_BSS_ID1              0x800C
#define AR_PHY_TURBO            0x9804
#define AR_PHY_FC_HT_EN         0x00000040
#define AR_PHY_FC_WALSH         0x00000100
#define AR_PHY_FC_SINGLE_HT_LTF1 0x00000200
#define AR_PHY_SYNTH_CONTROL    0x9874
#define AR_PHY_CCK_TX_CTRL      0xA204
#define AR_PHY_CCK_TX_CTRL_JAPAN 0x00000010

/* Kanal secim formulu (Linux phy.h): */
#define CHANSEL_DIV             15
#define CHANSEL_2G(_freq)       (((_freq) * 0x10000) / CHANSEL_DIV)

/* ===================================================================== *
 * Descriptor (Linux mac.h: struct ar5416_desc, 96 byte)
 * ===================================================================== */
typedef struct {
    uint32_t ds_link;
    uint32_t ds_data;
    uint32_t ds_ctl0;
    uint32_t ds_ctl1;
    union {
        struct {
            uint32_t ctl2, ctl3, ctl4, ctl5, ctl6;
            uint32_t ctl7, ctl8, ctl9, ctl10, ctl11;
            uint32_t status0, status1, status2, status3, status4;
            uint32_t status5, status6, status7, status8, status9;
        } tx;
        struct {
            uint32_t status0, status1, status2, status3, status4;
            uint32_t status5, status6, status7, status8;
        } rx;
    } u;
} __attribute__((packed, aligned(4))) ath_desc_t;

#define ds_ctl2     u.tx.ctl2
#define ds_ctl3     u.tx.ctl3
#define ds_ctl4     u.tx.ctl4
#define ds_ctl5     u.tx.ctl5
#define ds_ctl6     u.tx.ctl6
#define ds_ctl7     u.tx.ctl7
#define ds_txstatus0 u.tx.status0
#define ds_txstatus9 u.tx.status9
#define ds_rxstatus0 u.rx.status0
#define ds_rxstatus1 u.rx.status1
#define ds_rxstatus8 u.rx.status8

/* TX alanlari */
#define AR_FrameLen           0x00000FFF
#define AR_XmitPower          0x003F0000
#define AR_XmitPower_S        16
#define AR_TxIntrReq          0x20000000
#define AR_TxMore             0x00001000
#define AR_FrameType          0x00F00000
#define AR_FrameType_S        20
#define AR_NoAck              0x01000000
#define AR_XmitDataTries0     0x000F0000
#define AR_XmitDataTries0_S   16
#define AR_XmitRate0          0x000000FF
#define AR_EncrType           0x0C000000
#define AR_EncrType_S         26
#define AR_TxDone             0x00000001
#define AR_FrmXmitOK          0x00000001

/* RX alanlari */
#define AR_BufLen             0x0FFF0000
#define AR_BufLen_S           16
#define AR_RxIntrReq          0x00002000
#define AR_DataLen            0x00000FFF
#define AR_RxMore             0x00001000
#define AR_RxRate             0xFF000000
#define AR_RxDone             0x00000001
#define AR_RxFrameOK          0x00000002
#define AR_CRCErr             0x00000004
#define AR_DecryptCRCErr      0x00000008
#define AR_PHYErr             0x00000010

/* Hiz kodlari (Linux init.c: ath9k_legacy_rates) */
#define RATE_1M   0x1B
#define RATE_2M   0x1A
#define RATE_5M5  0x19
#define RATE_11M  0x18
#define RATE_6M   0x0B
#define RATE_9M   0x0F
#define RATE_12M  0x0A
#define RATE_18M  0x0E
#define RATE_24M  0x09
#define RATE_36M  0x0D
#define RATE_48M  0x08
#define RATE_54M  0x0C

/* ===================================================================== *
 * Durum
 * ===================================================================== */
#define RX_N          32
#define TX_N          16
#define RX_BUF_SZ     2048
#define TXQ_MGMT      0       /* yonetim kareleri icin QCU 0 */
#define TXQ_DATA      1       /* veri kareleri icin QCU 1 */

static volatile uint32_t *g_mmio = 0;
static net_controller_t *g_nc = 0;
static uint8_t  g_mac[6]  = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static uint8_t  g_bssid[6];
static int      g_state   = ASI_WIFI_OFF;
static char     g_ssid[33];
static char     g_pass[64];
static int      g_channel = 1;
static bool     g_hw_up   = false;

static ath_desc_t *g_rx_desc = 0;
static uint8_t     *g_rx_buf = 0;
static int          g_rx_rd  = 0;

static ath_desc_t *g_tx_desc = 0;
static uint8_t     *g_tx_buf = 0;   /* kare verisi hazirlik tamponu */
static int          g_tx_wr   = 0;

static bool g_have_pmk = false;
static uint8_t g_pmk[32], g_ptk[64];
static uint8_t g_anonce[32], g_snonce[32];
static bool g_ptk_ready = false;
static uint32_t g_pn = 1;           /* CCMP paket numarasi */
static uint16_t g_seq = 0;          /* 802.11 dizi numarasi */

/* Kurulan anahtarlar (WPA2 handshake sonrasi) */
static uint8_t g_tk[32];            /* PTK[32..63]: CCMP unicast TK */
static uint8_t g_gtk[16];           /* GTK: CCMP grup (multicast) */
static bool    g_have_gtk = false;

/* Son islenen karenin sinyal gucu (dBm, yaklasik) */
static int g_last_rssi = -90;

/* ===================================================================== *
 * MMIO yardimcilari
 * ===================================================================== */
static inline uint32_t rd(uint32_t a)           { return g_mmio[a >> 2]; }
static inline void     wr(uint32_t a, uint32_t v) { g_mmio[a >> 2] = v; }

static void udelay(uint32_t us) {
    volatile uint32_t i;
    for (i = 0; i < us * 10; i++) asm volatile("pause");
}
static void msleep(uint32_t ms) { clock_sleep_ms(ms); }

/* Kayit bekleme — Linux ath9k_hw_wait karsiligi */
static int wait_mask(uint32_t a, uint32_t mask, uint32_t want, int tries) {
    int i;
    for (i = 0; i < tries; i++) {
        if ((rd(a) & mask) == want) return 0;
        udelay(10);
    }
    return -1;
}

/* ===================================================================== *
 * EEPROM okuma — Linux pci.c: ath_pci_eeprom_read
 * ===================================================================== */
static int ee_read(int off, uint16_t *v) {
    rd(AR5416_EEPROM_OFFSET + ((uint32_t)off << AR5416_EEPROM_S)); /* tetik */
    if (wait_mask(AR_EEPROM_STATUS_DATA,
                  AR_EEPROM_STATUS_DATA_BUSY | AR_EEPROM_STATUS_DATA_PROT_ACCESS,
                  0, 2000) != 0) return -1;
    *v = (uint16_t)(rd(AR_EEPROM_STATUS_DATA) & AR_EEPROM_STATUS_DATA_VAL);
    return 0;
}

/* EEPROM'dan MAC adresi: 4K ve 9287 basliklarinda byte offset 10 */
static int read_mac(void) {
    uint16_t w5, w6, w7;
    int i, nz = 0;
    if (ee_read(5, &w5) || ee_read(6, &w6) || ee_read(7, &w7)) return -1;
    g_mac[0] = (uint8_t)(w5 & 0xFF);  g_mac[1] = (uint8_t)(w5 >> 8);
    g_mac[2] = (uint8_t)(w6 & 0xFF);  g_mac[3] = (uint8_t)(w6 >> 8);
    g_mac[4] = (uint8_t)(w7 & 0xFF);  g_mac[5] = (uint8_t)(w7 >> 8);
    g_mac[0] &= ~0x01;               /* tekil adres biti */
    for (i = 0; i < 6; i++) if (g_mac[i]) nz = 1;
    if (!nz || (g_mac[0] == 0xFF)) return -1;
    return 0;
}

/* ===================================================================== *
 * Reset — Linux hw.c: ath9k_hw_set_reset_power_on + ath9k_hw_set_reset
 * ===================================================================== */
static int hw_reset(void) {
    /* 1) Wake + AHB erisim yolu ac */
    wr(AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);
    wr(AR_RC, AR_RC_AHB);
    wr(AR_RTC_RESET, 0);
    udelay(2);
    wr(AR_RC, 0);
    wr(AR_RTC_RESET, AR_RTC_RESET_EN);
    if (wait_mask(AR_RTC_STATUS, AR_RTC_STATUS_M, AR_RTC_STATUS_ON, 2000) != 0) {
        kernel_debug("ATH9K: RTC uyanamadi");
        return -1;
    }

    /* 2) MAC warm reset */
    wr(AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);
    wr(AR_RC, AR_RC_AHB);
    wr(AR_RTC_RC, AR_RTC_RC_MAC_WARM);
    udelay(50);
    wr(AR_RTC_RC, 0);
    if (wait_mask(AR_RTC_RC, AR_RTC_RC_M, 0, 2000) != 0) {
        kernel_debug("ATH9K: MAC reset takildi");
        return -1;
    }
    wr(AR_RC, 0);

    /* 3) ASPM kapat (donanim ASLEEP moduna gecmesin) */
    wr(AR_WA, rd(AR_WA) | AR_WA_D3_L1_DISABLE | AR_WA_ASPM_TIMER_BASED_DISABLE);
    udelay(10);
    wr(AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);
    return 0;
}

/* ===================================================================== *
 * Kanal secimi — Linux ar9002_phy.c: ar9002_hw_set_channel (2 GHz)
 * ===================================================================== */
static int hw_set_channel(int ch) {
    uint32_t freq, reg32;
    int chansel;

    if (ch < 1 || ch > 14) return -1;
    freq = (ch == 14) ? 2484 : (2407 + ch * 5);
    g_channel = ch;

    /* MAC zaman asimlari + PHY mod (11n-20MHz acik) */
    wr(AR_PHY_TURBO, AR_PHY_FC_HT_EN | AR_PHY_FC_WALSH | AR_PHY_FC_SINGLE_HT_LTF1);
    wr(AR_GTXTO, 25u << AR_GTXTO_TIMEOUT_LIMIT_S);
    wr(AR_CST, 0xFu << AR_CST_TIMEOUT_LIMIT_S);

    /* CCK TX kontrolu: 14. kanalda Japonya yayilimi */
    if (ch == 14) wr(AR_PHY_CCK_TX_CTRL, rd(AR_PHY_CCK_TX_CTRL) | AR_PHY_CCK_TX_CTRL_JAPAN);
    else          wr(AR_PHY_CCK_TX_CTRL, rd(AR_PHY_CCK_TX_CTRL) & ~AR_PHY_CCK_TX_CTRL_JAPAN);

    /* 2 GHz kesirli mod (ar9002_hw_set_channel 2 GHz dallari) */
    chansel = CHANSEL_2G((int)freq);
    reg32 = rd(AR_PHY_SYNTH_CONTROL) & 0xC0000000u;
    reg32 |= (1u << 29)             /* bMode  = 1 (2 GHz)   */
           | (1u << 28)             /* fracMode = 1         */
           | (0u << 26)             /* aModeRefSel = 0      */
           | ((uint32_t)chansel & 0x03FFFFFFu);
    wr(AR_PHY_SYNTH_CONTROL, reg32);

    kernel_debug("ATH9K: kanal %d (%u MHz) ayarlandi", ch, freq);
    return 0;
}

/* ===================================================================== *
 * QCU/DCU kuyruk kurulumu — Linux mac.c: ath9k_hw_reset_txqueue
 * ===================================================================== */
static void qcu_setup(int q) {
    /* IFS: cwmin=15, cwmax=1023, aifs=2 */
    wr(AR_DLCL_IFS(q), (15u & AR_D_LCL_IFS_CWMIN)
                     | (1023u << 10)
                     | (2u << 20));
    /* Limit: sta-sh=32, sta-lg=32, fr-sh=7 */
    wr(AR_DRETRY_LIMIT(q), (7u & AR_D_RETRY_LIMIT_FR_SH)
                         | (32u << 8)
                         | (32u << 14));
    wr(AR_QMISC(q), AR_Q_MISC_DCU_EARLY_TERM_REQ);
    wr(AR_DMISC(q), AR_D_MISC_CW_BKOFF_EN | AR_D_MISC_FRAG_WAIT_EN | 0x2);
    wr(AR_DCHNTIME(q), 0);
}

/* ===================================================================== *
 * Halka kurulumu
 * ===================================================================== */
static int rx_ring_init(void) {
    int i;
    g_rx_desc = (ath_desc_t *)kmalloc((uint32_t)(RX_N * (int)sizeof(ath_desc_t)));
    g_rx_buf  = (uint8_t *)kmalloc((uint32_t)(RX_N * RX_BUF_SZ));
    if (!g_rx_desc || !g_rx_buf) { kernel_debug("ATH9K: RX halka alloc hatasi"); return -1; }
    memset(g_rx_desc, 0, (size_t)(RX_N * (int)sizeof(ath_desc_t)));

    for (i = 0; i < RX_N; i++) {
        ath_desc_t *d = &g_rx_desc[i];
        d->ds_link = (uint32_t)(uintptr_t)&g_rx_desc[(i + 1) % RX_N];
        d->ds_data = (uint32_t)(uintptr_t)(g_rx_buf + (i * RX_BUF_SZ));
        /* ar9002_hw_setuprxdesc: ds_ctl1 = size & AR_BufLen (0x0fff) */
        d->ds_ctl1 = (uint32_t)RX_BUF_SZ & 0x0FFFu;
        d->ds_rxstatus8 = 0;       /* AR_RxDone temiz: donanim doldurabilir */
    }
    wr(AR_RXDP, (uint32_t)(uintptr_t)&g_rx_desc[0]);
    kernel_debug("ATH9K: RX halkasi hazir (%d desc x %d bayt)", RX_N, RX_BUF_SZ);
    return 0;
}

static int tx_ring_init(void) {
    g_tx_desc = (ath_desc_t *)kmalloc((uint32_t)(TX_N * (int)sizeof(ath_desc_t)));
    g_tx_buf  = (uint8_t *)kmalloc(4096);
    if (!g_tx_desc || !g_tx_buf) { kernel_debug("ATH9K: TX halka alloc hatasi"); return -1; }
    memset(g_tx_desc, 0, (size_t)(TX_N * (int)sizeof(ath_desc_t)));
    qcu_setup(TXQ_MGMT);
    qcu_setup(TXQ_DATA);
    kernel_debug("ATH9K: TX halkasi hazir (%d desc, QCU %d/%d)", TX_N, TXQ_MGMT, TXQ_DATA);
    return 0;
}

/* ===================================================================== *
 * 802.11 kare gonderimi (tek parca, dogrudan donanima)
 * ===================================================================== */
static int tx_frame_raw(const uint8_t *frame, int len, int q, int noack,
                        int retries) {
    ath_desc_t *d;
    uint32_t t;

    if (!g_hw_up || !frame || len < 10 || len > 4000) return -1;

    d = &g_tx_desc[g_tx_wr];
    g_tx_wr = (g_tx_wr + 1) % TX_N;

    memcpy(g_tx_buf, frame, (size_t)len);

    /* Tum durum alanlari temiz (Linux ath9k_hw_cleartxdesc) */
    d->ds_txstatus0 = 0;
    d->u.tx.status1 = d->u.tx.status2 = d->u.tx.status3 = 0;
    d->u.tx.status4 = d->u.tx.status5 = d->u.tx.status6 = 0;
    d->u.tx.status7 = d->u.tx.status8 = d->ds_txstatus9 = 0;

    d->ds_link = 0;                                  /* zincir sonu (EOL) */
    d->ds_data = (uint32_t)(uintptr_t)g_tx_buf;
    /* ds_ctl0: uzunluk + guc(63) ; ds_ctl1: tip + segment uzunlugu */
    d->ds_ctl0 = ((uint32_t)len & AR_FrameLen) | (63u << AR_XmitPower_S);
    d->ds_ctl1 = (0u << AR_FrameType_S)              /* ATH9K_PKT_TYPE_NORMAL */
               | (uint32_t)len;
    if (noack) d->ds_ctl1 |= AR_NoAck;
    d->ds_ctl2 = ((uint32_t)(retries > 0 ? retries : 7) << AR_XmitDataTries0_S);
    d->ds_ctl3 = RATE_1M;                            /* oncelikle guvenli CCK */
    d->ds_ctl4 = 0;
    d->ds_ctl5 = 0;
    d->ds_ctl6 = 0;                                  /* EncrType: NONE (host CCMP) */
    d->ds_ctl7 = 0;

    /* Donanima ver: QTXDP + TXE (Linux ath9k_hw_puttxbuf + hw_txstart) */
    wr(AR_QTXDP(q), (uint32_t)(uintptr_t)d);
    wr(AR_Q_TXE, 1u << q);

    /* Tamamlanma bekle (AR_TxDone, ds_txstatus9) */
    for (t = 0; t < 2000; t++) {
        if (d->ds_txstatus9 & AR_TxDone) {
            if (d->ds_txstatus0 & AR_FrmXmitOK) return 0;
            /* ACK gelmedi: yine de kare gonderilmistir (noack/mgmt icin normal) */
            return 1;
        }
        udelay(20);
    }
    kernel_debug("ATH9K: TX zaman asimi (q=%d, len=%d)", q, len);
    return -1;
}

/* ===================================================================== *
 * Alim — donanim halkasindan bir 802.11 karesi oku
 * ===================================================================== */
static int rx_frame_raw(uint8_t *out, int *out_len, int *out_rate) {
    ath_desc_t *d;
    uint32_t s8, dlen;

    if (!g_hw_up) return 0;
    d = &g_rx_desc[g_rx_rd];
    s8 = d->ds_rxstatus8;
    if (!(s8 & AR_RxDone)) return 0;
    if (s8 & (AR_CRCErr | AR_PHYErr | AR_DecryptCRCErr)) {
        /* bozuk kareyi geri ver */
        d->ds_rxstatus8 = 0;
        g_rx_rd = (g_rx_rd + 1) % RX_N;
        wr(AR_RXDP, (uint32_t)(uintptr_t)d);
        return 0;
    }
    if (!(s8 & AR_RxFrameOK)) {
        d->ds_rxstatus8 = 0;
        g_rx_rd = (g_rx_rd + 1) % RX_N;
        wr(AR_RXDP, (uint32_t)(uintptr_t)d);
        return 0;
    }

    dlen = d->ds_rxstatus1 & AR_DataLen;
    if (dlen > (uint32_t)RX_BUF_SZ) dlen = RX_BUF_SZ;
    if (dlen < 10) dlen = 10;
    if (out) memcpy(out, (const void *)(uintptr_t)d->ds_data, (size_t)dlen);
    if (out_len) *out_len = (int)dlen;
    if (out_rate) *out_rate = (int)((d->ds_rxstatus0 & AR_RxRate) >> 24);
    {   /* sinyal gucu: ds_rxstatus0 dusuk byte (AR_RxRSSIAnt00) */
        int raw = (int)(d->ds_rxstatus0 & 0xFFu);
        if (raw > 127) raw -= 256;          /* iki'nin tamamlayicisi olabilir */
        if (raw > -30) raw -= 95;           /* ham olcek: dBm'e yaklasik cevir */
        if (raw < -99) raw = -99;
        g_last_rssi = raw;
    }

    /* kareyi tekrar donanima ver (ring ilerlet) */
    d->ds_rxstatus8 = 0;
    g_rx_rd = (g_rx_rd + 1) % RX_N;
    wr(AR_RXDP, (uint32_t)(uintptr_t)d);
    return 1;
}

/* ===================================================================== *
 * 802.11 kare kurulumu
 * ===================================================================== */
#define FC_ASSOC_REQ   0x0000
#define FC_ASSOC_RESP  0x0010
#define FC_PROBE_REQ   0x0040
#define FC_PROBE_RESP  0x0050
#define FC_BEACON      0x0080
#define FC_AUTH        0x00B0
#define FC_DEAUTH      0x00C0
#define FC_DATA_TODS   0x0108

#define FC_PROTECTED   0x4000

static void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static uint16_t g16(const uint8_t *p)   { return (uint16_t)(p[0] | (p[1] << 8)); }

static void mac_cpy(uint8_t *d, const uint8_t *s) { int i; for (i = 0; i < 6; i++) d[i] = s[i]; }
static int  mac_eq(const uint8_t *a, const uint8_t *b) {
    int i; for (i = 0; i < 6; i++) if (a[i] != b[i]) return 0; return 1;
}

static const uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static const uint8_t LLC_SNAP[8] = {0xAA,0xAA,0x03,0x00,0x00,0x00,0x88,0x8E};

/* Yonetim kare basligi: fc, dur, a1, a2, a3, seq */
static int mgmt_hdr(uint8_t *f, uint16_t fc, const uint8_t *a1,
                    const uint8_t *a3) {
    p16(f + 0, fc);
    p16(f + 2, 0);
    mac_cpy(f + 4, a1);
    mac_cpy(f + 10, g_mac);
    mac_cpy(f + 16, a3);
    p16(f + 22, (uint16_t)((g_seq++ & 0x0FFF) << 4));
    return 24;
}

/* --- Dogrulama (auth) karesi --- */
static int build_auth(uint8_t *f, const uint8_t *bssid, int seq_no) {
    int n = mgmt_hdr(f, FC_AUTH, bssid, bssid);
    p16(f + n, 0);   n += 2;   /* algoritma: acik sistem */
    p16(f + n, (uint16_t)seq_no); n += 2;
    p16(f + n, 0);   n += 2;   /* durum: basarili */
    return n;
}

/* --- Birlestirme (assoc) istegi --- */
static int build_assoc_req(uint8_t *f, const uint8_t *bssid,
                           const char *ssid, int has_rsn, const uint8_t *rsn,
                           int rsn_len) {
    int n = mgmt_hdr(f, FC_ASSOC_REQ, bssid, bssid);
    p16(f + n, 0x0411); n += 2;   /* capability: ESS + Privacy + ShortSlot */
    p16(f + n, 10);     n += 2;   /* dinleme araligi */
    /* SSID */
    {
        int l = 0; while (ssid[l] && l < 32) l++;
        f[n++] = 0; f[n++] = (uint8_t)l;
        memcpy(f + n, ssid, (size_t)l); n += l;
    }
    /* desteklenen hiz seti (1-54 Mbps) */
    f[n++] = 1; f[n++] = 8;
    f[n++] = 0x82; f[n++] = 0x84; f[n++] = 0x8B; f[n++] = 0x96;
    f[n++] = 0x0C; f[n++] = 0x12; f[n++] = 0x18; f[n++] = 0x24;
    if (has_rsn && rsn && rsn_len > 0 && rsn_len < 64) {
        f[n++] = 48; f[n++] = (uint8_t)rsn_len;
        memcpy(f + n, rsn, (size_t)rsn_len); n += rsn_len;
    }
    return n;
}

/* ===================================================================== *
 * Kare ayrastirma (beacon/probe resp)
 * ===================================================================== */
typedef struct {
    uint8_t  bssid[6];
    char     ssid[33];
    int      rssi;
    int      secure;
    int      channel;
    uint8_t  rsn[64];      /* beacon'daki RSN IE (M2/assoc icin) */
    uint8_t  rsn_len;
} parsed_net_t;

/* IE'lerden SSID + RSN/WPA + kanal bilgisini cikar */
static void parse_ies(const uint8_t *ies, int len, parsed_net_t *n) {
    int i = 0;
    while (i + 2 <= len) {
        uint8_t id = ies[i], elen = ies[i + 1];
        if (i + 2 + elen > len) break;
        if (id == 0 && elen > 0 && n->ssid[0] == 0) {
            int k, l = elen > 32 ? 32 : elen;
            for (k = 0; k < l; k++) n->ssid[k] = (char)ies[i + 2 + k];
            n->ssid[l] = 0;
        } else if (id == 48) {            /* RSN (WPA2) */
            n->secure = 1;
            if (elen > 0 && elen <= 64 && n->rsn_len == 0) {
                memcpy(n->rsn, &ies[i + 2], (size_t)elen);
                n->rsn_len = elen;
            }
        } else if (id == 221 && elen >= 4 &&      /* WPA (vendor) */
                   ies[i+2] == 0x00 && ies[i+3] == 0x50 &&
                   ies[i+4] == 0xF2 && ies[i+5] == 0x01) {
            n->secure = 1;
        } else if (id == 3 && elen == 1) {        /* DS parametresi */
            n->channel = ies[i + 2];
        }
        i += 2 + elen;
    }
}

/* Beacon/probe resp karesini ayrastir; basariya 1 doner */
static int parse_beacon(const uint8_t *f, int len, int rssi, parsed_net_t *out) {
    int ieoff, ielen;
    if (len < 36) return 0;
    {
        uint16_t fc = g16(f);
        if ((fc & 0x00FC) != FC_BEACON && (fc & 0x00FC) != FC_PROBE_RESP) return 0;
    }
    memset(out, 0, sizeof(*out));
    mac_cpy(out->bssid, f + 16);
    out->rssi = rssi;
    ieoff = 24 + 12;                 /* baslik + timestamp/int/cap */
    ielen = len - ieoff;
    if (ielen < 0) return 0;
    parse_ies(f + ieoff, ielen, out);
    if (out->ssid[0] == 0) return 0;
    return 1;
}

/* ===================================================================== *
 * Kare bekleme (tarama/handshake sirasinda RX turu)
 * ===================================================================== */
static int wait_frame(uint16_t fc_mask, uint16_t fc_val, uint8_t *buf,
                      int *len, int timeout_ms) {
    uint8_t tmp[2048];
    int t;
    for (t = 0; t < timeout_ms / 2; t++) {     /* her adim ~2 ms */
        uint8_t *p = buf ? buf : tmp;
        int rl = 0, rate = 0;
        if (rx_frame_raw(p, &rl, &rate) == 1) {
            if (rl >= 2 && (g16(p) & fc_mask) == fc_val) {
                if (len) *len = rl;
                return 1;
            }
        }
        clock_sleep_ms(2);
    }
    return 0;
}

/* ===================================================================== *
 * CCMP (AES-CCM) — host tarafli sifreleme
 * ===================================================================== */
static void ccmp_nonce(uint8_t nonce[13], const uint8_t *addr2,
                       const uint8_t pn[6]) {
    nonce[0] = 0;                       /* oncelik (TID) */
    memcpy(nonce + 1, addr2, 6);
    nonce[7]  = pn[5];                  /* buyuk sonlu PN */
    nonce[8]  = pn[4];
    nonce[9]  = pn[3];
    nonce[10] = pn[2];
    nonce[11] = pn[1];
    nonce[12] = pn[0];
}

/* CCMP AAD: 2 byte buyuk sonlu uzunluk + FC + A1..A3 + SC */
static int ccmp_aad(uint8_t *aad, const uint8_t *f, int hdrlen) {
    uint16_t fc = g16(f);
    int n = 0;
    /* Maskelenmis FC: MoreFrag/Retry/PwrMgmt/MoreData/Order temizlenir;
     * Protected (0x4000) SAKLANIR — hem TX hem RX ayni degeri kullanir. */
    fc &= (uint16_t)~0xBC00;
    aad[0] = 0; aad[1] = 0;             /* uzunluk sonradan yazilir */
    p16(aad + 2, fc);
    memcpy(aad + 4, f + 4, 6);          /* A1 — grup biti temiz */
    aad[4] &= (uint8_t)~0x01;
    memcpy(aad + 10, f + 10, 6);        /* A2 */
    memcpy(aad + 16, f + 16, 6);        /* A3 */
    p16(aad + 22, (uint16_t)(g16(f + 22) & 0xFFF0)); /* SC: frag=0 */
    n = 24;
    if (hdrlen > 24) {                  /* A4 (ToDS+FromDS) */
        memcpy(aad + 24, f + 24, 6);
        n = 30;
    }
    p16(aad, (uint16_t)(n - 2));
    return n;
}

/* Veri karesini CCMP ile sifrele: kare [hdrlen..] bolusu dondurulur.
 * basari = 0; cikti: 802.11 baslik (Protected biti set) + CCMP hdr +
 * sifreli govde + 8 byte MIC. */
static int ccmp_encrypt(const uint8_t *tk, uint8_t *frame, int hdrlen,
                        int *len, uint32_t pn) {
    uint8_t nonce[13], aad[40], hdr[40];
    uint8_t pn_b[6];
    uint8_t cc[8];
    int body = *len - hdrlen, aad_len, i;
    mbedtls_ccm_context ctx;
    uint8_t tmp[1400];

    if (body <= 0 || body + 8 > (int)sizeof(tmp)) return -1;

    /* PN bayt duzeni: [0]=LSB ... [5]=MSB */
    pn_b[0] = (uint8_t)(pn & 0xFF);       pn_b[1] = (uint8_t)((pn >> 8) & 0xFF);
    pn_b[2] = (uint8_t)((pn >> 16) & 0xFF); pn_b[3] = (uint8_t)((pn >> 24) & 0xFF);
    pn_b[4] = (uint8_t)((pn >> 32) & 0xFF); pn_b[5] = (uint8_t)((pn >> 40) & 0xFF);

    /* Protected biti + CCMP basligi */
    frame[1] |= (uint8_t)(FC_PROTECTED >> 8);
    for (i = 0; i < hdrlen && i < 40; i++) hdr[i] = frame[i];
    hdr[hdrlen + 0] = pn_b[0];
    hdr[hdrlen + 1] = 0;
    hdr[hdrlen + 2] = 0x20;               /* ExtIV | KeyID 0 */
    hdr[hdrlen + 3] = pn_b[1];
    hdr[hdrlen + 4] = pn_b[2];
    hdr[hdrlen + 5] = pn_b[3];
    hdr[hdrlen + 6] = pn_b[4];
    hdr[hdrlen + 7] = pn_b[5];

    ccmp_nonce(nonce, frame + 10, pn_b);
    aad_len = ccmp_aad(aad, frame, hdrlen);

    mbedtls_ccm_init(&ctx);
    if (mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, tk, 128) != 0 ||
        mbedtls_ccm_encrypt_and_tag(&ctx, (size_t)body, nonce, 13,
                                    aad, (size_t)aad_len,
                                    (const unsigned char *)(frame + hdrlen),
                                    tmp, cc, 8) != 0) {
        mbedtls_ccm_free(&ctx);
        return -1;
    }
    mbedtls_ccm_free(&ctx);

    /* CCMP(8) + sifreli govde + MIC(8) kareye yaz */
    memcpy(frame + hdrlen, hdr + hdrlen, 8);
    memcpy(frame + hdrlen + 8, tmp, (size_t)body);
    memcpy(frame + hdrlen + 8 + body, cc, 8);
    *len = hdrlen + 8 + body + 8;
    return 0;
}

/* Gelen sifreli veri karesini coz (basari = 0) */
static int ccmp_decrypt(const uint8_t *tk, uint8_t *frame, int hdrlen,
                        int *len) {
    uint8_t nonce[13], aad[40], pn_b[6], tag[8];
    int body = *len - hdrlen - 8, aad_len, i;
    mbedtls_ccm_context ctx;
    uint8_t tmp[1400];

    if (body <= 0 || body > (int)sizeof(tmp)) return -1;

    pn_b[0] = frame[hdrlen + 0];
    pn_b[1] = frame[hdrlen + 3];
    pn_b[2] = frame[hdrlen + 4];
    pn_b[3] = frame[hdrlen + 5];
    pn_b[4] = frame[hdrlen + 6];
    pn_b[5] = frame[hdrlen + 7];

    ccmp_nonce(nonce, frame + 10, pn_b);
    aad_len = ccmp_aad(aad, frame, hdrlen);

    mbedtls_ccm_init(&ctx);
    if (mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, tk, 128) != 0 ||
        mbedtls_ccm_auth_decrypt(&ctx, (size_t)body, nonce, 13,
                                 aad, (size_t)aad_len,
                                 (const unsigned char *)(frame + hdrlen + 8),
                                 tmp, tag, 8) != 0) {
        mbedtls_ccm_free(&ctx);
        return -1;   /* MIC hatasi: kare atilir */
    }
    mbedtls_ccm_free(&ctx);

    /* Protected biti temizle, govdeyi geri yaz */
    frame[1] &= (uint8_t)~(FC_PROTECTED >> 8);
    for (i = 0; i < hdrlen; i++) { /* baslik korunur */ (void)i; }
    memcpy(frame + hdrlen, tmp, (size_t)body);
    *len = hdrlen + body;
    return 0;
}

/* ===================================================================== *
 * MAC / BSSID yazma — Linux hw.c: ath9k_hw_write_associd
 * ===================================================================== */
static void set_mac(const uint8_t *m) {
    wr(AR_STA_ID0, (uint32_t)m[0] | ((uint32_t)m[1] << 8) |
                   ((uint32_t)m[2] << 16) | ((uint32_t)m[3] << 24));
    wr(AR_STA_ID1, (rd(AR_STA_ID1) & ~0xFFFFu) |
                   (uint32_t)m[4] | ((uint32_t)m[5] << 8));
}

static void set_bssid(const uint8_t *b, int aid) {
    wr(AR_BSS_ID0, (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                   ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24));
    wr(AR_BSS_ID1, (uint32_t)(b[4] | (b[5] << 8)) |
                   (((uint32_t)aid & 0x3FFFu) << 16));   /* AR_BSS_ID1_AID_S=16 */
}

/* ===================================================================== *
 * Desteklenen cipler: AR928x tek-cip aile (EDMA'siz halka yolu)
 * AR9300 (0x0030/32/33/34/35/3A) EDMA kullanir → bilerek disari.
 * ===================================================================== */
static int dev_supported(uint16_t device) {
    switch (device) {
    case 0x002A: case 0x002B: case 0x002C:
    case 0x002D: case 0x002E:                 /* AR9280 (2.4/5 GHz) */
    case 0x0036: case 0x0039:                 /* AR9285 (2.4 GHz 1x1) */
    case 0x0037: case 0x0038:                 /* AR9287 (2.4 GHz 2x2) */
        return 1;
    default:
        return 0;
    }
}

/* ===================================================================== *
 * Surucu baslatma
 * ===================================================================== */
static int drv_init(net_controller_t *nc) {
    if (!nc) return -1;
    if (nc->dev.vendor != 0x168C) return -1;          /* Atheros */
    if (!dev_supported(nc->dev.device)) return -1;
    if (nc->bar[0].is_io || nc->bar[0].address == 0) return -1;

    g_nc = nc;
    g_mmio = (volatile uint32_t *)(uintptr_t)nc->bar[0].address;

    kernel_debug("ATH9K: kart %04X:%04X BAR0=0x%llx",
                 nc->dev.vendor, nc->dev.device,
                 (unsigned long long)nc->bar[0].address);

    /* Bus mastering + bellek alani (PCI komut yazmaci 0x04) */
    {
        uint32_t cmd = pci_config_readl(nc->dev.bus, nc->dev.slot,
                                        nc->dev.func, 0x04);
        cmd |= PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER;
        cmd &= ~(uint32_t)PCI_CMD_INT_DISABLE;
        pci_config_writel(nc->dev.bus, nc->dev.slot, nc->dev.func, 0x04, cmd);
    }

    if (hw_reset() != 0) { kernel_debug("ATH9K: reset basarisiz"); return -1; }

    if (read_mac() != 0)
        kernel_debug("ATH9K: EEPROM MAC okunamadi, varsayilan kullanilir");
    else
        kernel_debug("ATH9K: EEPROM MAC %02x:%02x:%02x:%02x:%02x:%02x",
                     g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);

    /* DMA atama boyutu 128B (hw.c: ath9k_hw_init_global_settings) */
    wr(AR_TXCFG, (rd(AR_TXCFG) & ~7u) | 5u);       /* AR_TXCFG_DMASZ_128B */
    wr(AR_RXCFG, (rd(AR_RXCFG) & ~7u) | 5u);       /* AR_RXCFG_DMASZ_128B */
    wr(AR_RXFIFO_CFG, 0x200);                      /* hw.c:941 */
    wr(AR_CFG, AR_CFG_PHOK | AR_CFG_CLK_GATE_DIS);

    if (rx_ring_init() != 0 || tx_ring_init() != 0) return -1;

    set_mac(g_mac);
    set_bssid(BCAST, 0);
    if (hw_set_channel(1) != 0) return -1;

    /* Alimi baslat — Linux ar9002_hw_rx_enable: AR_CR = AR_CR_RXE */
    wr(AR_CR, 0x00000004);

    g_hw_up = true;
    g_state = ASI_WIFI_OFF;
    kernel_debug("ATH9K: hazir (halkalar: RX %d / TX %d, kanal 1)", RX_N, TX_N);
    return 0;
}

/* ===================================================================== *
 * Tarama — gercek beacon alimi (suni ag listesi YOK)
 * ===================================================================== */
static parsed_net_t g_nets[16];
static int          g_nets_n = 0;

static int drv_scan(asi_wifi_net_t *out, int max) {
    int ch, i, nfound = 0;
    uint8_t f[2048];

    if (!g_hw_up || !out || max <= 0) return 0;
    g_nets_n = 0;
    set_bssid(BCAST, 0);                    /* tum BSSID'leri kabul et */
    kernel_debug("ATH9K: tarama 1-14 kanal");

    for (ch = 1; ch <= 14; ch++) {
        if (hw_set_channel(ch) != 0) continue;
        msleep(30);
        for (i = 0; i < 150; i++) {         /* ~300 ms beacon penceresi */
            int n = 0, rate = 0, k, dup = 0;
            parsed_net_t p;
            if (rx_frame_raw(f, &n, &rate) != 1) { msleep(2); continue; }
            if (!parse_beacon(f, n, g_last_rssi, &p)) continue;
            if (p.channel == 0) p.channel = ch;
            for (k = 0; k < g_nets_n; k++) {
                if (strcmp(g_nets[k].ssid, p.ssid) == 0) {
                    dup = 1;
                    if (p.rssi > g_nets[k].rssi) g_nets[k].rssi = p.rssi;
                    if (p.secure) g_nets[k].secure = 1;
                    if (p.rsn_len > 0 && g_nets[k].rsn_len == 0) {
                        memcpy(g_nets[k].rsn, p.rsn, p.rsn_len);
                        g_nets[k].rsn_len = p.rsn_len;
                    }
                    break;
                }
            }
            if (!dup && g_nets_n < 16) g_nets[g_nets_n++] = p;
        }
    }

    for (i = 0; i < g_nets_n && nfound < max; i++) {
        memset(&out[nfound], 0, sizeof(out[nfound]));
        memcpy(out[nfound].ssid, g_nets[i].ssid, 33);
        out[nfound].rssi   = g_nets[i].rssi;
        out[nfound].secure = g_nets[i].secure;
        nfound++;
    }
    kernel_debug("ATH9K: tarama tamam: %d ag", nfound);
    return nfound;
}

/* ===================================================================== *
 * EAPOL bekleyicisi (handshake)
 * ===================================================================== */
static int wait_eapol(uint8_t *buf, int *len, int *out_hdrlen, int timeout_ms) {
    uint8_t tmp[2048];
    int t;
    for (t = 0; t < timeout_ms / 2; t++) {     /* her adim ~2 ms */
        uint8_t *p = buf ? buf : tmp;
        int n = 0, rate = 0, hdrlen;
        uint16_t fc;
        if (rx_frame_raw(p, &n, &rate) != 1) { msleep(2); continue; }
        if (n < 40) continue;
        fc = g16(p);
        if ((fc & 0x00FC) != 0x0008 && (fc & 0x00FC) != 0x0088) continue;
        if (!(fc & 0x0100)) continue;                 /* FromDS gerekli */
        hdrlen = ((fc & 0x0300) == 0x0300) ? 30 : 24;
        if (fc & 0x0080) hdrlen += 2;                 /* QoS */
        if (fc & 0x4000) continue;                    /* korumali: EAPOL acik gelir */
        if (n < hdrlen + 12) continue;
        if (p[hdrlen] == 0xAA && p[hdrlen + 1] == 0xAA &&
            p[hdrlen + 6] == 0x88 && p[hdrlen + 7] == 0x8E) {
            if (len) *len = n - hdrlen - 8;
            if (out_hdrlen) *out_hdrlen = hdrlen;
            return 1;
        }
    }
    return 0;
}

/* EAPOL karesini (LLC/SNAP ile) veri kare olarak gonder */
static int eapol_send(const uint8_t *eap, int elen) {
    static const uint8_t snap[8] = {0xAA,0xAA,0x03,0x00,0x00,0x00,0x88,0x8E};
    uint8_t f[1100];
    int n;
    if (elen <= 0 || elen > 1000) return -1;
    p16(f, FC_DATA_TODS);
    p16(f + 2, 0);
    mac_cpy(f + 4, g_bssid);        /* RA = BSSID */
    mac_cpy(f + 10, g_mac);         /* TA = SA (biz) */
    mac_cpy(f + 16, g_bssid);       /* DA = BSSID */
    p16(f + 22, (uint16_t)((g_seq++ & 0x0FFF) << 4));
    memcpy(f + 24, snap, 8);
    memcpy(f + 32, eap, (size_t)elen);
    n = 32 + elen;
    return tx_frame_raw(f, n, TXQ_DATA, 0, 7);
}

/* 64 byte tohum uret (rdtsc tabanli) */
static void fill_rand(uint8_t *b, int n) {
    uint64_t t;
    uint32_t lo, hi;
    int i;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    t = ((uint64_t)hi << 32) | lo;
    for (i = 0; i < n; i++) {
        t = t * 6364136223846793005ULL + 1442695040888963407ULL;
        b[i] = (uint8_t)(t >> 33);
    }
}

/* ===================================================================== *
 * WPA2-PSK dort-adimli anahtar degisimi
 * ===================================================================== */
static int wpa2_handshake(const parsed_net_t *net) {
    uint8_t eap[1400], f[1400], mic[16], gtk[16];
    uint8_t rsn[64];
    int elen = 0, hdrlen = 0, i, klen;
    uint32_t kdlen32 = 0;

    if (!net->secure || !g_pass[0]) return 0;   /* acik ag: handshake yok */

    /* PMK = PBKDF2(pass, ssid) */
    {
        uint8_t sb[33]; int sl = 0;
        while (net->ssid[sl] && sl < 32) { sb[sl] = (uint8_t)net->ssid[sl]; sl++; }
        wpa2_derive_pmk(g_pass, sb, (uint32_t)sl, g_pmk);
        g_have_pmk = true;
    }
    kernel_debug("ATH9K: WPA2-PSK PMK turetildi (%s)", net->ssid);

    /* M1: AP'den ANonce */
    if (!wait_eapol(eap, &elen, &hdrlen, 4000)) {
        kernel_debug("ATH9K: M1 gelmedi");
        return -1;
    }
    if (elen < 99 || eap[4] != 2) { kernel_debug("ATH9K: M1 bozuk"); return -1; }
    memcpy(g_anonce, eap + 17, 32);
    fill_rand((uint8_t *)g_snonce, 32);

    /* PTK = PRF(PMK, AA, SPA, ANonce, SNonce) */
    wpa2_derive_ptk(g_pmk, g_bssid, g_mac, g_anonce, g_snonce, g_ptk);
    kernel_debug("ATH9K: PTK turetildi");

    /* M2 hazirla: key_info = ver2 | pairwise | MIC */
    memset(f, 0, sizeof(f));
    klen = 0;
    f[0] = 1; f[1] = 3;                /* EAPOL v1, Key */
    f[4] = 2;                          /* RSN anahtar tipi */
    f[5] = 0x0A; f[6] = 0x01;          /* key_info = 0x010A */
    f[7] = 0; f[8] = 16;               /* key length = 16 (CCMP) */
    memcpy(f + 9, eap + 9, 8);         /* replay = M1 replay */
    memcpy(f + 17, g_snonce, 32);
    /* anahtar verisi: RSN IE (beacon'dan) */
    {
        int rl = net->rsn_len;
        if (rl <= 0 || rl > 64) rl = 0;
        if (rl > 0) {
            memcpy(rsn, net->rsn, (size_t)rl);
            memcpy(f + 99, rsn, (size_t)rl);
            klen = rl;
        }
    }
    f[97] = (uint8_t)(klen & 0xFF); f[98] = (uint8_t)(klen >> 8);
    {
        int tot = 4 + 95 + klen;
        f[2] = (uint8_t)((tot - 4) >> 8); f[3] = (uint8_t)(tot - 4);
        wpa2_mic(g_ptk, f, (uint32_t)tot, mic);   /* MIC alani 0 iken hesapla */
        memcpy(f + 81, mic, 16);
        if (eapol_send(f, tot) < 0) { kernel_debug("ATH9K: M2 gonderilemedi"); return -1; }
    }
    kernel_debug("ATH9K: M2 gonderildi");

    /* M3: MIC dogrula + GTK coz */
    if (!wait_eapol(eap, &elen, &hdrlen, 4000)) {
        kernel_debug("ATH9K: M3 gelmedi");
        return -1;
    }
    if (elen < 99) return -1;
    {
        uint8_t rmic[16], cmic[16];
        memcpy(rmic, eap + 81, 16);        /* alinan MIC */
        memset(eap + 81, 0, 16);           /* hesap icin sifirla */
        wpa2_mic(g_ptk, eap, (uint32_t)elen, cmic);
        memcpy(eap + 81, rmic, 16);        /* geri iade */
        if (memcmp(rmic, cmic, 16) != 0) {
            kernel_debug("ATH9K: M3 MIC uyusmazligi");
            return -1;
        }
    }
    {
        int kdlen = eap[97] | (eap[98] << 8);
        if (kdlen > 0 && 99 + kdlen <= elen) {
            if (wpa2_unwrap_gtk(g_ptk + 16, eap + 99, (uint32_t)kdlen,
                                gtk, &kdlen32) == 0 && kdlen32 >= 16) {
                memcpy(g_gtk, gtk, 16);
                g_have_gtk = true;
            }
        }
    }

    /* M4: replay = M3 replay, key data yok */
    memset(f, 0, sizeof(f));
    f[0] = 1; f[1] = 3;
    f[4] = 2;
    f[5] = 0x0A; f[6] = 0x01;          /* ver2 | pairwise | MIC */
    f[7] = 0; f[8] = 16;
    memcpy(f + 9, eap + 9, 8);         /* M3 replay */
    f[97] = 0; f[98] = 0;
    f[2] = 0; f[3] = 95;                    /* EAPOL uzunlugu: 95 */
    wpa2_mic(g_ptk, f, 99, mic);            /* MIC alani 0 iken hesapla */
    memcpy(f + 81, mic, 16);
    if (eapol_send(f, 99) < 0) { kernel_debug("ATH9K: M4 gonderilemedi"); return -1; }
    (void)i;

    memcpy(g_tk, g_ptk + 32, 32);      /* TK = PTK[32..63] */
    g_ptk_ready = true;
    kernel_debug("ATH9K: WPA2 handshake tamam (TK+GTK kuruldu)");
    return 0;
}

/* ===================================================================== *
 * Baglanti
 * ===================================================================== */
static int drv_connect(const char *ssid, const char *pass) {
    int ch, i, idx = -1;
    uint8_t f[2048];
    parsed_net_t net;
    int n, aid;

    if (!g_hw_up || !ssid || !ssid[0]) return -1;
    memset(g_ssid, 0, sizeof(g_ssid));
    strncpy(g_ssid, ssid, 32);
    g_pass[0] = 0;
    if (pass) { strncpy(g_pass, pass, 63); g_pass[63] = 0; }
    g_ptk_ready = false;
    g_have_gtk  = false;
    g_have_pmk  = false;
    g_state = ASI_WIFI_CONNECTING;

    /* 1) hedefi bul (yakin tarama) */
    set_bssid(BCAST, 0);
    for (ch = 1; ch <= 14 && idx < 0; ch++) {
        parsed_net_t p;
        if (hw_set_channel(ch) != 0) continue;
        msleep(30);
        for (i = 0; i < 150; i++) {
            int rn = 0, rate = 0;
            if (rx_frame_raw(f, &rn, &rate) != 1) { msleep(2); continue; }
            if (parse_beacon(f, rn, g_last_rssi, &p) &&
                strcmp(p.ssid, ssid) == 0) {
                if (p.channel == 0) p.channel = ch;
                net = p;
                idx = 1;
                break;
            }
        }
    }
    if (idx < 0) {
        kernel_debug("ATH9K: '%s' bulunamadi", ssid);
        g_state = ASI_WIFI_FAILED;
        return -1;
    }
    memcpy(g_bssid, net.bssid, 6);
    kernel_debug("ATH9K: hedef '%s' BSSID %02x:%02x:%02x:%02x:%02x:%02x ch=%d%s",
                 ssid, g_bssid[0], g_bssid[1], g_bssid[2],
                 g_bssid[3], g_bssid[4], g_bssid[5], net.channel,
                 net.secure ? " [WPA2]" : " [acik]");

    if (hw_set_channel(net.channel) != 0) { g_state = ASI_WIFI_FAILED; return -1; }
    set_bssid(g_bssid, 0);

    /* 2) dogrulama (auth, acik sistem) */
    n = build_auth(f, g_bssid, 1);
    if (tx_frame_raw(f, n, TXQ_MGMT, 0, 7) < 0) {
        kernel_debug("ATH9K: auth gonderilemedi");
        g_state = ASI_WIFI_FAILED; return -1;
    }
    if (!wait_frame(0x00FC, FC_AUTH, f, &n, 3000) || n < 30 ||
        g16(f + 24) != 0 || g16(f + 28) != 0) {
        kernel_debug("ATH9K: auth yaniti yok/basarisiz");
        g_state = ASI_WIFI_FAILED; return -1;
    }
    kernel_debug("ATH9K: auth tamam");

    /* 3) birlestirme (assoc) */
    n = build_assoc_req(f, g_bssid, ssid, net.secure, net.rsn, net.rsn_len);
    if (tx_frame_raw(f, n, TXQ_MGMT, 0, 7) < 0) {
        g_state = ASI_WIFI_FAILED; return -1;
    }
    if (!wait_frame(0x00FC, FC_ASSOC_RESP, f, &n, 3000) || n < 30 ||
        g16(f + 26) != 0) {
        kernel_debug("ATH9K: assoc yaniti yok/basarisiz");
        g_state = ASI_WIFI_FAILED; return -1;
    }
    aid = g16(f + 28) & 0x3FFF;
    set_bssid(g_bssid, aid ? aid : 1);
    kernel_debug("ATH9K: assoc tamam (AID=%d)", aid);

    /* 4) WPA2 handshake (sifreli ag ise) */
    if (wpa2_handshake(&net) != 0) {
        kernel_debug("ATH9K: WPA2 handshake basarisiz");
        g_state = ASI_WIFI_FAILED;
        return -1;
    }

    g_state = ASI_WIFI_CONNECTED;
    kernel_debug("ATH9K: baglandi: %s", ssid);
    return 0;
}

static int  drv_disconnect(void) {
    g_state = ASI_WIFI_OFF;
    g_ssid[0] = 0;
    g_ptk_ready = false;
    g_have_gtk = false;
    set_bssid(BCAST, 0);
    return 0;
}
static int  drv_state(void) { return g_state; }
static const char *drv_ssid(void) {
    return (g_state == ASI_WIFI_CONNECTED) ? g_ssid : "";
}

/* ===================================================================== *
 * Veri yolu: Ethernet <-> 802.11
 * ===================================================================== */
static const uint8_t SNAP_IP[6]   = {0xAA,0xAA,0x03,0x00,0x00,0x00};
static const uint8_t SNAP_ARP[6]  = {0xAA,0xAA,0x03,0x00,0x00,0x00};

bool ath9k_send_packet(void *ctx, const uint8_t *data, uint16_t length) {
    uint8_t f[1600];
    uint16_t etype;
    int n;
    (void)ctx;

    if (g_state != ASI_WIFI_CONNECTED || !g_hw_up) return false;
    if (!data || length < 14 || length > 1500) return false;

    etype = g16(data + 12);
    p16(f, FC_DATA_TODS);
    p16(f + 2, 0);
    mac_cpy(f + 4, g_bssid);
    mac_cpy(f + 10, g_mac);
    mac_cpy(f + 16, g_bssid);
    p16(f + 22, (uint16_t)((g_seq++ & 0x0FFF) << 4));
    memcpy(f + 24, SNAP_IP, 6);
    p16(f + 30, etype);
    memcpy(f + 32, data + 14, (size_t)(length - 14));
    n = 32 + (int)length - 14;

    if (g_ptk_ready) {
        if (ccmp_encrypt(g_tk, f, 24, &n, g_pn++) != 0) return false;
    }
    return tx_frame_raw(f, n, TXQ_DATA, 0, 7) >= 0;
}

bool ath9k_has_packet(void *ctx) {
    (void)ctx;
    if (!g_hw_up) return false;
    return (g_rx_desc[g_rx_rd].ds_rxstatus8 & AR_RxDone) != 0;
}

bool ath9k_receive_packet(void *ctx, uint8_t *buf, uint16_t *length) {
    uint8_t f[2048];
    (void)ctx;
    if (!g_hw_up || !buf || !length) return false;

    for (;;) {
        int n = 0, rate = 0, hdrlen, plen;
        uint16_t fc, etype;
        uint8_t *payload;

        if (rx_frame_raw(f, &n, &rate) != 1) return false;
        if (n < 30) continue;
        fc = g16(f);
        if ((fc & 0x00FC) != 0x0008 && (fc & 0x00FC) != 0x0088) continue;
        if (!(fc & 0x0100)) continue;                 /* yalnizca FromDS */

        hdrlen = ((fc & 0x0300) == 0x0300) ? 30 : 24;
        if (fc & 0x0080) hdrlen += 2;                 /* QoS */
        if (n < hdrlen + 16) continue;

        if (fc & 0x4000) {                            /* CCMP korumali */
            int is_grp = (f[4] & 0x01) != 0;
            uint8_t *tk;
            if (n < hdrlen + 16) continue;
            if ((f[hdrlen + 2] & 0x20) == 0) continue;   /* ExtIV yoksa atla */
            if (is_grp) {
                if (!g_have_gtk) continue;
                tk = g_gtk;
            } else {
                if (!g_ptk_ready) continue;
                tk = g_tk;
            }
            if (ccmp_decrypt(tk, f, hdrlen, &n) != 0) continue;
        }

        /* LLC/SNAP (RFC 1042) zorunlu */
        if (n < hdrlen + 8) continue;
        if (f[hdrlen] != 0xAA || f[hdrlen + 1] != 0xAA ||
            f[hdrlen + 6] != 0x88) continue;
        etype = g16(f + hdrlen + 6);
        if (etype == 0x888E) continue;                /* EAPOL veri yolu disi */

        payload  = f + hdrlen + 8;
        plen     = n - hdrlen - 8;
        if (etype == 0x0800 && plen >= 20) {          /* IPv4: FCS/artik baytlari kirp */
            int iplen = ((int)payload[2] << 8) | payload[3];
            if (iplen >= 20 && iplen <= plen) plen = iplen;
        } else if (etype == 0x0806 && plen >= 28) {
            plen = 28;                                /* ARP sabit uzunluk */
        }
        if (plen < 0 || 14 + plen > 1514) continue;

        /* Ethernet cercevesi: FromDS'de DA=addr1, SA=addr3 */
        mac_cpy(buf, f + 4);
        mac_cpy(buf + 6, f + 16);
        p16(buf + 12, etype);
        memcpy(buf + 14, payload, (size_t)plen);
        *length = (uint16_t)(14 + plen);
        return true;
    }
}

/* ===================================================================== *
 * Kayit
 * ===================================================================== */
static const wifi_driver_t g_drv = {
    .name       = "ath9k-ar928x",
    .init       = drv_init,
    .scan       = drv_scan,
    .connect    = drv_connect,
    .disconnect = drv_disconnect,
    .state      = drv_state,
    .ssid       = drv_ssid,
    .send       = ath9k_send_packet,
    .has        = ath9k_has_packet,
    .recv       = ath9k_receive_packet,
};

void ath9k_register(void) { wifi_register_driver(&g_drv); }
