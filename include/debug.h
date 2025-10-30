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

extern const char *ansi_color[];