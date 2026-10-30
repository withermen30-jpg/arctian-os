#include "ntp.h"
#include "socket.h"
#include "dns.h"
#include "ipv4.h"
#include "clock.h"
#include <string.h>

#define NTP_SERVER      "pool.ntp.org"
#define NTP_PORT        123
#define NTP_UNIX_DELTA  2208988800u

int ntp_sync(uint32_t timeout_ms) {
    if (!ipv4_config.configured) return -2;

    dns_result_t r[DNS_RESULT_MAX];
    int n = 0;
    if (!dns_resolve(NTP_SERVER, r, &n) || n <= 0) return -3;

    uint8_t ip[4];
    for (int i = 0; i < 4; i++) ip[i] = r[0].ip[i];

    int fd = ksock_socket(KSOCK_AF_INET, KSOCK_DGRAM, 0);
    if (fd < 0) return -4;
    if (ksock_bind(fd, 0, 0) != 0) { ksock_close(fd); return -5; }
    if (ksock_connect(fd, ip, NTP_PORT) != 0) { ksock_close(fd); return -6; }

    uint8_t req[48];
    memset(req, 0, sizeof(req));
    req[0] = 0x1B;

    if (ksock_send(fd, req, sizeof(req)) != (int)sizeof(req)) {
        ksock_close(fd);
        return -7;
    }

    uint8_t rep[48];
    int got = 0;
    uint64_t deadline = clock_ms() + timeout_ms;
    while ((int64_t)(clock_ms() - deadline) < 0) {
        ipv4_poll();
        got = ksock_recv(fd, rep, sizeof(rep));
        if (got >= 48) break;
        for (volatile int w = 0; w < 2000; w++) __asm__ volatile("pause");
    }
    ksock_close(fd);
    if (got < 48) return -8;

    uint32_t secs = ((uint32_t)rep[40] << 24) | ((uint32_t)rep[41] << 16) |
                    ((uint32_t)rep[42] << 8) | rep[43];
    if (secs < NTP_UNIX_DELTA) return -9;

    clock_set_unix(secs - NTP_UNIX_DELTA);
    return 0;
}
