#ifndef INSTALLER_H
#define INSTALLER_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    INSTALL_MODE_NONE,
    INSTALL_MODE_PERMANENT,
    INSTALL_MODE_TEST
} installer_mode_t;

typedef enum {
    INSTALL_SCREEN_WELCOME,
    INSTALL_SCREEN_USERNAME,
    INSTALL_SCREEN_DISK_SELECT,
    INSTALL_SCREEN_PROGRESS,
    INSTALL_SCREEN_COMPLETE
} installer_screen_t;

installer_mode_t installer_run(void);

#endif
