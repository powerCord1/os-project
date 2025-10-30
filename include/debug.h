#include <stdint.h>

#define LOGLEVEL_ERROR 1
#define LOGLEVEL_WARN 2
#define LOGLEVEL_INFO 3
#define LOGLEVEL_VERBOSE 4

#define LOGLEVEL 4

void log_err(const char *msg);
void log_warn(const char *msg);
void log_info(const char *msg);
void log_verbose(const char *msg);
const char *get_log_text(uint8_t type);
void log_test();
void breakpoint(void);

enum ansi_color_index {
    ANSI_COLOR_NC,
    ANSI_COLOR_BLACK,
    ANSI_COLOR_RED,
    ANSI_COLOR_GREEN,
    ANSI_COLOR_BROWN,
    ANSI_COLOR_BLUE,
    ANSI_COLOR_PURPLE,
    ANSI_COLOR_CYAN,
    ANSI_COLOR_LIGHT_GREY,
    ANSI_COLOR_DARK_GREY,
    ANSI_COLOR_LIGHT_RED,
    ANSI_COLOR_LIGHT_GREEN,
    ANSI_COLOR_YELLOW,
    ANSI_COLOR_LIGHT_BLUE,
    ANSI_COLOR_LIGHT_PURPLE,
    ANSI_COLOR_LIGHT_CYAN,
    ANSI_COLOR_WHITE,
};

const char *ansi_color[] = {
    [ANSI_COLOR_NC] = "\033[0m",
    [ANSI_COLOR_BLACK] = "\033[0;30m",
    [ANSI_COLOR_RED] = "\033[0;31m",
    [ANSI_COLOR_GREEN] = "\033[0;32m",
    [ANSI_COLOR_BROWN] = "\033[0;33m",
    [ANSI_COLOR_BLUE] = "\033[0;34m",
    [ANSI_COLOR_PURPLE] = "\033[0;35m",
    [ANSI_COLOR_CYAN] = "\033[0;36m",
    [ANSI_COLOR_LIGHT_GREY] = "\033[0;37m",
    [ANSI_COLOR_DARK_GREY] = "\033[1;30m",
    [ANSI_COLOR_LIGHT_RED] = "\033[1;31m",
    [ANSI_COLOR_LIGHT_GREEN] = "\033[1;32m",
    [ANSI_COLOR_YELLOW] = "\033[1;33m",
    [ANSI_COLOR_LIGHT_BLUE] = "\033[1;34m",
    [ANSI_COLOR_LIGHT_PURPLE] = "\033[1;35m",
    [ANSI_COLOR_LIGHT_CYAN] = "\033[1;36m",
    [ANSI_COLOR_WHITE] = "\033[1;37m",
};