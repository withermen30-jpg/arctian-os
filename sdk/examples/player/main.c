/* Medya Oynaticı — ABI v2 ses ornegi.
 *
 * C:/Music altindaki .wav dosyalarini listeler; calar, durdurur, ses
 * duzeyini ayarlar. Sürücü yoksa veya klasor bossa bellekte uretilen
 * test tonunu calar (wolf_audio_play_mem). Ilerleme cubugu wolf_timer
 * ile guncellenir; sure WAV basligindan uygulamada parse edilir.
 *
 * Derle : wolf build .        Calistir: wolf run player.wlf
 */
#include "wolf.h"

#define MAX_T 24
#define NAME_CAP 40

static char     g_paths[MAX_T][120];
static char     g_names[MAX_T][NAME_CAP];
static int      g_n;
static int      g_sel;

static wolf_id  g_list, g_prog, g_time, g_vol, g_now, g_stat;
static uint64_t g_start;
static int      g_dur;            /* ms; 0 = bilinmiyor */
static int      g_tone;           /* bellek tonu caliyor */

/* ---------- WAV basligindan sure (ms) ---------- */
static unsigned rd32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static unsigned rd16(const unsigned char *p) {
    return (unsigned)(p[0] | (p[1] << 8));
}

static int wav_ms(const unsigned char *d, int len) {
    if (len < 44) return -1;
    unsigned off = 12, bps = 0, dsz = 0;
    while ((int)(off + 8) <= len) {
        const unsigned char *id = d + off;
        unsigned sz = rd32(d + off + 4), body = off + 8;
        if (id[0] == 'f' && id[1] == 'm') {
            if ((int)(body + 16) <= len) {
                unsigned rate = rd32(d + body + 4);
                unsigned ch = rd16(d + body + 2), bits = rd16(d + body + 14);
                if (rate && ch && bits) bps = rate * ch * bits / 8u;
            }
        } else if (id[0] == 'd' && id[1] == 'a' && id[2] == 't' && id[3] == 'a') {
            dsz = sz;
            if ((int)(body + dsz) > len) dsz = (unsigned)(len - (int)body);
            break;
        }
        off = body + sz + (sz & 1u);
    }
    if (!bps || !dsz) return -1;
    return (int)((unsigned long long)dsz * 1000ull / bps);
}

/* ---------- bellekte test tonu (ucgen dalga, 8 kHz 16-bit mono) ----- */
static unsigned char g_tone_wav[44 + 8000 * 2];        /* 1 sn */

static void put32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static void put16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

static void build_tone(void) {
    unsigned char *h = g_tone_wav;
    unsigned dsz = 8000 * 2;
    put32(h + 0, 0x46464952u);                 /* "RIFF" */
    put32(h + 4, 36 + dsz);
    put32(h + 8, 0x45564157u);                 /* "WAVE" */
    put32(h + 12, 0x20746d66u);                /* "fmt " */
    put32(h + 16, 16); put16(h + 20, 1); put16(h + 22, 1);
    put32(h + 24, 8000); put32(h + 28, 16000); put16(h + 32, 2); put16(h + 34, 16);
    put32(h + 36, 0x61746164u);                /* "data" */
    put32(h + 40, dsz);
    /* 440 Hz ucgen dalga, yavas sonan */
    short *s = (short *)(h + 44);
    unsigned acc = 0;
    for (int i = 0; i < 8000; i++) {
        acc += 3605u;                    /* 440 Hz * 65536 / 8000 */
        acc &= 0xFFFFu;
        unsigned tri = (acc < 32768u) ? acc : (65535u - acc);   /* 0..32767 */
        int env = 16384 - (i * 16384 / 8000) / 2;               /* sonluk */
        s[i] = (short)(((int)(tri / 2) - 8192) * env / 16384);
    }
}

/* ---------- dosya tarama ---------- */
static int ends_wav(const char *s) {
    int n = wolf_strlen(s);
    if (n < 5) return 0;
    const char *e = s + n - 4;
    return e[0] == '.' &&
           ((e[1] == 'w' || e[1] == 'W') && (e[2] == 'a' || e[2] == 'A') &&
            (e[3] == 'v' || e[3] == 'V'));
}

static void scan_dir(const char *dir) {
    if (!wolf_fs_count(dir)) return;
    int n = wolf_fs_count(dir);
    for (int i = 0; i < n && g_n < MAX_T; i++) {
        char name[80];
        int isd = 0;
        if (wolf_fs_list(dir, i, name, sizeof(name), &isd) != 0) continue;
        if (isd || !ends_wav(name)) continue;
        wolf_strcpy(g_paths[g_n], sizeof(g_paths[g_n]), dir);
        wolf_strcat(g_paths[g_n], sizeof(g_paths[g_n]), "/");
        wolf_strcat(g_paths[g_n], sizeof(g_paths[g_n]), name);
        wolf_strcpy(g_names[g_n], NAME_CAP, name);
        wolf_list_add(g_list, name);
        g_n++;
    }
}

