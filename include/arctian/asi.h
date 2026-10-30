/**
 * Arctian System Interface (ASI) v2
 * kernel_plus ile desktop/uygulama modulleri arasindaki resmi arayuz.
 * Modul girisinde RDI bu tabloyu gosterir; ayrica g_asi global'i vardir.
 */
#ifndef ARCTIAN_ASI_H
#define ARCTIAN_ASI_H

#include <stdint.h>
#include <stddef.h>
#include "klog.h"
#include "recover.h"

#define ASI_VERSION 2u

#define CAP_NET        (1u << 0)
#define CAP_FS         (1u << 1)
#define CAP_DEV        (1u << 2)
#define CAP_ALL        ((uint32_t)0xFFFFFFFFu)
#define SANDBOX_EPERM  1

typedef struct {
    void (*clear)(void);
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*printf)(const char *fmt, ...);
} asi_console_t;

typedef struct {
    uint64_t total;
    uint64_t used;
    uint64_t free;
    uint64_t heap_total;
    uint64_t heap_used;
} asi_meminfo_t;

typedef struct {
    void *(*alloc)(size_t size);
    void  (*free)(void *ptr);
    void  (*memset)(void *dst, int c, size_t n);
    void  (*memcpy)(void *dst, const void *src, size_t n);
    void  (*info)(asi_meminfo_t *out);
} asi_mem_t;

typedef struct {
    int (*read_sectors)(uint32_t lba, uint32_t count, void *buf);
    int (*write_sectors)(uint32_t lba, uint32_t count, const void *buf);
    int (*present)(void);
} asi_storage_t;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday;
} asi_datetime_t;

typedef struct {
    uint64_t (*ticks_ms)(void);
    void     (*sleep_ms)(uint32_t ms);
    int      (*now)(asi_datetime_t *out);
    int      (*ntp_sync)(uint32_t timeout_ms);
    uint64_t (*unix_now)(void);
    /* Unix zamanini sistem saatine yazar (NTP sonucu). RTC'ye de islenir. */
    void     (*set_unix)(uint32_t unix_sec);
    /* 0.7 (H3): yerel saat dilimi kaydirmasi (dakika, UTC+3 = 180).
     * UTC tutulur; gosterilen zaman bu kaydirma ile yereldir. */
    void     (*set_tz_offset)(int minutes);
} asi_time_t;

typedef struct {
    int        (*width)(void);
    int        (*height)(void);
    uint32_t  *(*framebuffer)(void);
    uint32_t   (*rgb)(uint8_t r, uint8_t g, uint8_t b);
    void       (*present)(void);
    int        (*set_mode)(int width, int height);
    /* Kirli bolge sunumu (performans): yalnizca degisen dikdortgen kopyalanir.
     * TCG'de tam ekran blit saniyeler suruyordu; bu, arayuzu akici yapar. */
    void       (*present_rect)(int x, int y, int w, int h);
} asi_gfx_t;

typedef struct {
    int (*mouse_x)(void);
    int (*mouse_y)(void);
    int (*mouse_buttons)(void);
    int (*key_avail)(void);
    int (*key_pop)(void);   /* event queue'dan scancode/char */
    int (*mouse_scroll)(void);
    int (*hotkey_pop)(void);   /* 0=yok, 1=Alt+Tab, 2=Ctrl+Alt+T, 3..8=Ctrl+C/X/V/A/Z/Y */
    void (*kbd_layout)(int);   /* 0=TR-Q, 1=TR-F, 2=EN-Q */
    int (*ctrl_down)(void);    /* Ctrl anlik basilmi ise 1 (coklu secim icin) */
    int (*shift_down)(void);   /* Shift anlik basilmi ise 1 (aralik secim icin) */
    /* Sol tus basma kenari sayaci. Deger degistiyse iki yoklama arasinda bir
     * tiklanma olmus demektir; yavas kare hizinda (TCG) kaybolan tiklamalar
     * bu sayacla yakalanir. Yapisal ABI icin EN SONA eklendi. */
    uint32_t (*mouse_press_seq)(void);
} asi_input_t;

