#define HALT_ON_PANIC false
#define CLEAR_ON_PANIC true

void __attribute__((noreturn)) panic(const char *reason);