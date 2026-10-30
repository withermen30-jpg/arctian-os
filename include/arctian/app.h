#ifndef ARCTIAN_APP_H
#define ARCTIAN_APP_H

#include "lvgl.h"
#include "asi.h"
#include "update.h"

/* Her Arctian uygulamasi bu imzaya sahip bir giris noktasi tanimlar.
 * app_entry, DERLEME SIRASINDA -Dapp_entry=app_<isim>_entry ile yeniden adlandirilir. */
typedef void (*arctian_app_entry_t)(lv_obj_t *win, asi_t *asi);

typedef struct {
    char name[32];
    char title[48];
    char category[32];   /* "appviewer" ise asagidaki handles gecerlidir */
    const char *handles; /* appviewer: acilan uzantilar, virgulle: "py,ue,exe" */
    const char *icon_svg;
    arctian_app_entry_t entry;
    int win_w;           /* tercih edilen pencere genisligi (0 = varsayilan) */
    int win_h;           /* tercih edilen pencere yuksekligi (0 = varsayilan) */
    unsigned flags;      /* ARCTIAN_APP_* bit maskesi */
    const unsigned char *icon_png;  /* icon.svg yoksa icon.png baytlari (yoksa 0) */
    unsigned icon_png_len;
} arctian_app_t;

#define ARCTIAN_APP_NO_DESKTOP 0x1u  /* masaustunde simge gosterme */
#define ARCTIAN_APP_ENGINE     0x2u  /* arka plan motoru: baslatinca terminal ac */

/* apps_registry.c tarafindan uretilir */
extern const arctian_app_t arctian_apps[];
extern const int arctian_app_count;

/* appviewer bir uygulamaya hangi dosyanin acilacagini bildirir (bos = yok). */
extern const char *arctian_open_target;

/* ------------------------------------------------------------------ */
/* DuckStore uygulama platformu (desktop/main.c saglar)               */
/* ------------------------------------------------------------------ */
/* Kurulu dinamik uygulamalar (AFS'teki *.dpk paketleri) bu API ile    */
/* yonetilir. DuckStore uygulamasi yalnizca bu ara yuzu kullanir.      */
typedef struct {
    /* dpk/size: indirilen paket; 0 = basarili, <0 hata (err doldurulur). */
    int         (*install)(const void *dpk, uint32_t size, char *err, int errcap);
    int         (*uninstall)(const char *id);
    int         (*launch)(const char *id);
    int         (*installed)(const char *id);           /* 1/0 */
    const char *(*installed_version)(const char *id);   /* "" yoksa */
    int         (*count_installed)(void);
    const char *(*installed_id)(int i);
    const char *(*installed_title)(int i);
    const char *(*installed_version_at)(int i);
    void        (*refresh)(void);                       /* masaustu/baslat menusu */

    /* ---- Masaustu arka plani (AFS'teki .wp dosyalari) ---- */
    /* afs_name: ".wp" dosya adi; NULL veya "" = gomulu varsayilan arka plan. */
    int         (*set_wallpaper)(const char *afs_name);
    int         (*wallpaper_active)(void);              /* 1/0 */
    const char *(*wallpaper_name)(void);                /* aktif dosya adi veya "" */
} arctian_store_api_t;

extern arctian_store_api_t *arctian_store;

int         arctian_shell_app_count(void);
int         arctian_shell_app_info(int i, char *title, int tcap,
                                   char *name, int ncap, char *cat, int ccap);
int         arctian_shell_launch(const char *name_or_title);
int         arctian_shell_close(const char *name_or_title);
int         arctian_shell_window_count(void);
int         arctian_shell_window_info(int i, char *title, int tcap, int *visible);

/* Pencere boyutu degince (snap/buyut/kucult) uygulamanin icerigini
 * yeniden yerlestirmesi icin geri cagri kaydi (cb: pencere nesnesi). */
void        arctian_shell_set_relayout(lv_obj_t *win, void (*cb)(lv_obj_t *));

/* ---- Dosya deneyimi --------- */
/* Pencere bazli klavye kisayolu: masaustu bir textarea'ya ulasamazsa en ust
 * pencereye iletilir. hk: 3=Ctrl+C 4=Ctrl+X 5=Ctrl+V 6=Ctrl+A 7=Ctrl+Z 8=Ctrl+Y */
void        arctian_shell_set_hotkey(lv_obj_t *win, void (*cb)(int hk));
/* Pencereyi surukle-birak hedefi yap: masaustundeki bir dosya pencereye
 * birakildiginda cb(src_path) cagrilir; 1 donen dosyayi kabul eder. */
