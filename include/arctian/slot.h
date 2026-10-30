/**
 * Arctian A/B sistem yuvasi.
 *
 * Durum STATE_LBA (2050) icinde tek sektorde tutulur. Iki yuva vardir:
 * 0 = A, 1 = B. Guncelleme once "bekleyen" yuvaya yazilir, dogrulanir,
 * en son durum kaydi guncellenir. Elektrik kesilirse etkin yuva degismez.
 *
 * Yeni yuva masaustune ulasirsa boot_ok=1 yazilir. Acilis, bekleyen yuvayi
 * denemis ve boot_ok gormemis ise bir onceki yuvaya doner.
 */
#ifndef ARCTIAN_SLOT_H
#define ARCTIAN_SLOT_H

#include <stdint.h>
#include "asi.h"

#define SLOT_MAGIC      0x544C4F53u  /* "SOLT" */
#define SLOT_VERSION    1u
#define SLOT_A          0
#define SLOT_B          1

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;          /* sizeof(slot_state_t) */
    uint8_t  active;        /* 0 = A, 1 = B */
    uint8_t  pending;       /* 0xFF = yok */
    uint8_t  boot_ok;       /* etkin yuva masaustune ulasti mi */
    uint8_t  tries;         /* bekleyen yuva deneme sayaci */
    uint32_t crc;           /* onceki alanlar uzerinde */
} slot_state_t;

/* Asi tutulur; durum sektoru okunur. */
void slot_init(asi_t *asi);

/* Kalici durumu okur (0 = ok). */
int  slot_read(slot_state_t *out);

/* Durumu sektore yazar (0 = ok). Yazma sirasi: govde -> dogrulama -> kayit. */
int  slot_write(const slot_state_t *s);

int  slot_active(void);          /* etkin yuva */
int  slot_pending(void);         /* bekleyen yuva veya 0xFF */

/* Etkin yuvayi "saglam" isaretler (masaustu acildi). */
int  slot_mark_boot_ok(void);

/* Bekleyen yuvayi etkin yapar; boot_ok=0. Elektrik kesilirse eski yuva kalir. */
int  slot_switch_pending(void);

/* Acilista cagrilir: onceki deneme boot_ok vermediyse eski yuvaya doner.
 * 1 = geri donuldu, 0 = degisiklik yok. */
int  slot_boot_guard(int *from_slot, int *to_slot);

/* ---- oz-test kancasi ----
 * Gercek disk yazimini engeller; testte bellek ici sektor kullanilir.
 * cut_at > 0 ise yazma bu adimda "kesilir" (0 = kesme yok):
 *   1 = govde yazildi, kayit yazilmadi
 *   2 = yalniz kayit yazildi (govde yok) */
void slot_test_begin(void);
void slot_test_end(void);
void slot_test_cut(int cut_at);
int  slot_test_cut_hit(void);

#endif /* ARCTIAN_SLOT_H */
