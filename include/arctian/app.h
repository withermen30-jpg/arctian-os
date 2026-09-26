#ifndef ARCTIAN_APP_H
#define ARCTIAN_APP_H

#include "lvgl.h"
#include "asi.h"

typedef void (*arctian_app_entry_t)(lv_obj_t *win, asi_t *asi);

typedef struct {
    char name[32];
    char title[48];
    char category[32];
    const char *handles;
    const char *icon_svg;
    arctian_app_entry_t entry;
    int win_w;
    int win_h;
} arctian_app_t;

extern const arctian_app_t arctian_apps[];
extern const int arctian_app_count;

extern const char *arctian_open_target;

typedef struct {
    int         (*install)(const void *dpk, uint32_t size, char *err, int errcap);
    int         (*uninstall)(const char *id);
    int         (*launch)(const char *id);
    int         (*installed)(const char *id);
    const char *(*installed_version)(const char *id);
    int         (*count_installed)(void);
    const char *(*installed_id)(int i);
    const char *(*installed_title)(int i);
    const char *(*installed_version_at)(int i);
    void        (*refresh)(void);

    int         (*set_wallpaper)(const char *afs_name);
    int         (*wallpaper_active)(void);
    const char *(*wallpaper_name)(void);
} arctian_store_api_t;

extern arctian_store_api_t *arctian_store;

#endif