typedef struct {
    void (*shutdown)(void);
    void (*reboot)(void);

    /* Biriken disk yazimlarini (write-back onbellek) diske indirir.
     * Masaustu cerceve dongusunden periyodik cagrilir. */
    void (*writeback)(void);

    /* ---- Cokme koruma (crash recovery) ----
     * Uygulama baslatmadan once arctian_setjmp ile bir kurtarma noktasi
     * kaydedin, ardindan recover_begin(ctx, timeout_ms) cagirin. Uygulama
     * kodunda olusan CPU istisnasi veya timeout durumunda kernel panic
     * etmek yerine setjmp noktasina doner (bkz. recover.h).
     * recover_end: basariyla tamamlaninca cagirilir. */
    void (*recover_begin)(arctian_recover_t *ctx, uint32_t timeout_ms);
    void (*recover_end)(void);
} asi_system_t;

typedef struct {
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    uint32_t is_dir;
    uint32_t size;
} asi_stat_t;

typedef struct {
    void    *(*root)(void);
    void    *(*create)(void *dir, const char *name, int is_dir);
    void    *(*find)(void *dir, const char *name);
    int      (*write)(void *file, const void *data, uint32_t size);
    uint32_t (*read)(void *file, void *buf, uint32_t size);
    /* Kalici depo icin listeleme/silme (AFS) */
    int         (*count)(void);
    const char *(*name)(int idx);
    uint32_t    (*size)(int idx);
    int         (*remove)(const char *name);

    /* ---- Klasor (yol tabanli) API. path ornegi: "C:/apps".
     * Klasor adi '/' ile biter; adlar '/' ile ayrilir. C: kullanici diski
     * acilista otomatik olusturulur (apps, indirilenler, belgeler,
     * masaustu, logs). */
    int         (*mkdir)(const char *path);                       /* 0 = hazir */
    void       *(*create_in)(const char *path, const char *name, int is_dir);
    void       *(*find_in)(const char *path, const char *name);
    uint32_t    (*read_path)(const char *path, void *buf, uint32_t size);
    int         (*count_dir)(const char *path);
    int         (*list_dir)(const char *path, int idx, char *out, int cap, int *is_dir);

    int         (*stat)(const char *path, asi_stat_t *out);
    int         (*stat_handle)(void *file, asi_stat_t *out);
    int         (*chmod)(const char *path, uint32_t mode);
    int         (*chown)(const char *path, int uid, int gid);
    void        (*set_subject)(int uid, int gid);
    /* Yerel yeniden adlandirma/tasima (0.6): veri kopyalanmaz. */
    int         (*rename)(const char *from, const char *to);

    /* ---- Butunluk: denetim + onarim. fix=1 ise onarir. ---------- */
    int         (*fsck)(int fix, int *problems, int *fixed);
} asi_fs_t;

/* Baglanti turleri */
#define ASI_LINK_NONE     0
#define ASI_LINK_ETHERNET 1
#define ASI_LINK_WIFI     2

/* Wi-Fi durumlari */
#define ASI_WIFI_OFF        0
#define ASI_WIFI_CONNECTING 1
#define ASI_WIFI_CONNECTED  2
#define ASI_WIFI_FAILED     3

#define ASI_SOCK_STREAM  1
#define ASI_SOCK_DGRAM   2
#define ASI_SOCK_MAX     8

typedef struct {
    char ssid[33];
    int  rssi;    /* -30 (guclu) .. -90 (zayif) */
    int  secure;  /* 1 = sifre gerekli */
} asi_wifi_net_t;

/* Arka plan ag isi tamamlandiginda UI baglaminda cagrilir. */
typedef void (*asi_net_done_t)(int status, void *user);

