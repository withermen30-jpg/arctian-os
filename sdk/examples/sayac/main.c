/* 0.7 test: rol=cli .wlf uygulamasi.
 *
 * Pencere ACMAZ. Terminalde calisir; ciktisi terminale yazilir. Boylece
 * "her uygulama UI olmak zorunda degil" kurali uctan uca denenir.
 *
 * Derle (WSL): sdk/build.sh sdk/examples/sayac
 * Arctian'da : wf install sayac.wlf   ->   wf run sayac
 */
#include "wolf.h"

int wolf_main(const wolf_api_t *api) {
    wolf_bind(api);
    if (!wolf_ready()) return 1;

    char num[16];
    const char *arg = wolf_arg();

    wolf_log("sayac: rol=cli, pencere yok, terminalde calisiyor");
    if (arg && arg[0]) {
        wolf_log("arguman: ");
        wolf_log(arg);
    }
    for (int i = 1; i <= 5; i++) {
        wolf_itoa(i, num, sizeof(num));
        wolf_log(num);
    }
    wolf_log("sayac: bitti");
    return 0;
}
