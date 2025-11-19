#include <stdarg.h>

int serial_init();
int serial_received();
char read_serial();
void write_serial(char a);
void serial_writestring(const char *s);
void serial_writestringln(const char *s);
int vserial_printf(const char *restrict format, va_list parameters);
int serial_printf(const char *restrict format, ...);