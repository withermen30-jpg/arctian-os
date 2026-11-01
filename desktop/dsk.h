/* Arctian desktop - paylasilan dahili baslik. */
#pragma once
/**
 * Arctian desktop (LVGL)
 *  - Windows 11 tarzi, havada duran (floating) detayli taskbar
 *  - SVG ile cizilen baslat + uygulama + Wi-Fi ikonlari (lib/svg)
 *  - Pencere yonetimi: basliktan surukleme, kucult/buyut/kapat
 *  - Uygulama acilisinda playback animasyonlu yukleme ekrani
 *  - Duvar kagidi (PNG runtime decode) + yazilimsal imlec
 */
#include "lvgl.h"
#include "asi.h"
#include "lv_port.h"
#include "adl_wolf.h"
#include "lock.h"
#include "png.h"
#include "svg.h"
#include "app.h"
#include "dpk.h"
#include "wlf.h"

/* DPK ABI imzasi: DPK kodu desktop sembol ADRESLERINE mutlak linklenir.
 * Kod degisince adresler kayar ve eski paket page fault verir. Imza, sabit
 * bir islevin CALISMA ANINDAKI adresinden turetilir — paketleyicinin `nm` ile
 * okudugu degerle aynidir (desktop sabit adrese yuklenir, PIE degil).
 * Uyumsuz paket calistirilmadan reddedilir. */
const char *dpk_abi_signature(void);
#include "wolf.h"
#include "memory_map.h"
#include "sig.h"
#include "slot.h"
#include <stdint.h>
/* Taskbar simge yuvalari: sabitler + acik (sabit olmayan) uygulamalar. */
#define TB_MAX_ICONS 12

/* ---------------------------------------------------------------- */
/* Pencere yonetimi                                                  */
/* ---------------------------------------------------------------- */

#define MAX_WINS 8

#define UI_REF_H 1440

extern int g_ui_s;
int ui_s(int v);
/* Masaustu simge olcegi (kabuktan ayri; 720p'de 0.55) */
int ic_s(int v);
void ui_scale_refresh(void);

/* ---------------------------------------------------------------- */
/* Dock: 1600x900 mantiksal referans, ekran yuksekligine                 */
/* oranlanir. Tum dock olculeri bu birimle ifade edilir.                */
/* ---------------------------------------------------------------- */
#define DK_REF_H 900

int dk_s(int v);   /* tasarim birimi -> piksel */

/* ---------------------------------------------------------------- */
/* Dock: duz opak zemin, sabit + acik uygulamalar                    */
/* ---------------------------------------------------------------- */

#define TB_BH       dk_s(60)    /* dock yuksekligi */
#define TB_Y_OFF    dk_s(14)    /* ekran alt kenari ile dock arasi */
#define TB_IC       dk_s(44)    /* uygulama/dugme kutusu */
#define TB_GAP      dk_s(4)     /* uygulama dugmeleri arasi */
#define TB_LOGO     dk_s(44)    /* baslat dugmesi */
#define TB_SRCH_W   dk_s(200)
#define TB_SRCH_H   dk_s(40)
#define TB_SEP_H    dk_s(24)
#define TB_SEP_W    dk_s(1)
#define TB_STATUS_W dk_s(72)
#define TB_CLK_W    dk_s(89)

/* Renk paleti           */
#define DK_BG        0x262C40   /* dock zemini, tam opak */
#define DK_TEXT      0xEEF0F6
#define DK_MUTED     0xAAB2C8
#define DK_FIELD     0x333A54
#define DK_FIELD_HOV 0x3A4260
#define DK_ACCENT    0xF0A35E
#define DK_DIV_OPA   41         /* rgba(text, 0.16) */
#define DK_HOVER_OPA 31         /* rgba(text, 0.12) */
#define DK_PRESS_OPA 51         /* rgba(text, 0.20) */
#define DK_SEL_OPA   20         /* rgba(text, 0.08) */

/* Eski adlar: baska modullerde kullanilan sabitler (renk disi) */
#define TB_BLUE      DK_BG
#define TB_LAV       DK_BG
#define TB_INK       DK_TEXT

/* Baslat menusu karolarina sag tik (basarim: bu LVGL'de sag tik olayi yok,
 * tb_poll fare dugmesini yoklayarak tetikler). */
#define SM_TILE_MAX 12

#define MAX_DICONS 24
/* Simge boyutu calisma zamaninda belirlenir (buyuk simge modu):
 * ui_scale_refresh() g_icon_w/h degerlerini gunceller. */
extern int g_icon_w;
extern int g_icon_h;
extern int g_icon_big;          /* 1 = buyuk simge modu (kv: bigicons) */
#define ICON_W g_icon_w
#define ICON_H g_icon_h
#define GRID_X ic_s(40)
#define GRID_Y ic_s(32)

