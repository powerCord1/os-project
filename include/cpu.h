#include <stdbool.h>

void halt();
__attribute__((noreturn)) void halt_cf();
char get_cpu_vendor();
void idle();
bool is_pe_enabled();
void cpu_init();
void sse_init();
bool is_apic_enabled();