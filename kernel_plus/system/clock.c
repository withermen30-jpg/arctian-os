#include "clock.h"
#include "kernel.h"
#include "pic.h"
#include "isr.h"
#include "rtc.h"

#define PIT_INPUT_HZ  1193182u
#define PIT_TICK_HZ   1000u
#define PIT_CH0_DATA  0x40
#define PIT_CMD       0x43

static volatile uint64_t s_ticks_ms = 0;

static void pit_irq_handler(void) {
    s_ticks_ms++;
}

void clock_init(void) {
    uint32_t divisor = PIT_INPUT_HZ / PIT_TICK_HZ;
    if (divisor == 0) divisor = 1;
    if (divisor > 65535u) divisor = 65535u;

    isr_install_handler(IRQ_TIMER, pit_irq_handler);

    outb(PIT_CMD, 0x34);
    outb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    pic_set_mask(IRQ_TIMER, false);
}

uint64_t clock_ms(void) {
    return s_ticks_ms;
}

uint64_t clock_uptime_ms(void) {
    return s_ticks_ms;
}

/* Kesmeler kapaliyken hlt uyandirilamaz: PIT kesti gelmez, surucu init
 * yollarindaki uyku sonsuza kadar surerdi. Bu durumda sayac okunarak
 * yaklasik gercek zaman bekleme yapilir. */
static int irqs_enabled(void) {
    uint64_t f;
    __asm__ volatile("pushfq; popq %0" : "=r"(f));
    return (f & 0x200u) != 0;
}

static void clock_busy_ms(uint32_t ms) {
    const uint32_t div = PIT_INPUT_HZ / PIT_TICK_HZ;   /* 1 ms'deki PIT sayimi */
    uint64_t need = (uint64_t)ms * div;
    uint64_t acc = 0, stuck = 0;
    uint32_t prev = 0;
    while (acc < need) {
        outb(PIT_CMD, 0x00);                            /* kanali kilitle */
        uint32_t cur = inb(PIT_CH0_DATA) | (inb(PIT_CH0_DATA) << 8);
        if (prev) {
            /* Sayac geriye dogru sayar; yukari atirma = sayacin bosalmasi.
             * Yeniden yuklenme miktarini bilmedigimiz icin eksik sayariz,
             * yani bekleme uzar ama kisa dusmez. */
            uint32_t d = (cur <= prev) ? (prev - cur) : prev;
            if (d) { acc += d; stuck = 0; }
            else if (++stuck > 4000000ull) break;      /* sayac calismiyor */
        }
        prev = cur;
        __asm__ volatile("pause");
    }
}

void clock_sleep_ms(uint32_t ms) {
    if (!irqs_enabled()) { clock_busy_ms(ms); return; }
    uint64_t target = s_ticks_ms + (uint64_t)ms;
    while (s_ticks_ms < target) asm volatile("hlt");
}

static int clock_weekday(int y, int m, int d) {
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static int64_t clock_days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void clock_civil_from_days(int64_t z, int *yy, int *mm, int *dd) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)(yoe + era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp + (mp < 10 ? 3 : -9);
    *yy = y + (m <= 2);
    *mm = (int)m;
    *dd = (int)d;
}

static int64_t clock_rtc_unix(const rtc_time_t *t) {
    return clock_days_from_civil(t->year, t->month, t->day) * 86400
         + (int64_t)t->hour * 3600 + (int64_t)t->minute * 60 + t->second;
}

static int64_t s_ntp_offset;

/* Yerel saat dilimi kaydirmasi (dakika). RTC ve NTP UTC tutar; kullaniciya
 * gosterilen zaman yerel saate cevrilir. */
static int s_tz_min = 0;

void clock_set_tz_offset(int minutes) {
    if (minutes < -14 * 60) minutes = -14 * 60;
    if (minutes > 14 * 60)  minutes = 14 * 60;
    s_tz_min = minutes;
}

int clock_tz_offset(void) { return s_tz_min; }

uint32_t clock_unix(void) {
    rtc_time_t t;
    if (!rtc_read_time(&t)) return 0;
    int64_t u = clock_rtc_unix(&t) + s_ntp_offset;
    return (uint32_t)(u < 0 ? 0 : u);
}

void clock_set_unix(uint32_t unix_sec) {
    rtc_time_t t;
    if (!rtc_read_time(&t)) return;
    s_ntp_offset = (int64_t)unix_sec - clock_rtc_unix(&t);

    /* Sonucu CMOS RTC'ye de yaz: aksi halde NTP duzeltmesi yalniz RAM'de
     * kaliyor ve yeniden baslatmada saat geri kayiyordu. */
    int64_t days = (int64_t)unix_sec / 86400;
    int64_t sod  = (int64_t)unix_sec % 86400;
    int y, m, d;
    clock_civil_from_days(days, &y, &m, &d);
    rtc_time_t nt;
    nt.year   = (uint16_t)y;
    nt.month  = (uint8_t)m;
    nt.day    = (uint8_t)d;
    nt.hour   = (uint8_t)(sod / 3600);
    nt.minute = (uint8_t)((sod % 3600) / 60);
    nt.second = (uint8_t)(sod % 60);
    rtc_set_time(&nt);
    s_ntp_offset = 0;   /* RTC artik dogru: ek kaydirma gerekmez */
}

int64_t clock_ntp_offset(void) {
    return s_ntp_offset;
}

bool clock_now(asi_datetime_t *out) {
    if (!out) return false;

    rtc_time_t t;
    if (!rtc_read_time(&t)) return false;

    /* clock_unix UTC'dir; kullaniciya yerel saat gosterilir (H3). */
    int64_t u = clock_rtc_unix(&t) + s_ntp_offset + (int64_t)s_tz_min * 60;
    int64_t days = u / 86400;
    int64_t sod = u % 86400;
    if (sod < 0) { sod += 86400; days--; }

    int y, m, d;
    clock_civil_from_days(days, &y, &m, &d);

    out->year    = y;
    out->month   = m;
    out->day     = d;
    out->hour    = (int)(sod / 3600);
    out->minute  = (int)((sod % 3600) / 60);
    out->second  = (int)(sod % 60);
    out->weekday = clock_weekday(y, m, d);
    return true;
}