/* ================================================================ */
/* DuckStore: DPK paketleri, runtime yukleyici ve uygulama platformu */
/* ================================================================ */
#define MAX_DYN_APPS   32
#define DYN_FNAME_LEN  40

#define SET_KV_MAX 32
#define RES_OPT_N        4
#define RES_DEFAULT_IDX  0   /* 1280x720 (HD) varsayilan */

/* ---------------------------------------------------------------- */
/* Seri (COM1) tani cikisi - self-test bloklari icin                 */
/* ---------------------------------------------------------------- */
#if defined(ARCTIAN_DS_TEST) || defined(ARCTIAN_UPDATE_SELFTEST)
#endif

/* ---------------------------------------------------------------- */
/* DuckStore uctan uca self-test (yalnizca -DARCTIAN_DS_TEST ile)    */
/* ---------------------------------------------------------------- */
#ifdef ARCTIAN_DS_TEST
#endif

#ifdef ARCTIAN_UPDATE_SELFTEST
#endif

/* --- Tip tanimlari --- */
typedef struct {
    char title[40];
    int  app_idx;
    int  pinned;
    lv_obj_t *btn;
} tb_slot_t;

typedef struct {
    lv_obj_t *win;
    lv_obj_t *btn;   /* taskbar running button */
    char title[40];
    int  pid;
    void (*relayout)(lv_obj_t *win);   /* boyut degisince icerik yeniden akar */
    void (*hotkey)(int hk);            /* pencereye iletilen klavye kisayolu */
    int  (*drop)(const char *src);     /* pencereye birakilan dosya; 1=kabul */
    /* buyutulmus (USER_1) moddan geri alinirken onceki geometriye donmek
     * icin ("onceki boyuta don"). 0 = henuz kayit yok. */
    int  max_saved_w, max_saved_h, max_saved_x, max_saved_y;
    /* Pencere acilirken uygulamanin bagladigi klavye grubu. Pencere one
     * geldiginde kabuk bu grubu geri yukler (odak eski uygulamada kalmaz). */
    lv_group_t *fgroup;
    int ws;         /* sanal masaustu (calisma alani) numarasi */
    int ws_hidden;  /* 1 = yalnizca calisma alani degisimi nedeniyle gizli */
} winrec_t;

/* ---------------------------------------------------------------- */
/* Yukleme animasyonu + uygulama acma                                 */
/* ---------------------------------------------------------------- */

typedef struct { lv_obj_t *overlay; char title[40]; } app_ctx_t;

/* ---- Kullanici (registry) uygulamalari ---- */
typedef struct { lv_obj_t *overlay; int idx; } runctx_t;

typedef struct {
    char name[48];
    int  is_file;
    int  app_idx;
    const char *svg;
    const arctian_app_t *app;
    int  gx, gy;
    int  selected;
    lv_obj_t *cell;
} dicon_t;

typedef struct {
    char     id[DPK_ID_LEN];
    char     title[48];
    char     category[32];
    char     version[16];
    char     publisher[32];
    char     desc[176];
    char     fname[DYN_FNAME_LEN];
    uint32_t load_addr;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t entry_off;
    int      win_w, win_h;
    char    *icon_svg;
    unsigned char *icon_png;
    uint32_t icon_png_len;
    uint32_t perms;
    int      open;
    /* 0.7: uygulama bicimi. 0 = DPK (native), 1 = WLF native, 2 = WLF bytecode. */
    int      kind;
    uint32_t flags;
    /* 0.7 uygulama modeli: her uygulama bir .wlf'tir ve rol tasir. */
    int      role;                  /* WLF_ROLE_* */
    char     handles[WLF_HANDLES_LEN];  /* "py,pyw" (runtime/cli) */
    char     entry_name[WLF_ENTRY_LEN]; /* "python" (gosterim) */
    int      terminal;              /* 1 = calisirken terminal gorunur */
} dyn_app_t;

/* ---- Ayarlar (kalici settings.cfg + canli uygulama) ---- */

typedef struct { const char *name; uint32_t a; uint32_t a2; } accent_def_t;
typedef struct { char k[24]; char v[96]; } set_kv_t;

/* ---------------------------------------------------------------- */
/* Metin duzenleyici (Notepad benzeri pencere)                       */
/* ---------------------------------------------------------------- */

