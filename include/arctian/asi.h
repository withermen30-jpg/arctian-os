#ifndef ARCTIAN_ASI_H
#define ARCTIAN_ASI_H

#include <stdint.h>
#include <stddef.h>

#define ASI_VERSION 2u

typedef struct {
    void (*clear)(void);
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*printf)(const char *fmt, ...);
} asi_console_t;

typedef struct {
    void *(*alloc)(size_t size);
    void  (*free)(void *ptr);
    void  (*memset)(void *dst, int c, size_t n);
    void  (*memcpy)(void *dst, const void *src, size_t n);
} asi_mem_t;

typedef struct {
    int (*read_sectors)(uint32_t lba, uint32_t count, void *buf);
    int (*write_sectors)(uint32_t lba, uint32_t count, const void *buf);
    int (*present)(void);
} asi_storage_t;

typedef struct {
    uint64_t (*ticks_ms)(void);
    void     (*sleep_ms)(uint32_t ms);
} asi_time_t;

typedef struct {
    int        (*width)(void);
    int        (*height)(void);
    uint32_t  *(*framebuffer)(void);
    uint32_t   (*rgb)(uint8_t r, uint8_t g, uint8_t b);
    void       (*present)(void);
} asi_gfx_t;

typedef struct {
    int (*mouse_x)(void);
    int (*mouse_y)(void);
    int (*mouse_buttons)(void);
    int (*key_avail)(void);
    int (*key_pop)(void);
} asi_input_t;

typedef struct {
    void (*shutdown)(void);
    void (*reboot)(void);
} asi_system_t;

typedef struct {
    void    *(*root)(void);
    void    *(*create)(void *dir, const char *name, int is_dir);
    void    *(*find)(void *dir, const char *name);
    int      (*write)(void *file, const void *data, uint32_t size);
    uint32_t (*read)(void *file, void *buf, uint32_t size);
    int         (*count)(void);
    const char *(*name)(int idx);
    uint32_t    (*size)(int idx);
    int         (*remove)(const char *name);
} asi_fs_t;

#define ASI_LINK_NONE     0
#define ASI_LINK_ETHERNET 1
#define ASI_LINK_WIFI     2

#define ASI_WIFI_OFF        0
#define ASI_WIFI_CONNECTING 1
#define ASI_WIFI_CONNECTED  2
#define ASI_WIFI_FAILED     3

typedef struct {
    char ssid[33];
    int  rssi;
    int  secure;
} asi_wifi_net_t;

typedef struct {
    int  (*present)(void);
    int  (*send)(const void *data, uint32_t len);
    int  (*recv)(void *buf, uint32_t maxlen);
    int  (*has)(void);
    void (*mac)(uint8_t out[6]);

    int  (*link_type)(void);
    int  (*link_up)(void);
    void (*ip)(uint8_t out[4]);
    void (*netmask)(uint8_t out[4]);
    void (*gateway)(uint8_t out[4]);
    void (*dns)(uint8_t out[4]);
    int  (*configured)(void);
    int  (*autoconfig)(void);

    int  (*wifi_hw)(void);
    int  (*wifi_scan)(asi_wifi_net_t *out, int max);
    int  (*wifi_connect)(const char *ssid, const char *pass);
    int  (*wifi_disconnect)(void);
    int  (*wifi_state)(void);
    const char *(*wifi_ssid)(void);

    void (*poll)(void);
    int  (*dns_resolve)(const char *host, uint8_t out_ip[4]);
    int  (*tcp_connect)(const uint8_t ip[4], uint16_t port);
    int  (*tcp_send)(int conn, const void *data, uint32_t len);
    int  (*tcp_recv)(int conn, void *buf, uint32_t maxlen);
    int  (*tcp_close)(int conn);
    int  (*tcp_state)(int conn);
    const char *(*http_get)(const char *url, uint32_t *len_out);

    const char *(*https_get)(const char *url, uint32_t *len_out);

    int (*download)(const char *url, void *dst, uint32_t cap, uint32_t *len_out);
} asi_net_t;

typedef struct {
    uint32_t        version;
    asi_console_t  *console;
    asi_mem_t      *mem;
    asi_storage_t  *storage;
    asi_time_t     *time;
    asi_gfx_t      *gfx;
    asi_input_t    *input;
    asi_system_t   *system;
    asi_fs_t       *fs;
    asi_net_t      *net;
} asi_t;

extern asi_t *g_asi;

#endif
