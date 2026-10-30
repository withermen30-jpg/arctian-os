/**
 * Arctian cokme koruma (crash recovery).
 *
 * Dinamik/kayitli bir uygulama acilirken masaustu bir kurtarma noktasi
 * (setjmp) kaydeder ve kernel'e bildirir. Uygulama kodu calisirken bir CPU
 * istisnasi (page fault, GPF, gecersiz opcode...) olusursa veya uygulama
 * belirlenen sureden (varsayilan 5 sn) uzun sure yuklemede kalirsa, kernel
 * panic etmek yerine bu noktaya geri doner. Masaustu ardindan cokme
 * diyalogunu gosterir ve uygulama penceresini kapatir. Isletim sistemi
 * ayakta kalir.
 *
 * Onemli: yapi duzeni (offsetler) lib/recover.asm ile birebir ayni olmalidir.
 */
#ifndef ARCTIAN_RECOVER_H
#define ARCTIAN_RECOVER_H

#include <stdint.h>

typedef struct {
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;        /* setjmp sonrasi yigin isaretcisi */
    uint64_t rip;        /* setjmp donus adresi */
    uint64_t rflags;
    volatile uint32_t active;      /* 1 = kurtarma etkin */
    volatile uint32_t deadline_ms; /* mutlak zaman asimi (ms) */
    volatile int      code;        /* longjmp kodu (1 istisna, 2 zaman asimi) */
    char              reason[160]; /* insan-okur cokme sebebi */
} arctian_recover_t;

/* 0 doner (ilk cagri). Kurtarma tetiklenince longjmp ile nonzero koda doner. */
int  arctian_setjmp(arctian_recover_t *ctx);
void arctian_longjmp(arctian_recover_t *ctx, int code) __attribute__((noreturn));

#endif /* ARCTIAN_RECOVER_H */
