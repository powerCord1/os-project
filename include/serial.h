int serial_init(void);
int serial_received(void);
char read_serial(void);
void write_serial(char a);
void serial_writestring(const char *s);
void serial_writestringln(const char *s);