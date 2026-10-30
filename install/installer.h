/**
 * Arctian Installer Header
 * Pardus benzeri Türkçe kurulum sihirbazı
 */
#ifndef INSTALLER_H
#define INSTALLER_H

#include <stdint.h>
#include <stdbool.h>

/* Installer modes */
typedef enum {
    INSTALL_MODE_NONE,
    INSTALL_MODE_PERMANENT,
    INSTALL_MODE_TEST
} installer_mode_t;

/* Installer screens */
typedef enum {
    INSTALL_SCREEN_WELCOME,
    INSTALL_SCREEN_USERNAME,
    INSTALL_SCREEN_DISK_SELECT,
    INSTALL_SCREEN_PROGRESS,
    INSTALL_SCREEN_COMPLETE
} installer_screen_t;

/* Run the installer. Returns mode selected by user. */
installer_mode_t installer_run(void);

#endif /* INSTALLER_H */
