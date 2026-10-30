/* 0.7 test: rol=runtime .wlf uygulamasi (minik "python" benzeri calisma
 * zamani). Kendi basina acilinca pencere ACMAZ; terminal acilir ve dosya
 * ister. ".py" dosyasina cift tiklaninca bu uygulama secilir ve dosya
 * arguman olarak gelir.
 *
 * Derle (WSL): sdk/build.sh sdk/examples/pyrt
 */
#include "wolf.h"

int wolf_main(const wolf_api_t *api) {
    wolf_bind(api);
    if (!wolf_ready()) return 1;

    const char *path = wolf_arg();
    if (!path || !path[0]) {
        wolf_log("pyrt: dosya verilmedi");
        wolf_log("kullanim: wf run pyrt C:/ornek.py");
        wolf_log("(.py dosyasina cift tiklamak da bu uygulamayi secer)");
        return 0;
    }

    static char buf[4096];
    int n = wolf_fs_read(path, buf, sizeof(buf) - 1);
    if (n < 0) {
        wolf_log("pyrt: dosya okunamadi:");
        wolf_log(path);
        return 2;
    }
    buf[n] = 0;
    wolf_log("pyrt 0.1 - demo calisma zamani");
    wolf_log(path);

    char line[200];
    int li = 0;
    for (int i = 0; i <= n; i++) {
        char c = (i < n) ? buf[i] : '\n';
        if (c == '\r') continue;
        if (c == '\n') {
            line[li] = 0;
            if (li) wolf_log(line);
            li = 0;
        } else if (li < (int)sizeof(line) - 1) {
            line[li++] = c;
        }
    }
    wolf_log("pyrt: bitti");
    return 0;
}
