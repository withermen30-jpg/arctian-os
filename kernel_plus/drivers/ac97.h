#ifndef AC97_H
#define AC97_H

#include <stdint.h>
#include <stdbool.h>

#define AC97_SR_FIFOE 0x0010u
#define AC97_SR_BCIS  0x0008u
#define AC97_SR_LVBCI 0x0004u
#define AC97_SR_CELV  0x0002u
#define AC97_SR_DCH   0x0001u

int      ac97_init(void);
bool     ac97_present(void);

void     ac97_set_rate(uint32_t hz);
void     ac97_set_volume(int percent);
uint32_t ac97_rate(void);

void     ac97_reset_dma(void);
void     ac97_set_bdbar(uint32_t phys);
void     ac97_set_lvi(uint8_t idx);
void     ac97_start_dma(void);
void     ac97_stop_dma(void);
uint8_t  ac97_civ(void);
uint16_t ac97_status(void);

#endif
