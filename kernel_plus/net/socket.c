#include "socket.h"
#include "tcp.h"
#include "udp.h"
#include "ipv4.h"
#include "clock.h"
#include "kernel.h"
#include "serial.h"
#include <string.h>

typedef struct {
    int      type;
    int      state;
    int      conn;
    uint16_t local_port;
    uint16_t peer_port;
    uint8_t  peer_ip[4];
    uint16_t head;
    uint16_t count;
    uint16_t len[KSOCK_DGRAM_QUEUE];
    uint16_t sport[KSOCK_DGRAM_QUEUE];
    uint8_t  sip[KSOCK_DGRAM_QUEUE][4];
    uint8_t  buf[KSOCK_DGRAM_QUEUE][KSOCK_DGRAM_BUF];
} ksock_t;

static ksock_t   s_socks[KSOCK_MAX];
static uint16_t  s_next_port = 49152;

static void ksock_udp_dispatch(const uint8_t src_ip[4], uint16_t src_port,
                               uint16_t dst_port, const uint8_t *data, uint16_t len);

void ksock_init(void) {
    for (int i = 0; i < KSOCK_MAX; i++) {
        memset(&s_socks[i], 0, sizeof(s_socks[i]));
        s_socks[i].state = KSOCK_FREE;
        s_socks[i].conn = -1;
    }
    s_next_port = 49152;
}

static ksock_t *sock_get(int fd) {
    if (fd < 0 || fd >= KSOCK_MAX) return 0;
    if (s_socks[fd].state == KSOCK_FREE) return 0;
    return &s_socks[fd];
}

static bool port_in_use(uint16_t port) {
    if (port == 0) return false;
    for (int i = 0; i < KSOCK_MAX; i++)
        if (s_socks[i].state != KSOCK_FREE && s_socks[i].local_port == port) return true;
    return false;
}

static uint16_t alloc_port(void) {
    for (int tries = 0; tries < 16384; tries++) {
        uint16_t p = s_next_port;
        s_next_port = (s_next_port >= 65535) ? 49152 : (uint16_t)(s_next_port + 1);
        if (p < 1024) continue;
        if (!port_in_use(p)) return p;
    }
    return 0;
}

static bool ip_is_any(const uint8_t ip[4]) {
    return (ip[0] | ip[1] | ip[2] | ip[3]) == 0;
}

static bool ip_is_local(const uint8_t ip[4]) {
    for (int i = 0; i < 4; i++)
        if (ip[i] != ipv4_config.src_ip[i]) return false;
    return true;
}

int ksock_socket(int domain, int type, int proto) {
    if (domain != KSOCK_AF_INET) return -1;
    if (type != KSOCK_STREAM && type != KSOCK_DGRAM) return -1;
    (void)proto;

    for (int i = 0; i < KSOCK_MAX; i++) {
        if (s_socks[i].state != KSOCK_FREE) continue;
        memset(&s_socks[i], 0, sizeof(s_socks[i]));
        s_socks[i].type = type;
        s_socks[i].state = KSOCK_OPEN;
        s_socks[i].conn = -1;
        return i;
    }
    return -1;
}

int ksock_bind(int fd, const uint8_t ip[4], uint16_t port) {
    ksock_t *s = sock_get(fd);
    if (!s) return -1;
    if (s->state != KSOCK_OPEN) return -1;

    if (ip && !ip_is_any(ip) && !ip_is_local(ip)) return -1;

    if (port == 0) {
        port = alloc_port();
        if (port == 0) return -1;
    } else if (port_in_use(port)) {
        return -1;
    }

    s->local_port = port;
    s->state = KSOCK_BOUND;
    if (s->type == KSOCK_DGRAM) udp_bind(port, ksock_udp_dispatch);
    return 0;
}

int ksock_listen(int fd) {
    ksock_t *s = sock_get(fd);
    if (!s || s->type != KSOCK_STREAM) return -1;
    if (s->state != KSOCK_OPEN && s->state != KSOCK_BOUND) return -1;

    if (s->local_port == 0) {
        s->local_port = alloc_port();
        if (s->local_port == 0) return -1;
    }

    int c = tcp_listen(s->local_port);
    if (c < 0) return -1;

    s->conn = c;
    s->state = KSOCK_CONN;
    return 0;
}