typedef struct {
    char path[160];
    lv_obj_t *ta;
    lv_obj_t *status;
    /* Geri al/yinele + otomatik kayit + satir numaralari     */
    lv_obj_t  *gutter;                 /* satir numarasi sutunu */
    lv_obj_t  *win;                    /* pencere (temizlik icin) */
    lv_timer_t *autosave;              /* 5 sn temiz kayit zamanlayicisi */
    char     *last;                    /* son bilinen metin (yigin guncelleme icin) */
    char     *undo[16];                /* geri al yiginı (metin anlik goruntuleri) */
    char     *redo[16];                /* yinele yigini */
    int       undo_n, redo_n;
    int       dirty;                   /* kaydedilmemis degisiklik var */
    int       guard;                   /* programatik set_text kaydi engeller */
} ed_ctx_t;

/* --- Global durum (dsk_state.c) --- */

extern const unsigned char wallpaper_png[];
extern const unsigned int  wallpaper_png_len;
extern const unsigned char logo_png[];
extern const unsigned int  logo_png_len;

extern const unsigned char font_ttf[];
extern const unsigned int  font_ttf_len;
extern const unsigned char font_serif[];
extern const unsigned int  font_serif_len;
extern const unsigned char font_arimo[];
extern const unsigned int  font_arimo_len;
/* IBM Plex Sans 400 + 600                    */
extern const unsigned char font_plex[];
extern const unsigned int  font_plex_len;
extern const unsigned char font_plex_sb[];
extern const unsigned int  font_plex_sb_len;
extern const unsigned char font_anton[];
extern const unsigned int  font_anton_len;
extern const unsigned char font_abril[];
extern const unsigned int  font_abril_len;
extern const unsigned char *const arctian_setup_png[];
extern const unsigned int arctian_setup_png_len[];
extern const unsigned arctian_setup_png_n;
extern asi_t * g_asi;
extern int s_desktop_started;
extern lv_obj_t * g_start_menu;
extern lv_obj_t * g_taskbar;
extern tb_slot_t g_tb[TB_MAX_ICONS];
extern int g_tb_n;
extern int g_tb_dirty;
extern char g_tb_pins[TB_MAX_ICONS][24];
extern int g_tb_pin_n;
extern lv_obj_t * g_tb_clock;
extern lv_obj_t * g_date_label;
extern char g_dshort[8][24];
extern int g_dshort_n;
extern int g_opt_dicons;
extern int g_opt_clockshow;
extern int g_opt_clock24;
extern int g_accent_idx;
extern volatile int g_shell_dirty;
extern const char SVG_LOGO[];
extern const char SVG_TERMINAL[];
extern const char SVG_FOLDER[];
extern const char SVG_GLOBE[];
extern const char SVG_SETTINGS[];
extern const char SVG_MEDIA[];
extern const char SVG_CLOSE[];
extern const char SVG_MAX[];
extern const char SVG_MIN[];
extern const char SVG_POWER[];
extern const char SVG_SEARCH[];
extern const char SVG_NOTES[];
extern const char SVG_TB_WIFI[];
extern const char SVG_TB_BATTERY[];
extern const char SVG_TB_WIFI_D[];
extern const char SVG_TB_BATTERY_D[];
extern const char SVG_SEARCH_D[];
extern const char SVG_TB_BELL_D[];
extern const char SVG_TB_SLIDERS_D[];

/* Dock sistem ikonlari                    */
extern const char SVG_DK_SEARCH[];
extern const char SVG_DK_WIFI[];
extern const char SVG_DK_BELL[];
extern const char SVG_DK_SLIDERS[];
extern winrec_t * g_recs[MAX_WINS];
/* Klavye ile tam gezinme: masaustu grubu. Simge ve gorev cubugu
 * ogeleri bu gruba eklenir; Tab/Enter ile gezinilir. */
