#ifndef ARCTIAN_IPC_H
#define ARCTIAN_IPC_H

#include <stdint.h>
#include "sync.h"

#define PIPE_BUF_MAX 4096

typedef struct {
    uint8_t buf[PIPE_BUF_MAX];
    uint32_t cap;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    volatile int closed;
    waitq_t rd_wq;
    waitq_t wr_wq;
} pipe_t;

void pipe_init(pipe_t *p, uint32_t cap);
int  pipe_write(pipe_t *p, const void *src, uint32_t n);
int  pipe_read(pipe_t *p, void *dst, uint32_t n);
void pipe_close(pipe_t *p);

#define MQ_MAX_SLOTS 16
#define MQ_MSG_MAX   64

typedef struct {
    uint8_t slots[MQ_MAX_SLOTS][MQ_MSG_MAX];
    uint32_t msg_size;
    uint32_t cap;
    uint32_t head;
    uint32_t count;
    volatile int closed;
    waitq_t rd_wq;
    waitq_t wr_wq;
} mq_t;

void mq_init(mq_t *q, uint32_t msg_size, uint32_t cap);
int  mq_send(mq_t *q, const void *msg);
int  mq_recv(mq_t *q, void *msg);
void mq_close(mq_t *q);

#define SHM_MAX_REGIONS 4
#define SHM_REGION_MAX  (64u * 1024u)

void *shm_alloc(uint32_t size);
int   shm_retain(void *p);
int   shm_release(void *p);
int   shm_size(void *p);
int   shm_count(void);

int   ipc_selftest(void);

#endif