typedef struct {
    int  (*present)(void);
    int  (*send)(const void *data, uint32_t len);
    int  (*recv)(void *buf, uint32_t maxlen);
    int  (*has)(void);
    void (*mac)(uint8_t out[6]);

    /* ---- Baglanti (netlink) ---- */
    int  (*link_type)(void);        /* ASI_LINK_* */
    int  (*link_up)(void);          /* 1/0 */
    void (*ip)(uint8_t out[4]);
    void (*netmask)(uint8_t out[4]);
    void (*gateway)(uint8_t out[4]);
    void (*dns)(uint8_t out[4]);
    int  (*configured)(void);
    int  (*autoconfig)(void);       /* IP ata (ethernet/dhcp) */

    /* ---- Wi-Fi ---- */
    int  (*wifi_hw)(void);          /* 1 = fiziksel wifi karti var */
    int  (*wifi_scan)(asi_wifi_net_t *out, int max);
    int  (*wifi_connect)(const char *ssid, const char *pass);
    int  (*wifi_disconnect)(void);
    int  (*wifi_state)(void);       /* ASI_WIFI_* */
    const char *(*wifi_ssid)(void); /* bagli SSID veya "" */

    /* ---- Uygulama ag API (tarayici icin) ---- */
    void (*poll)(void);             /* ipv4_poll + tcp_poll */
    int  (*dns_resolve)(const char *host, uint8_t out_ip[4]);
    int  (*tcp_connect)(const uint8_t ip[4], uint16_t port);
    int  (*tcp_send)(int conn, const void *data, uint32_t len);
    int  (*tcp_recv)(int conn, void *buf, uint32_t maxlen);
    int  (*tcp_close)(int conn);
    int  (*tcp_state)(int conn);
    const char *(*http_get)(const char *url, uint32_t *len_out); /* govde veya NULL */

    /* ---- HTTPS (TLS 1.3, mbedTLS) ---- */
    const char *(*https_get)(const char *url, uint32_t *len_out); /* govde veya NULL */

    /* ---- Akisli buyuk dosya indirme (sabit tampon kullanmaz) ----
     * Govde dst'ye yazilir; cap = basliklar + govde icin ayrilan alan.
     * Donus 0 ise *len_out = govde uzunlugu. https:// de desteklenir. */
    int (*download)(const char *url, void *dst, uint32_t cap, uint32_t *len_out);

    /* ---- Arka plan (fiber) ag isleri: arayuzu bloklamaz ----
     * fn kendi yigini uzerinde calisir ve icindeki ag cagrilari
     * ipv4_poll icinde gönüllü olarak yield eder. Is bitince done,
     * UI karesi icinde cagrilir. Ayni anda tek is calisir.
     * Donus 0 = kabul, -1 = mesgul/eslenemedi. */
    int  (*submit)(void (*fn)(void *), asi_net_done_t done, void *user);
    int  (*busy)(void);             /* 1 = arka plan isi suruyor */
    void (*service)(void);          /* her karede cagrilir; isi ilerletir */

    /* Son getirmenin nihai (yonlendirme sonrasi) URL'si. ABI uyumu icin
     * yapinin SONUNDA tutulur. */
    const char *(*last_url)(void);

    /* JS document.cookie tarafindan yazilan cerezi cekirdek deposuna ekler.
     * host: guncel sayfa ana bilgisayari; name/value: cerez adi ve degeri. */
    void (*cookie_set)(const char *host, const char *name, const char *value);

    /* ---- Soket API (BSD benzeri) ----
     * type: ASI_SOCK_STREAM (TCP) veya ASI_SOCK_DGRAM (UDP).
     * fd: 0..ASI_SOCK_MAX-1; hata -1. bind/connect/send/recv/close. */
    int  (*sock_socket)(int type);
    int  (*sock_bind)(int fd, const uint8_t ip[4], uint16_t port);
    int  (*sock_listen)(int fd);
    int  (*sock_accept)(int fd);
    int  (*sock_connect)(int fd, const uint8_t ip[4], uint16_t port);
    int  (*sock_send)(int fd, const void *data, uint32_t len);
    int  (*sock_recv)(int fd, void *buf, uint32_t maxlen);
    int  (*sock_close)(int fd);
    int  (*sock_state)(int fd);
    int  (*sock_port)(int fd);
} asi_net_t;

