#ifndef ARCTIAN_DISKCRYPT_H
#define ARCTIAN_DISKCRYPT_H

#include <stdint.h>

void        diskcrypt_init(void);
int         diskcrypt_present(void);
int         diskcrypt_unlocked(void);
int         diskcrypt_unlock(const char *pass);
int         diskcrypt_setup(const char *pass);
int         diskcrypt_disable(const char *pass);
const char *diskcrypt_status(void);

#endif
