#include <stdbool.h>

void halt();
__attribute__((noreturn)) void halt_catch_fire();
char get_cpu_vendor();
void idle();
bool is_pe_enabled();
void cpu_init();
void sse_init();