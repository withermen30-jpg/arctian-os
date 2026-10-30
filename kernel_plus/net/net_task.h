#ifndef ARCTIAN_NET_TASK_H
#define ARCTIAN_NET_TASK_H

#include <stdint.h>
#include "asi.h"

/* Isbirlikci (cooperative) arka plan ag gorevi.
 * Kendi yigini uzerinde calisir; ag bekleme donguleri ipv4_poll icindeki
 * yield noktasinda kontrolu UI karesine birakir. Boylece UI donmaz. */

void net_task_init(void);
int  net_task_submit(void (*fn)(void *), asi_net_done_t done, void *user);
int  net_task_busy(void);
void net_task_service(void);
void net_task_yield_point(void);
int  net_task_available(void);

#endif /* ARCTIAN_NET_TASK_H */
