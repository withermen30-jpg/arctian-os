/* Arctian Asistani - uygulama penceresi (0.7)
 *
 * Asistan daha once yalnizca gorev cubugundaki dugmeden acilan bir kenar
 * paneliydi. 0.7'de ayni motor (desktop/assistant.c) ayri bir uygulama
 * olarak da acilir: baslat menusunde "Asistan" olarak gorunur, `run asistan`
 * ile calisir.
 *
 * Motorla iliski tek yonlu: pencere `assistant_set_sink` ile kendi balon
 * cizicisini baglar, `assistant_ask` ile cumleyi motora verir. Pencere
 * kapaninca kanca birakilir; panel yeniden varsayilan hedefe yazar.
 */
#include "app.h"
#include "dsk.h"
#include "adl_wolf.h"
#include "lv_port.h"
#include <stdint.h>

static lv_obj_t *s_list;
static lv_obj_t *s_input;
static lv_obj_t *s_win;
static lv_group_t *s_group;

static lv_obj_t *as_label(lv_obj_t *p, const char *txt, uint32_t col)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_style_text_font(l, adl_font(), 0);
    return l;
}

static void scroll_end(void)
{
    lv_obj_update_layout(s_list);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_scroll_to_y(s_list, lv_obj_get_scroll_bottom(s_list) + 100000,
                       LV_ANIM_OFF);
}

/* Tek satirlik balon: mine=1 ise sagda ve vurgu renginde. */
static void bubble(const char *txt, int mine)
{
    if (!s_list) return;
    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, mine ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *b = lv_obj_create(row);
    lv_obj_remove_style_all(b);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(b, LV_PCT(mine ? 84 : 88));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(b, ui_s(12), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(mine ? ADL_ACCENT : ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(b, ui_s(10), 0);
    lv_obj_set_style_border_width(b, mine ? 0 : 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(ADL_BORDER), 0);

    lv_obj_t *l = as_label(b, txt, mine ? 0xFFFFFF : ADL_TEXT);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));

    scroll_end();
}

/* Motorun yanitlari buraya dokulur. */
static void app_sink(const char *txt) { bubble(txt, 0); }

static void app_send(void)
{
    if (!s_input || !s_list) return;
    const char *q = lv_textarea_get_text(s_input);
    if (!q || !q[0]) return;
    /* Metni KOPYALA: lv_textarea_set_text tamponu temizler ve q bosalirdi
     * (yanit hic uretilmiyordu). */
    char line[160];
    int i = 0;
    for (; q[i] && i < (int)sizeof(line) - 1; i++) line[i] = q[i];
    line[i] = 0;

    lv_textarea_set_text(s_input, "");
    bubble(line, 1);
    assistant_ask(line);
}

static void send_cb(lv_event_t *e) { (void)e; app_send(); }

static void chip_cb(lv_event_t *e)
{
    const char *txt = (const char *)lv_event_get_user_data(e);
    if (!txt || !s_input) return;
    lv_textarea_set_text(s_input, txt);
    app_send();
}

static void chip(lv_obj_t *p, const char *txt)
{
    lv_obj_t *c = lv_obj_create(p);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(c, LV_SIZE_CONTENT, ui_s(30));
    lv_obj_set_style_radius(c, ui_s(15), 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(ADL_BORDER), 0);
    lv_obj_set_style_pad_hor(c, ui_s(12), 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, chip_cb, LV_EVENT_CLICKED, (void *)txt);
    lv_obj_t *l = as_label(c, txt, ADL_TEXT);
    lv_obj_center(l);
}

static void win_del_cb(lv_event_t *e)
{
    (void)e;
    assistant_set_sink(0);      /* panel yeniden varsayilan hedefe yazar */
    /* klavye grubu acilisda yaratiliyordu ama hic silinmiyordu */
    if (s_group) {
        lv_indev_t *kp = lv_port_get_keypad();
        if (kp && lv_indev_get_group(kp) == s_group) lv_indev_set_group(kp, 0);
        lv_group_delete(s_group);
        s_group = 0;
    }
    s_list = s_input = s_win = 0;
}

void app_entry(lv_obj_t *win, asi_t *asi)
{
    (void)asi;
    s_win = win;
    s_list = s_input = 0;

    lv_obj_set_style_pad_all(win, ui_s(14), 0);
    lv_obj_set_style_pad_row(win, ui_s(8), 0);
    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_add_event_cb(win, win_del_cb, LV_EVENT_DELETE, 0);

    /* Baslik seridi */
    lv_obj_t *hdr = lv_obj_create(win);
    lv_obj_remove_style_all(hdr);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(hdr, LV_PCT(100));
    lv_obj_set_height(hdr, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_COLUMN);
    as_label(hdr, "Arctian Asistan", ADL_TEXT);
    lv_obj_t *sub = as_label(hdr,
        "Yerel calisir; internete veri gondermez.", ADL_TEXT_DIM);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);

    /* Sohbet listesi */
    s_list = lv_obj_create(win);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, ui_s(8), 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    /* Oneri cipleri */
    lv_obj_t *chips = lv_obj_create(win);
    lv_obj_remove_style_all(chips);
    lv_obj_remove_flag(chips, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(chips, LV_PCT(100));
    lv_obj_set_height(chips, ui_s(34));
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chips, ui_s(6), 0);
    chip(chips, "Sistem durumu");
    chip(chips, "Saat kac");
    chip(chips, "Terminal");
    chip(chips, "Yardim");

    /* Giris alani */
    s_input = lv_textarea_create(win);
    lv_obj_remove_style_all(s_input);
    lv_textarea_set_one_line(s_input, true);
    lv_textarea_set_max_length(s_input, 120);
    lv_textarea_set_placeholder_text(s_input, "Bir sey yazin: \"terminal ac\"");
    lv_obj_set_width(s_input, LV_PCT(100));
    lv_obj_set_height(s_input, ui_s(44));
    lv_obj_set_style_bg_color(s_input, lv_color_hex(ADL_SURFACE_2), 0);
    lv_obj_set_style_bg_opa(s_input, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_input, ui_s(12), 0);
    lv_obj_set_style_pad_all(s_input, ui_s(10), 0);
    lv_obj_set_style_text_color(s_input, lv_color_hex(ADL_TEXT), 0);
    lv_obj_set_style_text_font(s_input, adl_font(), 0);
    lv_obj_add_event_cb(s_input, send_cb, LV_EVENT_READY, 0);

    lv_group_t *g = lv_group_create();
    s_group = g;
    lv_group_add_obj(g, s_input);
    lv_group_focus_obj(s_input);
    lv_indev_t *kp = lv_port_get_keypad();
    if (kp) lv_indev_set_group(kp, g);

    /* Motor yanitlarini bu pencereye yonlendir. */
    assistant_set_sink(app_sink);
    app_sink("Merhaba! Bir uygulama acmami isterseniz adini yazin "
             "(ornek: \"terminal ac\"), durum sorabilir ya da not alabilirsiniz.");
}
