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
void breakpoint(void);