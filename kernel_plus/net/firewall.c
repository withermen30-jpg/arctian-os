#include "firewall.h"
#include "ipv4.h"
#include "tcp.h"
#include "afs.h"
#include "kernel.h"
#include "serial.h"
#include <string.h>

#define FW_FILE "firewall.cfg"
#define FW_MAGIC0 'A'
#define FW_MAGIC1 'F'
#define FW_MAGIC2 'W'
#define FW_MAGIC3 '1'

static asi_fw_rule_t s_rules[ASI_FW_MAX_RULES];
static int s_count = 0;
static int s_enabled = 0;
static int s_loaded = 0;

static void fw_ensure_loaded(void) {
    if (s_loaded) return;
    if (afs_is_locked()) return;
    firewall_load();
}

int firewall_enabled(void) {
    fw_ensure_loaded();
    return s_enabled ? 1 : 0;
}

int firewall_count(void) {
    fw_ensure_loaded();
    return s_count;
}

int firewall_get_rule(int idx, asi_fw_rule_t *out) {
    fw_ensure_loaded();
    if (!out || idx < 0 || idx >= s_count) return -1;
    *out = s_rules[idx];
    return 0;
}

static int fw_match(const asi_fw_rule_t *r, int dir, uint8_t proto,
                    const uint8_t ip[4], uint16_t port) {
    if (!r->enabled) return 0;
    if (r->dir != ASI_FW_DIR_ANY && r->dir != dir) return 0;
    if (r->proto != ASI_FW_PROTO_ANY && r->proto != proto) return 0;

    int any_ip = 1;
    for (int i = 0; i < 4; i++) {
        if (r->ip[i] || r->mask[i]) { any_ip = 0; break; }
    }
    if (!any_ip) {
        for (int i = 0; i < 4; i++) {
            if ((r->ip[i] & r->mask[i]) != (ip[i] & r->mask[i])) return 0;
        }
    }

    if (r->port_lo || r->port_hi) {
        uint16_t lo = r->port_lo;
        uint16_t hi = r->port_hi ? r->port_hi : r->port_lo;
        if (hi < lo) { uint16_t t = lo; lo = hi; hi = t; }
        if (port < lo || port > hi) return 0;
    }
    return 1;
}

int firewall_check(int dir, uint8_t proto, const uint8_t ip[4], uint16_t port) {
    fw_ensure_loaded();
    if (!s_enabled) return 1;
    for (int i = 0; i < s_count; i++) {
        if (fw_match(&s_rules[i], dir, proto, ip, port)) {
            s_rules[i].hits++;
            return (s_rules[i].action == ASI_FW_ALLOW) ? 1 : 0;
        }
    }
    return 1;
}

int firewall_outbound(uint8_t proto, const uint8_t dst_ip[4], uint16_t dst_port) {
    return firewall_check(ASI_FW_DIR_OUT, proto, dst_ip, dst_port);
}

int firewall_inbound(uint8_t proto, const uint8_t src_ip[4], uint16_t dst_port) {
    return firewall_check(ASI_FW_DIR_IN, proto, src_ip, dst_port);
}

int firewall_set_enabled(int on) {
    fw_ensure_loaded();
    s_enabled = on ? 1 : 0;
    firewall_save();
    return 0;
}

static void fw_sanitize(asi_fw_rule_t *r) {
    if (r->dir != ASI_FW_DIR_IN && r->dir != ASI_FW_DIR_OUT &&
        r->dir != ASI_FW_DIR_ANY) r->dir = ASI_FW_DIR_ANY;
    if (r->proto != ASI_FW_PROTO_TCP && r->proto != ASI_FW_PROTO_UDP &&
        r->proto != ASI_FW_PROTO_ANY) r->proto = ASI_FW_PROTO_ANY;
    r->action = (r->action == ASI_FW_ALLOW) ? ASI_FW_ALLOW : ASI_FW_DENY;
    r->enabled = r->enabled ? 1 : 0;
    r->name[ASI_FW_NAME_MAX - 1] = 0;
}

int firewall_add_rule(const asi_fw_rule_t *r) {
    fw_ensure_loaded();
    if (!r || s_count >= ASI_FW_MAX_RULES) return -1;
    s_rules[s_count] = *r;
    fw_sanitize(&s_rules[s_count]);
    s_rules[s_count].hits = 0;
    int idx = s_count++;
    firewall_save();
    return idx;
}

int firewall_remove_rule(int idx) {
    fw_ensure_loaded();
    if (idx < 0 || idx >= s_count) return -1;
    for (int i = idx; i < s_count - 1; i++) s_rules[i] = s_rules[i + 1];
    s_count--;
    if (s_count < 0) s_count = 0;
    firewall_save();
    return 0;
}

