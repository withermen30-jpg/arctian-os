#include "slot.h"
#include "superblock.h"
#include <stdint.h>

static asi_t   *s_asi;
static int      s_test;
static int      s_cut;        /* 0 = yok, >0 = kesme noktasi */
static int      s_cut_hit;
static uint8_t  s_test_sec[512];

static uint32_t scrc(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

static uint32_t state_crc(const slot_state_t *s) {
    return scrc((const uint8_t *)s, (uint32_t)(sizeof(*s) - sizeof(s->crc)));
}

void slot_init(asi_t *asi) { s_asi = asi; }

void slot_test_begin(void) {
    s_test = 1;
    s_cut = 0;
    s_cut_hit = 0;
    for (int i = 0; i < (int)sizeof(s_test_sec); i++) s_test_sec[i] = 0;
    /* Taze kurulum: A etkin ve saglam. */
    slot_state_t s;
    s.magic = SLOT_MAGIC;
    s.version = SLOT_VERSION;
    s.size = (uint16_t)sizeof(s);
    s.active = SLOT_A;
    s.pending = 0xFF;
    s.boot_ok = 1;
    s.tries = 0;
    s.crc = state_crc(&s);
    for (uint32_t i = 0; i < sizeof(s); i++) s_test_sec[i] = ((const uint8_t *)&s)[i];
}

void slot_test_end(void) { s_test = 0; s_cut = 0; s_cut_hit = 0; }
void slot_test_cut(int cut_at) { s_cut = cut_at; s_cut_hit = 0; }
int  slot_test_cut_hit(void) { return s_cut_hit; }

int slot_read(slot_state_t *out) {
    if (!out) return -1;
    uint8_t buf[512];
    int rc = -1;

    if (s_test) {
        for (int i = 0; i < 512; i++) buf[i] = s_test_sec[i];
        rc = 0;
    } else if (s_asi && s_asi->storage && s_asi->storage->read_sectors) {
        if (s_asi->storage->read_sectors(STATE_LBA, 1, buf) == 0) rc = 0;
    }
    if (rc != 0) return -2;

    slot_state_t s;
    for (uint32_t i = 0; i < sizeof(s); i++) ((uint8_t *)&s)[i] = buf[i];
    if (s.magic != SLOT_MAGIC || s.version != SLOT_VERSION) return -3;
    if (s.size != (uint16_t)sizeof(s)) return -3;
    if (s.crc != state_crc(&s)) return -3;
    if (s.active > 1) return -3;
    *out = s;
    return 0;
}

int slot_write(const slot_state_t *s) {
    if (!s) return -1;
    slot_state_t c = *s;
    c.magic = SLOT_MAGIC;
    c.version = SLOT_VERSION;
    c.size = (uint16_t)sizeof(c);
    c.crc = state_crc(&c);

    if (s_test) {
        if (s_cut == 1) {            /* govde yazildi, kayit (durum) yazilmadi */
            s_cut_hit = 1;
            return -2;
        }
        for (uint32_t i = 0; i < sizeof(c); i++) s_test_sec[i] = ((const uint8_t *)&c)[i];
        return 0;
    }
    if (!s_asi || !s_asi->storage || !s_asi->storage->write_sectors) return -2;
    uint8_t buf[512];
    for (int i = 0; i < 512; i++) buf[i] = 0;
    for (uint32_t i = 0; i < sizeof(c); i++) buf[i] = ((const uint8_t *)&c)[i];
    if (s_asi->storage->write_sectors(STATE_LBA, 1, buf) != 0) return -3;
    return 0;
}

int slot_active(void) {
    slot_state_t s;
    return (slot_read(&s) == 0) ? (int)s.active : SLOT_A;
}

int slot_pending(void) {
    slot_state_t s;
    return (slot_read(&s) == 0) ? (int)s.pending : 0xFF;
}

int slot_mark_boot_ok(void) {
    slot_state_t s;
    if (slot_read(&s) != 0) return -1;
    if (s.boot_ok == 1) return 0;
    s.boot_ok = 1;
    return slot_write(&s);
}

int slot_switch_pending(void) {
    slot_state_t s;
    if (slot_read(&s) != 0) return -1;
    int cand = (s.pending != 0xFF) ? (int)s.pending : (int)(1 - s.active);
    if (cand == (int)s.active) return -1;
    s.active = (uint8_t)cand;
    s.pending = 0xFF;
    s.boot_ok = 0;                 /* masaustune ulasirsa 1 yazilir */
    if (s.tries < 3) s.tries++;
    return slot_write(&s);
}

int slot_boot_guard(int *from_slot, int *to_slot) {
    slot_state_t s;
    if (slot_read(&s) != 0) return 0;
    if (s.boot_ok) return 0;
    /* Etkin yuva masaustune ulasamadi: onceki yuvaya don. */
    int bad = s.active;
    s.active = (uint8_t)(1 - bad);
    s.pending = 0xFF;
    s.boot_ok = 1;
    if (slot_write(&s) != 0) return 0;
    if (from_slot) *from_slot = bad;
    if (to_slot) *to_slot = s.active;
    return 1;
}
