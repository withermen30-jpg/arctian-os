#include "dsk.h"


asi_t *g_asi = 0;

int s_desktop_started = 0;


lv_obj_t *g_start_menu;

lv_obj_t *g_taskbar;

tb_slot_t g_tb[TB_MAX_ICONS];

int g_tb_n = 0;

int g_tb_dirty = 0;

char g_tb_pins[TB_MAX_ICONS][24];

int g_tb_pin_n = 0;

lv_obj_t *g_tb_clock = 0;

lv_obj_t *g_date_label = 0;


/* Masaustune kullanici tarafindan eklenen kisayollar (uygulama adlari). */
char g_dshort[8][24];

int  g_dshort_n = 0;


int g_opt_dicons = 1;

int g_opt_clockshow = 1;

int g_opt_clock24 = 1;

int g_accent_idx = 0;

volatile int g_shell_dirty = 0;


/* ---------------------------------------------------------------- */
/* Ikon SVG'leri (viewBox 0 0 40 40 / 32 32 / 12 12)                 */
/* ---------------------------------------------------------------- */

const char SVG_LOGO[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#1F2937\" stroke=\"#7DD3FC\" stroke-width=\"1.5\"/>"
    "<path d=\"M20 10 L10 32 L15 32 L17 26 L23 26 L25 32 L30 32 L20 10 Z M18 21 L20 15 L22 21 Z\" fill=\"#F3F4F6\"/>"
    "<circle cx=\"20\" cy=\"34\" r=\"2.5\" fill=\"#7DD3FC\"/></svg>";


const char SVG_TERMINAL[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#2A2E38\" stroke=\"#9CA3AF\" stroke-width=\"1.5\"/>"
    "<path d=\"M12 14 L18 20 L12 26\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/>"
    "<line x1=\"22\" y1=\"26\" x2=\"28\" y2=\"26\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/></svg>";


const char SVG_FOLDER[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#2A2E38\" stroke=\"#9CA3AF\" stroke-width=\"1.5\"/>"
    "<path d=\"M10 14 L18 14 L21 18 L30 18 L30 28 L10 28 Z\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/></svg>";


const char SVG_GLOBE[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#2A2E38\" stroke=\"#9CA3AF\" stroke-width=\"1.5\"/>"
    "<circle cx=\"20\" cy=\"20\" r=\"10\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/>"
    "<ellipse cx=\"20\" cy=\"20\" rx=\"4\" ry=\"10\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2\"/>"
    "<line x1=\"10\" y1=\"20\" x2=\"30\" y2=\"20\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/></svg>";


const char SVG_SETTINGS[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#2A2E38\" stroke=\"#9CA3AF\" stroke-width=\"1.5\"/>"
    "<circle cx=\"20\" cy=\"20\" r=\"4\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/>"
    "<path d=\"M20 8 L20 13 M20 27 L20 32 M8 20 L13 20 M27 20 L32 20 M11 11 L15 15 M25 25 L29 29 M11 29 L15 25 M25 15 L29 11\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.5\"/></svg>";


const char SVG_MEDIA[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#1F2937\" stroke=\"#7DD3FC\" stroke-width=\"1.5\"/>"
    "<polygon points=\"16,13 28,20 16,27\" fill=\"#7DD3FC\"/></svg>";


const char SVG_CLOSE[] =
    "<svg viewBox=\"0 0 12 12\"><path d=\"M2 2 L10 10 M10 2 L2 10\" stroke=\"#FFFFFF\" stroke-width=\"1.6\" fill=\"none\"/></svg>";

const char SVG_MAX[] =
    "<svg viewBox=\"0 0 12 12\"><rect x=\"2.5\" y=\"2.5\" width=\"7\" height=\"7\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"1.4\"/></svg>";

const char SVG_MIN[] =
    "<svg viewBox=\"0 0 12 12\"><line x1=\"2.5\" y1=\"6\" x2=\"9.5\" y2=\"6\" stroke=\"#F3F4F6\" stroke-width=\"1.6\"/></svg>";


const char SVG_POWER[] =
    "<svg viewBox=\"0 0 24 24\">"
    "<circle cx=\"12\" cy=\"13\" r=\"7\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2.2\"/>"
    "<line x1=\"12\" y1=\"3\" x2=\"12\" y2=\"12\" stroke=\"#F3F4F6\" stroke-width=\"2.2\"/></svg>";


const char SVG_SEARCH[] =
    "<svg viewBox=\"0 0 20 20\">"
    "<circle cx=\"8.5\" cy=\"8.5\" r=\"5\" fill=\"none\" stroke=\"#9AA0A6\" stroke-width=\"2\"/>"
    "<line x1=\"12.5\" y1=\"12.5\" x2=\"17\" y2=\"17\" stroke=\"#9AA0A6\" stroke-width=\"2\"/></svg>";


const char SVG_NOTES[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#2A2E38\" stroke=\"#9CA3AF\" stroke-width=\"1.5\"/>"
    "<line x1=\"10\" y1=\"14\" x2=\"30\" y2=\"14\" stroke=\"#F3F4F6\" stroke-width=\"2.4\"/>"
    "<line x1=\"10\" y1=\"20\" x2=\"30\" y2=\"20\" stroke=\"#F3F4F6\" stroke-width=\"2.4\"/>"
    "<line x1=\"10\" y1=\"26\" x2=\"24\" y2=\"26\" stroke=\"#F3F4F6\" stroke-width=\"2.4\"/></svg>";


const char SVG_TB_WIFI[] =
    "<svg viewBox=\"0 0 18 18\">"
    "<g transform=\"translate(9,5)\" stroke=\"#e6dcff\" stroke-width=\"1.3\" fill=\"none\">"
    "<path d=\"M -8 -1 Q 0 -8.5 8 -1\"/>"
    "<path d=\"M -5 2 Q 0 -2.5 5 2\"/>"
    "<path d=\"M -2.2 4.8 Q 0 3 2.2 4.8\"/>"
    "<circle cx=\"0\" cy=\"7.6\" r=\"0.85\" fill=\"#e6dcff\" stroke=\"none\"/>"
    "</g></svg>";


const char SVG_TB_BATTERY[] =
    "<svg viewBox=\"0 0 24 14\">"
    "<rect x=\"1\" y=\"2\" width=\"18\" height=\"10\" rx=\"2.4\" fill=\"none\" stroke=\"#e6dcff\" stroke-width=\"1.4\"/>"
    "<rect x=\"20\" y=\"5\" width=\"2.4\" height=\"4\" rx=\"0.6\" fill=\"#e6dcff\"/>"
    "<rect x=\"3.2\" y=\"4.2\" width=\"11\" height=\"5.6\" rx=\"1\" fill=\"#e6dcff\"/></svg>";


/* Goreveli (taskbar) cam zemin uzerindeki koyu varyantlar */
const char SVG_TB_WIFI_D[] =
    "<svg viewBox=\"0 0 28 28\">"
    "<g transform=\"translate(14,15)\" stroke=\"#101318\" stroke-width=\"2\" fill=\"none\" stroke-linecap=\"round\">"
    "<path d=\"M -10 -1 Q 0 -11 10 -1\"/>"
    "<path d=\"M -6 3.2 Q 0 -2.6 6 3.2\"/>"
    "<path d=\"M -2.6 7 Q 0 3.6 2.6 7\"/>"
    "</g>"
    "<circle cx=\"14\" cy=\"21.5\" r=\"1.7\" fill=\"#101318\"/>"
    "</svg>";


const char SVG_TB_BATTERY_D[] =
    "<svg viewBox=\"0 0 28 28\" opacity=\"0.83\">"
    "<rect x=\"2\" y=\"9\" width=\"21\" height=\"10\" rx=\"2.4\" fill=\"none\" stroke=\"#101318\" stroke-width=\"1.6\"/>"
    "<rect x=\"24\" y=\"12\" width=\"2.4\" height=\"4\" rx=\"0.6\" fill=\"#101318\"/>"
    "<rect x=\"4\" y=\"11\" width=\"3.4\" height=\"6\" rx=\"0.5\" fill=\"#101318\"/>"
    "<rect x=\"8.6\" y=\"11\" width=\"3.4\" height=\"6\" rx=\"0.5\" fill=\"#101318\"/>"
    "<rect x=\"13.2\" y=\"11\" width=\"3.4\" height=\"6\" rx=\"0.5\" fill=\"#101318\"/>"
    "<rect x=\"17.8\" y=\"11\" width=\"3.4\" height=\"6\" rx=\"0.5\" fill=\"#101318\"/></svg>";


const char SVG_SEARCH_D[] =
    "<svg viewBox=\"0 0 20 20\" opacity=\"0.83\">"
    "<circle cx=\"8.5\" cy=\"8.5\" r=\"5\" fill=\"none\" stroke=\"#101318\" stroke-width=\"2\"/>"
    "<line x1=\"12.5\" y1=\"12.5\" x2=\"17\" y2=\"17\" stroke=\"#101318\" stroke-width=\"2\"/></svg>";


/* Bildirim zili (gorev cubugu cam zemin uzerinde koyu varyant)      */
const char SVG_TB_BELL_D[] =
    "<svg viewBox=\"0 0 28 28\">"
    "<g transform=\"translate(14,13)\" stroke=\"#101318\" stroke-width=\"2\" fill=\"none\" "
    "stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M -7 -2 Q -7 -70 -1 -70 Q 5 -7 5 -2 L 6.4 0 L -8.4 0 Z\"/>"
    "<path d=\"M -2.4 3.2 Q 0 5.2 2.4 3.2\"/>"
    "</g></svg>";


/* Hizli Ayarlar (uc yatagi kaydirma dugmesi)      */
const char SVG_TB_SLIDERS_D[] =
    "<svg viewBox=\"0 0 28 28\">"
    "<g transform=\"translate(14,14)\" stroke=\"#101318\" stroke-width=\"2\" fill=\"none\" stroke-linecap=\"round\">"
    "<path d=\"M -8 -4.5 L 8 -4.5\"/>"
    "<circle cx=\"-2\" cy=\"-4.5\" r=\"2.4\" fill=\"#101318\" stroke=\"none\"/>"
    "<path d=\"M -8 0 L 8 0\"/>"
    "<circle cx=\"4\" cy=\"0\" r=\"2.4\" fill=\"#101318\" stroke=\"none\"/>"
    "<path d=\"M -8 4.5 L 8 4.5\"/>"
    "<circle cx=\"0\" cy=\"4.5\" r=\"2.4\" fill=\"#101318\" stroke=\"none\"/>"
    "</g></svg>";


/* ---- dock sistem ikonlari ----------------------
 * Tuval 24x24 (pil 26x24), cizgi rengi text #EEF0F6, yuvarlak uclar.
 * Arama ikonu text-muted (#AAB2C8) renktedir.
 *
 * NOT: lib/svg.c yay (A) ve kubik (C) komutlarini cizmiyor (duz kiris
 * veya bos cikti). Bu yuzden sartnamedeki yaylar, ayni tepe noktasini
 * veren kuadratik (Q) Bezier ile yazildi; 20 px'te fark gorunmez. */

const char SVG_DK_SEARCH[] =
    "<svg viewBox=\"0 0 24 24\">"
    "<g fill=\"none\" stroke=\"#AAB2C8\" stroke-width=\"1.8\" "
    "stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<circle cx=\"10.5\" cy=\"10.5\" r=\"6.5\"/>"
    "<path d=\"M15.5 15.5L21 21\"/>"
    "</g></svg>";

const char SVG_DK_WIFI[] =
    "<svg viewBox=\"0 0 24 24\">"
    "<g fill=\"none\" stroke=\"#EEF0F6\" stroke-width=\"1.6\" "
    "stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M2 9.2Q12 1.56 22 9.2\"/>"
    "<path d=\"M5.5 12.8Q12 8 18.5 12.8\"/>"
    "<path d=\"M9 16.2Q12 14.2 15 16.2\"/>"
    "</g>"
    "<circle cx=\"12\" cy=\"19.6\" r=\"1\" fill=\"#EEF0F6\"/>"
    "</svg>";

const char SVG_DK_BELL[] =
    "<svg viewBox=\"0 0 24 24\">"
    "<g fill=\"none\" stroke=\"#EEF0F6\" stroke-width=\"1.6\" "
    "stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M6 8Q6 2.8 12 2.8Q18 2.8 18 8Q19 12 21 17L3 17Q5 12 6 8\"/>"
    "<path d=\"M10.3 21Q12 22.9 13.7 21\"/>"
    "</g></svg>";

const char SVG_DK_SLIDERS[] =
    "<svg viewBox=\"0 0 24 24\">"
    "<g fill=\"none\" stroke=\"#EEF0F6\" stroke-width=\"1.6\" "
    "stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M4 8L12 8M18 8L20 8M4 16L6 16M12 16L20 16\"/>"
    "<circle cx=\"15\" cy=\"8\" r=\"2.4\"/>"
    "<circle cx=\"9\" cy=\"16\" r=\"2.4\"/>"
    "</g></svg>";


winrec_t *g_recs[MAX_WINS];

int g_last_x = 0, g_last_y = 0;


uint32_t g_pending_caps = CAP_ALL;


arctian_recover_t g_recover;

lv_timer_t *g_launch_timer = 0;

lv_obj_t   *g_launch_overlay = 0;

lv_obj_t   *g_launch_win = 0;

void       *g_launch_ctx = 0;

int         g_crash_dyn = -1;

char        g_launch_title[48];


/* ---------------------------------------------------------------- */
/* Ag paneli (taskbar Wi-Fi ikonu)                                   */
/* ---------------------------------------------------------------- */

lv_obj_t *g_net_panel = 0;

lv_obj_t *g_net_body = 0;

int   g_net_stage = 0;          /* 0=durum/liste, 1=sifre */

char  g_net_ssid[33];

char  g_net_pass[64];

asi_wifi_net_t g_scan[16];

int   g_scan_n = 0;


/* ---------------------------------------------------------------- */
/* Taskbar (Windows 11 tarzi)                                        */
/* ---------------------------------------------------------------- */

lv_obj_t *g_clock_label = 0;

int g_clock_last_sec = -1;

int g_clock_state = -1;


lv_obj_t *g_power_label = 0;

uint64_t  g_power_last = 0;


lv_obj_t *g_cal_overlay = 0;

int g_cal_y = 0, g_cal_m = 0;


const char *const g_month_tr[12] = {
    "Ocak", "Subat", "Mart", "Nisan", "Mayis", "Haziran",
    "Temmuz", "Agustos", "Eylul", "Ekim", "Kasim", "Aralik"
};

const char *const g_wday_tr[7] = { "Pt", "Sa", "Ca", "Pe", "Cu", "Ct", "Pz" };

lv_font_t *s_f_time = 0;

lv_font_t *s_f_date = 0;

lv_font_t *s_f_search = 0;

lv_font_t *s_f_dlabel = 0;


lv_obj_t *g_tb_logo = 0;

lv_obj_t *g_tb_search = 0;
lv_obj_t *g_tb_ai = 0;      /* asistan dugmesi */

lv_obj_t *g_tb_sep1 = 0;

lv_obj_t *g_tb_sep2 = 0;

lv_obj_t *g_tb_sep3 = 0;

lv_obj_t *g_tb_apps_cont = 0;

lv_obj_t *g_tb_status = 0;

lv_obj_t *g_tb_menu = 0;

char      g_tb_menu_title[40];

lv_obj_t *s_sm_tiles[SM_TILE_MAX];

const arctian_app_t *s_sm_apps[SM_TILE_MAX];

int s_sm_n = 0;

const arctian_app_t *s_rmb_app = 0;


/* ---- Pil paneli (pile tiklama) ---- */

lv_obj_t *g_bat_panel = 0;


/* Sag tik algilama + dinamik yenileme (ana donguden cagrilir). */
int s_tb_pr = 0;


const char SVG_FILE[] =
    "<svg viewBox=\"0 0 40 40\">"
    "<rect x=\"0\" y=\"0\" width=\"40\" height=\"40\" rx=\"10\" fill=\"#1F2937\" stroke=\"#9CA3AF\" stroke-width=\"1.5\"/>"
    "<path d=\"M14 8 L24 8 L30 14 L30 32 L14 32 Z\" fill=\"#2A2E38\" stroke=\"#F3F4F6\" stroke-width=\"2\"/>"
    "<path d=\"M24 8 L24 14 L30 14\" fill=\"none\" stroke=\"#F3F4F6\" stroke-width=\"2\"/></svg>";


/* ================================================================ */
/* Masaustu simgeleri: SVG ikonlar, izgara, secim, sag tik, birlikte-ac */
/* ================================================================ */

const char *arctian_open_target = "";


dicon_t g_dicons[MAX_DICONS];

int g_dicon_count = 0;

int g_sel_count = 0;

lv_obj_t *g_icon_layer = 0;

lv_obj_t *g_rubber = 0;

lv_obj_t *g_context_menu = 0;

lv_obj_t *g_dialog = 0;

int g_modal = 0;

char g_ow_file[48];


/* ---- Birlikte ac diyalogu ---- */
char g_open_target_buf[48];


/* ---- Etkilesim dongusu ---- */
int ic_pl = 0, ic_pr = 0;

int ic_drag = -1, ic_dox = 0, ic_doy = 0;

int ic_rub = 0, ic_rx = 0, ic_ry = 0;

int ic_last_i = -1;


/* ---------------------------------------------------------------- */
/* Masaustu arka plani (degistirilebilir)                            */
/*   - Ozel arka planlar AFS'te ".wp" uzantisiyla saklanir (PNG).    */
/*   - Aktif secim "wallpaper.cfg" dosyasinda tutulur; yoksa gomulu   */
/*     varsayilan arka plan kullanilir.                              */
/* ---------------------------------------------------------------- */
lv_obj_t *g_wallpaper = 0;

int g_ui_s = 1000;

char g_wall_active[40];   /* aktif .wp dosya adi; "" = gomulu */


dyn_app_t g_dyn[MAX_DYN_APPS];

int  g_dyn_count = 0;

int  g_launch_dyn = -1;

uint8_t g_pkg_buf[DPK_MAX_PACKAGE];

const accent_def_t g_accents[] = {
    { "Arctian Mavi", 0x3B82F6, 0x8B5CF6 },
    { "Menekse",      0x8B5CF6, 0xEC4899 },
    { "Zumrut",       0x22C55E, 0x14B8A6 },
    { "Turkuaz",      0x06B6D4, 0x3B82F6 },
    { "Turuncu",      0xF97316, 0xEF4444 },
    { "Pembe",        0xEC4899, 0x8B5CF6 },
    { "Ates",         0xEF4444, 0xF97316 },
    { "Amber",        0xF59E0B, 0xF97316 },
    { "Indigo",       0x6366F1, 0x8B5CF6 },
    { "Celik",        0x64748B, 0x94A3B8 }
};

const int g_accent_n = (int)(sizeof(g_accents) / sizeof(g_accents[0]));

set_kv_t s_kv[SET_KV_MAX];

int s_kv_n = 0;


const res_opt_t g_res_opts[RES_OPT_N] = {
    { 1280,  720, "1280 x 720  (HD)"        },
    { 1600,  900, "1600 x 900  (HD+)"       },
    { 1920, 1080, "1920 x 1080  (Full HD)"  },
    { 2560, 1440, "2560 x 1440  (QHD / 2K)" },
};


int s_res_pending = -1;


arctian_settings_api_t g_settings_api = {
    settings_accent_count, settings_accent_name, settings_accent_color,
    settings_accent_color2, settings_accent_current, settings_accent_set,
    settings_get_int, settings_set_int,
    settings_resolution_count, settings_resolution_name,
    settings_resolution_current, settings_resolution_set
};

arctian_settings_api_t *arctian_settings = &g_settings_api;


int g_lock_request;
/* Kabuk nesli: shell_rebuild() her cagrildiginda artar; ust katman
 * panelleri (asistan vb.) eski nesnelerin silindigini boyle anlar. */
int g_shell_gen;


arctian_store_api_t g_store_api = {
    store_install, store_uninstall, store_launch, store_installed,
    store_installed_version, store_count_installed, store_installed_id,
    store_installed_title, store_installed_version_at, rebuild_shell_ui,
    store_set_wallpaper, store_wallpaper_active, store_wallpaper_name
};

arctian_store_api_t *arctian_store = &g_store_api;


lv_group_t *g_ed_group;

lv_group_t *g_ed_prev;


/* Klavye gezinme grubu + simge boyutu (buyuk simge modu)      */
lv_group_t *g_desktop_group = 0;
int g_icon_w = 0;
int g_icon_h = 0;
int g_icon_big = 0;