extern lv_group_t * g_desktop_group;
extern int g_last_x;
extern int g_last_y;
extern uint32_t g_pending_caps;
extern arctian_recover_t g_recover;
extern lv_timer_t * g_launch_timer;
extern lv_obj_t   * g_launch_overlay;
extern lv_obj_t   * g_launch_win;
extern void       * g_launch_ctx;
extern int g_crash_dyn;
extern char g_launch_title[48];
extern lv_obj_t * g_net_panel;
extern lv_obj_t * g_net_body;
extern int g_net_stage;
extern char g_net_ssid[33];
extern char g_net_pass[64];
extern asi_wifi_net_t g_scan[16];
extern int g_scan_n;
extern lv_obj_t * g_clock_label;
extern int g_clock_last_sec;
extern int g_clock_state;
extern lv_obj_t * g_power_label;
extern uint64_t g_power_last;
extern lv_obj_t * g_cal_overlay;
extern int g_cal_y;
extern int g_cal_m;
extern const char *const g_month_tr[12];
extern const char *const g_wday_tr[7];
extern lv_font_t * s_f_time;
extern lv_font_t * s_f_date;
extern lv_font_t * s_f_search;
extern lv_font_t * s_f_dlabel;
extern lv_obj_t * g_tb_logo;
extern lv_obj_t * g_tb_search;
extern lv_obj_t * g_tb_ai;      /* asistan dugmesi */
extern lv_obj_t * g_tb_sep1;
extern lv_obj_t * g_tb_sep2;
extern lv_obj_t * g_tb_sep3;
extern lv_obj_t * g_tb_apps_cont;
extern lv_obj_t * g_tb_status;
extern lv_obj_t * g_tb_menu;
extern char g_tb_menu_title[40];
extern lv_obj_t * s_sm_tiles[SM_TILE_MAX];
extern const arctian_app_t * s_sm_apps[SM_TILE_MAX];
extern int s_sm_n;
extern const arctian_app_t * s_rmb_app;
extern lv_obj_t * g_bat_panel;
extern int s_tb_pr;
extern const char SVG_FILE[];
extern const char * arctian_open_target;
extern dicon_t g_dicons[MAX_DICONS];
extern int g_dicon_count;
extern int g_sel_count;
extern lv_obj_t * g_icon_layer;
extern lv_obj_t * g_rubber;
extern lv_obj_t * g_context_menu;
extern lv_obj_t * g_dialog;
extern int g_modal;
extern char g_ow_file[48];
extern char g_open_target_buf[48];
extern int ic_pl;
extern int ic_pr;
extern int ic_drag;
extern int ic_dox;
extern int ic_doy;
extern int ic_rub;
extern int ic_rx;
extern int ic_ry;
extern int ic_last_i;
extern lv_obj_t * g_wallpaper;
extern char g_wall_active[40];
extern dyn_app_t g_dyn[MAX_DYN_APPS];
extern int g_dyn_count;
extern int g_launch_dyn;
extern uint8_t g_pkg_buf[DPK_MAX_PACKAGE];
extern const accent_def_t g_accents[];
extern const int g_accent_n;
extern set_kv_t s_kv[SET_KV_MAX];
extern int s_kv_n;
typedef struct { int w, h; const char *label; } res_opt_t;
extern const res_opt_t g_res_opts[RES_OPT_N];
extern int s_res_pending;
extern arctian_settings_api_t g_settings_api;
extern arctian_settings_api_t * arctian_settings;
extern int g_lock_request;
extern int g_shell_gen;
extern arctian_store_api_t g_store_api;
extern arctian_store_api_t * arctian_store;
extern lv_group_t * g_ed_group;
extern lv_group_t * g_ed_prev;

/* --- Fonksiyon protokolleri --- */
void scopy(char *d, const char *s, int max);
void scpy(char *d, int cap, const char *s);
void base_obj(lv_obj_t *o, uint32_t bg, int radius, uint32_t border);
lv_obj_t *label(lv_obj_t *parent, const char *txt, uint32_t col);
void canvas_buf_free_cb(lv_event_t *e);
lv_obj_t *make_icon(lv_obj_t *parent, const char *svg, int size);
lv_obj_t *make_icon_png(lv_obj_t *parent, const unsigned char *data, unsigned len, int size);
lv_obj_t *make_app_icon(lv_obj_t *parent, const arctian_app_t *a, int size);
void update_app_indicators(void);
void win_close_rec(winrec_t *r);
void win_close_cb(lv_event_t *e);
void win_min_cb(lv_event_t *e);
void win_max_cb(lv_event_t *e);
void title_drag_cb(lv_event_t *e);
void title_dbl_cb(lv_event_t *e);
/* boyut degisince icerigin yeniden yerlesmesi icin uygulama kaydi */
void win_set_relayout(lv_obj_t *win, void (*cb)(lv_obj_t *));
/* Pencere kaydini verilen LVGL nesnesinden bulur (wlf_host kullanir). */
winrec_t *winrec_of(lv_obj_t *o);/* 0.6 oz-testleri (yalnizca ARCTIAN_06_TEST ile derlenir). */
void t06_selftest(void);

/* ---------------------------------------------------------------- */
/* 0.7 izin sistemi (perm.c + consent.c)                              */
/* ---------------------------------------------------------------- */
/* Kararlar */
#define PERM_ASK     0
#define PERM_ONCE    1
#define PERM_ALWAYS  2
#define PERM_DENY    3
/* Soru turleri */
#define PERM_T_PATH  10
#define PERM_T_EXEC  11
#define PERM_T_NET   12

void perm_init(void);                    /* kapilari kur + kararlari oku */
int  perm_norm(char *out, int cap, const char *in);   /* 1=ok, 0=reddet */

