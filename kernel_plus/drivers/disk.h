#ifndef DISK_H
#define DISK_H

#include <stdint.h>
#include <stdbool.h>

#define DISK_MAX 4

typedef struct {
    bool present;
    uint16_t io;
    uint8_t drive;
    uint32_t sectors;
    char name[48];
} disk_t;

void disk_init(void);
int disk_count(void);
disk_t *disk_get(int index);
int disk_install_arctian(int index, int *written, int total);

#endif