int ksock_accept(int fd) {
    ksock_t *s = sock_get(fd);
    if (!s || s->type != KSOCK_STREAM) return -1;
    if (s->state != KSOCK_CONN || s->conn < 0) return -1;

    int c = tcp_accept(s->conn);
    if (c < 0) return -1;

    int nfd = ksock_socket(KSOCK_AF_INET, KSOCK_STREAM, 0);
    if (nfd < 0) {
        tcp_close(c);
        return -1;
    }

    ksock_t *ns = &s_socks[nfd];
    ns->conn = c;
    ns->local_port = s->local_port;
    ns->state = KSOCK_CONN;
    return nfd;
}

int ksock_connect(int fd, const uint8_t ip[4], uint16_t port) {
    ksock_t *s = sock_get(fd);
    if (!s) return -1;
    if (s->state == KSOCK_CONN) return -1;
    if (!ip || port == 0) return -1;

    for (int i = 0; i < 4; i++) s->peer_ip[i] = ip[i];
    s->peer_port = port;

    if (s->type == KSOCK_DGRAM) {
        if (s->local_port == 0) {
            s->local_port = alloc_port();
            if (s->local_port == 0) return -1;
            udp_bind(s->local_port, ksock_udp_dispatch);
        }
        s->state = KSOCK_CONN;
        return 0;
    }

    int c = tcp_connect(ip, port);
    if (c < 0) return -1;

    s->conn = c;
    s->state = KSOCK_CONN;
    return 0;
}

int ksock_send(int fd, const void *buf, uint32_t len) {
    ksock_t *s = sock_get(fd);
    if (!s || s->state != KSOCK_CONN || !buf) return -1;

    if (s->type == KSOCK_DGRAM) {
        if (s->peer_port == 0) return -1;
        if (len > 65507) return -1;
        if (s->local_port == 0) {
            s->local_port = alloc_port();
            if (s->local_port == 0) return -1;
            udp_bind(s->local_port, ksock_udp_dispatch);
        }
        if (!udp_send(s->peer_ip, s->local_port, s->peer_port, buf, (uint16_t)len))
            return -1;
        return (int)len;
    }

    uint32_t off = 0;
    while (off < len) {
        uint32_t remain = len - off;
        uint16_t chunk = (uint16_t)(remain > 4096 ? 4096 : remain);
        if (!tcp_send(s->conn, (const uint8_t *)buf + off, chunk))
            return off ? (int)off : -1;
        off += chunk;
    }
    return (int)len;
}

int ksock_recv(int fd, void *buf, uint32_t maxlen) {
    ksock_t *s = sock_get(fd);
    if (!s || s->state != KSOCK_CONN || !buf) return -1;

    if (s->type == KSOCK_DGRAM) {
        if (s->count == 0) return 0;
        uint16_t n = s->len[s->head];
        if (n > maxlen) n = (uint16_t)maxlen;
        memcpy(buf, s->buf[s->head], n);
        s->head = (uint16_t)((s->head + 1) % KSOCK_DGRAM_QUEUE);
        s->count--;
        return (int)n;
    }

    uint16_t cap = (uint16_t)(maxlen > 65535 ? 65535 : maxlen);
    return tcp_receive(s->conn, (uint8_t *)buf, cap);
}

int ksock_close(int fd) {
    ksock_t *s = sock_get(fd);
    if (!s) return -1;

    if (s->type == KSOCK_DGRAM && s->local_port)
        udp_unbind(s->local_port, ksock_udp_dispatch);
    if (s->type == KSOCK_STREAM && s->conn >= 0)
        tcp_close(s->conn);

    memset(s, 0, sizeof(*s));
    s->state = KSOCK_FREE;
    s->conn = -1;
    return 0;
}

int ksock_state(int fd) {
    ksock_t *s = sock_get(fd);
    if (!s) return -1;
    if (s->type == KSOCK_STREAM && s->conn >= 0) return tcp_state(s->conn);
    return s->state;
}

int ksock_port(int fd) {
    ksock_t *s = sock_get(fd);
    if (!s) return -1;
    return (int)s->local_port;
}

int ksock_peer(int fd, uint8_t ip[4], uint16_t *port) {
    ksock_t *s = sock_get(fd);
    if (!s) return -1;
    if (ip) for (int i = 0; i < 4; i++) ip[i] = s->peer_ip[i];
    if (port) *port = s->peer_port;
    return 0;
}

