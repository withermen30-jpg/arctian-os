/* Ornek Wolf uygulamasi (native .wlf)
 *
 * Derle (Windows):  ..\..\build.bat
 * Derle (Linux/WSL): ../../build.sh
 * Arctian'da:        wf install merhaba.wlf  →  wf run merhaba
 */
#include "wolf.h"

int tik = 0;
int bar = 0;
int durum = 0;

void artir(int w, void *user) {
    tik = tik + 1;
    wolf_value(bar, tik * 10);
    if (tik == 10) {
        wolf_text(durum, "On tik tamamlandi!");
        wolf_notify("Merhaba", "Sayac 10'a ulasti");
    } else {
        wolf_text(durum, "Devam edin");
    }
}

void sifirla(int w, void *user) {
    tik = 0;
    wolf_value(bar, 0);
    wolf_text(durum, "Sifirlandi");
}

int wolf_main(const wolf_api_t *api) {
    wolf_bind(api);
    if (!wolf_ready()) return 1;

    int root = wolf_root();
    wolf_win_title("Merhaba Wolf");
    wolf_label(root, 20, 16, "libWolf ornek uygulamasi");
    wolf_separator(root, 20, 44, 400);

    int b1 = wolf_button(root, 20, 60, 140, 44, "Artir");
    int b2 = wolf_button(root, 172, 60, 140, 44, "Sifirla");
    bar = wolf_progress(root, 20, 120, 380, 18, 0);
    durum = wolf_label(root, 20, 150, "Hazir");
    wolf_label(root, 20, 190, "Dosya sistemi testi: /belgeler/wolf_test.txt");

    wolf_on_click(b1, artir);
    wolf_on_click(b2, sifirla);

    wolf_fs_write("C:/belgeler/wolf_test.txt", "libWolf calisiyor\n", 19);
    return 0;
}
