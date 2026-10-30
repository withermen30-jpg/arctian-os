#include "dsk.h"



/* ------------------------------------------------------------------ */
/* Rust uygulamalari icin kolaylik FFI (disaridan linklenir)          */
/*   arctian_net_http_get(asi, url, &len) -> govde                    */
/*     Rust nesneleri --just-symbols=desktop.elf ile bu sembolleri     */
/*     cozer. Bunlar C ABI'dir; uygulama dili C/Zig/Rust olabilir.    */
/* ------------------------------------------------------------------ */
const char *arctian_net_http_get(void *asi, const char *url, uint32_t *len) {
    asi_t *a = (asi_t *)asi;
    return (a && a->net && a->net->http_get) ? a->net->http_get(url, len) : 0;
}

const char *arctian_net_https_get(void *asi, const char *url, uint32_t *len) {
    asi_t *a = (asi_t *)asi;
    return (a && a->net && a->net->https_get) ? a->net->https_get(url, len) : 0;
}

int arctian_net_link_type(void *asi) {
    asi_t *a = (asi_t *)asi;
    return (a && a->net && a->net->link_type) ? a->net->link_type() : 0;
}

