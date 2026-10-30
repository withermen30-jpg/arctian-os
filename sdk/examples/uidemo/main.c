/* 0.7 test: rol=ui .wlf uygulamasi (klasik pencere). */
#include "wolf.h"

int wolf_main(const wolf_api_t *api) {
    wolf_bind(api);
    if (!wolf_ready()) return 1;
    int root = wolf_root();
    wolf_win_title("UI Deneme");
    wolf_label(root, 20, 18, "rol=ui: pencere acildi");
    wolf_label(root, 20, 46, "diger roller terminalde calisir");
    (void)wolf_button(root, 20, 84, 160, 40, "Tamam");
    return 0;
}
