#ifndef ARCTIAN_LV_PORT_H
#define ARCTIAN_LV_PORT_H

#include "lvgl.h"
#include "asi.h"
#include <stdint.h>

void lv_port_init(asi_t *asi);
void lv_port_tick_advance(uint32_t ms);

/* Biriken kirli dikdortgeni alir (ekran koordinati) ve sifirlar.
 * 1 = gecerli alan var, 0 = bu karede cizim yok (sunum gerekmez). */
int  lv_port_take_dirty(int *x, int *y, int *w, int *h);

void lv_port_set_resolution(int w, int h);

/* Pointer input device (fare imleci icin) */
lv_indev_t *lv_port_get_indev(void);

/* Fiziksel klavye input device (keypad). */
lv_indev_t *lv_port_get_keypad(void);

#endif /* ARCTIAN_LV_PORT_H */