/* Karar deposu    */
int  perm_dec_get(const char *id, int type, const char *src);
int  perm_dec_set(const char *id, int type, const char *src, int verd);
void perm_dec_consume(const char *id, int type, const char *src);
void perm_dec_load(void);
void perm_dec_save(void);
void perm_dec_clear(void);
void perm_dec_drop(const char *id);
int  perm_dec_count(void);

/* Ozne    */
int  subj_open(const char *id, const char *title, int trusted, const char *pkg);
void subj_close(void);
int  subj_is_open(void);
const char *subj_home(void);
const char *subj_pkg(void);
const char *subj_id(void);
int  subj_trusted(void);

/* uygulama kodu calisiyor mu? */
void perm_dispatch_begin(void);
void perm_dispatch_end(void);
int  perm_app_open(lv_obj_t *body, const char *id, const char *title,
                   int trusted, const char *pkg);
void perm_app_close(lv_obj_t *body);
void perm_body_del_cb(lv_event_t *e);
int  perm_begin_obj(lv_obj_t *o);
void perm_end_obj(void);
int  perm_obj_in_app(lv_obj_t *o);

/* Kapilari       */
int  perm_policy(const char *path, int is_write);
int  perm_exec(const char *cmd);
int  perm_net(void);
void consent_grant_user_path(const char *id, const char *path);

/* Onay penceresi    */
void consent_set_hook(int (*fn)(int type, const char *src));
int  consent_ask(int type, const char *src);
int  consent_is_open(void);
int  consent_answer_now(void);
void consent_answer(int v);
void consent_send_esc(void);
int  consent_test_show(int type, const char *src);
void consent_test_end(void);

/* 0.7 oz-testleri (yalnizca ARCTIAN_07_TEST ile derlenir). */
void t07_selftest(void);
/* ---- Arctian Asistani ---------- */
void assistant_init(void);      /* paneli bir kez kur */
void assistant_toggle(void);    /* goster/gizle */
/* 0.7: asistan ayri bir uygulama olarak da calisir (desktop/apps/asistan).
 * Pencere kendi balon cizicisini baglar; motor ayni kalir. */
void assistant_set_sink(void (*fn)(const char *txt));
void assistant_ask(const char *q);
void assistant_status_report(void);
/* Kare hizi HUD'u (Ctrl+Alt+F). */
void fps_hud_toggle(void);void win_set_focus_group(lv_obj_t *win, lv_group_t *g);
/* Sanal masaustu      */
extern int g_ws;
void workspace_switch(int delta);
void workspace_move_top(int delta);   /* etkin pencereyi alana tasi */
void workspace_apply(void);
void workspace_notify(void);
/* klavye kisayolu yonlendirmesi: masaustu isleyemezse en ust pencere      */
void win_set_hotkey(lv_obj_t *win, void (*cb)(int hk));
/* surukle-birak hedefi: pencereye birakilan dosya      */
void win_set_drop(lv_obj_t *win, int (*cb)(const char *src));
winrec_t *win_at(int x, int y);   /* nokta uzerindeki pencere (yoksa 0) */
/* Hizli Ayarlar + OSD + bildirim merkezi      */
void qc_init(void);               /* panelleri/perde katmanlarini kurar */
void qc_reinit(void);             /* kabuk yeniden kurulunca cagrilir */
void qc_show_quick(void);         /* hizli ayarlar panelini ac/kapa */
void qc_show_notif(void);         /* bildirim merkezini ac/kapa */
void qc_osd(const char *label, int pct);  /* 1.5 sn ses/parlaklik gostergesi */
void qc_notify_add(const char *title, const char *msg);  /* bildirim yigina */
int  qc_unread_count(void);   /* okunmamis bildirim sayisi (rozet) */
void qc_unread_clear(void);   /* merkez acilinca sayaci sifirla */
void tb_refresh_bell_badge(void);   /* zil rozetini guncelle */
void dock_battery_level(int pct);   /* Pil ikonu dolgusu (0-100) */
void tb_ws_refresh(void);           /* calisma alani noktalari (0.6) */
void tb_reset_objects(void);        /* kabuk yeniden kurulurken isaretcileri sil */
void qc_apply_overlays(void);     /* parlaklik/gece perdesini uygular */
void add_ctrl(lv_obj_t *tb, const char *svg, uint32_t bgcol,
              lv_event_cb_t cb, void *ud, int xoff);
lv_obj_t *create_window(const char *title, int w, int h);
void build_welcome_window(void);
int win_pid(lv_obj_t *win);
winrec_t *winrec_top(void);
int winrec_alive(winrec_t *r);    /* 0.7: kayit hala tabloda mi (serbest
                                   * liste geri donusumunde bayat pointer
                                   * karsilastirmalarina karsi) */