/* ---------- oynatma ---------- */
static void fmt_time(int ms, char *out, int cap) {
    int s = ms / 1000;
    char b[8];
    wolf_strcpy(out, cap, "");
    wolf_itoa(s / 60, b, sizeof(b)); wolf_strcat(out, cap, b);
    wolf_strcat(out, cap, ":");
    int ss = s % 60;
    if (ss < 10) wolf_strcat(out, cap, "0");
    wolf_itoa(ss, b, sizeof(b)); wolf_strcat(out, cap, b);
}

static void play_track(int i) {
    if (i < 0 || i >= g_n) return;
    g_sel = i;
    g_tone = 0;
    if (wolf_audio_play(g_paths[i]) != 0) {
        wolf_text(g_stat, "calinamadi (WAV 8/16-bit PCM olmali)");
        return;
    }
    g_start = wolf_ticks_ms();
    unsigned char hdr[64];
    int rd = wolf_fs_read(g_paths[i], hdr, (int)sizeof(hdr));
    g_dur = rd > 0 ? wav_ms(hdr, rd) : -1;
    wolf_text(g_now, g_names[i]);
    wolf_text(g_stat, "caliniyor");
}

static void play_built_in(void) {
    build_tone();
    if (wolf_audio_play_mem(g_tone_wav, (int)sizeof(g_tone_wav)) != 0) {
        wolf_text(g_stat, "ses surucusu yok");
        return;
    }
    g_tone = 1;
    g_start = wolf_ticks_ms();
    g_dur = 1000;
    wolf_text(g_now, "Test tonu (440 Hz, bellekten)");
    wolf_text(g_stat, "caliniyor");
}

/* ---------- zamanlayici: ilerleme cubugu ---------- */
static void tick_cb(wolf_id t, void *user) {
    (void)t; (void)user;
    int playing = wolf_audio_playing();
    if (playing && g_dur > 0) {
        int el = (int)(wolf_ticks_ms() - g_start);
        int pct = el * 100 / g_dur;
        if (pct > 100) pct = 100;
        wolf_value(g_prog, pct);
        char b[24], t1[8], t2[8];
        fmt_time(el > g_dur ? g_dur : el, t1, sizeof(t1));
        fmt_time(g_dur, t2, sizeof(t2));
        wolf_strcpy(b, sizeof(b), t1);
        wolf_strcat(b, sizeof(b), " / ");
        wolf_strcat(b, sizeof(b), t2);
        wolf_text(g_time, b);
    } else if (!playing) {
        wolf_value(g_prog, 0);
    }
}

/* ---------- olaylar ---------- */
static void on_pick(wolf_id w, void *user) {
    (void)user;
    int sel = wolf_list_selected(w);
    if (sel >= 0) play_track(sel);
}
static void on_play(wolf_id w, void *user) {
    (void)w; (void)user;
    if (wolf_audio_playing()) { wolf_audio_stop(); wolf_text(g_stat, "durdu"); return; }
    if (g_n) play_track(g_sel);
    else play_built_in();
}
static void on_stop(wolf_id w, void *user) {
    (void)w; (void)user;
    wolf_audio_stop();
    wolf_text(g_stat, "durduruldu");
}
static void on_tone(wolf_id w, void *user) {
    (void)w; (void)user;
    play_built_in();
}
static void on_vol(wolf_id w, void *user) {
    (void)user;
    wolf_audio_set_vol(wolf_value_of(w));
}

