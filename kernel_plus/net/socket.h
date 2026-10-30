#ifndef SOCKET_H
#define SOCKET_H

#include <stdint.h>
#include <stdbool.h>

#define KSOCK_AF_INET     2
#define KSOCK_STREAM      1
#define KSOCK_DGRAM       2

#define KSOCK_MAX         8
#define KSOCK_DGRAM_BUF   1472
#define KSOCK_DGRAM_QUEUE 2

#define KSOCK_FREE  0
#define KSOCK_OPEN  1
#define KSOCK_BOUND 2
#define KSOCK_CONN  3

int  ksock_socket(int domain, int type, int proto);
int  ksock_bind(int fd, const uint8_t ip[4], uint16_t port);
int  ksock_listen(int fd);
int  ksock_accept(int fd);
int  ksock_connect(int fd, const uint8_t ip[4], uint16_t port);
int  ksock_send(int fd, const void *buf, uint32_t len);
int  ksock_recv(int fd, void *buf, uint32_t maxlen);
int  ksock_close(int fd);
int  ksock_state(int fd);
int  ksock_port(int fd);
int  ksock_peer(int fd, uint8_t ip[4], uint16_t *port);

void ksock_init(void);
void ksock_selftest(void);

#endif