int firewall_clear(void) {
    s_count = 0;
    firewall_save();
    return 0;
}

int firewall_save(void) {
    if (afs_is_locked()) return -1;

    static uint8_t buf[12 + ASI_FW_MAX_RULES * sizeof(asi_fw_rule_t)];
    uint32_t en = (uint32_t)(s_enabled ? 1 : 0);
    uint32_t cnt = (uint32_t)s_count;
    buf[0] = FW_MAGIC0; buf[1] = FW_MAGIC1; buf[2] = FW_MAGIC2; buf[3] = FW_MAGIC3;
    memcpy(buf + 4, &en, 4);
    memcpy(buf + 8, &cnt, 4);
    if (cnt) memcpy(buf + 12, s_rules, cnt * sizeof(asi_fw_rule_t));
    uint32_t len = 12 + cnt * (uint32_t)sizeof(asi_fw_rule_t);

    int uid = 0, gid = 0;
    afs_get_subject(&uid, &gid);
    afs_set_subject(0, 0);
    void *h = afs_create_handle(FW_FILE);
    int rc = h ? afs_write_handle(h, buf, len) : -1;
    afs_set_subject(uid, gid);
    s_loaded = 1;
    return rc;
}

int firewall_load(void) {
    if (afs_is_locked()) return -1;

    static uint8_t buf[12 + ASI_FW_MAX_RULES * sizeof(asi_fw_rule_t)];
    uint32_t n = afs_read_path(FW_FILE, buf, sizeof(buf));
    s_loaded = 1;
    if (n < 12) return -1;
    if (buf[0] != FW_MAGIC0 || buf[1] != FW_MAGIC1 ||
        buf[2] != FW_MAGIC2 || buf[3] != FW_MAGIC3) return -1;

    uint32_t en = 0, cnt = 0;
    memcpy(&en, buf + 4, 4);
    memcpy(&cnt, buf + 8, 4);
    s_enabled = en ? 1 : 0;
    if (cnt > ASI_FW_MAX_RULES) cnt = ASI_FW_MAX_RULES;
    uint32_t avail = (n - 12) / (uint32_t)sizeof(asi_fw_rule_t);
    if (cnt > avail) cnt = avail;
    if (cnt) memcpy(s_rules, buf + 12, cnt * sizeof(asi_fw_rule_t));
    s_count = (int)cnt;
    return 0;
}

void firewall_init(void) {
    memset(s_rules, 0, sizeof(s_rules));
    s_count = 0;
    s_enabled = 0;
    s_loaded = 0;
}

int firewall_selftest(void) {
    asi_fw_rule_t save[ASI_FW_MAX_RULES];
    int sc = s_count, se = s_enabled;
    memcpy(save, s_rules, sizeof(save));

    asi_fw_rule_t r;
    memset(&r, 0, sizeof(r));
    r.dir = ASI_FW_DIR_OUT;
    r.proto = ASI_FW_PROTO_TCP;
    r.action = ASI_FW_DENY;
    r.enabled = 1;
    r.port_lo = 9999;
    r.port_hi = 9999;
    const char *nm = "selftest";
    for (int i = 0; i < 8; i++) r.name[i] = nm[i];

    memset(s_rules, 0, sizeof(s_rules));
    s_rules[0] = r;
    s_count = 1;
    s_enabled = 1;
    s_loaded = 1;

    uint8_t dip[4] = {10, 0, 2, 2};
    int blocked = (firewall_outbound(IPV4_PROTO_TCP, dip, 9999) == 0);
    int allowed = (firewall_outbound(IPV4_PROTO_TCP, dip, 80) == 1);
    int conn = tcp_connect(dip, 9999);

    s_count = sc;
    s_enabled = se;
    memcpy(s_rules, save, sizeof(save));

    int ok = blocked && allowed && conn < 0;

    int pers = 0;
    if (!afs_is_locked()) {
        s_count = 0; s_enabled = 0; s_loaded = 1;
        firewall_save();
        s_rules[0] = r; s_count = 1; s_enabled = 1; s_loaded = 1;
        firewall_save();
        memset(s_rules, 0, sizeof(s_rules));
        s_count = 0; s_enabled = 0; s_loaded = 0;
        firewall_load();
        pers = (s_count == 1 && s_enabled == 1 &&
                s_rules[0].port_lo == 9999 && s_rules[0].action == ASI_FW_DENY);
        s_count = 0; s_enabled = 0; s_loaded = 1;
        firewall_save();
    }

    serial_printf("[fw] selftest blok=%d izin=%d conn=%d kalici=%d -> %s\n",
                  blocked, allowed, conn, pers, ok ? "OK" : "HATA");
    return ok ? 0 : 1;
}
