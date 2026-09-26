#ifndef ARCTIAN_LOADER_H
#define ARCTIAN_LOADER_H

#include <stdint.h>
#include "superblock.h"

int module_find_by_name(const char *name, module_entry_t *out);
int module_load(const module_entry_t *entry);

#endif
