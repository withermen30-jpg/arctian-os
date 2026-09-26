#ifndef ARCTIAN_SERIAL_H
#define ARCTIAN_SERIAL_H

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);
void serial_printf(const char *fmt, ...);

#endif
