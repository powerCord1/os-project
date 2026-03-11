#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#define USE_SOL true

typedef struct serial_config {
    uint64_t base;
    bool is_mmio;
    bool exists;
} serial_config_t;

extern serial_config_t debug_port;
extern bool serial_initialised;
extern bool sol_enabled;

void serial_init();
static void serial_configure(uint64_t base, bool is_mmio);
static void serial_out(uint8_t offset, uint8_t value);
static uint8_t serial_in(uint8_t offset);
static char read_serial();
static void write_serial(char a);
static int serial_received();
static int is_transmit_empty();
void serial_writestring(const char *s);
void serial_writestringln(const char *s);
int vserial_printf(const char *restrict format, va_list parameters);
int serial_printf(const char *restrict format, ...);