void recover_set_reason(const char *s);
void recover_prepare(const char *title);
void recover_begin_call(uint32_t timeout_ms);
void recover_disarm(void);
void launch_rebuild_reset(void);   /* kabuk yeniden kurulurken yarim kalan
                                    * acilis zamanlayicisini iptal eder */
void loading_done_cb(lv_timer_t *timer);
void open_app(const char *title);
int title_eq(const char *a, const char *b);
void app_btn_cb(lv_event_t *e);
void tb_app_cb(lv_event_t *e);
void dyn_win_del_cb(lv_event_t *e);
void run_app_done(lv_timer_t *t);
void show_error_window(const char *title, const char *msg);
void open_registry_app(int idx);
void reg_tile_cb(lv_event_t *e);
void start_menu_anim_cb(void *var, int32_t value);
void toggle_start_menu(void);
/* Dock arama kutusundan canli arama.      */
void startmenu_filter(const char *q);
/* 0.6: odak calmadan canli arama + ilk sonucu ac */
void startmenu_search_live(const char *q);
int  startmenu_launch_first(void);
int  startmenu_is_open(void);
void start_btn_cb(lv_event_t *e);
void ai_btn_cb(lv_event_t *e);   /* asistan dugmesi */
void sm_item_cb(lv_event_t *e);
void net_panel_close(void);
void net_close_cb(lv_event_t *e);
void net_overlay_click_cb(lv_event_t *e);
void wifi_pick_cb(lv_event_t *e);
void wifi_back_cb(lv_event_t *e);
void wifi_connect_cb(lv_event_t *e);
void wifi_disc_cb(lv_event_t *e);
void pass_ta_cb(lv_event_t *e);
lv_obj_t *net_row(lv_obj_t *parent, const char *ssid, int secure, int rssi);
void net_panel_refresh(void);
void open_network_panel(void);
void wifi_btn_cb(lv_event_t *e);
void power_tick(void);
void clock_tick(void);
int cal_leap(int y);
int cal_days(int y, int m);
int cal_first_wday(int y, int m);
void cal_nav_cb(lv_event_t *e);
void cal_out_cb(lv_event_t *e);
void cal_render(void);
void clock_btn_cb(lv_event_t *e);
lv_obj_t *make_logo(lv_obj_t *parent, int size);
int tb_streq(const char *a, const char *b);
int app_idx_by_name(const char *nm);
int app_idx_by_title(const char *t);
winrec_t *win_find(const char *title);
void tb_pins_load(void);
void tb_pins_save(void);
int tb_pin_find_name(const char *nm);
void tb_pin_add_name(const char *nm);
void tb_pin_del_name(const char *nm);
void dshort_load(void);
void dshort_save(void);
int dshort_find(const char *nm);
void dshort_toggle(const char *nm);
void tb_menu_desk_cb(lv_event_t *e);
void tb_show_desk_menu(int x, int y, const arctian_app_t *a);
void tb_menu_close(void);
void tb_show_app_menu(int x, int y, const arctian_app_t *a);
int  sm_all_hit(int mx, int my);
void tb_menu_overlay_cb(lv_event_t *e);
void tb_menu_pin_cb(lv_event_t *e);
void tb_menu_unpin_cb(lv_event_t *e);
void tb_menu_kill_cb(lv_event_t *e);
void tb_menu_button(lv_obj_t *p, int idx, const char *txt, lv_event_cb_t cb);
void tb_show_menu(int x, const char *title);
void bat_panel_close(void);
void bat_overlay_cb(lv_event_t *e);
void battery_btn_cb(lv_event_t *e);
void tb_layout(void);
void tb_btn_click_cb(lv_event_t *e);
void tb_rebuild_icons(void);
void tb_sep(lv_obj_t *parent, lv_obj_t **out);
void build_taskbar(lv_obj_t *scr);
void tb_poll(void);
void power_cancel_cb(lv_event_t *e);
void power_off_cb(lv_event_t *e);
void power_reboot_cb(lv_event_t *e);
void power_sleep_cb(lv_event_t *e);
void open_power_dialog(void);
void power_btn_cb(lv_event_t *e);
void settings_btn_cb(lv_event_t *e);
void switch_user_cb(lv_event_t *e);
lv_obj_t *sm_card(lv_obj_t *parent, int x, int y, int w, int h, int radius);
void sm_pinned(lv_obj_t *parent, int x, int y, int w, int h,
const arctian_app_t *app, const char *svg,
const char *text, lv_event_cb_t cb, const void *ud);
void sm_rec(lv_obj_t *parent, int x, int y, int w,
const char *svg, const char *name, const char *sub);
lv_obj_t *sm_header(lv_obj_t *parent, int x, int y, const char *text);
void sm_search_bar(lv_obj_t *parent, int mw);
void build_start_menu(lv_obj_t *scr);
int slen2(const char *s);
int chr_low(char c);
int s_eq(const char *a, const char *b);
int iabs2(int v);
int has_ext(const char *name, char ext[], int extsz);
int app_handles_ext(int i, const char *ext);
void icon_apply_pos(int i);
void icon_set_sel(int i, int sel);
void clear_selection(void);
void icon_build(int i);
void dicon_add(const char *name, int is_file, int app_idx, const char *svg, const arctian_app_t *app);
int has_suffix_ci(const char *nm, const char *ext);
int afs_name_is_hidden(const char *nm);
void dicons_fill(void);
void build_desktop_icons(void);
void dicons_rebuild(void);
int icon_hit(int x, int y, int *out);
int rect_at(lv_obj_t *o, int x, int y);
int window_at(int x, int y);
int point_over_ui(int x, int y);
void open_arcapp_file(const char *file);
void dialog_close(void);
void dialog_cancel_cb(lv_event_t *e);
void dialog_pick_cb(lv_event_t *e);
void open_with_dialog(int idx);
void menu_close(void);
void menu_delete_cb(lv_event_t *e);
void menu_openwith_cb(lv_event_t *e);
void open_context_menu(int x, int y, int idx);
void open_dicon(int idx);
void rubber_show(int x0, int y0, int x1, int y1);
void rubber_hide(void);
void poll_icons(void);
uint32_t mix2(uint32_t a, uint32_t b, int w);
uint32_t *scale_hq(const uint32_t *src, int sw, int sh, int dw, int dh);
int afs_index_of(const char *name);
void wallpaper_show(uint32_t *scaled, int W, int H);
int wallpaper_from(const uint8_t *data, uint32_t len);
int wallpaper_load_afs(const char *name);
void wallpaper_cfg_read(void);
void wallpaper_cfg_write(const char *name);
void build_wallpaper(void);
int store_set_wallpaper(const char *afs_name);
int store_wallpaper_active(void);
const char *store_wallpaper_name(void);
int point_in_poly(float px, float py, const float *poly, int n);
void build_cursor(void);
void tour_show(void);
void help_toggle(void);
void help_close(void);
void setup_wizard_show(void);
void bin_copy(char *dst, int cap, const char *src);
void crash_ui_close_cb(lv_event_t *e);
void destroy_win_obj(lv_obj_t *win);
void crash_log_write(const char *title, const char *reason, int code);
void show_crash_dialog(const char *title, const char *reason);
void handle_app_crash(int code);
int hex_nib(char c);
void pct_decode(char *dst, int cap, const char *src, int len);
int key_eq(const char *k, int klen, const char *want);
int dpk_is_package_name(const char *nm);
int dpk_validate(const uint8_t *buf, uint32_t size, const dpk_header_t **out);
int perm_low(int c);
int perm_word_eq(const char *s, int len, const char *w);
uint32_t perms_parse(const char *s);
void dpk_parse_meta(const uint8_t *m, uint32_t len, dyn_app_t *d);
int dyn_find(const char *id);
dyn_app_t *dyn_register(const char *id);
int afs_read_at(const char *path, void *buf, uint32_t cap, uint32_t *out_len);
int afs_read_file(const char *name, void *buf, uint32_t cap, uint32_t *out_len);
int afs_write_app_pkg(const char *id, const void *buf, uint32_t len);
char *icon_dup(const uint8_t *p, uint32_t n);
unsigned char *icon_dup_bin(const uint8_t *p, uint32_t n);
void dyn_apply_package(dyn_app_t *d, const uint8_t *buf, uint32_t size, const dpk_header_t *h);
void dyn_scan_one(const char *full_path);
void dyn_scan_afs(void);
/* .wlf calisma zamani: widget agaci + wolf_api_t (wlf_host.c) */
wolf_api_t *wlf_api_get(lv_obj_t *body);
void wlf_host_reset(lv_obj_t *body);
void wlf_host_error(const char *msg);
/* 0.7: terminal modu. sink verilirse log/notify/dialog oraya yazilir;
 * arg, rol=cli/runtime uygulamasina verilen "birlikte ac" dosyasidir. */
