#include "dsk.h"
#include "version.h"        /* surum: version.json -> ARCTIAN_VERSION */

void build_welcome_window(void) {
    lv_obj_t *w = create_window("Arctian'a Hos Geldiniz", 560, 300);
    lv_obj_t *body = label(w,
        "Arctian a hoşgeldiniz.\n\n"
        "Sizler için özelleştirilmiş sistemi ile,\n"
        "modern ve sadece görünümlü arctian " ARCTIAN_VERSION " artık hazır.\n\n"
        "Bu pencereyi sag ustteki X ile kapatabilirsin.",
        ADL_TEXT_DIM);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 20, 56);
}
