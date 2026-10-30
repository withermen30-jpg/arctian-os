#ifndef NTP_H
#define NTP_H

#include <stdint.h>

int ntp_sync(uint32_t timeout_ms);

#endif