void wlf_host_set_sink(void (*fn)(const char *s));
void wlf_host_set_arg(const char *arg);
/* wolf_id -> LVGL nesnesi (VM olay baglama icin) */
lv_obj_t *wolf_host_obj(wolf_id id);

int  wlf_vm_run(const uint8_t *code, uint32_t len, lv_obj_t *body, asi_t *asi);

/* ---- .wlf (libWolf) uygulama bicimi (0.7) ---- */
int  wlf_is_package_name(const char *nm);
int  wlf_validate(const uint8_t *buf, uint32_t size, const wlf_header_t **out);
void wlf_apply_package(dyn_app_t *d, const uint8_t *buf, uint32_t size, const wlf_header_t *h);
int  wlf_scan_one(const char *full_path);
int  wlf_preload(dyn_app_t *d, char *err, int cap);
void wlf_run_entry(dyn_app_t *d, lv_obj_t *body, asi_t *asi);
/* Rol=cli / runtime: gorunmez kapta calistir, cikti `puts`a gider (terminal). */
int  wlf_run_cli(dyn_app_t *d, const char *arg, void (*puts)(const char *s));
int  wlf_install_file(const char *path, char *err, int cap);   /* .wlf dosyasini C:/apps'e kur */
int  wlf_remove(const char *id);
int  wlf_install_url(const char *url, char *err, int cap);
void wlf_diag(const char *tag, const char *s);   /* seri teshis */

