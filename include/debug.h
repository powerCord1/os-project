#include <stdbool.h>
#include <stdint.h>

#define LOGLEVEL_ERROR 1
#define LOGLEVEL_WARN 2
#define LOGLEVEL_INFO 3
#define LOGLEVEL_VERBOSE 4

#define LOGLEVEL 4

#define DEBUG true

void log_err(const char *format, ...);
void log_warn(const char *format, ...);
void log_info(const char *format, ...);
void log_verbose(const char *format, ...);
const char *get_log_text(uint8_t type);
void log_test();
void log_kbd_action(const char *format, ...);
void breakpoint();

struct ansi_colors {
    const char *nc;
    const char *black;
    const char *red;
    const char *green;
    const char *brown;
    const char *blue;
    const char *purple;
    const char *cyan;
    const char *light_grey;
    const char *dark_grey;
    const char *light_red;
    const char *light_green;
    const char *yellow;
    const char *light_blue;
    const char *light_purple;
    const char *light_cyan;
    const char *white;
};

static const struct ansi_colors ansi_color = {
    .nc = "\033[0m",
    .black = "\033[0;30m",
    .red = "\033[0;31m",
    .green = "\033[0;32m",
    .brown = "\033[0;33m",
    .blue = "\033[0;34m",
    .purple = "\033[0;35m",
    .cyan = "\033[0;36m",
    .light_grey = "\033[0;37m",
    .dark_grey = "\033[1;30m",
    .light_red = "\033[1;31m",
    .light_green = "\033[1;32m",
    .yellow = "\033[1;33m",
    .light_blue = "\033[1;34m",
    .light_purple = "\033[1;35m",
    .light_cyan = "\033[1;36m",
    .white = "\033[1;37m",
};