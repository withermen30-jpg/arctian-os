#ifndef ARCTIAN_UPDATE_H
#define ARCTIAN_UPDATE_H

#include <stdint.h>
#include "asi.h"

/* Arctian guncelleme mekanizmasi.
 *
 * Sunucudan satir tabanli bir surum manifesti ceker (GET {base}/v1/update),
 * manifestte listelenen modul paketlerini indirir, boyut + CRC32 ile
 * dogrular ve DuckStore kurulum hattindan (store_install) gecirir.
 *
 * Manifest formati:
 *   #arctian-update 1
 *   version=<surum>
 *   notes=<kisa aciklama>
 *   @file
 *   id=<uygulama kimligi>
 *   version=<modul surumu>
 *   size=<bayt>
 *   crc=<8 haneli CRC32 (IEEE)>
 *   package=/packages/<id>.wlf
 */

#define UPDATE_MAX_FILES 16
#define UPDATE_SIG_HEX_LEN 513

typedef struct {
    char     id[32];
    char     version[24];
    uint32_t size;
    uint32_t crc;          /* indirilen paketin tamami uzerinde CRC32 */
    char     package[64];  /* sunucu-goreli yol: /packages/<id>.dpk */
} update_file_t;

/* 0.7: sistem imaji tanimi.       */
typedef struct {
    char id[16];
    uint32_t size;
    char sha256[65];
    char package[64];
} update_image_t;

typedef struct {
    char          version[24];
    char          notes[128];
    int           count;
    update_file_t files[UPDATE_MAX_FILES];
    /* 0.7       */
    uint32_t      released;         /* yayin ani, Unix saniye */
    int           has_image;
    update_image_t image;
    char          sig[UPDATE_SIG_HEX_LEN];   /* manifest imzasi (hex) */
} update_manifest_t;

/* Durum makinesi */
enum {
    UPDATE_IDLE = 0,
    UPDATE_CHECKING,     /* manifest cekiliyor */
    UPDATE_AVAILABLE,    /* yeni surum var */
    UPDATE_UPTODATE,     /* sistem guncel */
    UPDATE_DOWNLOADING,  /* moduller indiriliyor/kuruluyor */
    UPDATE_DONE,         /* guncelleme tamamlandi */
    UPDATE_ERROR
};

typedef struct {
    int  state;
    int  progress;         /* 0..100 */
    int  cur;              /* su anki dosya (1 tabanli) */
    int  total;
    char version[24];      /* sunucudaki surum */
    char message[96];
} update_status_t;

/* Baslatma: ASI tutulur, indirme tamponu ayrilir, kalici surum okunur. */
void        update_init(asi_t *asi);

/* Sunucu tabani (varsayilan ARCTIAN_UPDATE_URL; kalici "url" ile degisir). */
void        update_set_base_url(const char *url);
const char *update_base_url(void);

const char *update_current_version(void);   /* derleme surumu (ARCTIAN_VERSION) */
const char *update_applied_version(void);   /* kalici olarak kurulmus surum */
const update_status_t   *update_status(void);
const update_manifest_t *update_manifest(void);
int         update_has_update(void);        /* 1 = yeni surum mevcut */

/* Kurulum geri cagrilari (desktop/main.c baglar). */
void        update_set_installer(int (*fn)(const void *wlf, uint32_t size, char *err, int errcap));
void        update_set_notify(void (*fn)(void));

/* Bloklayici (ag uzerinde calisir): 0 = basarili, <0 hata, >0 = yeni surum. */
int         update_check(void);
int         update_apply(void);

/* ---- 0.7: ayar okuyuculari, manifesto, kapanis/acilis kurulum ---- */

/* Ayarlar: update_auto ve update_delay_days okuyuculari (desktop/main.c baglar). */
void update_set_settings(int (*auto_fn)(void), int (*delay_fn)(void));

/* Gecikme kurali: yayin anindan `delay_days` gun gectiyse 1.
 * delay<=0 ise saat aranmaz. now=0 (saat bilinmiyor) + delay>0 -> 0. */
int  update_eligible(uint32_t released, int delay_days, uint64_t now_unix);
int  update_eligible_now(void);

/* Manifesto imzasi dogrulandi mi (0/1). */
int  update_manifest_verified(void);

/* Yuvada hazir paket var mi.       */
void update_set_ready(int ready);
int  update_ready(void);

/* Kapanis ve acilis adimlari: uygun ve hazirsa yuvayi cevirir.
 * 1 = yuva degistirildi, 0 = islem yok, <0 = hata (kapanisi durdurmaz). */
int  update_shutdown_step(void);
int  update_boot_step(void);

/* Sekmedeki "Kur" dugmesi gecikmeyi yok sayar (kullanici karari).     */
int  update_install_now(void);

/* ---- oz-test kancalari (ARCTIAN_07_TEST) ---- */
void update_test_set_now(uint64_t unix_now);   /* 0 = saat yok */
int  update_test_parse(const char *text, uint32_t len);  /* manifestoyu ayristir */
void update_test_reset(void);

#endif /* ARCTIAN_UPDATE_H */