static void ksock_udp_dispatch(const uint8_t src_ip[4], uint16_t src_port,
                               uint16_t dst_port, const uint8_t *data, uint16_t len) {
    for (int i = 0; i < KSOCK_MAX; i++) {
        ksock_t *s = &s_socks[i];
        if (s->state == KSOCK_FREE || s->type != KSOCK_DGRAM) continue;
        if (s->local_port != dst_port) continue;

        if (s->count >= KSOCK_DGRAM_QUEUE) {
            s->head = (uint16_t)((s->head + 1) % KSOCK_DGRAM_QUEUE);
            s->count--;
        }
        uint16_t slot = (uint16_t)((s->head + s->count) % KSOCK_DGRAM_QUEUE);
        uint16_t n = (len > KSOCK_DGRAM_BUF) ? KSOCK_DGRAM_BUF : len;
        memcpy(s->buf[slot], data, n);
        s->len[slot] = n;
        s->sport[slot] = src_port;
        for (int k = 0; k < 4; k++) s->sip[slot][k] = src_ip[k];
        s->count++;
        return;
    }
}

#ifdef ARCTIAN_SOCK_SELFTEST
void ksock_selftest(void) {
    static const uint8_t host[4] = { 10, 0, 2, 2 };
    char rx[64];
    int ok_tcp = 0, ok_udp = 0;

    serial_puts("\n[sock] ==== SOCKET SELF-TEST ====\n");

    if (!ipv4_config.configured) {
        serial_puts("[sock] ag yapilandirilmamis, atlandi\n");
        return;
    }

    int fd = ksock_socket(KSOCK_AF_INET, KSOCK_STREAM, 0);
    serial_printf("[sock] tcp socket fd=%d\n", fd);
    if (fd >= 0 && ksock_connect(fd, host, 18081) == 0) {
        serial_puts("[sock] tcp baglandi\n");
        if (ksock_send(fd, "PING\n", 5) == 5) {
            uint64_t deadline = clock_ms() + 4000;
            int n = 0;
            while ((int64_t)(clock_ms() - deadline) < 0) {
                ipv4_poll();
                n = ksock_recv(fd, rx, sizeof(rx) - 1);
                if (n > 0) break;
                for (int w = 0; w < 2000; w++) { __asm__ volatile("pause"); }
            }
            if (n > 0) {
                rx[n] = 0;
                serial_printf("[sock] tcp alindi %d bayt: %s\n", n, rx);
                if (n >= 4 && rx[0] == 'P' && rx[1] == 'O' && rx[2] == 'N' && rx[3] == 'G')
                    ok_tcp = 1;
            } else {
                serial_puts("[sock] tcp yanit yok\n");
            }
        }
    } else {
        serial_puts("[sock] tcp baglanti basarisiz\n");
    }
    if (fd >= 0) ksock_close(fd);

    fd = ksock_socket(KSOCK_AF_INET, KSOCK_DGRAM, 0);
    serial_printf("[sock] udp socket fd=%d\n", fd);
    if (fd >= 0 && ksock_bind(fd, 0, 0) == 0 &&
        ksock_connect(fd, host, 18082) == 0) {
        serial_printf("[sock] udp yerel port=%d\n", ksock_port(fd));
        if (ksock_send(fd, "UDPPING\n", 8) == 8) {
            uint64_t deadline = clock_ms() + 4000;
            int n = 0;
            while ((int64_t)(clock_ms() - deadline) < 0) {
                ipv4_poll();
                n = ksock_recv(fd, rx, sizeof(rx) - 1);
                if (n > 0) break;
                for (int w = 0; w < 2000; w++) { __asm__ volatile("pause"); }
            }
            if (n > 0) {
                rx[n] = 0;
                serial_printf("[sock] udp alindi %d bayt: %s\n", n, rx);
                if (n >= 7 && rx[0] == 'U' && rx[1] == 'D' && rx[2] == 'P')
                    ok_udp = 1;
            } else {
                serial_puts("[sock] udp yanit yok\n");
            }
        }
    } else {
        serial_puts("[sock] udp baglanti basarisiz\n");
    }
    if (fd >= 0) ksock_close(fd);

    serial_printf("[sock] SONUC tcp=%d udp=%d -> %s\n",
                  ok_tcp, ok_udp, (ok_tcp && ok_udp) ? "OK" : "HATA");
}
#endif
