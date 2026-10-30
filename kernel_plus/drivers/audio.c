#include "kernel.h"
#include "audio.h"
#include "ac97.h"
#include "alloc.h"

#define AUDIO_SLOTS      32
#define AUDIO_SLOT_BYTES 16384
#define AUDIO_RING_BYTES (AUDIO_SLOTS * AUDIO_SLOT_BYTES)

typedef struct {
    uint32_t addr;
    uint32_t ctl;
} __attribute__((packed)) audio_bd_t;

static audio_bd_t *s_bdl;
static uint8_t    *s_ring;

static int       s_ready;
static int       s_playing;
static int       s_started;
static const uint8_t *s_src;
static uint32_t  s_src_len;
static uint32_t  s_src_pos;
static int       s_bits;
static int       s_channels;
static uint32_t  s_rate;
static int       s_volume = 80;
static const char *s_err = "";

static int       s_tail;
static int       s_count;
static int       s_open;
static uint32_t  s_open_off;

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int16_t sample_at(const uint8_t *p, int bits) {
    if (bits == 8) return (int16_t)(((int)p[0] - 128) << 8);
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

int audio_present(void) { return s_ready; }
int audio_is_playing(void) { return s_playing; }
int audio_volume(void) { return s_volume; }
const char *audio_last_error(void) { return s_err ? s_err : ""; }
uint32_t audio_sample_rate(void) { return ac97_rate(); }

static void program_bd(int idx, uint32_t bytes) {
    s_bdl[idx].addr = (uint32_t)(uintptr_t)(s_ring + (uint32_t)idx * AUDIO_SLOT_BYTES);
    s_bdl[idx].ctl  = bytes / 2u;
}

static uint32_t convert_into(uint8_t *dst, uint32_t max_bytes) {
    uint32_t written = 0;
    int frame_bytes = (s_bits / 8) * s_channels;
    if (frame_bytes <= 0) return 0;

    while (written + 4u <= max_bytes) {
        if (s_src_pos + (uint32_t)frame_bytes > s_src_len) break;
        const uint8_t *f = s_src + s_src_pos;
        int16_t l, r;
        if (s_channels == 2) {
            l = sample_at(f, s_bits);
            r = sample_at(f + s_bits / 8, s_bits);
        } else {
            l = r = sample_at(f, s_bits);
        }
        dst[written + 0] = (uint8_t)(l & 0xFF);
        dst[written + 1] = (uint8_t)((l >> 8) & 0xFF);
        dst[written + 2] = (uint8_t)(r & 0xFF);
        dst[written + 3] = (uint8_t)((r >> 8) & 0xFF);
        written += 4u;
        s_src_pos += (uint32_t)frame_bytes;
    }
    return written;
}

void audio_service(void) {
    if (!s_ready || !s_playing) return;

    if (s_started) {
        if (ac97_status() & AC97_SR_DCH) {
            uint8_t civ = ac97_civ();
            s_tail = (int)((civ + 1u) & 31u);
            s_count = 0;
            s_open = s_tail;
            s_open_off = 0;
        } else {
            uint8_t civ = ac97_civ();
            while (s_count > 0 && (uint8_t)s_tail != civ) {
                s_tail = (s_tail + 1) & 31;
                s_count--;
            }
        }
    }

    int committed_new = 0;
    while (s_count < AUDIO_SLOTS) {
        if (s_open_off >= AUDIO_SLOT_BYTES) {
            program_bd(s_open, AUDIO_SLOT_BYTES);
            s_open = (s_open + 1) & 31;
            s_count++;
            s_open_off = 0;
            committed_new = 1;
            continue;
        }
        uint32_t n = convert_into(s_ring + (uint32_t)s_open * AUDIO_SLOT_BYTES + s_open_off,
                                  AUDIO_SLOT_BYTES - s_open_off);
        if (n == 0) {
            if (s_open_off > 0) {
                program_bd(s_open, s_open_off);
                s_open = (s_open + 1) & 31;
                s_count++;
                s_open_off = 0;
                committed_new = 1;
            }
            break;
        }
        s_open_off += n;
    }

    if (!s_started && s_count > 0) {
        ac97_set_lvi((uint8_t)((s_open - 1) & 31));
        ac97_start_dma();
        s_started = 1;
    } else if (s_started && committed_new) {
        ac97_set_lvi((uint8_t)((s_open - 1) & 31));
    }

    if (s_started && s_count == 0 && s_src_pos >= s_src_len &&
        (ac97_status() & AC97_SR_DCH)) {
        s_playing = 0;
        s_started = 0;
        ac97_stop_dma();
    }
}

void audio_stop(void) {
    s_playing = 0;
    s_started = 0;
    s_count = 0;
    s_tail = 0;
    s_open = 0;
    s_open_off = 0;
    s_src = 0;
    s_src_len = 0;
    s_src_pos = 0;
    if (s_ready) ac97_stop_dma();
}

int audio_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_volume = percent;
    if (s_ready) ac97_set_volume(percent);
    return percent;
}

