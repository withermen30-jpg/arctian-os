#ifndef SPOOL_H
#define SPOOL_H

#include <stdint.h>

void spool_init(void);
int  spool_submit(const char *name, const void *data, uint32_t len);
int  spool_count(void);
int  spool_info(int i, int *id, char *name, int cap, uint32_t *len);
int  spool_cancel(int id);
int  spool_process(void);
int  spool_last_path(char *out, int cap);

#endif