int wolf_main(const wolf_api_t *api) {
    wolf_bind(api);
    if (!wolf_ready()) return 1;
    wolf_win_title("Medya Oynatici");

    wolf_id root = wolf_root();
    if (!root) return 1;

    wolf_id head = wolf_label(root, 12, 8, "Medya Oynatici");
    wolf_style(head, WOLF_SP_TEXT_COLOR, 0xE2E8F0, 0);

    /* sol: sarki listesi */
    g_list = wolf_listbox(root, 12, 34, 210, 190);
    wolf_style(g_list, WOLF_SP_RADIUS, 10, 0);
    wolf_style(g_list, WOLF_SP_SHADOW_WIDTH, 10, 0);
    wolf_style(g_list, WOLF_SP_SHADOW_OPA, 40, 0);
    wolf_style(g_list, WOLF_SP_SHADOW_COLOR, 0x000000, 0);
    wolf_on_click(g_list, on_pick);

    scan_dir("C:/Music");
    if (!g_n) scan_dir("C:/Users/Public/Music");

    /* sag panel */
    wolf_id panel = wolf_panel(root, 234, 34, 234, 190);
    wolf_style(panel, WOLF_SP_RADIUS, 14, 0);
    wolf_style(panel, WOLF_SP_BG_COLOR, 0x1E293B, 0);
    wolf_style(panel, WOLF_SP_BG_GRAD_COLOR, 0x0F172A, 0);
    wolf_style(panel, WOLF_SP_BG_GRAD_DIR, 1, 0);
    wolf_style(panel, WOLF_SP_BG_OPA, 100, 0);
    wolf_style(panel, WOLF_SP_BORDER_WIDTH, 1, 0);
    wolf_style(panel, WOLF_SP_BORDER_COLOR, 0x334155, 0);
    wolf_style(panel, WOLF_SP_SHADOW_WIDTH, 14, 0);
    wolf_style(panel, WOLF_SP_SHADOW_OPA, 45, 0);
    wolf_style(panel, WOLF_SP_SHADOW_COLOR, 0x000000, 0);
    wolf_style(panel, WOLF_SP_SHADOW_OFS_Y, 4, 0);

    g_now = wolf_label(panel, 14, 14, g_n ? g_names[0] : "sarki secilmedi");
    wolf_style(g_now, WOLF_SP_TEXT_COLOR, 0xF8FAFC, 0);

    g_prog = wolf_progress(panel, 14, 48, 206, 8, 0);
    wolf_style(g_prog, WOLF_SP_BG_COLOR, 0x0F172A, 0);
    wolf_style(g_prog, WOLF_SP_BG_OPA, 100, 0);
    wolf_style(g_prog, WOLF_SP_BG_COLOR, 0x22C55E, WOLF_SEL(0, WOLF_PF_INDICATOR));
    wolf_style(g_prog, WOLF_SP_BG_GRAD_COLOR, 0x84CC16, WOLF_SEL(0, WOLF_PF_INDICATOR));
    wolf_style(g_prog, WOLF_SP_BG_GRAD_DIR, 2, WOLF_SEL(0, WOLF_PF_INDICATOR));
    wolf_style(g_prog, WOLF_SP_RADIUS, 999, 0);
    wolf_style(g_prog, WOLF_SP_RADIUS, 999, WOLF_SEL(0, WOLF_PF_INDICATOR));

    g_time = wolf_label(panel, 14, 62, "0:00 / 0:00");
    wolf_style(g_time, WOLF_SP_TEXT_COLOR, 0x64748B, 0);

    wolf_id b1 = wolf_button(panel, 14, 92, 72, 34, "Cal / Dur");
    wolf_style(b1, WOLF_SP_RADIUS, 17, 0);
    wolf_style(b1, WOLF_SP_BG_COLOR, 0x16A34A, 0);
    wolf_style(b1, WOLF_SP_BG_COLOR, 0x4ADE80, WOLF_SEL(WOLF_SF_HOVER, WOLF_PF_MAIN));
    wolf_on_click(b1, on_play);

    wolf_id b2 = wolf_button(panel, 94, 92, 72, 34, "Durdur");
    wolf_style(b2, WOLF_SP_RADIUS, 17, 0);
    wolf_style(b2, WOLF_SP_BG_COLOR, 0xDC2626, 0);
    wolf_style(b2, WOLF_SP_BG_COLOR, 0xF87171, WOLF_SEL(WOLF_SF_HOVER, WOLF_PF_MAIN));
    wolf_on_click(b2, on_stop);

    wolf_id b3 = wolf_button(panel, 174, 92, 46, 34, "Ton");
    wolf_style(b3, WOLF_SP_RADIUS, 17, 0);
    wolf_style(b3, WOLF_SP_BG_COLOR, 0x7C3AED, 0);
    wolf_style(b3, WOLF_SP_BG_COLOR, 0xA78BFA, WOLF_SEL(WOLF_SF_HOVER, WOLF_PF_MAIN));
    wolf_on_click(b3, on_tone);

    wolf_id vlabel = wolf_label(panel, 14, 140, "Ses");
    wolf_style(vlabel, WOLF_SP_TEXT_COLOR, 0x94A3B8, 0);
    g_vol = wolf_slider(panel, 44, 140, 146, 10, 80);
    wolf_style(g_vol, WOLF_SP_BG_COLOR, 0x334155, 0);
    wolf_style(g_vol, WOLF_SP_BG_OPA, 100, 0);
    wolf_style(g_vol, WOLF_SP_BG_COLOR, 0xF59E0B, WOLF_SEL(0, WOLF_PF_INDICATOR));
    wolf_style(g_vol, WOLF_SP_BG_COLOR, 0xFBBF24, WOLF_SEL(0, WOLF_PF_KNOB));
    wolf_style(g_vol, WOLF_SP_RADIUS, 999, WOLF_SEL(0, WOLF_PF_INDICATOR));
    wolf_on_change(g_vol, on_vol);
    wolf_audio_set_vol(80);

    g_stat = wolf_label(panel, 14, 162, "");
    wolf_style(g_stat, WOLF_SP_TEXT_COLOR, 0x94A3B8, 0);

    /* alt bilgi */
    wolf_id foot = wolf_label(root, 12, 234, "");
    wolf_style(foot, WOLF_SP_TEXT_COLOR, 0x64748B, 0);
    if (!wolf_audio_ok()) wolf_text(foot, "uyari: ses surucusu bulunamadi");
    else if (!g_n) wolf_text(foot, "C:/Music bossa 'Ton' dugmesiyle bellek tonunu dene");
    else {
        char b[40];
        wolf_strcpy(b, sizeof(b), "");
        wolf_itoa(g_n, b, sizeof(b));
        wolf_strcat(b, sizeof(b), " sarki bulundu (C:/Music)");
        wolf_text(foot, b);
    }

    wolf_timer(200, tick_cb, 0);
    return 0;
}
