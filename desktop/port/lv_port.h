#ifndef ARCTIAN_LV_PORT_H
#define ARCTIAN_LV_PORT_H

#include "lvgl.h"
#include "asi.h"
#include <stdint.h>

void lv_port_init(asi_t *asi);
void lv_port_tick_advance(uint32_t ms);

lv_indev_t *lv_port_get_indev(void);

lv_indev_t *lv_port_get_keypad(void);

#endif
