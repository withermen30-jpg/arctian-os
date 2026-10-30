#ifndef ARCTIAN_ADL_WOLF_H
#define ARCTIAN_ADL_WOLF_H

#include "lvgl.h"

/* ADL-Wolf renk token'lari (koyu, canli accent). Degerler yuksek kontrast
 * moduna gore calisma zamaninda degisir; bu yuzden sabit deil fonksiyon
 * makrosudur (ADL_ACCENT ornegindeki desen). */
uint32_t   adl_bg(void);
uint32_t   adl_surface(void);
uint32_t   adl_surface_2(void);
uint32_t   adl_border(void);
uint32_t   adl_text(void);
uint32_t   adl_text_dim(void);
uint32_t   adl_danger(void);
uint32_t   adl_warn(void);
uint32_t   adl_ok(void);
#define ADL_BG         adl_bg()
#define ADL_SURFACE    adl_surface()
#define ADL_SURFACE_2  adl_surface_2()
#define ADL_BORDER     adl_border()
#define ADL_TEXT       adl_text()
#define ADL_TEXT_DIM   adl_text_dim()
#define ADL_DANGER     adl_danger()
#define ADL_WARN       adl_warn()
#define ADL_OK         adl_ok()

/* Vurgu rengi calisma zamaninda degistirilebilir (Ayarlar uygulamasi). */
uint32_t   adl_accent(void);
uint32_t   adl_accent_2(void);
void       adl_set_accent(uint32_t accent, uint32_t accent2);
#define ADL_ACCENT     adl_accent()
#define ADL_ACCENT_2   adl_accent_2()

/* Gomulu TTF'den olusturulan arayuz fontu (Turkce destekli) */
void       adl_font_init(void);
lv_font_t *adl_font(void);

/* Erisilebilirlik: yaz olcegi, yuksek kontrast, odak halkasi.
 * Degisiklikler yeni olusturulan nesnelere uygulanir; mevcut kabuk icin
 * settings.c -> shell_rebuild cagrisi gerekir. */
void       adl_set_font_scale(int percent);   /* 100/125/150 (min 80, max 200) */
int        adl_font_scale(void);
void       adl_font_apply_scale(void);        /* fontu yeniden olustur + temayi tazele */
void       adl_set_contrast(int on);          /* 1 = yuksek kontrast teması */
int        adl_contrast(void);
void       adl_set_light(int on);             /* 1 = acik tema */
int        adl_light(void);
void       adl_set_focus_ring(int on);        /* 1 = klavye gezinme odak halkasi */
int        adl_focus_ring(void);

/* lv_obj_remove_style_all() tema apply_cb'nin ekledigi odak halkasi stilini
 * de siler. Klavye gezinme grubundaki nesneler (masaustu ikonlari, gorev
 * cubugu dugmeleri) olusturulduktan hemen sonra bu cagriyi yapmalidir;
 * aksi halde odak halkasi hic cizilmez. */
void adl_focus_ring_attach(lv_obj_t *obj);

void adl_wolf_theme_init(void);

#endif /* ARCTIAN_ADL_WOLF_H */
