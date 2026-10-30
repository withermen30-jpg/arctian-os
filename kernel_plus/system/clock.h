#ifndef ARCTIAN_SYSTEM_CLOCK_H
#define ARCTIAN_SYSTEM_CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "asi.h"

void     clock_init(void);
uint64_t clock_ms(void);
uint64_t clock_uptime_ms(void);
void     clock_sleep_ms(uint32_t ms);
bool     clock_now(asi_datetime_t *out);
uint32_t clock_unix(void);
void     clock_set_unix(uint32_t unix_sec);
int64_t  clock_ntp_offset(void);

/* Yerel saat dilimi (dakika). RTC/NTP UTC tutar; kullaniciya gosterilen
 * zaman bu kaydirma ile yerel saate cevrilir. */
void     clock_set_tz_offset(int minutes);
int      clock_tz_offset(void);

#endif