void        arctian_shell_set_drop(lv_obj_t *win, int (*cb)(const char *src_path));
/* (x,y) noktasindaki pencereye dosya birak; 1 = pencere kabul etti. */
int         arctian_shell_drop_at(int x, int y, const char *src_path);
/* 1 = (x,y) masaustudur (gorev cubugu/menuler/pencereler disi). */
int         arctian_shell_over_desktop(int x, int y);
/* Uzanti eslemesine gore ac: txt/md/js -> Editor, png -> Goruntuleyici,
 * wav -> Medya, bilinmeyen -> ArcApp. Her zaman 1 doner. */
int         arctian_shell_open_file(const char *path);
/* Ctrl/Shift anlik durumu (coklu secim: Ctrl+tik, aralik: Shift+tik). */
int         arctian_shell_ctrl_down(void);
int         arctian_shell_shift_down(void);
/* Odakli textarea'nin metin geri al (redo=0) / yinele (redo=1) islemi.
 * 1 = islendi (odakli bir duzenleyici vardi). */
int         arctian_shell_text_undo(int redo);

void        arctian_clipboard_set(const char *text);
const char *arctian_clipboard_get(void);
int         arctian_clipboard_len(void);
void        arctian_clipboard_clear(void);
void        arctian_notify(const char *title, const char *msg);
/* Dosyayi C:/Cop'a tasir (Cop Kutusu). 0=ok, -1=hata, -2=boyut siniri. */
int         arctian_shell_trash(const char *path);
void        arctian_shell_network_panel(void);
void        arctian_shell_power_dialog(void);
void        arctian_shell_lock_screen(void);
void        arctian_shell_editor(const char *path);

typedef struct {
    int         (*accent_count)(void);
    const char *(*accent_name)(int i);
    uint32_t    (*accent_color)(int i);
    uint32_t    (*accent_color_2)(int i);
    int         (*accent_current)(void);
    void        (*accent_set)(int i);

    int         (*get_int)(const char *key, int def);
    void        (*set_int)(const char *key, int val);

    int         (*resolution_count)(void);
    const char *(*resolution_name)(int i);
    int         (*resolution_current)(void);
    int         (*resolution_set)(int i);
} arctian_settings_api_t;

extern arctian_settings_api_t *arctian_settings;

/* ------------------------------------------------------------------ */
/* Kimlik dogrulama (desktop/auth.c saglar)                           */
/* ------------------------------------------------------------------ */
/* Coklu hesap. Kullanici kayitlari AFS'te "C:/etc/users"              */
/* dosyasinda satir basina bir kullanici olarak tutulur; parola       */
/* salt'li yinelenen SHA-256 ile saklanir. Her kullanicinin bir uid'si */
/* (1000+) ve "C:/Users/<ad>" ev klasoru vardir.                       */
int         arctian_auth_user_count(void);            /* kayitli hesap sayisi */
const char *arctian_auth_user_name(int i);            /* i. hesabin adi, "" yoksa */
int         arctian_auth_user_has_password(int i);    /* i. hesap parolali mi */
uint32_t    arctian_auth_user_uid(int i);             /* i. hesabin uid'si */
int         arctian_auth_find(const char *name);      /* indeks veya -1 */
const char *arctian_auth_home(const char *name);      /* "C:/Users/<ad>"; name bos = oturum */
int         arctian_auth_create_user(const char *name, const char *pw_or_null); /* 0 = ok, ev klasoru olusturulur */
int         arctian_auth_remove_user(const char *name);   /* 0 = ok */
int         arctian_auth_verify_user(const char *name, const char *pw);   /* 1 = parola dogru */
int         arctian_auth_set_password_for(const char *name, const char *pw, const char *pw2); /* 0 = ok */
void        arctian_auth_set_current(const char *name);   /* oturum kullanicisini ayarla */
const char *arctian_auth_current(void);               /* oturumdaki ad, "" yoksa */
uint32_t    arctian_auth_current_uid(void);           /* oturumdaki uid, 0 yoksa */

/* Geriye donuk tek-kullanici yardimcilari (oturum kullanicisi uzerinde) */
int         arctian_auth_has_user(void);              /* 1 = en az bir hesap */
const char *arctian_auth_username(void);              /* oturum/ilk hesap adi, "" yoksa */
int         arctian_auth_has_password(void);          /* oturum hesabi parolali mi */
int         arctian_auth_set_password(const char *pw, const char *pw2);   /* 0 = ok */
int         arctian_auth_verify(const char *pw);      /* 1 = parola dogru */

#endif /* ARCTIAN_APP_H */