int audio_play_wav(const void *data, uint32_t len) {
    const uint8_t *d = (const uint8_t *)data;
    s_err = "";

    if (!s_ready) { s_err = "ses surucusu yok"; return -1; }
    if (!d || len < 44) { s_err = "gecersiz WAV dosyasi"; return -1; }
    if (!(d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' &&
          d[8] == 'W' && d[9] == 'A' && d[10] == 'V' && d[11] == 'E')) {
        s_err = "WAV bicimi taninmadi";
        return -1;
    }

    uint16_t fmt = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t *pcm = 0;
    uint32_t pcmlen = 0;

    uint32_t off = 12;
    while (off + 8u <= len) {
        const uint8_t *id = d + off;
        uint32_t sz = rd32(d + off + 4);
        uint32_t body = off + 8u;
        if (id[0] == 'f' && id[1] == 'm' && id[2] == 't' && id[3] == ' ') {
            if (body + 16u <= len) {
                fmt = rd16(d + body);
                ch = rd16(d + body + 2);
                rate = rd32(d + body + 4);
                bits = rd16(d + body + 14);
            }
        } else if (id[0] == 'd' && id[1] == 'a' && id[2] == 't' && id[3] == 'a') {
            if (body < len) {
                pcm = d + body;
                pcmlen = sz;
                if (pcmlen > len - body) pcmlen = len - body;
            }
            break;
        }
        uint32_t adv = sz + (sz & 1u);
        if (adv < 8u) break;
        off = body + adv;
    }

    if (!pcm || fmt == 0) { s_err = "WAV verisi eksik"; return -1; }
    if (fmt != 1 && fmt != 0xFFFE) { s_err = "desteklenmeyen WAV bicimi"; return -1; }
    if (ch != 1 && ch != 2) { s_err = "yalniz mono/stereo"; return -1; }
    if (bits != 8 && bits != 16) { s_err = "yalniz 8/16-bit PCM"; return -1; }
    if (rate == 0) rate = 44100;
    if (pcmlen == 0) { s_err = "bos ses verisi"; return -1; }

    audio_stop();

    s_src = pcm;
    s_src_len = pcmlen;
    s_src_pos = 0;
    s_bits = bits;
    s_channels = ch;
    s_rate = rate;

    ac97_set_rate(rate);
    ac97_set_volume(s_volume);
    ac97_reset_dma();
    ac97_set_bdbar((uint32_t)(uintptr_t)s_bdl);

    s_tail = 0;
    s_count = 0;
    s_open = 0;
    s_open_off = 0;
    s_started = 0;
    s_playing = 1;

    audio_service();
    return 0;
}

int audio_init(void) {
    s_ready = 0;
    if (ac97_init() != 0) return -1;

    s_bdl = (audio_bd_t *)kmalloc((uint32_t)(AUDIO_SLOTS * sizeof(audio_bd_t)));
    s_ring = (uint8_t *)kmalloc(AUDIO_RING_BYTES);
    if (!s_bdl || !s_ring) return -1;

    for (int i = 0; i < AUDIO_SLOTS; i++) {
        s_bdl[i].addr = 0;
        s_bdl[i].ctl = 0;
    }

    s_ready = 1;
    kernel_debug("AUDIO: hazir (%d slot x %d bayt)", AUDIO_SLOTS, AUDIO_SLOT_BYTES);
    return 0;
}