typedef struct {
    int      pid;
    int      ppid;
    int      state;
    int      exit_code;
    uint32_t caps;
    uint32_t uid;
    uint32_t gid;
    uint32_t cpu_ticks;
    char     name[32];
} asi_proc_info_t;

typedef struct {
    int  (*spawn)(const char *name, void (*entry)(void *), void *arg);
    int  (*fork)(const char *name, void (*entry)(void *), void *arg);
    int  (*exec)(void (*entry)(void *), void *arg);
    void (*exit)(int code);
    int  (*wait)(int pid, int *code);
    int  (*kill)(int pid);
    int  (*getpid)(void);
    int  (*getppid)(void);
    int  (*count)(void);
    int  (*info)(int i, int *pid, char *name, int cap, int *state, int *ppid, int *code);
    int  (*app_open)(const char *title);
    void (*app_close)(int pid, int code);
    int  (*reap)(int *code_out);

    int      (*app_open_caps)(const char *title, uint32_t caps);
    uint32_t (*caps_of)(int pid);

    int      (*info_ex)(int i, asi_proc_info_t *out);
    uint32_t (*sys_ticks)(void);
} asi_proc_t;

typedef struct {
    int (*selftest)(void);
    int (*thread_count)(void);
    int (*shm_count)(void);
    int (*pipe_test)(void);
} asi_lab_t;

typedef struct {
    void     (*enter)(int pid);
    void     (*leave)(void);
    uint32_t (*current)(void);
    int      (*check)(uint32_t cap);
    int      (*last_error)(void);
    int      (*selftest)(void);
    void     (*set_uid)(int uid, int gid);
    uint32_t (*uid)(void);
    uint32_t (*gid)(void);

    /* ---- 0.7: ozne + kapi ----
     * Masaustu bunlari cagirir; durum cekirdekte (kernel_plus) tutulur ki
     * dosya kapisi ayni kaynagi gorsun. Yapisal ABI icin EN SONA eklendi. */
    void        (*subject_begin)(const char *id, const char *title,
                                 const char *home, int trusted);
    void        (*subject_end)(void);
    const char *(*subject_id)(void);
    const char *(*subject_title)(void);
    const char *(*subject_home)(void);
    int         (*subject_trusted)(void);
    int         (*subject_active)(void);
    void        (*dispatch_begin)(void);
    void        (*dispatch_end)(void);
    int         (*dispatch_active)(void);
    int         (*set_policy)(int (*fn)(const char *path, int is_write));
} asi_sandbox_t;

typedef struct {
    int      (*present)(void);
    int      (*play_wav)(const void *data, uint32_t len);
    void     (*stop)(void);
    int      (*is_playing)(void);
    int      (*set_volume)(int percent);
    int      (*volume)(void);
    void     (*service)(void);
    const char *(*last_error)(void);
    uint32_t (*sample_rate)(void);
} asi_audio_t;

typedef struct {
    int         (*present)(void);
    int         (*unlocked)(void);
    int         (*unlock)(const char *pass);
    int         (*setup)(const char *pass);
    int         (*disable)(const char *pass);
    const char *(*status)(void);
} asi_crypt_t;

/* ---- Guvenlik duvari (firewall) ----
 * Yon: 1 = gelen, 2 = giden, 3 = her ikisi. Protokol: 0 = hepsi, 6 = TCP,
 * 17 = UDP. Eylem: 0 = engelle, 1 = izin ver. Kural eslesmezse varsayilan
 * izin'dir; amac istenmeyen portlari acikca kapatmaktir. */
#define ASI_FW_MAX_RULES 32
#define ASI_FW_NAME_MAX  24

#define ASI_FW_DIR_IN   1
#define ASI_FW_DIR_OUT  2
#define ASI_FW_DIR_ANY  3

