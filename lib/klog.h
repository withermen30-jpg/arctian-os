#ifndef KLOG_H
#define KLOG_H

#include <stdint.h>

#define KLOG_MAX      128
#define KLOG_TAG      12
#define KLOG_MSG      80

#define KLOG_DEBUG 0
#define KLOG_INFO  1
#define KLOG_WARN  2
#define KLOG_ERR   3
#define KLOG_FATAL 4

typedef struct {
    uint8_t  level;
    uint32_t tick;
    char     tag[KLOG_TAG];
    char     msg[KLOG_MSG];
} klog_entry_t;

void klog_init(void);
void klog_write(int level, const char *tag, const char *msg);
int  klog_count(void);
int  klog_get(int i, klog_entry_t *out);
void klog_clear(void);
void klog_set_sink(void (*sink)(const klog_entry_t *e, void *ud), void *ud);
int  klog_flush(void);
const char *klog_level_str(int level);

#endif