const arctian_app_t *app_get(int i);
int app_total(void);
uint32_t dyn_perms_for(int idx);
void dyn_app_entry(lv_obj_t *win, asi_t *asi);
void arctian_dyn_begin_launch(int idx);
void arctian_dyn_mark_closed(int idx);
int arctian_dyn_preload(int d, char *err, int cap);

/* ---- Terminal koprusu (0.7) ----
 * Terminal uygulamasi acilista kendi cikis islevini kaydeder; kabuk ve
 * rol=cli/runtime .wlf uygulamalari ciktiyi buradan terminale yazar. */
typedef void (*term_puts_fn)(const char *s);
void        arctian_term_set_puts(term_puts_fn fn);
void        arctian_term_write(const char *s);
int         arctian_term_run(const char *cmd);      /* terminali ac/one al, komutu calistir */
const char *arctian_term_pending(void);             /* bekleyen komut (tuketilir, yoksa 0) */
int         arctian_term_open(void);                /* yalnizca terminali ac/one al */
int         arctian_term_is_open(void);

int kv_find(const char *k);
const char *kv_get(const char *k, const char *def);
int itoa_buf(int val, char *out, int cap);
int kv_get_int(const char *k, int def);
void kv_set(const char *k, const char *v);
void settings_apply_values(void);
void settings_load(void);
void settings_save(void);
void settings_apply_runtime(void);
void settings_set_int(const char *key, int val);
void settings_accent_set(int i);
int settings_accent_count(void);
const char *settings_accent_name(int i);
uint32_t settings_accent_color(int i);
uint32_t settings_accent_color2(int i);
int settings_accent_current(void);
int settings_get_int(const char *key, int def);
void shell_rebuild(void);
void shell_apply_resolution(int idx);
void boot_apply_saved_resolution(void);
int settings_resolution_count(void);
const char *settings_resolution_name(int i);
int settings_resolution_current(void);
int settings_resolution_set(int i);
void arctian_shell_network_panel(void);
void arctian_shell_power_dialog(void);
void arctian_shell_lock_screen(void);
void lock_hide_desktop(int hide);
void rebuild_desktop_ui(void);
void rebuild_menu_ui(void);
void rebuild_shell_ui(void);
int store_install(const void *dpk, uint32_t size, char *err, int errcap);
int store_uninstall(const char *id);
int store_launch(const char *id);
int store_installed(const char *id);
const char *store_installed_version(const char *id);
int store_count_installed(void);
const char *store_installed_id(int i);
const char *store_installed_title(int i);
const char *store_installed_version_at(int i);
int shell_match(const char *a, const char *b);
int arctian_shell_app_count(void);
int arctian_shell_app_info(int i, char *title, int tcap,
char *name, int ncap, char *cat, int ccap);
int arctian_shell_launch(const char *q);
int arctian_shell_close(const char *q);
int arctian_shell_window_count(void);
int arctian_shell_window_info(int i, char *title, int tcap, int *visible);
void ed_split(const char *path, char *dir, int dcap, char *name, int ncap);
void ed_save_cb(lv_event_t *e);
void ed_set_focus(lv_obj_t *ta);
void ed_focus_cb(lv_event_t *e);
void ed_del_cb(lv_event_t *e);
void arctian_shell_editor(const char *path);
const char *arctian_net_http_get(void *asi, const char *url, uint32_t *len);
const char *arctian_net_https_get(void *asi, const char *url, uint32_t *len);
int arctian_net_link_type(void *asi);
void dst_puts(const char *s);
void dst_putx(uint32_t v);
void duckstore_selftest(void);
void update_selftest(void);
void *arctian_js_arena(unsigned long *out_size);
void desktop_main(void *arg);