#define ASI_FW_PROTO_ANY 0
#define ASI_FW_PROTO_TCP 6
#define ASI_FW_PROTO_UDP 17

#define ASI_FW_DENY  0
#define ASI_FW_ALLOW 1

typedef struct {
    char     name[ASI_FW_NAME_MAX];
    uint8_t  dir;       /* ASI_FW_DIR_* */
    uint8_t  proto;     /* ASI_FW_PROTO_* */
    uint8_t  action;    /* ASI_FW_DENY / ASI_FW_ALLOW */
    uint8_t  enabled;   /* 1 = kural aktif */
    uint8_t  ip[4];     /* hedef/kaynak IP; 0.0.0.0 = hepsi */
    uint8_t  mask[4];   /* ag maskesi; hepsi sifir = hepsi */
    uint16_t port_lo;   /* 0 (ve port_hi 0) = hepsi */
    uint16_t port_hi;   /* tek port icin port_lo'ya esit */
    uint32_t hits;      /* eslesme sayaci (cekirdek gunceller) */
} asi_fw_rule_t;

typedef struct {
    int (*enabled)(void);
    int (*set_enabled)(int on);
    int (*count)(void);
    int (*rule)(int idx, asi_fw_rule_t *out);   /* 0 = ok, -1 = yok */
    int (*add)(const asi_fw_rule_t *r);         /* >=0 = yeni kuralin sirasi */
    int (*remove)(int idx);
    int (*clear)(void);
    int (*save)(void);
    int (*load)(void);
    int (*selftest)(void);
} asi_firewall_t;

/* ---- Guc yonetimi / pil ---- */
#define ASI_POWER_AC      0
#define ASI_POWER_BATTERY 1

typedef struct {
    int  (*source)(void);        /* ASI_POWER_AC / ASI_POWER_BATTERY */
    int  (*percent)(void);       /* 0..100, -1 bilinmiyor */
    int  (*present)(void);       /* 1 = pil takili */
    int  (*charging)(void);      /* 1 = sarj oluyor */
    int  (*minutes)(void);       /* kalan sure (dk), -1 bilinmiyor */
    int  (*acpi)(void);          /* 1 = ACPI tablolari bulundu */
    void (*sleep)(void);         /* uyku (tus/fare ile uyanir) */
} asi_power_t;

typedef struct {
    int  (*submit)(const char *name, const void *data, uint32_t len);
    int  (*count)(void);
    int  (*info)(int i, int *id, char *name, int cap, uint32_t *len);
    int  (*cancel)(int id);
    int  (*process)(void);
    int  (*last_path)(char *out, int cap);
} asi_spool_t;

typedef struct {
    void (*write)(int level, const char *tag, const char *msg);
    int  (*count)(void);
    int  (*get)(int i, klog_entry_t *out);
    void (*clear)(void);
    int  (*flush)(void);
} asi_log_t;

typedef struct {
    int         (*count)(void);
    const char *(*name)(int i);
    const char *(*status)(int i);
} asi_dev_t;

typedef struct {
    uint32_t        version;   /* ASI_VERSION */
    asi_console_t  *console;
    asi_mem_t      *mem;
    asi_storage_t  *storage;
    asi_time_t     *time;
    asi_gfx_t      *gfx;
    asi_input_t    *input;
    asi_system_t   *system;
    asi_fs_t       *fs;
    asi_net_t      *net;
    asi_proc_t     *proc;
    asi_lab_t      *lab;
    asi_sandbox_t  *sandbox;
    asi_audio_t    *audio;
    asi_crypt_t    *crypt;
    asi_firewall_t *firewall;
    asi_power_t    *power;
    asi_spool_t    *spool;
    asi_log_t      *log;
    asi_dev_t      *dev;
} asi_t;

/* kernel_plus tarafindan doldurulur; tum moduller erisir. */
extern asi_t *g_asi;

#endif /* ARCTIAN_ASI_H */
